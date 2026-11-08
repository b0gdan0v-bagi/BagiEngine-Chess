#include "Search.h"

#include <Chess/Game/GameStatus.h>
#include <Chess/Game/MoveGenerator.h>

namespace BECore::Chess {

    namespace {

        /// Larger than any score, so that the first move of a node always beats "nothing found yet"
        constexpr int kInfinity = kMateScore + 1000;

        /// A check is searched one ply deeper only up to this ply, so a perpetual check cannot grow the tree without end
        constexpr int kMaxExtensionPly = 32;
        /// No line is searched further from the root than this many plies, quiescence included: the deepest full-width line is
        /// kMaxSearchDepth plus the extensions, and what is left at that ply is judged by the static score. The move lists of a node
        /// live on the stack: a Negamax or Quiescence frame takes about 3 KB in a Debug build (measured, macOS), so 96 plies are
        /// about 300 KB, which fits the 512 KB that a std::thread of the Background pool gets on macOS (Linux gives 8 MB, Windows 1 MB).
        /// The killers and the history of Search() (QuietMoves, about 33 KB) are on that stack too; the second static_assert below counts them
        constexpr int kMaxPly = kMaxSearchDepth + kMaxExtensionPly;
        /// The size of a Negamax or Quiescence frame in a Debug build (measured on macOS) and the stack of a Background pool thread there (a std::thread with the default
        /// 512 KB); the assert keeps the comment above honest if kMaxSearchDepth or the extension grows
        constexpr size_t kApproxFrameBytes = 3072;
        constexpr size_t kBackgroundStackBytes = 512 * 1024;
        static_assert(static_cast<size_t>(kMaxPly) * kApproxFrameBytes < kBackgroundStackBytes, "the deepest line must fit the stack of a Background thread");

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
        // Quiet moves, below the promotions: the two killers of the ply, then the history, which is kept under kHistoryMax so
        // that it never reaches the second killer
        constexpr int kFirstKillerScore = 1 << 15;
        constexpr int kSecondKillerScore = kFirstKillerScore - 1;
        constexpr int kHistoryMax = 1 << 14;
        static_assert(kHistoryMax < kSecondKillerScore && kSecondKillerScore < kFirstKillerScore && kFirstKillerScore < kPromotionScore);
        /// Order() is given this ply by the nodes that do not use killers and history
        constexpr int kNoKillerPly = -1;
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
        static_assert(static_cast<size_t>(kMaxPly) * kApproxFrameBytes + sizeof(QuietMoves) < kBackgroundStackBytes,
                      "the deepest line and the killers and the history must fit the stack of a Background thread");

        class Searcher {
        public:
            Searcher(Position& position, const SearchSettings& settings, const SearchControl& control) : _position(position), _settings(settings), _control(control) {}

            SearchResult Run();

        private:
            /// Counts a node and looks at the limits: the node limit in every node, the clock and the token every kSearchPollInterval
            void CountNode() noexcept;
            [[nodiscard]] bool LimitsReached() const;
            /// @p ply is where the killers are looked up; kNoKillerPly orders the quiet moves as the generator gave them
            [[nodiscard]] ScoredMoves Order(const MoveList& moves, Move preferred, int ply = kNoKillerPly) const;

            int SearchRoot(int depth, const MoveList& rootMoves, Move& best);
            int Negamax(int depth, int alpha, int beta, int ply);
            int Quiescence(int alpha, int beta, int ply);

            Position& _position;
            const SearchSettings& _settings;
            const SearchControl& _control;
            uint64_t _nodes = 0;
            bool _stopped = false;
            /// Off during the first iteration, which must finish
            bool _stopEnabled = false;
            QuietMoves _quiet;
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

        ScoredMoves Searcher::Order(const MoveList& moves, Move preferred, int ply) const {
            const bool useQuiet = _settings.quietOrdering && ply != kNoKillerPly;
            const PieceColor color = _position.SideToMove();
            ScoredMoves scored;
            for (const Move move : moves) {
                int score = OrderScore(_position, move, preferred);
                if (useQuiet && score == 0) {
                    score = _quiet.Score(move, color, ply);
                }
                scored.push_back(ScoredMove{move, score});
            }
            return scored;
        }

