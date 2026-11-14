#include "ChessTestUtil.h"

#include <Chess/Engine/Search.h>
#include <Chess/Game/GameStatus.h>
#include <Chess/Game/MoveGenerator.h>
#include <thread>

/// The alpha-beta search: pure functions of a position (no engine services), the threads in two tests are the test's own
namespace BECore::Chess::Tests {

    namespace {

        using Clock = std::chrono::steady_clock;

        /// Plies a mate in one, two needs: the search sees the mate after the reply to the key move, one ply more than the key's own
        constexpr int kMateInOneDepth = 2;
        constexpr int kMateInTwoDepth = 4;
        constexpr int kMinimaxDepth = 3;
        constexpr int kOrderingDepth = 4;
        /// The depths of the comparisons with and without the table; the table is built from a depth of 3
        constexpr int kTableDepth = 4;
        constexpr int kDeeperTableDepth = 5;
        constexpr int kMateInThreeDepth = 6;
        /// A table of one and of four entries: every store evicts or is evicted, so nothing in the search may rely on an entry staying
        constexpr size_t kTinyTables[] = {1, 4};
        /// A queen is worth well over this, so a score above it is "a queen up"
        constexpr int kQueenUpScore = 500;
        constexpr int kDeepDepth = 64;
        /// The nodes after which a search of the tests is stopped: a few polls of the clock, a few iterations of a middlegame
        constexpr uint64_t kNodeBudget = 4000;
        constexpr auto kCancelAfter = std::chrono::milliseconds(50);
        /// The time a stopped search has to return in. The poll is a thousand nodes, a millisecond; the rest is the machine. Only the
        /// DISABLED_StopLatencyYardstick uses it: a normal test never compares a duration to an upper bound (tests/CLAUDE.md)
        constexpr auto kStopBudget = std::chrono::milliseconds(1000);

        SearchResult SearchDepth(Position& position, int depth) {
            SearchSettings settings;
            settings.maxDepth = depth;
            return Search(position, settings);
        }

        SearchResult SearchWithTable(Position& position, int depth, size_t tableEntries) {
            SearchSettings settings;
            settings.maxDepth = depth;
            settings.tableEntries = tableEntries;
            return Search(position, settings);
        }

        SearchResult SearchFen(eastl::string_view fen, int depth) {
            Position position = PositionFromFen(fen);
            return SearchDepth(position, depth);
        }

        bool IsLegalIn(const Position& position, Move move) {
            const MoveList moves = MoveGenerator::GenerateLegal(position);
            return eastl::find(moves.begin(), moves.end(), move) != moves.end();
        }

        bool IsMate(const Position& position) {
            return position.IsInCheck() && MoveGenerator::GenerateLegal(position).empty();
        }

        /// What a position is, as far as a search could change it: the key and the FEN
        struct Snapshot {
            ZobristKey key = 0;
            eastl::string fen;
            bool operator==(const Snapshot&) const = default;
        };

        Snapshot SnapshotOf(const Position& position) {
            return Snapshot{position.Key(), position.ToFen()};
        }

        const eastl::string_view kPerftFens[] = {Position::kStartFen, kKiwipeteFen, kPosition3Fen, kPosition4Fen, kPosition5Fen, kPosition6Fen};

        /// Whether the side to move can mate at once
        bool HasMateInOne(Position& position) {
            for (const Move move : MoveGenerator::GenerateLegal(position)) {
                position.DoMove(move);
                const bool mate = IsMate(position);
                position.UndoMove();
                if (mate) {
                    return true;
                }
            }
            return false;
        }

        /// The definition of "mate in two with this first move", without the search: whatever the other side replies (and it has a
        /// reply), the side that moved can mate at once
        bool ForcesMateInTwo(Position& position, Move first) {
            position.DoMove(first);
            const MoveList replies = MoveGenerator::GenerateLegal(position);
            bool forced = !replies.empty();
            for (const Move reply : replies) {
                position.DoMove(reply);
                forced = HasMateInOne(position);
                position.UndoMove();
                if (!forced) {
                    break;
                }
            }
            position.UndoMove();
            return forced;
        }

        eastl::vector<Move> MateInTwoKeys(Position& position) {
            eastl::vector<Move> keys;
            for (const Move move : MoveGenerator::GenerateLegal(position)) {
                if (ForcesMateInTwo(position, move)) {
                    keys.push_back(move);
                }
            }
            return keys;
        }

        /// Plain negamax over the rules the search has, with nothing else: no pruning, no extension, no quiescence, the static
        /// evaluation at depth 0. It is the proof that the pruning does not change the score
        int Minimax(Position& position, int depth, int ply, uint64_t& nodes) {
            ++nodes;
            if (ply > 0 && (position.RepetitionCount() >= 2 || position.IsInsufficientMaterial())) {
                return kDrawScore;
            }
            if (depth <= 0) {
                return Evaluate(position);
            }
            const MoveList moves = MoveGenerator::GenerateLegal(position);
            if (moves.empty()) {
                return position.IsInCheck() ? -MateIn(ply) : kDrawScore;
            }
            if (position.HalfmoveClock() >= kFiftyMovePlies) {
                return kDrawScore;
            }
            int best = -2 * kMateScore;
            for (const Move move : moves) {
                position.DoMove(move);
                best = eastl::max(best, -Minimax(position, depth - 1, ply + 1, nodes));
                position.UndoMove();
            }
            return best;
        }

    }  // namespace

    // -- mates ---------------------------------------------------------------------------------------------------------------

    TEST(SearchTest, WhiteFindsAMateInOne) {
        // The back rank: the rook on a1 goes to a8 and the pawns shut the king in
        Position position = PositionFromFen("6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1");

        const SearchResult result = SearchDepth(position, kMateInOneDepth);

        EXPECT_EQ(result.best, Mv("a1a8"));
        EXPECT_EQ(result.score, MateIn(1));
    }

    TEST(SearchTest, BlackFindsAMateInOne) {
        // The same mate for Black: the rook on a8 goes to a1
        Position position = PositionFromFen("r3k3/8/8/8/8/8/5PPP/6K1 b - - 0 1");

        const SearchResult result = SearchDepth(position, kMateInOneDepth);

        EXPECT_EQ(result.best, Mv("a8a1"));
        EXPECT_EQ(result.score, MateIn(1));
    }

