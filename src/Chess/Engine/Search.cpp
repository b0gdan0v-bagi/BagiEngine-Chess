#include "Search.h"

#include <Chess/Engine/TranspositionTable.h>
#include <Chess/Game/GameStatus.h>
#include <Chess/Game/MoveGenerator.h>

namespace BECore::Chess {

    namespace {

        /// Larger than any score, so that the first move of a node always beats "nothing found yet"
        constexpr int kInfinity = kMateScore + 1000;

        /// A check is searched one ply deeper only up to this ply, so a perpetual check cannot grow the tree without end
        constexpr int kMaxExtensionPly = 32;
        /// No line is searched further from the root than this many plies, quiescence included: the deepest full-width line is
        /// kMaxSearchDepth plus the extensions, and what is left at that ply is judged by the static score. The move lists of the nodes
        /// are not on the native stack: the search is a loop over frames (one per ply, below) that live on the heap and are made when a
        /// ply is first reached, so a deep line costs no stack, only the killers and the history (QuietMoves, about 33 KB) are in the Searcher itself
        constexpr int kMaxPly = kMaxSearchDepth + kMaxExtensionPly;
        /// The stack of a Background pool thread (512 KB on macOS); the budget for the Searcher, which Search() puts on the caller's stack
        constexpr size_t kBackgroundStackBytes = 512 * 1024;
        /// The share of that stack that the killers and the history (QuietMoves) may take: one eighth
        constexpr size_t kQuietMovesStackShare = 8;

        /// A position that has occurred this many times (itself counted) scores as a draw inside the tree: once before is enough
        constexpr int kRepetitionDrawCount = 2;

        // Move ordering: bigger is tried first. The bands do not overlap: a capture is worth kCaptureScore plus up to
        // kVictimWeight * 5 (a queen taken) and a quiet promotion kPromotionScore plus the piece
        constexpr int kPreferredMoveScore = 1 << 24;
        constexpr int kCaptureScore = 1 << 20;
        constexpr int kPromotionScore = 1 << 16;
        /// "Most valuable victim, least valuable attacker": the victim's kind counts kVictimWeight times, the attacker's kind once
        /// (the kinds are 1..6, so the weight is above the largest)
        constexpr int kVictimWeight = 8;
        // Quiet moves, below the promotions: the two killers of the ply, then the history. An entry is halved when a bonus would take it
        // past kHistoryMax, so it stays in the band below the killers (a single bonus at a depth over 90 can overshoot kHistoryMax, still
        // far under kSecondKillerScore)
        constexpr int kFirstKillerScore = 1 << 15;
        constexpr int kSecondKillerScore = kFirstKillerScore - 1;
        constexpr int kHistoryMax = 1 << 14;
        static_assert(kHistoryMax < kSecondKillerScore && kSecondKillerScore < kFirstKillerScore && kFirstKillerScore < kPromotionScore);
        /// Order() is given this ply by the nodes that do not use killers and history
        constexpr int kNoKillerPly = -1;
        /// The table is built only for a search of at least this depth: the levels with fewer plies pay no allocation
        constexpr int kMinTableDepth = 3;
        constexpr int kKillersPerPly = 2;
        /// The history is kept per color of the mover
        constexpr size_t kColorCount = 2;

        struct ScoredMove {
            Move move;
            int score = 0;
        };
        using ScoredMoves = eastl::fixed_vector<ScoredMove, kMaxMoves, false>;

        bool IsCapture(const Position& position, Move move) noexcept {
            return move.Kind() == MoveKind::EnPassant || !position.PieceAt(move.ToIndex()).IsEmpty();
        }

