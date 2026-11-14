#include "ChessTestUtil.h"
#include "SeededRandom.h"
#include "StockfishUtil.h"

#include <Chess/Engine/UciProcess.h>
#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/Position.h>
#include <EASTL/sort.h>
#include <chrono>

/// Stockfish as the independent referee of our rules: our MoveGenerator and Stockfish's `go perft` list the same moves in the same positions.
/// This is the in-process twin of `tools/stockfish_bridge.py --cross-check`. The test talks to the process directly through UciProcess
/// (UciEngine has no raw commands), skips itself without a Stockfish (StockfishUtil.h) and starts one process per case
namespace BECore::Chess::Tests {

    namespace {

        constexpr auto kHandshakeTimeout = std::chrono::seconds(10);
        constexpr auto kLineTimeout = std::chrono::seconds(5);

        constexpr StringView kMateFen = "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1";
        constexpr StringView kStalemateFen = "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1";

        constexpr uint64_t kWalkSeed = 0x9E3779B97F4A7C15ULL;
        constexpr int kWalkGames = 3;
        constexpr size_t kWalkPlies = 70;
        constexpr size_t kWalkFloor = 150;  ///< positions compared by the walk: fewer means it stopped early

        /// What `go perft N` printed: the leaves under each root move (in the order Stockfish gave them) and the total
        struct PerftResult {
            Vector<Pair<String, uint64_t>> divide;
            uint64_t nodes = 0;
        };

        /// The moves of a `go perft 1` result, sorted
        Vector<String> MoveTexts(const PerftResult& result) {
            Vector<String> moves;
            for (const auto& entry : result.divide) {
                moves.push_back(entry.first);
            }
            eastl::sort(moves.begin(), moves.end());
            return moves;
        }

        /// The number in @p text (white space around it allowed), or nothing for anything else
        Optional<uint64_t> ParseCount(StringView text) {
            while (!text.empty() && text.front() == ' ') {
                text.remove_prefix(1);
            }
            while (!text.empty() && (text.back() == ' ' || text.back() == '\r')) {
                text.remove_suffix(1);
            }
            uint64_t value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc() || end != text.data() + text.size()) {
                return std::nullopt;
            }
            return value;
        }

        /// A move as UCI spells it: e2e4, e7e8q
        bool IsMoveText(StringView text) {
            const auto file = [](char c) { return c >= 'a' && c <= 'h'; };
            const auto rank = [](char c) { return c >= '1' && c <= '8'; };
            if (text.size() != 4 && text.size() != 5) {
                return false;
            }
            const bool squares = file(text[0]) && rank(text[1]) && file(text[2]) && rank(text[3]);
            return squares && (text.size() == 4 || StringView("qrbn").find(text[4]) != StringView::npos);
        }

        /// A Stockfish child, spoken to in lines; a failure is reported with ADD_FAILURE and names the FEN it happened on
        class StockfishReferee {
        public:
            /// Starts the program at @p path and does the `uci` / `isready` handshake once; empty on a failure (already reported)
            static UniquePtr<StockfishReferee> Start(const String& path) {
                ProcessLaunch launch;
                launch.path = path;
                auto process = UciProcess::Start(launch);
                if (!process) {
                    ADD_FAILURE() << "could not start Stockfish at " << path.c_str() << ": " << process.error().detail.c_str();
                    return nullptr;
                }
                UniquePtr<StockfishReferee> referee(new StockfishReferee(std::move(*process)));
                if (!referee->Handshake()) {
                    return nullptr;
                }
                return referee;
            }

            ~StockfishReferee() {
                if (_process->IsRunning()) {
                    static_cast<void>(_process->WriteLine("quit"));
                }
                _process->Close();
            }