    TEST(SearchTest, AMateByPromotionIsFound) {
        // c8 gives check with the queen (or the rook), the king on a8 has b8 and a7, b7 are covered by the king on b6 and the new piece
        Position position = PositionFromFen("k7/2P5/1K6/8/8/8/8/8 w - - 0 1");

        const SearchResult result = SearchDepth(position, kMateInOneDepth);

        EXPECT_EQ(result.score, MateIn(1));
        ASSERT_TRUE(result.best.IsPromotion());
        position.DoMove(result.best);
        EXPECT_TRUE(IsMate(position));
    }

    TEST(SearchTest, AMateInOneEndsTheIterations) {
        // The mate is seen on the first iteration (the quiescence search looks at the evasions of a king in check and finds none),
        // and a mate that is within the depth cannot get shorter, so the deeper iterations are not run
        Position position = PositionFromFen("6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1");

        const SearchResult result = SearchDepth(position, kDeepDepth);

        EXPECT_EQ(result.score, MateIn(1));
        EXPECT_EQ(result.depth, 1);
        EXPECT_FALSE(result.stopped);
    }

    TEST(SearchTest, ARookSacrificeMatesInTwo) {
        // The well-known puzzle: Ra6! and if bxa6 then b7#, else Rxa7#. Black has no move that saves it, and nothing else of
        // White's works (a rook move on the a-file that is not a6 leaves a bishop, a pawn or the stalemate to Black)
        Position position = PositionFromFen("kbK5/pp6/1P6/8/8/8/8/R7 w - - 0 1");
        ASSERT_EQ(MateInTwoKeys(position).size(), 1U);

        const SearchResult result = SearchDepth(position, kMateInTwoDepth);

        EXPECT_EQ(result.best, Mv("a1a6"));
        EXPECT_EQ(result.score, MateIn(3));
    }

    TEST(SearchTest, ABlackMateInTwoIsFoundToo) {
        // The puzzle above seen from the other side: colors swapped, ranks flipped, Black to move
        Position position = PositionFromFen("r7/8/8/8/8/1p6/PP6/KBk5 b - - 0 1");
        ASSERT_EQ(MateInTwoKeys(position).size(), 1U);

        const SearchResult result = SearchDepth(position, kMateInTwoDepth);

        EXPECT_EQ(result.best, Mv("a8a3"));
        EXPECT_EQ(result.score, MateIn(3));
    }

    TEST(SearchTest, AQuietMateInTwoWithOneKeyMove) {
        // Found by looking at every first move with ForcesMateInTwo (above): Qg7 is the only one. The queen takes the seventh
        // rank, the king on c8 has b8 only (the rook holds the d-file), and Rd8 mates whatever it does
        Position position = PositionFromFen("2k5/8/8/8/5K2/3R2Q1/8/8 w - - 0 1");
        ASSERT_EQ(MateInTwoKeys(position).size(), 1U);

        const SearchResult result = SearchDepth(position, kMateInTwoDepth);

        EXPECT_EQ(result.best, Mv("g3g7"));
        EXPECT_EQ(result.score, MateIn(3));
    }

    TEST(SearchTest, ALongerDefenseIsWorthMore) {
        // After Ra6 Black is mated in two plies: the reply and then the mate. A lost side scores minus a mate, further from the root the better
        const SearchResult result = SearchFen("kbK5/pp6/RP6/8/8/8/8/8 b - - 0 1", kMateInTwoDepth);

        EXPECT_EQ(result.score, -MateIn(2));
    }

    TEST(SearchTest, ThePositionAfterTheEndOfTheGameHasNoMove) {
        Position mated = PositionFromFen("R5k1/5ppp/8/8/8/8/8/4K3 b - - 0 1");
        Position stalemate = PositionFromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");

        const SearchResult lost = SearchDepth(mated, 3);
        const SearchResult drawn = SearchDepth(stalemate, 3);

        EXPECT_TRUE(lost.best.IsNull());
        EXPECT_TRUE(lost.complete);
        EXPECT_EQ(lost.score, -MateIn(0));
        EXPECT_TRUE(drawn.best.IsNull());
        EXPECT_TRUE(drawn.complete);
        EXPECT_EQ(drawn.score, kDrawScore);
    }

    // -- material ------------------------------------------------------------------------------------------------------------

    TEST(SearchTest, AHangingQueenIsTaken) {
        const SearchResult result = SearchFen("4k3/p7/8/3q4/8/2N5/8/R3K3 w - - 0 1", kMateInOneDepth);

        EXPECT_EQ(result.best, Mv("c3d5"));
        EXPECT_GT(result.score, kQueenUpScore);
    }

    TEST(SearchTest, ADefendedPawnIsNotTakenWithTheQueen) {
        // Qxd5 wins a pawn and loses the queen to Qxd5. At the horizon of one ply the capture looks like a pawn up; the quiescence
        // search lets Black recapture. Without it the same search takes the pawn: the test fails if the quiescence stops mattering
        const eastl::string_view fen = "3qk3/8/8/3p4/8/8/8/3QK3 w - - 0 1";
        Position position = PositionFromFen(fen);
        SearchSettings settings;
        settings.maxDepth = 1;

        const SearchResult quiet = Search(position, settings);
        settings.quiescence = false;
        const SearchResult greedy = Search(position, settings);

        EXPECT_NE(quiet.best, Mv("d1d5"));
        EXPECT_LT(quiet.score, kPawnValue);
        EXPECT_EQ(greedy.best, Mv("d1d5"));
    }

    TEST(SearchTest, ThePawnTakesTheQueenAndNotTheRook) {
        // The pawn on e5 attacks both d6 and f6; the queen is the better prize
        const SearchResult result = SearchFen("4k3/8/3q1r2/4P3/8/8/8/4K3 w - - 0 1", kMateInOneDepth);

        EXPECT_EQ(result.best, Mv("e5d6"));
    }

    // -- draws ---------------------------------------------------------------------------------------------------------------

    TEST(SearchTest, ALostSideStalemates) {
        // Black is a queen down; the bishop takes it (c5xd4) and the king on a8 is left without a move (a7 by the bishop, b8
        // and b7 by the king on c8): a draw, which is the best Black can have
        Position position = PositionFromFen("K1k5/8/8/2b5/2pQ4/8/8/8 b - - 0 1");
        position.DoMove(Mv("c5d4"));
        ASSERT_TRUE(MoveGenerator::GenerateLegal(position).empty());
        ASSERT_FALSE(position.IsInCheck());
        position.UndoMove();

        const SearchResult result = SearchDepth(position, kMateInOneDepth);

        EXPECT_EQ(result.best, Mv("c5d4"));
        EXPECT_EQ(result.score, kDrawScore);
    }