        int OrderScore(const Position& position, Move move, Move preferred) noexcept {
            if (!preferred.IsNull() && move == preferred) {
                return kPreferredMoveScore;
            }
            int score = 0;
            if (IsCapture(position, move)) {
                const PieceType victim = move.Kind() == MoveKind::EnPassant ? PieceType::Pawn : position.PieceAt(move.ToIndex()).type;
                const PieceType attacker = position.PieceAt(move.FromIndex()).type;
                score = kCaptureScore + kVictimWeight * static_cast<int>(victim) - static_cast<int>(attacker);
                if (move.IsPromotion()) {
                    score += static_cast<int>(move.PromotionType());
                }
            } else if (move.IsPromotion()) {
                score = kPromotionScore + static_cast<int>(move.PromotionType());
            }
            return score;
        }

        /// Moves the best of moves[index..] to moves[index], the others keep their order: with this the order of equal scores is
        /// the one of the generator, which a plain selection swap would shuffle
        void PickBest(ScoredMoves& moves, size_t index) noexcept {
            size_t best = index;
            for (size_t i = index + 1; i < moves.size(); ++i) {
                if (moves[i].score > moves[best].score) {
                    best = i;
                }
            }
            if (best != index) {
                eastl::rotate(moves.begin() + static_cast<ptrdiff_t>(index), moves.begin() + static_cast<ptrdiff_t>(best), moves.begin() + static_cast<ptrdiff_t>(best) + 1);
            }
        }

        /// Whether @p score is a mate that falls within @p depth plies of the root
        bool IsMateWithin(int score, int depth) noexcept {
            return IsMateScore(score) && kMateScore - (score > 0 ? score : -score) <= depth;
        }

        /// The killers and the history of one search, zeroed per call; no heap
        class QuietMoves {
        public:
            /// The order score of a quiet move of @p color at @p ply: a killer of the ply, else its history
            [[nodiscard]] int Score(Move move, PieceColor color, int ply) const noexcept {
                const Move* killers = _killers[ply];
                if (move == killers[0]) {
                    return kFirstKillerScore;
                }
                if (move == killers[1]) {
                    return kSecondKillerScore;
                }
                return _history[static_cast<size_t>(color)][move.FromIndex()][move.ToIndex()];
            }

            /// A quiet @p move cut off a node of @p depth plies at @p ply: it becomes the first killer (the one it pushes aside
            /// the second) and its history grows by depth squared
            void Reward(Move move, PieceColor color, int ply, int depth) noexcept {
                Move* killers = _killers[ply];
                if (killers[0] != move) {
                    killers[1] = killers[0];
                    killers[0] = move;
                }
                int& entry = _history[static_cast<size_t>(color)][move.FromIndex()][move.ToIndex()];
                const int bonus = eastl::min(depth * depth, kHistoryMax);
                if (entry + bonus > kHistoryMax) {
                    Halve();
                }
                entry += bonus;
            }

        private:
            /// Keeps the order of the entries (up to ties, which halving makes) and makes room: the whole table, so the entries stay comparable
            void Halve() noexcept {
                for (auto& perColor : _history) {
                    for (auto& perFrom : perColor) {
                        for (int& entry : perFrom) {
                            entry /= 2;
                        }
                    }
                }
            }

            Move _killers[kMaxPly + 1][kKillersPerPly] = {};
            int _history[kColorCount][kSquareCount][kSquareCount] = {};
        };
        static_assert(sizeof(QuietMoves) < kBackgroundStackBytes / kQuietMovesStackShare, "the killers and the history live in the Searcher, which Search() puts on the caller's stack");

        /// What a frame searches: the root (the moves of the iteration, no cutoff, no table), a full-width node, or a quiescence node
        CORE_ENUM(FrameKind, uint8_t, Root, Full, Quiet)

