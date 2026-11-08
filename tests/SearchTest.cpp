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
        /// A queen is worth well over this, so a score above it is "a queen up"
        constexpr int kQueenUpScore = 500;
        constexpr int kDeepDepth = 64;
        /// The nodes after which a search of the tests is stopped: a few polls of the clock, a few iterations of a middlegame
        constexpr uint64_t kNodeBudget = 4000;
        constexpr auto kCancelAfter = std::chrono::milliseconds(50);
        /// The time a stopped search has to return in. The poll is a thousand nodes, a millisecond; the rest is the machine
        constexpr auto kStopBudget = std::chrono::milliseconds(1000);

        SearchResult SearchDepth(Position& position, int depth) {
            SearchSettings settings;
            settings.maxDepth = depth;
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
        // and then its score must be the one of the plain negamax above, whatever the order the moves are tried in
        const eastl::string_view fens[] = {
            Position::kStartFen, kKiwipeteFen, kPosition3Fen, kPosition4Fen, kPosition5Fen, kPosition6Fen, "kbK5/pp6/1P6/8/8/8/8/R7 w - - 0 1", "K1k5/8/8/2b5/2pQ4/8/8/8 b - - 0 1",
        };
        for (const eastl::string_view fen : fens) {
            Position position = PositionFromFen(fen);
            SearchSettings settings;
            settings.maxDepth = kMinimaxDepth;
            settings.quiescence = false;
            settings.checkExtension = false;
            // The early stop for a mate within the depth is the only thing that makes the last iteration not the depth asked for
            const SearchResult pruned = Search(position, settings);
            uint64_t minimaxNodes = 0;
            const int reference = Minimax(position, pruned.depth, 0, minimaxNodes);

            EXPECT_EQ(pruned.score, reference) << fen.data();
            EXPECT_LT(pruned.nodes, minimaxNodes) << fen.data();
        }
    }

    TEST(SearchTest, QuietOrderingSearchesFewerNodes) {
        // The killers and the history put the quiet moves that cut off before the ones that do not: the same depth in fewer nodes.
        // Measured at depth 4 (ordered / plain): start 4228 / 25970, position 3 9204 / 16556, position 5 26448 / 26566, position 6
        // 83208 / 83837. Kiwipete and position 4 are so tactical that a capture cuts every node a quiet move could: the two orders
        // search them the same. So: never more nodes, fewer on the other four, fewer in the sum
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

    // -- determinism and hand-back -------------------------------------------------------------------------------------------

    TEST(SearchTest, TheSameSearchGivesTheSameResult) {
        for (const eastl::string_view fen : kPerftFens) {
            Position first = PositionFromFen(fen);
            Position second = PositionFromFen(fen);

            const SearchResult a = SearchDepth(first, 3);
            const SearchResult b = SearchDepth(second, 3);

            EXPECT_EQ(a.best, b.best) << fen.data();
            EXPECT_EQ(a.score, b.score) << fen.data();
            EXPECT_EQ(a.nodes, b.nodes) << fen.data();
            EXPECT_EQ(a.depth, 3) << fen.data();
        }
    }

    TEST(SearchTest, TheNumbersAreTheSameOnEveryPlatform) {
        // Pinned: the order of equal moves is the generator's, so these do not change with the compiler, the library or the machine.
        // Kiwipete at depth 3 is the same as before the killers and the history (no quiet move cuts a node there); the start position
        // has 1246 nodes (2845 with the old order). They are not a measure of strength; a change of the evaluation or of the ordering moves them, and then they are rewritten
        const SearchResult start = SearchFen(Position::kStartFen, 3);
        const SearchResult kiwipete = SearchFen(kKiwipeteFen, 3);

        EXPECT_EQ(start.best, Mv("b1c3"));
        EXPECT_EQ(start.score, 50);
        EXPECT_EQ(start.nodes, 1246U);
        EXPECT_EQ(kiwipete.best, Mv("e2a6"));
        EXPECT_EQ(kiwipete.score, 45);
        EXPECT_EQ(kiwipete.nodes, 32789U);
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

        const Clock::time_point start = Clock::now();
        const SearchResult result = Search(position, settings, control);
        const Clock::duration elapsed = Clock::now() - start;
        canceller.join();

        EXPECT_LT(elapsed, kCancelAfter + kStopBudget);
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
        const Clock::time_point start = Clock::now();
        control.deadline = start + kCancelAfter;

        const SearchResult result = Search(position, settings, control);
        const Clock::duration elapsed = Clock::now() - start;

        EXPECT_LT(elapsed, kCancelAfter + kStopBudget);
        EXPECT_TRUE(result.stopped);
        EXPECT_TRUE(IsLegalIn(position, result.best));
    }

    TEST(SearchTest, ChecksThatCanGoOnForeverEndTheSearch) {
        // Both queens can give check again and again; the extension stops at its ply limit and the repetition is a draw
        Position position = PositionFromFen("7k/6q1/8/8/8/8/1Q6/K7 w - - 0 1");
        SearchSettings settings;
        settings.maxDepth = 6;
        SearchControl control;
        const Clock::time_point start = Clock::now();
        control.deadline = start + std::chrono::seconds(10);

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