    TEST(SearchTest, AWinningSideDoesNotStalemate) {
        // Qf7 leaves the king on h8 without a move and no check: a draw for a side that is a queen up
        Position position = PositionFromFen("7k/4Q3/8/8/8/8/8/6K1 w - - 0 1");
        position.DoMove(Mv("e7f7"));
        ASSERT_TRUE(MoveGenerator::GenerateLegal(position).empty());
        ASSERT_FALSE(position.IsInCheck());
        position.UndoMove();

        const SearchResult result = SearchDepth(position, kMateInOneDepth);

        EXPECT_NE(result.best, Mv("e7f7"));
        EXPECT_GT(result.score, kQueenUpScore);
    }

    TEST(SearchTest, ALostSideTakesTheRepetition) {
        // The queen and the black king go there and back, so the position of the start is on the board for the second time. The king
        // can step to b8 again, to a position that has already been, and that is a draw; every other move loses to the queen
        Position position = PositionFromFen("k7/8/8/8/8/8/8/3QK3 b - - 0 1");
        for (const char* uci : {"a8b8", "d1d2", "b8a8", "d2d1"}) {
            position.DoMove(Mv(uci));
        }
        ASSERT_EQ(position.RepetitionCount(), 2);

        const SearchResult result = SearchDepth(position, kMateInOneDepth);

        EXPECT_EQ(result.best, Mv("a8b8"));
        EXPECT_EQ(result.score, kDrawScore);
    }

    TEST(SearchTest, TheSamePositionWithoutItsHistoryIsLost) {
        // The control of the test above: no moves behind the position, so nothing repeats and Black is a queen down
        const SearchResult result = SearchFen("k7/8/8/8/8/8/8/3QK3 b - - 0 1", kMateInOneDepth);

        EXPECT_LT(result.score, -kQueenUpScore);
    }

    TEST(SearchTest, AWinningSideAvoidsTheRepetition) {
        // The mirror of the repetition: now the queen's side is to move and the queen move that repeats a position is a draw it does not want
        Position position = PositionFromFen("k7/8/8/8/8/8/8/3QK3 w - - 0 1");
        for (const char* uci : {"d1d2", "a8b8", "d2d1", "b8a8"}) {
            position.DoMove(Mv(uci));
        }
        ASSERT_EQ(position.RepetitionCount(), 2);

        const SearchResult result = SearchDepth(position, kMateInOneDepth);

        EXPECT_NE(result.best, Mv("d1d2"));
        EXPECT_GT(result.score, kQueenUpScore);
    }

    TEST(SearchTest, FiftyMovesWithoutProgressAreADraw) {
        // The clock says 100 plies: every move leads to a drawn position (the root itself is not judged, so it still has a move)
        const SearchResult result = SearchFen("4k3/8/8/8/8/8/8/R3K3 w - - 100 80", kMateInOneDepth);

        EXPECT_EQ(result.score, kDrawScore);
        EXPECT_FALSE(result.best.IsNull());
    }

    TEST(SearchTest, InsufficientMaterialIsADraw) {
        const SearchResult result = SearchFen("4k3/8/8/8/8/8/2B5/4K3 w - - 0 1", kMateInOneDepth);

        EXPECT_EQ(result.score, kDrawScore);
    }

    // -- the pruning is right ------------------------------------------------------------------------------------------------

    TEST(SearchTest, AlphaBetaGivesTheScoreOfMinimax) {
        // With the quiescence search and the check extension switched off by their flags the search is depth-limited alpha-beta,
        // and then its score must be the one of the plain negamax above, whatever the order the moves are tried in. The transposition
        // table is on (the default size, and a table of four entries): at depth 3 a position is reached at the plies 1 and 3 only (the side
        // to move is in its key), and the ply 3 is the horizon, so an entry is never deeper than the node that reads it and the table
        // changes no score
        const eastl::string_view fens[] = {
            Position::kStartFen, kKiwipeteFen, kPosition3Fen, kPosition4Fen, kPosition5Fen, kPosition6Fen, "kbK5/pp6/1P6/8/8/8/8/R7 w - - 0 1", "K1k5/8/8/2b5/2pQ4/8/8/8 b - - 0 1",
        };
        for (const size_t tableEntries : {kDefaultTableEntries, kTinyTables[1]}) {
            for (const eastl::string_view fen : fens) {
                Position position = PositionFromFen(fen);
                SearchSettings settings;
                settings.maxDepth = kMinimaxDepth;
                settings.quiescence = false;
                settings.checkExtension = false;
                settings.tableEntries = tableEntries;
                // The early stop for a mate within the depth is the only thing that makes the last iteration not the depth asked for
                const SearchResult pruned = Search(position, settings);
                uint64_t minimaxNodes = 0;
                const int reference = Minimax(position, pruned.depth, 0, minimaxNodes);

                EXPECT_EQ(pruned.score, reference) << fen.data() << " table " << tableEntries;
                EXPECT_LT(pruned.nodes, minimaxNodes) << fen.data() << " table " << tableEntries;
            }
        }
    }

    TEST(SearchTest, QuietOrderingSearchesFewerNodes) {
        // The killers and the history put the quiet moves that cut off before the ones that do not: the same depth in fewer nodes.
        // Measured at depth 4 (ordered / plain): start 4228 / 25970, position 3 9204 / 16556, position 5 26448 / 26566, position 6
        // 83208 / 83837. Kiwipete and position 4 are so tactical that a capture cuts almost every node (3 and 4 quiet cutoffs at
        // depth 4, counted), too few to change the order of any later node: the two orders search them the same. So: never more
        // nodes, fewer on the other four, fewer in the sum
        uint64_t orderedTotal = 0;
        uint64_t plainTotal = 0;
        int better = 0;
        for (const eastl::string_view fen : kPerftFens) {
            Position position = PositionFromFen(fen);
            SearchSettings settings;
            settings.maxDepth = kOrderingDepth;
            const SearchResult ordered = Search(position, settings);
            settings.quietOrdering = false;
            const SearchResult plain = Search(position, settings);

            EXPECT_LE(ordered.nodes, plain.nodes) << fen.data();
            EXPECT_TRUE(IsLegalIn(position, ordered.best)) << fen.data();
            better += ordered.nodes < plain.nodes ? 1 : 0;
            orderedTotal += ordered.nodes;
            plainTotal += plain.nodes;
        }
        EXPECT_EQ(better, 4);
        EXPECT_LT(orderedTotal, plainTotal);
    }