        /// One ply of the line being searched, the state that a recursive call kept in its locals: the moves of the node in the order
        /// they are tried, the one being searched, and the window. The frame of ply p is _frames[p]; it is made when the ply is first reached
        /// and reused by every node of that ply, so a search pays for the depth it reaches (a search of 1 or 2 plies makes the frames of the
        /// quiescence it runs, not 96 of them)
        struct Frame {
            ScoredMoves moves;
            /// The index in @ref moves of the next move to try
            size_t next = 0;
            Move bestMove;
            /// The move made on the position, whose child is being searched
            Move current;
            int depth = 0;
            int alpha = 0;
            int beta = 0;
            /// The alpha the node was entered with, after the table narrowed the window: what decides the bound of the entry stored
            int windowAlpha = 0;
            int best = 0;
            uint64_t historyDrawsBefore = 0;
            PieceColor mover = PieceColor::White;
            FrameKind kind = FrameKind::Full;
            bool tableUsable = false;
            /// Whether @ref current is neither a capture nor a promotion (a full-width frame only): the moves that become killers
            bool currentQuiet = false;
        };

        /// Where a Searcher is: before its first Advance(), searching, or over
        CORE_ENUM(SearchPhase, uint8_t, NotStarted, Searching, Finished)

        /// The iterative deepening search as a loop over the frames, run on a position; SearchJob and Search() are this class. Loop() is the
        /// whole search: a node is entered by EnterFull() or EnterQuiet(), which either answer at once (a leaf: the value is left in _value) or
        /// push a frame, and a finished frame hands its value to the frame below it (_returning). The order of every check, count and update is
        /// that of a plain recursive negamax, which the pinned node counts of the tests hold.
        /// Loop() keeps the window and the best of the frame it is in in locals and writes them to the frame only when it leaves it (to descend,
        /// to pause or to finish), so the moves of a node cost no reads of the frame for them
        class Searcher {
        public:
            Searcher(Position& position, const SearchSettings& settings, const SearchControl& control) : _position(position), _settings(settings), _control(control) {}

            [[nodiscard]] SearchProgress Advance(uint64_t maxNodes);
            [[nodiscard]] bool IsDone() const noexcept {
                return _phase == SearchPhase::Finished;
            }
            [[nodiscard]] SearchResult GetResult() const noexcept;
            [[nodiscard]] int FinishedDepth() const noexcept {
                return _result.depth;
            }

        private:
            /// Counts a node and looks at the limits: the node limit in every node, the clock and the token every kSearchPollInterval nodes
            void CountNode() noexcept;
            [[nodiscard]] bool LimitsReached() const;
            /// Whether no 50-move draw can fall inside the subtree of a node with @p depth plies left (extensions included), so that a
            /// result of the table does not depend on the clock. A capture or a pawn move resets the clock, so quiescence cannot reach it
            [[nodiscard]] bool ClockAllowsTable(int depth) const noexcept;
            /// Fills @p scored with @p moves in the order they are tried first to last (PickBest does the rest). @p ply is where the killers are looked up;
            /// kNoKillerPly orders the quiet moves as the generator gave them
            void Order(const MoveList& moves, Move preferred, int ply, ScoredMoves& scored) const;

            /// The start of a search: the stop before it began, the root moves, the table. False when the search ends here
            [[nodiscard]] bool Begin();
            void StartIteration();
            [[nodiscard]] SearchProgress Loop(uint64_t sliceEnd);
            /// The frame of @p ply, made on first use; it becomes the top of the line
            Frame& Acquire(int ply);
            /// The node reached by the move just made: true when it pushed a frame (the search goes on in its moves), false when it is a leaf and
            /// its value is in _value
            [[nodiscard]] bool EnterFull(int depth, int alpha, int beta, int ply);
            [[nodiscard]] bool EnterQuiet(int alpha, int beta, int ply);
            /// The moves of the top frame are used up (or it cut off): its value goes down to the frame below, or the iteration ends at the root
            void Finish(Frame& frame);
            void FinishIteration(const Frame& root);
            /// The search is stopped: takes back the moves still made on the position (one per frame below the top) and ends it
            void Abort() noexcept;

            Position& _position;
            const SearchSettings& _settings;
            const SearchControl& _control;
            uint64_t _nodes = 0;
            bool _stopped = false;
            /// Off during the first iteration, which must finish
            bool _stopEnabled = false;
            QuietMoves _quiet;
            /// Built in Begin() for a search deep enough; empty means no table
            std::optional<TranspositionTable> _table;
            /// How many times a node returned a draw that depends on the history (a repeated position, the 50-move clock). A node
            /// whose subtree moved it stores nothing
            uint64_t _historyDraws = 0;