            /// `go perft @p depth` from @p fen; nothing on a failure (reported, with the FEN)
            Optional<PerftResult> Perft(StringView fen, int depth) {
                const String where(fen.data(), fen.size());
                if (!Send(Format("position fen {}", fen), where) || !Send(Format("go perft {}", depth), where)) {
                    return std::nullopt;
                }
                PerftResult result;
                while (true) {
                    const auto line = _process->ReadLine(kLineTimeout);
                    if (!line) {
                        ADD_FAILURE() << "no answer to go perft " << depth << " for " << where.c_str() << ": " << Describe(line.error()).c_str();
                        return std::nullopt;
                    }
                    const StringView text(line->data(), line->size());
                    constexpr StringView kTotal = "Nodes searched:";
                    if (text.substr(0, kTotal.size()) == kTotal) {
                        const Optional<uint64_t> total = ParseCount(text.substr(kTotal.size()));
                        if (!total) {
                            ADD_FAILURE() << "no count in '" << line->c_str() << "' for " << where.c_str();
                            return std::nullopt;
                        }
                        result.nodes = *total;
                        return result;
                    }
                    const size_t colon = text.find(": ");
                    if (colon != StringView::npos && IsMoveText(text.substr(0, colon))) {
                        const Optional<uint64_t> count = ParseCount(text.substr(colon + 2));
                        if (!count) {
                            ADD_FAILURE() << "no count in '" << line->c_str() << "' for " << where.c_str();
                            return std::nullopt;
                        }
                        result.divide.emplace_back(String(text.data(), colon), *count);
                    }
                }
            }

            /// The legal moves of @p fen as Stockfish counts them, sorted; nothing on a failure
            Optional<Vector<String>> Moves(StringView fen) {
                const Optional<PerftResult> result = Perft(fen, 1);
                if (!result) {
                    return std::nullopt;
                }
                return MoveTexts(*result);
            }

        private:
            explicit StockfishReferee(UniquePtr<UciProcess> process) : _process(std::move(process)) {}

            bool Send(const String& line, const String& where) {
                const auto written = _process->WriteLine(line);
                if (!written) {
                    ADD_FAILURE() << "Stockfish did not take '" << line.c_str() << "' for " << where.c_str() << ": " << Describe(written.error()).c_str();
                }
                return written.has_value();
            }

            bool WaitFor(StringView expected) {
                while (true) {
                    const auto line = _process->ReadLine(kHandshakeTimeout);
                    if (!line) {
                        ADD_FAILURE() << "Stockfish did not say '" << String(expected.data(), expected.size()).c_str() << "': " << Describe(line.error()).c_str();
                        return false;
                    }
                    if (*line == expected) {
                        return true;
                    }
                }
            }

            bool Handshake() {
                return Send("uci", "the handshake") && WaitFor("uciok") && Send("isready", "the handshake") && WaitFor("readyok");
            }

            UniquePtr<UciProcess> _process;
        };

        /// "e2e4 g1f3" style list of the elements of @p moves that @p others does not hold
        String Missing(const Vector<String>& moves, const Vector<String>& others) {
            String text;
            for (const String& move : moves) {
                if (eastl::find(others.begin(), others.end(), move) == others.end()) {
                    text = text.empty() ? move : Format("{} {}", text, move);
                }
            }
            return text;
        }

        Vector<String> OurMoves(const Position& position) {
            Vector<String> moves;
            for (const Move move : MoveGenerator::GenerateLegal(position)) {
                moves.push_back(move.ToUci());
            }
            eastl::sort(moves.begin(), moves.end());
            return moves;
        }

        /// Compares the move sets for @p position; true when equal, else the failure lists the moves only one side has
        bool ExpectSameMoves(StockfishReferee& referee, const Position& position) {
            const String fen = position.ToFen();
            const Optional<Vector<String>> theirs = referee.Moves(fen);
            if (!theirs) {
                return false;
            }
            const Vector<String> ours = OurMoves(position);
            if (ours == *theirs) {
                return true;
            }
            ADD_FAILURE() << "the legal moves differ in " << fen.c_str() << "\n  only ours:      " << Missing(ours, *theirs).c_str() << "\n  only Stockfish: " << Missing(*theirs, ours).c_str();
            return false;
        }

        uint64_t OurPerft(Position& position, int depth) {
            const MoveList moves = MoveGenerator::GenerateLegal(position);
            if (depth <= 1) {
                return moves.size();
            }
            uint64_t nodes = 0;
            for (const Move move : moves) {
                position.DoMove(move);
                nodes += OurPerft(position, depth - 1);
                position.UndoMove();
            }
            return nodes;
        }