    TEST(SearchTest, QuietOrderingKeepsTheScoreOfPlainAlphaBeta) {
        // Without the quiescence search and the extension every score is exact whatever the order of the moves, so the two
        // orders must agree on it (the best move may differ between equal moves)
        for (const eastl::string_view fen : kPerftFens) {
            Position position = PositionFromFen(fen);
            SearchSettings settings;
            settings.maxDepth = kOrderingDepth;
            settings.quiescence = false;
            settings.checkExtension = false;
            const SearchResult ordered = Search(position, settings);
            settings.quietOrdering = false;
            const SearchResult plain = Search(position, settings);

            EXPECT_EQ(ordered.score, plain.score) << fen.data();
            EXPECT_EQ(ordered.depth, plain.depth) << fen.data();
        }
    }

    // -- the transposition table ---------------------------------------------------------------------------------------------

    TEST(SearchTest, TheTableSearchesFewerNodes) {
        // The same position again, reached by another order of moves, is answered from the table. Measured (with the table / without,
        // killers on in both), depth 4: Kiwipete 72179 / 90552, position 3 5125 / 9204, position 5 18652 / 26448, position 6 67226 / 83208;
        // depth 5: Kiwipete 329010 / 545821, position 5 105814 / 188297. Not every position gains at every depth (the start position
        // at depth 4: 4833 / 4228), so the cases are the ones that were measured. In these the score is the same too (not a guarantee of
        // the table: a deeper entry can answer a shallower node of another ply, so a score can differ; it is what these positions show)
        struct Case {
            eastl::string_view fen;
            int depth;
        };
        const Case cases[] = {
            {kKiwipeteFen, kTableDepth},  {kPosition3Fen, kTableDepth},      {kPosition5Fen, kTableDepth},
            {kPosition6Fen, kTableDepth}, {kKiwipeteFen, kDeeperTableDepth}, {kPosition5Fen, kDeeperTableDepth},
        };
        for (const Case& c : cases) {
            Position position = PositionFromFen(c.fen);
            const SearchResult with = SearchWithTable(position, c.depth, kDefaultTableEntries);
            const SearchResult without = SearchWithTable(position, c.depth, 0);

            EXPECT_LT(with.nodes, without.nodes) << c.fen.data() << " depth " << c.depth;
            EXPECT_TRUE(IsLegalIn(position, with.best)) << c.fen.data() << " depth " << c.depth;
            EXPECT_EQ(with.depth, without.depth) << c.fen.data() << " depth " << c.depth;
            EXPECT_EQ(with.score, without.score) << c.fen.data() << " depth " << c.depth;
        }
    }

    TEST(SearchTest, NoTableIsBuiltBelowDepthThree) {
        // The levels easy and medium search 1 and 2 plies: they pay no allocation, and a table that is not built changes nothing
        for (const int depth : {1, 2}) {
            Position position = PositionFromFen(kKiwipeteFen);
            const SearchResult with = SearchWithTable(position, depth, kDefaultTableEntries);
            const SearchResult without = SearchWithTable(position, depth, 0);

            EXPECT_EQ(with.best, without.best) << depth;
            EXPECT_EQ(with.score, without.score) << depth;
            EXPECT_EQ(with.nodes, without.nodes) << depth;
        }
    }

    TEST(SearchTest, AMateKeepsItsDistanceThroughTheTable) {
        // A mate in three moves (five plies) found at depth 6 and deeper. An entry is written at one ply and read at another, so
        // the mate score of the table is counted from the node (ScoreToTable / ScoreFromTable, tested alone in TranspositionTableTest):
        // with the table, with none and with a tiny one the score is exactly the mate in five plies, never a ply more or less. The
        // search does not make the read at another ply often (a wrong ply on the read does not change these answers, tried)
        const eastl::string_view fens[] = {"k7/8/8/3K4/8/8/8/7R w - - 0 1", "k7/8/3K3R/8/8/8/8/8 w - - 0 1", "k7/8/8/2K5/8/8/8/1R6 w - - 0 1"};
        for (const eastl::string_view fen : fens) {
            for (const int depth : {kMateInThreeDepth, kMateInThreeDepth + 2}) {
                for (const size_t tableEntries : {size_t{0}, kDefaultTableEntries, kTinyTables[0], kTinyTables[1]}) {
                    Position position = PositionFromFen(fen);

                    const SearchResult result = SearchWithTable(position, depth, tableEntries);

                    EXPECT_EQ(result.score, MateIn(5)) << fen.data() << " depth " << depth << " table " << tableEntries;
                    EXPECT_TRUE(IsLegalIn(position, result.best)) << fen.data() << " depth " << depth << " table " << tableEntries;
                }
            }
        }
    }

    TEST(SearchTest, ATinyTableChangesNoMate) {
        // A table of one or four entries is overwritten all the time. The mates of the tests above still come out with the same
        // score and, where the position has one key move, the same move
        struct Case {
            eastl::string_view fen;
            const char* key;
            int score;
        };
        const Case cases[] = {
            {"kbK5/pp6/1P6/8/8/8/8/R7 w - - 0 1", "a1a6", MateIn(3)},
            {"r7/8/8/8/8/1p6/PP6/KBk5 b - - 0 1", "a8a3", MateIn(3)},
            {"2k5/8/8/8/5K2/3R2Q1/8/8 w - - 0 1", "g3g7", MateIn(3)},
            {"kbK5/pp6/RP6/8/8/8/8/8 b - - 0 1", nullptr, -MateIn(2)},
        };
        for (const Case& c : cases) {
            for (const size_t tableEntries : {size_t{0}, kTinyTables[0], kTinyTables[1], kDefaultTableEntries}) {
                Position position = PositionFromFen(c.fen);

                const SearchResult result = SearchWithTable(position, kMateInTwoDepth, tableEntries);

                EXPECT_EQ(result.score, c.score) << c.fen.data() << " table " << tableEntries;
                EXPECT_TRUE(IsLegalIn(position, result.best)) << c.fen.data() << " table " << tableEntries;
                if (c.key != nullptr) {
                    EXPECT_EQ(result.best, Mv(c.key)) << c.fen.data() << " table " << tableEntries;
                }
            }
        }
    }