            SearchPhase _phase = SearchPhase::NotStarted;
            SearchResult _result;
            MoveList _rootMoves;
            int _maxDepth = 1;
            /// The iteration being searched
            int _depth = 1;
            /// The best move of the last finished iteration, tried first by the next one
            Move _best;
            eastl::vector<eastl::unique_ptr<Frame>> _frames;
            /// The ply of the frame whose moves are being tried, -1 between iterations. Each frame below it has a move made on the position
            int _top = -1;
            /// A child has answered with _value and the move that led to it is still made: Loop() takes it back and gives the value to the top frame
            bool _returning = false;
            int _value = 0;
        };

        void Searcher::CountNode() noexcept {
            ++_nodes;
            if (!_stopEnabled || _stopped) {
                return;
            }
            if (_control.nodeLimit != 0 && _nodes >= _control.nodeLimit) {
                _stopped = true;
            } else if (_nodes % kSearchPollInterval == 0 && LimitsReached()) {
                _stopped = true;
            }
        }

        bool Searcher::LimitsReached() const {
            if (_control.token.IsCancelled()) {
                return true;
            }
            if (_control.deadline && std::chrono::steady_clock::now() >= *_control.deadline) {
                return true;
            }
            return _control.nodeLimit != 0 && _nodes >= _control.nodeLimit;
        }

        bool Searcher::ClockAllowsTable(int depth) const noexcept {
            return _position.HalfmoveClock() + depth + kMaxExtensionPly < kFiftyMovePlies;
        }

        void Searcher::Order(const MoveList& moves, Move preferred, int ply, ScoredMoves& scored) const {
            const bool useQuiet = _settings.quietOrdering && ply != kNoKillerPly;
            const PieceColor color = _position.SideToMove();
            scored.clear();
            for (const Move move : moves) {
                int score = OrderScore(_position, move, preferred);
                if (useQuiet && score == 0) {
                    score = _quiet.Score(move, color, ply);
                }
                scored.push_back(ScoredMove{move, score});
            }
        }

        Frame& Searcher::Acquire(int ply) {
            if (static_cast<size_t>(ply) >= _frames.size()) {
                _frames.push_back(eastl::make_unique<Frame>());
            }
            _top = ply;
            return *_frames[static_cast<size_t>(ply)];
        }

        bool Searcher::EnterFull(int depth, int alpha, int beta, int ply) {
            CountNode();
            if (_stopped) {
                _value = 0;
                return false;
            }
            if (ply >= kMaxPly) {
                _value = Evaluate(_position);
                return false;
            }
            if (_position.RepetitionCount() >= kRepetitionDrawCount) {
                ++_historyDraws;
                _value = kDrawScore;
                return false;
            }
            if (_position.IsInsufficientMaterial()) {
                _value = kDrawScore;
                return false;
            }

            const bool inCheck = _position.IsInCheck();
            if (inCheck && _settings.checkExtension && ply < kMaxExtensionPly) {
                ++depth;
            }
            if (depth <= 0) {
                // The node counts a second time as the quiescence node it turns into
                if (_settings.quiescence) {
                    return EnterQuiet(alpha, beta, ply);
                }
                _value = Evaluate(_position);
                return false;
            }

            // The table: an entry at least as deep gives its score (or a bound that narrows the window) where the clock allows,
            // any entry gives its move for the ordering
            const bool tableUsable = _table && ClockAllowsTable(depth);
            Move tableMove;
            if (_table) {
                if (const TableEntry* entry = _table->Probe(_position.Key())) {
                    tableMove = entry->move;
                    if (tableUsable && entry->depth >= depth) {
                        const int stored = ScoreFromTable(entry->score, ply);
                        switch (entry->bound) {
                            case TableBound::Exact:
                                _value = stored;
                                return false;
                            case TableBound::Lower:
                                if (stored >= beta) {
                                    _value = stored;
                                    return false;
                                }
                                alpha = eastl::max(alpha, stored);
                                break;
                            case TableBound::Upper:
                                if (stored <= alpha) {
                                    _value = stored;
                                    return false;
                                }
                                beta = eastl::min(beta, stored);
                                break;
                        }
                    }
                }
            }

            const MoveList moves = MoveGenerator::GenerateLegal(_position);
            if (moves.empty()) {
                _value = inCheck ? -MateIn(ply) : kDrawScore;
                return false;
            }
            if (_position.HalfmoveClock() >= kFiftyMovePlies) {
                ++_historyDraws;
                _value = kDrawScore;
                return false;
            }

            Frame& frame = Acquire(ply);
            frame.kind = FrameKind::Full;
            frame.historyDrawsBefore = _historyDraws;
            frame.depth = depth;
            frame.alpha = alpha;
            frame.beta = beta;
            frame.windowAlpha = alpha;
            frame.best = -kInfinity;
            frame.bestMove = Move{};
            frame.next = 0;
            frame.tableUsable = tableUsable;
            frame.mover = _position.SideToMove();
            Order(moves, tableMove, ply, frame.moves);
            return true;
        }