        /// Our perft divide of @p fen at @p depth against Stockfish's: every root move has the same count, and the totals agree
        void ExpectSamePerft(StockfishReferee& referee, StringView fen, int depth, uint64_t expectedNodes) {
            const Optional<PerftResult> theirs = referee.Perft(fen, depth);
            ASSERT_TRUE(theirs.has_value());
            EXPECT_EQ(theirs->nodes, expectedNodes) << "Stockfish at depth " << depth << " of " << String(fen.data(), fen.size()).c_str();

            Position position = PositionFromFen(fen);
            uint64_t total = 0;
            for (const auto& [text, count] : theirs->divide) {
                const auto move = MoveGenerator::ParseUci(position, text);
                if (!move) {
                    ADD_FAILURE() << "Stockfish has the root move " << text.c_str() << ", which our generator does not know, in " << String(fen.data(), fen.size()).c_str();
                    continue;
                }
                position.DoMove(*move);
                const uint64_t ours = depth <= 1 ? 1 : OurPerft(position, depth - 1);
                position.UndoMove();
                EXPECT_EQ(ours, count) << "leaves under " << text.c_str() << " at depth " << depth << " of " << String(fen.data(), fen.size()).c_str();
                total += ours;
            }
            EXPECT_EQ(total, expectedNodes) << "our total at depth " << depth;
            EXPECT_EQ(OurPerft(position, depth), expectedNodes) << "our perft at depth " << depth;
        }

    }  // namespace

    TEST(StockfishRefereeTest, TheSamePositionsOfPerft) {
        // The positions of PerftTest: castling both ways, pins, en passant, promotions (also with a capture) and a king in check all show up
        SKIP_WITHOUT_STOCKFISH(path);
        const auto referee = StockfishReferee::Start(*path);
        ASSERT_NE(referee, nullptr);

        for (const StringView fen : {Position::kStartFen, kKiwipeteFen, kPosition3Fen, kPosition4Fen, kPosition5Fen, kPosition6Fen}) {
            SCOPED_TRACE(String(fen.data(), fen.size()).c_str());
            const Position position = PositionFromFen(fen);
            EXPECT_TRUE(ExpectSameMoves(*referee, position));
        }
        // Position 4 with the colors exchanged: Black to move, the same promotions and castling the other way
        const Position mirrored = PositionFromFen("r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1");
        EXPECT_TRUE(ExpectSameMoves(*referee, mirrored));
    }

    TEST(StockfishRefereeTest, TheSameMovesAlongSeededRandomGames) {
        SKIP_WITHOUT_STOCKFISH(path);
        const auto referee = StockfishReferee::Start(*path);
        ASSERT_NE(referee, nullptr);

        SeededRandom random(kWalkSeed);
        size_t compared = 0;
        size_t differing = 0;
        for (int game = 0; game < kWalkGames && differing == 0; ++game) {
            Position position = PositionFromFen(Position::kStartFen);
            for (size_t ply = 0; ply <= kWalkPlies; ++ply) {
                ++compared;
                if (!ExpectSameMoves(*referee, position)) {
                    ++differing;
                    break;  // the walk past a position that is wrong says little, and the failure names it
                }
                const MoveList moves = MoveGenerator::GenerateLegal(position);
                if (moves.empty() || ply == kWalkPlies) {
                    break;
                }
                position.DoMove(moves[random.Below(moves.size())]);
            }
        }
        EXPECT_EQ(differing, 0u);
        EXPECT_GE(compared, kWalkFloor) << "the walks ended early";
    }

    TEST(StockfishRefereeTest, AFinishedGameHasNoMoveOnEitherSide) {
        SKIP_WITHOUT_STOCKFISH(path);
        const auto referee = StockfishReferee::Start(*path);
        ASSERT_NE(referee, nullptr);

        for (const StringView fen : {kMateFen, kStalemateFen}) {
            SCOPED_TRACE(String(fen.data(), fen.size()).c_str());
            const Optional<Vector<String>> theirs = referee->Moves(fen);
            ASSERT_TRUE(theirs.has_value());
            EXPECT_TRUE(theirs->empty());
            EXPECT_TRUE(MoveGenerator::GenerateLegal(PositionFromFen(fen)).empty());
        }
    }

    TEST(StockfishRefereeTest, TheSameLeavesUnderEveryMove) {
        // The 8902 leaves of the start position at depth 3 are the number PerftTest holds; Kiwipete at depth 3 has 97862
        SKIP_WITHOUT_STOCKFISH(path);
        const auto referee = StockfishReferee::Start(*path);
        ASSERT_NE(referee, nullptr);

        ASSERT_NO_FATAL_FAILURE(ExpectSamePerft(*referee, Position::kStartFen, 3, 8902));
        ASSERT_NO_FATAL_FAILURE(ExpectSamePerft(*referee, kKiwipeteFen, 3, 97862));
    }

}  // namespace BECore::Chess::Tests