    // The draw rules and the table. A table result is the value of a position, and a draw by repetition or by the 50-move clock is
    // the value of a position together with the way to it, so these tests hold the table to three rules: a draw that depends on the history is
    // never stored, the table is not taken where the clock can reach 100 inside the subtree, and nothing lives from one call to the next

    TEST(SearchTest, ALostSideTakesTheRepetitionWithTheTable) {
        // Rule: the draw of a repeated position is returned before the table is looked at. The same test as above at depth 5, where the table is built
        for (const size_t tableEntries : {size_t{0}, kDefaultTableEntries, kTinyTables[1]}) {
            Position position = PositionFromFen("k7/8/8/8/8/8/8/3QK3 b - - 0 1");
            for (const char* uci : {"a8b8", "d1d2", "b8a8", "d2d1"}) {
                position.DoMove(Mv(uci));
            }
            ASSERT_EQ(position.RepetitionCount(), 2);

            const SearchResult result = SearchWithTable(position, kDeeperTableDepth, tableEntries);

            EXPECT_EQ(result.best, Mv("a8b8")) << tableEntries;
            EXPECT_EQ(result.score, kDrawScore) << tableEntries;
        }
    }

    TEST(SearchTest, TheSamePositionWithoutItsHistoryIsLostWithTheTable) {
        for (const size_t tableEntries : {size_t{0}, kDefaultTableEntries}) {
            Position position = PositionFromFen("k7/8/8/8/8/8/8/3QK3 b - - 0 1");

            const SearchResult result = SearchWithTable(position, kDeeperTableDepth, tableEntries);

            EXPECT_LT(result.score, -kQueenUpScore) << tableEntries;
        }
    }

    TEST(SearchTest, AWinningSideAvoidsTheRepetitionWithTheTable) {
        for (const size_t tableEntries : {size_t{0}, kDefaultTableEntries, kTinyTables[1]}) {
            Position position = PositionFromFen("k7/8/8/8/8/8/8/3QK3 w - - 0 1");
            for (const char* uci : {"d1d2", "a8b8", "d2d1", "b8a8"}) {
                position.DoMove(Mv(uci));
            }
            ASSERT_EQ(position.RepetitionCount(), 2);

            const SearchResult result = SearchWithTable(position, kDeeperTableDepth, tableEntries);

            EXPECT_NE(result.best, Mv("d1d2")) << tableEntries;
            EXPECT_GT(result.score, kQueenUpScore) << tableEntries;
        }
    }

    TEST(SearchTest, FiftyMovesWithoutProgressAreADrawWithTheTable) {
        // The clock says 100 plies: the table cannot be taken (the clock is past the bound) and nothing is stored
        for (const size_t tableEntries : {size_t{0}, kDefaultTableEntries}) {
            Position position = PositionFromFen("4k3/8/8/8/8/8/8/R3K3 w - - 100 80");

            const SearchResult result = SearchWithTable(position, kTableDepth, tableEntries);

            EXPECT_EQ(result.score, kDrawScore) << tableEntries;
            EXPECT_FALSE(result.best.IsNull()) << tableEntries;
        }
    }

    TEST(SearchTest, NoStateCrossesTwoCalls) {
        // Rule: the table is owned by one call. The position with its shuffle behind it (a draw by the repetition) and the same position
        // from the bare FEN (lost) are searched one after the other, in both orders, on fresh objects: each answer is its own, whatever the call before it stored
        const eastl::string_view fen = "k7/8/8/8/8/8/8/3QK3 b - - 0 1";
        for (const bool historyFirst : {true, false}) {
            int drawn = 0;
            int lost = 0;
            for (int call = 0; call < 2; ++call) {
                const bool withHistory = (call == 0) == historyFirst;
                Position position = PositionFromFen(fen);
                if (withHistory) {
                    for (const char* uci : {"a8b8", "d1d2", "b8a8", "d2d1"}) {
                        position.DoMove(Mv(uci));
                    }
                }

                const SearchResult result = SearchWithTable(position, kDeeperTableDepth, kDefaultTableEntries);

                if (withHistory) {
                    EXPECT_EQ(result.score, kDrawScore) << historyFirst;
                    ++drawn;
                } else {
                    EXPECT_LT(result.score, -kQueenUpScore) << historyFirst;
                    ++lost;
                }
            }
            EXPECT_EQ(drawn, 1);
            EXPECT_EQ(lost, 1);
        }
    }

    TEST(SearchTest, AShuffleBehindThePositionGivesTheSameAnswerWithAndWithoutTheTable) {
        // Rule: a draw that depends on the history is not stored. White's queen and the black king have shuffled (Qd1-d2, Ka8-b8, Qd2-d1):
        // the position of the start is behind Black, who can go back to it by Kb8-a8 and draw. The table on, none and a tiny one
        // must agree on the move and the score at depth 6, where the repetition is met again and again below the root. This is a
        // guard on one position, not a proof: a table that stored those draws too still gives this answer (tried), the case that
        // breaks it needs a repetition inside the tree that the same node, reached another way, does not have
        for (const size_t tableEntries : {size_t{0}, kDefaultTableEntries, kTinyTables[1]}) {
            Position position = PositionFromFen("k7/8/8/8/8/8/8/3QK3 w - - 0 1");
            for (const char* uci : {"d1d2", "a8b8", "d2d1"}) {
                position.DoMove(Mv(uci));
            }
            ASSERT_EQ(position.RepetitionCount(), 1);

            const SearchResult result = SearchWithTable(position, kMateInThreeDepth, tableEntries);

            EXPECT_EQ(result.best, Mv("b8a8")) << tableEntries;
            EXPECT_EQ(result.score, kDrawScore) << tableEntries;
        }
    }

    TEST(SearchTest, TheClockBoundKeepsTheTableOutOfAFiftyMoveDraw) {
        // Rule: a result of the table is taken (and stored) only where no 50-move draw can fall inside the subtree. A rook and king against
        // king is won, but at the clock of 96 every line reaches 100 plies within four plies, so whatever White does the position is a
        // draw. A search of the same position at the clock of 0 in a new call then finds it won: the draw of the first call is not in the second.
        // The clock is not part of the key, so this is the case the bound is for; the position cannot show it being broken (without
        // the bound these answers do not change, tried: the draws are also counted and block the stores), the sweep below is the guard
        const eastl::string_view tired = "4k3/8/8/8/8/8/8/R3K3 w - - 96 80";
        const eastl::string_view fresh = "4k3/8/8/8/8/8/8/R3K3 w - - 0 1";
        for (const size_t tableEntries : {size_t{0}, kDefaultTableEntries}) {
            Position drawn = PositionFromFen(tired);
            const SearchResult tiredResult = SearchWithTable(drawn, kDeeperTableDepth, tableEntries);
            Position won = PositionFromFen(fresh);
            const SearchResult freshResult = SearchWithTable(won, kDeeperTableDepth, tableEntries);

            EXPECT_EQ(tiredResult.score, kDrawScore) << tableEntries;
            EXPECT_GT(freshResult.score, kRookValue) << tableEntries;
        }
    }