        bool Searcher::EnterQuiet(int alpha, int beta, int ply) {
            CountNode();
            if (_stopped) {
                _value = 0;
                return false;
            }
            if (ply >= kMaxPly) {
                _value = Evaluate(_position);
                return false;
            }

            // In check there is no standing pat: every evasion is looked at, and no evasion is a mate
            const bool inCheck = _position.IsInCheck();
            int best = -kInfinity;
            if (!inCheck) {
                best = Evaluate(_position);
                if (best >= beta) {
                    _value = best;
                    return false;
                }
                alpha = eastl::max(alpha, best);
            }

            MoveList moves = MoveGenerator::GenerateLegal(_position);
            if (inCheck) {
                if (moves.empty()) {
                    _value = -MateIn(ply);
                    return false;
                }
            } else {
                // Captures and promotions to a queen; the other promotions are left to the full-width part of the search
                const auto quiet = [this](Move move) { return !IsCapture(_position, move) && !(move.IsPromotion() && move.PromotionType() == PieceType::Queen); };
                moves.erase(eastl::remove_if(moves.begin(), moves.end(), quiet), moves.end());
                if (moves.empty()) {
                    _value = best;
                    return false;
                }
            }

            Frame& frame = Acquire(ply);
            frame.kind = FrameKind::Quiet;
            frame.alpha = alpha;
            frame.beta = beta;
            frame.best = best;
            frame.next = 0;
            frame.tableUsable = false;
            Order(moves, Move{}, kNoKillerPly, frame.moves);
            return true;
        }

        void Searcher::Finish(Frame& frame) {
            if (frame.kind == FrameKind::Root) {
                FinishIteration(frame);
                return;
            }
            // A subtree that returned a draw because of the history is not stored: the same position reached by another path
            // may not have that draw
            if (frame.tableUsable && _historyDraws == frame.historyDrawsBefore) {
                const TableBound bound = frame.best >= frame.beta ? TableBound::Lower : (frame.best <= frame.windowAlpha ? TableBound::Upper : TableBound::Exact);
                // Nothing reached alpha, so no move of a fail-low node is known to be best
                _table->Store(_position.Key(), bound == TableBound::Upper ? Move{} : frame.bestMove, ScoreToTable(frame.best, _top), frame.depth, bound);
            }
            _value = frame.best;
            --_top;
            _returning = true;
        }

        void Searcher::FinishIteration(const Frame& root) {
            // The root never cuts off (its window is open), so its best score is its alpha
            _best = root.bestMove;
            _result.best = _best;
            _result.score = root.best;
            _result.depth = _depth;
            _result.complete = true;
            _top = -1;
            // A mate that is already inside the depth searched cannot get shorter with more depth
            if (IsMateWithin(root.best, _depth)) {
                _phase = SearchPhase::Finished;
            } else if (_depth < _maxDepth && LimitsReached()) {
                _stopped = true;
                _phase = SearchPhase::Finished;
            } else if (_depth >= _maxDepth) {
                _phase = SearchPhase::Finished;
            } else {
                ++_depth;
            }
        }

