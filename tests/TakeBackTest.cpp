#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/TakeBack.h>
#include <tests/ChessTestUtil.h>

/// How many moves the person's takeback takes back (Game/TakeBack.h): pure, and a check with the model that the person is on the move after it
namespace BECore::Chess::Tests {

    TEST(TakeBackTest, NothingBeforeTheFirstMoveWhoeverPlays) {
        EXPECT_EQ(TakeBackPlies(0, PieceColor::White, std::nullopt), 0u);
        EXPECT_EQ(TakeBackPlies(0, PieceColor::White, PieceColor::Black), 0u);
        EXPECT_EQ(TakeBackPlies(0, PieceColor::White, PieceColor::White), 0u) << "the program to move with no move played";
    }

    TEST(TakeBackTest, TwoPeopleOrTwoProgramsTakeBackOneMove) {
        EXPECT_EQ(TakeBackPlies(1, PieceColor::Black, std::nullopt), 1u);
        EXPECT_EQ(TakeBackPlies(7, PieceColor::White, std::nullopt), 1u);
    }

    TEST(TakeBackTest, AgainstTheProgramThePersonToMoveTakesBackTheAnswerAndTheOwnMove) {
        EXPECT_EQ(TakeBackPlies(2, PieceColor::White, PieceColor::Black), 2u);
        EXPECT_EQ(TakeBackPlies(9, PieceColor::Black, PieceColor::White), 2u);
    }

    TEST(TakeBackTest, AgainstTheProgramThatMadeTheOnlyMoveThereIsNothingOfThePersonsToTakeBack) {
        EXPECT_EQ(TakeBackPlies(1, PieceColor::Black, PieceColor::White), 0u);
    }

    TEST(TakeBackTest, WhileTheProgramIsToMoveOnlyThePersonsLastMoveGoesBack) {
        EXPECT_EQ(TakeBackPlies(1, PieceColor::Black, PieceColor::Black), 1u) << "it thinks over its answer to the first move";
        EXPECT_EQ(TakeBackPlies(6, PieceColor::White, PieceColor::White), 1u);
    }

    TEST(TakeBackTest, AfterTheTakebackThePersonIsOnTheMoveAgain) {
        // The person mates the program (Black) and is mated by it: either way the person plays the next move
        ChessGameModel mated;
        for (const char* uci : {"f2f3", "e7e5", "g2g4", "d8h4"}) {
            ASSERT_TRUE(mated.TryMove(Mv(uci)).has_value()) << uci;
        }
        ASSERT_TRUE(mated.IsGameOver());
        EXPECT_EQ(mated.TakeBack(TakeBackPlies(mated.GetMoves().size(), mated.GetTurn(), PieceColor::Black)), 2u);
        EXPECT_EQ(mated.GetTurn(), PieceColor::White);
        EXPECT_FALSE(mated.IsGameOver());

        ChessGameModel mating;
        for (const char* uci : {"e2e4", "f7f6", "d2d4", "g7g5", "d1h5"}) {
            ASSERT_TRUE(mating.TryMove(Mv(uci)).has_value()) << uci;
        }
        ASSERT_TRUE(mating.IsGameOver());
        EXPECT_EQ(mating.TakeBack(TakeBackPlies(mating.GetMoves().size(), mating.GetTurn(), PieceColor::Black)), 1u);
        EXPECT_EQ(mating.GetTurn(), PieceColor::White);
        EXPECT_EQ(mating.GetMoves().size(), 4u);
    }

}  // namespace BECore::Chess::Tests
