#include <tests/ChessUiHarness.h>

/// The puzzle mode on the screen: what the HUD says while a puzzle is on (StatusText's puzzle words through ChessHudComponent), and the menu section "Puzzles". The puzzle's own rules are
/// PuzzleSessionTest's and ChessGamePuzzleTest's; the scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        /// White solves: Black's setup h8g8, then Ra1a7, the reply g8h8 and Rb2b8#
        constexpr StringView kMateInTwo = "ladder\t7k/8/8/8/8/8/1R6/R6K b - - 0 1\th8g8 a1a7 g8h8 b2b8\t1200\tmateIn2\n";

        constexpr int kFrameLimit = 40;  ///< frames of a quarter second: far more than the reply needs

        PuzzleSet SetOf(std::initializer_list<StringView> rows) {
            String tsv = "id\tfen\tmoves\trating\tthemes\n";
            for (const StringView row : rows) {
                tsv.append(row.data(), row.size());
            }
            Vector<String> problems;
            PuzzleSet set = PuzzleSet::Parse(StringView(tsv.data(), tsv.size()), &problems);
            EXPECT_TRUE(problems.empty());
            return set;
        }

    }  // namespace

    TEST_F(ChessUiTest, AStartedPuzzleSaysYourMoveAndNothingUnderIt) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo}));
        ASSERT_TRUE(game.BeginPuzzle(0));
        Settle();

        EXPECT_EQ(Var("turn"), "Your move");
        EXPECT_EQ(Var("status"), "");
    }

    TEST_F(ChessUiTest, ARightMoveSaysTheReplyIsDueThenYourMoveAgainAfterTheReply) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo}));
        ASSERT_TRUE(game.BeginPuzzle(0));
        Settle();

        ClickMove("a1", "a7");
        FrameOfGameTime();
        ASSERT_EQ(Model().GetMoves().size(), 2u);
        EXPECT_EQ(Var("turn"), "Replying");
        EXPECT_EQ(Var("status"), "");

        for (int frame = 0; frame < kFrameLimit && Model().GetMoves().size() < 3; ++frame) {
            FrameOfGameTime();
        }
        ASSERT_EQ(Model().GetMoves().size(), 3u) << "the reply was played";
        EXPECT_EQ(Var("turn"), "Your move");
    }

    TEST_F(ChessUiTest, AWrongMoveSaysSoUntilTheNextMoveAndTheBoardIsTheOneBeforeIt) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo}));
        ASSERT_TRUE(game.BeginPuzzle(0));
        Settle();

        ClickMove("b2", "b3");
        FrameOfGameTime();
        ASSERT_EQ(Model().GetMoves().size(), 1u) << "taken back";
        EXPECT_EQ(Var("status"), "Wrong move");
        EXPECT_EQ(Var("turn"), "Your move");
        EXPECT_EQ(game.GetPuzzle().GetMistakes(), 1u);

        ClickMove("a1", "a7");
        FrameOfGameTime();
        EXPECT_EQ(Var("status"), "") << "the next move of the person takes the word away";
    }

    TEST_F(ChessUiTest, TheSolvingMoveSaysSolvedAndTheGamesOwnCheckmateStandsUnderIt) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo}));
        ASSERT_TRUE(game.BeginPuzzle(0));
        Settle();
        ClickMove("a1", "a7");
        for (int frame = 0; frame < kFrameLimit && Model().GetMoves().size() < 3; ++frame) {
            FrameOfGameTime();
        }
        ASSERT_EQ(Model().GetMoves().size(), 3u);

        ClickMove("b2", "b8");
        FrameOfGameTime();

        ASSERT_TRUE(Model().IsGameOver());
        EXPECT_EQ(Var("turn"), "Solved");
        EXPECT_EQ(Var("status"), "Checkmate");
    }

    TEST_F(ChessUiTest, AViewOverAPuzzleShowsTheViewsLineAndTheTurnStaysThePuzzles) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo}));
        ASSERT_TRUE(game.BeginPuzzle(0));
        Settle();

        ASSERT_TRUE(Model().ShowMoves(0));
        EXPECT_EQ(Var("status"), "At the start - tap board");
        EXPECT_EQ(Var("turn"), "Your move");

        ASSERT_TRUE(Model().EndViewing());
        EXPECT_EQ(Var("status"), "");
    }

    TEST_F(ChessUiTest, TheEndOfAPuzzleGivesTheGamesOwnWordsAtOnce) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo}));
        ASSERT_TRUE(game.BeginPuzzle(0));
        Settle();
        ASSERT_EQ(Var("turn"), "Your move");

        game.Reset();
        EXPECT_EQ(Var("turn"), "White to move");

        ASSERT_TRUE(game.BeginPuzzle(0));
        ASSERT_EQ(Var("turn"), "Your move");
        ASSERT_TRUE(game.LeavePuzzle());
        EXPECT_EQ(Var("turn"), "White to move");
    }

}  // namespace BECore::Chess::Tests