        int Searcher::Negamax(int depth, int alpha, int beta, int ply) {
            CountNode();
            if (_stopped) {
                return 0;
            }
            if (ply >= kMaxPly) {
                return Evaluate(_position);
            }
            if (_position.RepetitionCount() >= kRepetitionDrawCount || _position.IsInsufficientMaterial()) {
                return kDrawScore;
            }

            const bool inCheck = _position.IsInCheck();
            if (inCheck && _settings.checkExtension && ply < kMaxExtensionPly) {
                ++depth;
            }
            if (depth <= 0) {
                return _settings.quiescence ? Quiescence(alpha, beta, ply) : Evaluate(_position);
            }

            const MoveList moves = MoveGenerator::GenerateLegal(_position);
            if (moves.empty()) {
                return inCheck ? -MateIn(ply) : kDrawScore;
            }
            if (_position.HalfmoveClock() >= kFiftyMovePlies) {
                return kDrawScore;
            }

            ScoredMoves scored = Order(moves, Move{}, ply);
            const PieceColor mover = _position.SideToMove();
            int best = -kInfinity;
            for (size_t i = 0; i < scored.size(); ++i) {
                PickBest(scored, i);
                const Move move = scored[i].move;
                const bool quiet = !IsCapture(_position, move) && !move.IsPromotion();
                _position.DoMove(move);
                const int score = -Negamax(depth - 1, -beta, -alpha, ply + 1);
                _position.UndoMove();
                if (_stopped) {
                    return 0;
                }
                if (score > best) {
                    best = score;
                    if (score > alpha) {
                        alpha = score;
                        if (alpha >= beta) {
                            if (quiet && _settings.quietOrdering) {
                                _quiet.Reward(move, mover, ply, depth);
                            }
                            break;
                        }
                    }
                }
            }
            return best;
        }

        int Searcher::Quiescence(int alpha, int beta, int ply) {
            CountNode();
            if (_stopped) {
                return 0;
            }
            if (ply >= kMaxPly) {
                return Evaluate(_position);
            }

            // In check there is no standing pat: every evasion is looked at, and no evasion is a mate
            const bool inCheck = _position.IsInCheck();
            int best = -kInfinity;
            if (!inCheck) {
                best = Evaluate(_position);
                if (best >= beta) {
                    return best;
                }
                alpha = eastl::max(alpha, best);
            }

            MoveList moves = MoveGenerator::GenerateLegal(_position);
            if (inCheck) {
                if (moves.empty()) {
                    return -MateIn(ply);
                }
            } else {
                // Captures and promotions to a queen; the other promotions are left to the full-width part of the search
                const auto quiet = [this](Move move) { return !IsCapture(_position, move) && !(move.IsPromotion() && move.PromotionType() == PieceType::Queen); };
                moves.erase(eastl::remove_if(moves.begin(), moves.end(), quiet), moves.end());
            }

            ScoredMoves scored = Order(moves, Move{});
            for (size_t i = 0; i < scored.size(); ++i) {
                PickBest(scored, i);
                _position.DoMove(scored[i].move);
                const int score = -Quiescence(-beta, -alpha, ply + 1);
                _position.UndoMove();
                if (_stopped) {
                    return 0;
                }
                if (score > best) {
                    best = score;
                    if (score > alpha) {
                        alpha = score;
                        if (alpha >= beta) {
                            break;
                        }
                    }
                }
            }
            return best;
        }

        int Searcher::SearchRoot(int depth, const MoveList& rootMoves, Move& best) {
            ScoredMoves scored = Order(rootMoves, best);
            int alpha = -kInfinity;
            Move bestMove;
            for (size_t i = 0; i < scored.size(); ++i) {
                PickBest(scored, i);
                _position.DoMove(scored[i].move);
                const int score = -Negamax(depth - 1, -kInfinity, -alpha, 1);
                _position.UndoMove();
                if (_stopped) {
                    return 0;
                }
                if (score > alpha) {
                    alpha = score;
                    bestMove = scored[i].move;
                }
            }
            best = bestMove;
            return alpha;
        }

        SearchResult Searcher::Run() {
            SearchResult result;
            if (LimitsReached()) {
                result.stopped = true;
                return result;
            }

            const MoveList rootMoves = MoveGenerator::GenerateLegal(_position);
            if (rootMoves.empty()) {
                result.complete = true;
                result.score = _position.IsInCheck() ? -MateIn(0) : kDrawScore;
                return result;
            }

            const int maxDepth = eastl::clamp(_settings.maxDepth, 1, kMaxSearchDepth);
            Move best;
            for (int depth = 1; depth <= maxDepth; ++depth) {
                // The first iteration is never stopped: it is what makes the answer exist
                _stopEnabled = depth > 1;
                const int score = SearchRoot(depth, rootMoves, best);
                if (_stopped) {
                    break;
                }
                result.best = best;
                result.score = score;
                result.depth = depth;
                result.complete = true;
                // A mate that is already inside the depth searched cannot get shorter with more depth
                if (IsMateWithin(score, depth)) {
                    break;
                }
                if (depth < maxDepth && LimitsReached()) {
                    _stopped = true;
                    break;
                }
            }
            result.nodes = _nodes;
            result.stopped = _stopped;
            return result;
        }

    }  // namespace

    SearchResult Search(Position& position, const SearchSettings& settings, const SearchControl& control) {
        return Searcher(position, settings, control).Run();
    }

}  // namespace BECore::Chess