        void Searcher::Abort() noexcept {
            for (int i = 0; i < _top; ++i) {
                _position.UndoMove();
            }
            _top = -1;
            _phase = SearchPhase::Finished;
        }

        bool Searcher::Begin() {
            if (LimitsReached()) {
                _stopped = true;
                _phase = SearchPhase::Finished;
                return false;
            }

            _rootMoves = MoveGenerator::GenerateLegal(_position);
            if (_rootMoves.empty()) {
                _result.complete = true;
                _result.score = _position.IsInCheck() ? -MateIn(0) : kDrawScore;
                _phase = SearchPhase::Finished;
                return false;
            }
            if (!_settings.excludedRootMoves.empty()) {
                const auto excluded = [this](Move move) { return eastl::find(_settings.excludedRootMoves.begin(), _settings.excludedRootMoves.end(), move) != _settings.excludedRootMoves.end(); };
                _rootMoves.erase(eastl::remove_if(_rootMoves.begin(), _rootMoves.end(), excluded), _rootMoves.end());
                ASSERT(!_rootMoves.empty(), "Search: every legal root move is excluded");
                if (_rootMoves.empty()) {
                    _phase = SearchPhase::Finished;
                    return false;
                }
            }

            _maxDepth = eastl::clamp(_settings.maxDepth, 1, kMaxSearchDepth);
            if (_settings.tableEntries != 0 && _maxDepth >= kMinTableDepth) {
                _table.emplace(_settings.tableEntries);
            }
            _depth = 1;
            _phase = SearchPhase::Searching;
            return true;
        }

        void Searcher::StartIteration() {
            // The first iteration is never stopped: it is what makes the answer exist
            _stopEnabled = _depth > 1;
            Frame& root = Acquire(0);
            root.kind = FrameKind::Root;
            root.depth = _depth;
            root.alpha = -kInfinity;
            root.beta = kInfinity;
            root.best = -kInfinity;
            root.bestMove = Move{};
            root.next = 0;
            root.tableUsable = false;
            Order(_rootMoves, _best, kNoKillerPly, root.moves);
            _returning = false;
        }

        SearchProgress Searcher::Loop(uint64_t sliceEnd) {
            for (;;) {
                if (_top < 0) {
                    StartIteration();
                }
                Frame& frame = *_frames[static_cast<size_t>(_top)];
                // The state of the frame lives in locals while its moves are tried (registers) and goes back to the
                // frame when the loop leaves it: to descend into a child that has moves of its own, to pause, or to finish
                const int ply = _top;
                const FrameKind kind = frame.kind;
                const int depth = frame.depth;
                const int beta = frame.beta;
                const size_t count = frame.moves.size();
                int alpha = frame.alpha;
                int best = frame.best;
                Move bestMove = frame.bestMove;
                Move current = frame.current;
                bool currentQuiet = frame.currentQuiet;
                size_t next = frame.next;

                // Takes the score of the move just searched, from the mover's side; true when it cuts the node off
                const auto absorb = [&](int score) {
                    if (score > best) {
                        best = score;
                        bestMove = current;
                        if (score > alpha) {
                            alpha = score;
                            if (alpha >= beta) {
                                if (kind == FrameKind::Full && currentQuiet && _settings.quietOrdering) {
                                    _quiet.Reward(current, frame.mover, ply, depth);
                                }
                                return true;
                            }
                        }
                    }
                    return false;
                };
                const auto store = [&]() {
                    frame.alpha = alpha;
                    frame.best = best;
                    frame.bestMove = bestMove;
                    frame.current = current;
                    frame.currentQuiet = currentQuiet;
                    frame.next = next;
                };

                // Whether the frame is over: it cut off, or has no move left to try
                bool over = false;
                if (_returning) {
                    _position.UndoMove();
                    if (_stopped) {
                        Abort();
                        return SearchProgress::Done;
                    }
                    _returning = false;
                    over = absorb(-_value);
                }
                // The moves of the frame; a child that is a leaf is taken back and scored right here
                bool descended = false;
                while (!over && next < count) {
                    // The only place a slice ends: before a child is entered (and counted), so a pause changes no count
                    if (_nodes >= sliceEnd) {
                        store();
                        return SearchProgress::Paused;
                    }

                    PickBest(frame.moves, next);
                    current = frame.moves[next].move;
                    ++next;
                    if (kind == FrameKind::Full) {
                        currentQuiet = !IsCapture(_position, current) && !current.IsPromotion();
                    }
                    _position.DoMove(current);
                    if (kind == FrameKind::Quiet ? EnterQuiet(-beta, -alpha, ply + 1) : EnterFull(depth - 1, -beta, -alpha, ply + 1)) {
                        descended = true;
                        break;
                    }
                    _position.UndoMove();
                    if (_stopped) {
                        Abort();
                        return SearchProgress::Done;
                    }
                    over = absorb(-_value);
                }
                store();
                if (descended) {
                    continue;
                }
                Finish(frame);
                if (_phase == SearchPhase::Finished) {
                    return SearchProgress::Done;
                }
            }
        }

