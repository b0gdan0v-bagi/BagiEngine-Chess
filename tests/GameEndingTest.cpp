#include <Chess/Game/GameEnding.h>
#include <tests/ChessTestUtil.h>

/// Who resigns (Game/GameEnding.h): pure, the three cases of who plays
namespace BECore::Chess::Tests {

    TEST(GameEndingTest, AgainstTheProgramThePersonResignsWhoeverIsToMove) {
        for (const PieceColor toMove : {PieceColor::White, PieceColor::Black}) {
            EXPECT_EQ(ResigningColor(toMove, PieceColor::Black, false), PieceColor::White) << "the program plays Black";
            EXPECT_EQ(ResigningColor(toMove, PieceColor::White, false), PieceColor::Black) << "the program plays White";
        }
    }

    TEST(GameEndingTest, TwoPeopleResignForTheSideToMove) {
        EXPECT_EQ(ResigningColor(PieceColor::White, std::nullopt, false), PieceColor::White);
        EXPECT_EQ(ResigningColor(PieceColor::Black, std::nullopt, false), PieceColor::Black);
    }

    TEST(GameEndingTest, TwoProgramsHaveNobodyToResign) {
        for (const PieceColor toMove : {PieceColor::White, PieceColor::Black}) {
            EXPECT_FALSE(ResigningColor(toMove, std::nullopt, true).has_value());
        }
    }

}  // namespace BECore::Chess::Tests