    TEST(SearchTest, TheClockBoundHoldsAtEveryClock) {
        // The same rook ending at clocks from 0 to 99, depth 5: with and without the table the move and the score agree. The clock decides
        // where the bound lets the table in (a node with the clock c and d plies left is allowed when c + d + 32 is under 100), so the sweep
        // crosses the bound in the middle of the tree. Measured equal at each of these clocks; a guard on one position, not a proof
        for (const int clock : {0, 40, 60, 64, 68, 72, 80, 90, 96, 99}) {
            const eastl::string fen = Format("4k3/8/8/8/8/8/8/R3K3 w - - {} 80", clock);
            Position withTable = PositionFromFen(fen.c_str());
            Position withoutTable = PositionFromFen(fen.c_str());

            const SearchResult with = SearchWithTable(withTable, kDeeperTableDepth, kDefaultTableEntries);
            const SearchResult without = SearchWithTable(withoutTable, kDeeperTableDepth, 0);

            EXPECT_EQ(with.score, without.score) << clock;
            EXPECT_EQ(with.best, without.best) << clock;
        }
    }

    // -- determinism and hand-back -------------------------------------------------------------------------------------------

    TEST(SearchTest, TheSameSearchGivesTheSameResult) {
        for (const eastl::string_view fen : kPerftFens) {
            Position first = PositionFromFen(fen);
            Position second = PositionFromFen(fen);

            const SearchResult a = SearchDepth(first, kMinimaxDepth);
            const SearchResult b = SearchDepth(second, kMinimaxDepth);

            EXPECT_EQ(a.best, b.best) << fen.data();
            EXPECT_EQ(a.score, b.score) << fen.data();
            EXPECT_EQ(a.nodes, b.nodes) << fen.data();
            EXPECT_EQ(a.depth, kMinimaxDepth) << fen.data();
        }
    }

    TEST(SearchTest, TheNumbersAreTheSameOnEveryPlatform) {
        // Pinned: the order of equal moves is the generator's and the table is a function of the keys and the depths alone, so these
        // do not change with the compiler, the library or the machine. With the table off the numbers are the ones from before the table
        // (the start position 1246 nodes, Kiwipete 32789 at depth 3). They are not a measure of strength; a change of the evaluation,
        // of the ordering or of the table moves them, and then they are rewritten
        const SearchResult start = SearchFen(Position::kStartFen, kMinimaxDepth);
        const SearchResult kiwipete = SearchFen(kKiwipeteFen, kMinimaxDepth);
        const SearchResult startDeep = SearchFen(Position::kStartFen, kDeeperTableDepth);
        const SearchResult kiwipeteDeep = SearchFen(kKiwipeteFen, kDeeperTableDepth);

        EXPECT_EQ(start.best, Mv("b1c3"));
        EXPECT_EQ(start.score, 50);
        EXPECT_EQ(start.nodes, 1263U);
        EXPECT_EQ(kiwipete.best, Mv("e2a6"));
        EXPECT_EQ(kiwipete.score, 45);
        EXPECT_EQ(kiwipete.nodes, 32760U);
        EXPECT_EQ(startDeep.best, Mv("b1c3"));
        EXPECT_EQ(startDeep.score, 40);
        EXPECT_EQ(startDeep.nodes, 31253U);
        EXPECT_EQ(kiwipeteDeep.best, Mv("d5e6"));
        EXPECT_EQ(kiwipeteDeep.score, 40);
        EXPECT_EQ(kiwipeteDeep.nodes, 329010U);
    }

    TEST(SearchTest, WithoutTheTableTheNumbersAreTheOldOnes) {
        // tableEntries = 0 is the search of before the table: the same moves, scores and nodes as pinned then
        Position start = Position::Start();
        Position kiwipete = PositionFromFen(kKiwipeteFen);

        const SearchResult startResult = SearchWithTable(start, kMinimaxDepth, 0);
        const SearchResult kiwipeteResult = SearchWithTable(kiwipete, kMinimaxDepth, 0);

        EXPECT_EQ(startResult.best, Mv("b1c3"));
        EXPECT_EQ(startResult.score, 50);
        EXPECT_EQ(startResult.nodes, 1246U);
        EXPECT_EQ(kiwipeteResult.best, Mv("e2a6"));
        EXPECT_EQ(kiwipeteResult.score, 45);
        EXPECT_EQ(kiwipeteResult.nodes, 32789U);
    }

    // -- excluded root moves -------------------------------------------------------------------------------------------------

    TEST(SearchTest, NoExcludedRootMovesIsTheSearchOfBefore) {
        // An empty list is the default: the move, the score and the nodes are the pinned ones of the tests above
        Position position = Position::Start();
        SearchSettings settings;
        settings.maxDepth = kMinimaxDepth;
        settings.excludedRootMoves.clear();

        const SearchResult result = Search(position, settings);

        EXPECT_EQ(result.best, Mv("b1c3"));
        EXPECT_EQ(result.score, 50);
        EXPECT_EQ(result.nodes, 1263U);
    }

    TEST(SearchTest, ExcludingTheBestMoveGivesAnotherLegalMove) {
        for (const eastl::string_view fen : kPerftFens) {
            Position position = PositionFromFen(fen);
            const SearchResult first = SearchDepth(position, kMinimaxDepth);
            SearchSettings settings;
            settings.maxDepth = kMinimaxDepth;
            settings.excludedRootMoves.push_back(first.best);

            const SearchResult second = Search(position, settings);

            EXPECT_TRUE(second.complete) << fen.data();
            EXPECT_NE(second.best, first.best) << fen.data();
            EXPECT_TRUE(IsLegalIn(position, second.best)) << fen.data();
            EXPECT_EQ(second.depth, kMinimaxDepth) << fen.data();
        }
    }