        SearchProgress Searcher::Advance(uint64_t maxNodes) {
            if (_phase == SearchPhase::Finished) {
                return SearchProgress::Done;
            }
            if (_phase == SearchPhase::NotStarted) {
                if (!Begin()) {
                    return SearchProgress::Done;
                }
            } else if (_stopEnabled && LimitsReached()) {
                // A token cancelled or a deadline passed while the search was paused: seen at once, not after the next poll of the nodes
                _stopped = true;
                Abort();
                return SearchProgress::Done;
            }
            // Zero would pause before the first node for ever
            const uint64_t slice = maxNodes == 0 ? 1 : maxNodes;
            const uint64_t sliceEnd = slice > UINT64_MAX - _nodes ? UINT64_MAX : _nodes + slice;
            return Loop(sliceEnd);
        }

        SearchResult Searcher::GetResult() const noexcept {
            SearchResult result = _result;
            result.nodes = _nodes;
            result.stopped = _stopped;
            return result;
        }

    }  // namespace

    struct SearchJob::Impl {
        Impl(Position startPosition, SearchSettings searchSettings, SearchControl searchControl)
            : position(eastl::move(startPosition)), settings(eastl::move(searchSettings)), control(eastl::move(searchControl)), searcher(position, settings, control) {}

        Position position;
        SearchSettings settings;
        SearchControl control;
        Searcher searcher;
    };

    SearchJob::SearchJob(Position position, SearchSettings settings, SearchControl control) : _impl(eastl::make_unique<Impl>(eastl::move(position), eastl::move(settings), eastl::move(control))) {}

    SearchJob::~SearchJob() = default;
    SearchJob::SearchJob(SearchJob&&) noexcept = default;
    SearchJob& SearchJob::operator=(SearchJob&&) noexcept = default;

    SearchProgress SearchJob::Advance(uint64_t maxNodes) {
        return _impl->searcher.Advance(maxNodes);
    }

    bool SearchJob::IsDone() const noexcept {
        return _impl->searcher.IsDone();
    }

    SearchResult SearchJob::GetResult() const noexcept {
        return _impl->searcher.GetResult();
    }

    int SearchJob::FinishedDepth() const noexcept {
        return _impl->searcher.FinishedDepth();
    }

    SearchResult Search(Position& position, const SearchSettings& settings, const SearchControl& control) {
        Searcher searcher(position, settings, control);
        (void)searcher.Advance(UINT64_MAX);
        return searcher.GetResult();
    }

}  // namespace BECore::Chess