    TEST(SearchTest, ExcludingTheBestMoveOfAHangingQueenLeavesALowerScore) {
        // White to move takes the queen with the rook; without that move the position is only a quiet one
        Position position = PositionFromFen("k7/8/8/8/3q4/8/8/3RK3 w - - 0 1");
        const SearchResult best = SearchDepth(position, kMateInOneDepth);
        SearchSettings settings;
        settings.maxDepth = kMateInOneDepth;
        settings.excludedRootMoves.push_back(best.best);

        const SearchResult other = Search(position, settings);

        EXPECT_EQ(best.best, Mv("d1d4"));
        EXPECT_NE(other.best, Mv("d1d4"));
        EXPECT_LT(other.score, best.score - kQueenUpScore / 2);
    }

    TEST(SearchTest, ExcludingEveryMoveButOneGivesThatMove) {
        for (const eastl::string_view fen : {eastl::string_view(Position::kStartFen), eastl::string_view(kKiwipeteFen)}) {
            Position position = PositionFromFen(fen);
            const MoveList legal = MoveGenerator::GenerateLegal(position);
            for (size_t keep : {size_t{0}, legal.size() / 2, legal.size() - 1}) {
                SearchSettings settings;
                settings.maxDepth = 2;
                for (size_t i = 0; i < legal.size(); ++i) {
                    if (i != keep) {
                        settings.excludedRootMoves.push_back(legal[i]);
                    }
                }

                const SearchResult result = Search(position, settings);

                EXPECT_TRUE(result.complete) << fen.data() << " " << keep;
                EXPECT_EQ(result.best, legal[keep]) << fen.data() << " " << keep;
            }
        }
    }

    TEST(SearchTest, AnExcludedSearchComesBackEqualAndTwiceTheSame) {
        Position position = PositionFromFen(kKiwipeteFen);
        const Snapshot before = SnapshotOf(position);
        SearchSettings settings;
        settings.maxDepth = kMinimaxDepth;
        settings.excludedRootMoves.push_back(Mv("e2a6"));

        const SearchResult a = Search(position, settings);
        const SearchResult b = Search(position, settings);

        EXPECT_EQ(SnapshotOf(position), before);
        EXPECT_EQ(a.best, b.best);
        EXPECT_EQ(a.score, b.score);
        EXPECT_EQ(a.nodes, b.nodes);
    }

    TEST(SearchTest, ThePositionComesBackEqual) {
        for (const eastl::string_view fen : kPerftFens) {
            Position position = PositionFromFen(fen);
            const Snapshot before = SnapshotOf(position);

            (void)SearchDepth(position, 3);

            EXPECT_EQ(SnapshotOf(position), before) << fen.data();
            EXPECT_FALSE(position.CanUndo()) << fen.data();
        }
    }

    TEST(SearchTest, ThePositionComesBackEqualWithItsHistory) {
        Position position = Position::Start();
        for (const char* uci : {"e2e4", "e7e5", "g1f3", "b8c6"}) {
            position.DoMove(Mv(uci));
        }
        const Snapshot before = SnapshotOf(position);

        (void)SearchDepth(position, 3);

        EXPECT_EQ(SnapshotOf(position), before);
        // Four moves are still behind it, no more and no fewer
        for (int i = 0; i < 4; ++i) {
            EXPECT_TRUE(position.CanUndo());
            position.UndoMove();
        }
        EXPECT_FALSE(position.CanUndo());
    }

    TEST(SearchTest, ThePositionComesBackEqualAfterTheTableSearch) {
        // The table is private to the call and the search plays on the position's own stack: whatever stops the search, the key, the FEN and
        // the history that were there come back
        Position position = Position::Start();
        for (const char* uci : {"e2e4", "e7e5", "g1f3", "b8c6"}) {
            position.DoMove(Mv(uci));
        }
        const Snapshot before = SnapshotOf(position);

        const SearchResult complete = SearchWithTable(position, kDeeperTableDepth, kDefaultTableEntries);
        EXPECT_EQ(SnapshotOf(position), before);
        EXPECT_FALSE(complete.stopped);

        SearchSettings settings;
        settings.maxDepth = kDeepDepth;
        SearchControl limited;
        limited.nodeLimit = kNodeBudget;
        const SearchResult byNodes = Search(position, settings, limited);
        EXPECT_TRUE(byNodes.stopped);
        EXPECT_EQ(SnapshotOf(position), before);

        SearchControl cancelled;
        cancelled.token = CancellationToken::Create();
        cancelled.token.Cancel();
        const SearchResult byToken = Search(position, settings, cancelled);
        EXPECT_TRUE(byToken.stopped);
        EXPECT_EQ(SnapshotOf(position), before);

        // Four moves are still behind it, no more and no fewer
        for (int i = 0; i < 4; ++i) {
            EXPECT_TRUE(position.CanUndo());
            position.UndoMove();
        }
        EXPECT_FALSE(position.CanUndo());
    }

    // -- stopping ------------------------------------------------------------------------------------------------------------

    TEST(SearchTest, TheNodeLimitStopsTheSearchWithTheLastIteration) {
        Position position = PositionFromFen(kKiwipeteFen);
        const Snapshot before = SnapshotOf(position);
        SearchSettings settings;
        settings.maxDepth = kDeepDepth;
        SearchControl control;
        control.nodeLimit = kNodeBudget;

        const SearchResult result = Search(position, settings, control);

        EXPECT_TRUE(result.stopped);
        EXPECT_TRUE(result.complete);
        EXPECT_GE(result.depth, 1);
        EXPECT_LT(result.depth, kDeepDepth);
        EXPECT_GE(result.nodes, kNodeBudget);
        EXPECT_TRUE(IsLegalIn(position, result.best));
        EXPECT_EQ(SnapshotOf(position), before);
    }

    TEST(SearchTest, ANodeLimitIsDeterministic) {
        Position first = PositionFromFen(kKiwipeteFen);
        Position second = PositionFromFen(kKiwipeteFen);
        SearchSettings settings;
        settings.maxDepth = kDeepDepth;
        SearchControl control;
        control.nodeLimit = kNodeBudget;

        const SearchResult a = Search(first, settings, control);
        const SearchResult b = Search(second, settings, control);

        EXPECT_EQ(a.best, b.best);
        EXPECT_EQ(a.score, b.score);
        EXPECT_EQ(a.depth, b.depth);
        EXPECT_EQ(a.nodes, b.nodes);
    }

    TEST(SearchTest, TheFirstIterationFinishesWhateverTheLimit) {
        // A limit of one node is already used up in the first iteration, and still the search has a move to give
        Position position = PositionFromFen(kKiwipeteFen);
        SearchSettings settings;
        settings.maxDepth = kDeepDepth;
        SearchControl control;
        control.nodeLimit = 1;

        const SearchResult result = Search(position, settings, control);

        EXPECT_TRUE(result.complete);
        EXPECT_EQ(result.depth, 1);
        EXPECT_TRUE(result.stopped);
        EXPECT_TRUE(IsLegalIn(position, result.best));
    }

    TEST(SearchTest, ACancelledTokenStopsTheSearchBeforeItBegins) {
        Position position = PositionFromFen(kKiwipeteFen);
        const Snapshot before = SnapshotOf(position);
        SearchSettings settings;
        settings.maxDepth = kDeepDepth;
        SearchControl control;
        control.token = CancellationToken::Create();
        control.token.Cancel();

        const SearchResult result = Search(position, settings, control);

        EXPECT_FALSE(result.complete);
        EXPECT_TRUE(result.stopped);
        EXPECT_TRUE(result.best.IsNull());
        EXPECT_EQ(result.depth, 0);
        EXPECT_EQ(result.nodes, 0U);
        EXPECT_EQ(SnapshotOf(position), before);
    }

    TEST(SearchTest, ADeadlineThatHasPassedStopsTheSearchBeforeItBegins) {
        Position position = PositionFromFen(kKiwipeteFen);
        SearchSettings settings;
        settings.maxDepth = kDeepDepth;
        SearchControl control;
        control.deadline = Clock::now() - std::chrono::seconds(1);

        const SearchResult result = Search(position, settings, control);

        EXPECT_FALSE(result.complete);
        EXPECT_TRUE(result.best.IsNull());
    }

    TEST(SearchTest, ATokenCancelledFromAnotherThreadStopsADeepSearch) {
        Position position = PositionFromFen(kKiwipeteFen);
        const Snapshot before = SnapshotOf(position);
        SearchSettings settings;
        settings.maxDepth = kDeepDepth;
        SearchControl control;
        control.token = CancellationToken::Create();
        std::thread canceller([token = control.token]() mutable {
            std::this_thread::sleep_for(kCancelAfter);
            token.Cancel();
        });

        // A depth of 64 never ends: the call returning at all is the proof that the token stopped it
        const SearchResult result = Search(position, settings, control);
        canceller.join();

        EXPECT_TRUE(result.stopped);
        EXPECT_TRUE(result.complete);
        EXPECT_LT(result.depth, kDeepDepth);
        EXPECT_TRUE(IsLegalIn(position, result.best));
        EXPECT_EQ(SnapshotOf(position), before);
    }

    TEST(SearchTest, ADeadlineStopsADeepSearch) {
        Position position = PositionFromFen(kPosition6Fen);
        SearchSettings settings;
        settings.maxDepth = kDeepDepth;
        SearchControl control;
        control.deadline = Clock::now() + kCancelAfter;

        const SearchResult result = Search(position, settings, control);

        EXPECT_TRUE(result.stopped);
        EXPECT_TRUE(IsLegalIn(position, result.best));
    }

    /// By hand, on a quiet machine: ChessTests --gtest_also_run_disabled_tests --gtest_filter=SearchTest.DISABLED_StopLatencyYardstick.
    /// A stopped deep search returns within kStopBudget of the cancel and of the deadline (OwnEngineTest has the one of the engine and its Shutdown)
    TEST(SearchTest, DISABLED_StopLatencyYardstick) {
        {
            Position position = PositionFromFen(kKiwipeteFen);
            SearchSettings settings;
            settings.maxDepth = kDeepDepth;
            SearchControl control;
            control.token = CancellationToken::Create();
            std::thread canceller([token = control.token]() mutable {
                std::this_thread::sleep_for(kCancelAfter);
                token.Cancel();
            });

            const Clock::time_point start = Clock::now();
            const SearchResult result = Search(position, settings, control);
            const Clock::duration elapsed = Clock::now() - start;
            canceller.join();

            EXPECT_TRUE(result.stopped);
            EXPECT_LT(elapsed, kCancelAfter + kStopBudget) << "a cancelled token";
        }
        {
            Position position = PositionFromFen(kPosition6Fen);
            SearchSettings settings;
            settings.maxDepth = kDeepDepth;
            SearchControl control;
            const Clock::time_point start = Clock::now();
            control.deadline = start + kCancelAfter;

            const SearchResult result = Search(position, settings, control);
            const Clock::duration elapsed = Clock::now() - start;

            EXPECT_TRUE(result.stopped);
            EXPECT_LT(elapsed, kCancelAfter + kStopBudget) << "a deadline";
        }
    }

    TEST(SearchTest, ChecksThatCanGoOnForeverEndTheSearch) {
        // Both queens can give check again and again; the extension stops at its ply limit and the repetition is a draw
        Position position = PositionFromFen("7k/6q1/8/8/8/8/1Q6/K7 w - - 0 1");
        SearchSettings settings;
        settings.maxDepth = 6;
        SearchControl control;
        control.deadline = Clock::now() + std::chrono::seconds(30);  // the give-up of a broken search, never reached

        const SearchResult result = Search(position, settings, control);

        EXPECT_FALSE(result.stopped);
        EXPECT_TRUE(IsLegalIn(position, result.best));
    }

    // -- legality ------------------------------------------------------------------------------------------------------------

    TEST(SearchTest, TheMoveIsLegalInThePerftPositions) {
        for (const eastl::string_view fen : kPerftFens) {
            Position position = PositionFromFen(fen);

            const SearchResult result = SearchDepth(position, 2);

            EXPECT_TRUE(result.complete) << fen.data();
            EXPECT_TRUE(IsLegalIn(position, result.best)) << fen.data();
            EXPECT_EQ(result.depth, 2) << fen.data();
        }
    }

    TEST(SearchTest, TheMoveIsLegalWithCastlingEnPassantAndPromotionInReach) {
        // Positions with a castling, an en passant capture and a promotion among the moves
        const eastl::string_view fens[] = {
            "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
            "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3",
            "8/P6k/8/8/8/8/p6K/8 w - - 0 1",
        };
        for (const eastl::string_view fen : fens) {
            Position position = PositionFromFen(fen);

            const SearchResult result = SearchDepth(position, 3);

            EXPECT_TRUE(IsLegalIn(position, result.best)) << fen.data();
        }
    }

}  // namespace BECore::Chess::Tests
