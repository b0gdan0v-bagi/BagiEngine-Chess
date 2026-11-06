#include "ChessTestUtil.h"

#include <Chess/Game/BoardOrientation.h>

/// Where a square is drawn for each orientation: pure maths, nothing to start
namespace BECore::Chess::Tests {

    namespace {

        constexpr PieceColor kWhite = PieceColor::White;
        constexpr PieceColor kBlack = PieceColor::Black;
        constexpr int kBoardSize = 8;  // files and ranks

        BoardCell Cell(const char* name, PieceColor bottom) {
            return CellOf(Sq(name), bottom);
        }

    }  // namespace

    TEST(BoardOrientationTest, BlackIsAtTheBottomOnlyWhenTheProgramPlaysWhite) {
        EXPECT_TRUE(BottomColor(std::nullopt) == kWhite);
        EXPECT_TRUE(BottomColor(kBlack) == kWhite);
        EXPECT_TRUE(BottomColor(kWhite) == kBlack);
    }

    TEST(BoardOrientationTest, WhiteAtTheBottomHasA1AtTheBottomLeft) {
        EXPECT_EQ(Cell("a1", kWhite).column, 0);
        EXPECT_EQ(Cell("a1", kWhite).rowFromTop, 7);
        EXPECT_EQ(Cell("h8", kWhite).column, 7);
        EXPECT_EQ(Cell("h8", kWhite).rowFromTop, 0);
        EXPECT_EQ(Cell("e2", kWhite).column, 4);
        EXPECT_EQ(Cell("e2", kWhite).rowFromTop, 6);
    }

    TEST(BoardOrientationTest, BlackAtTheBottomHasA1AtTheTopRight) {
        EXPECT_EQ(Cell("a1", kBlack).column, 7);
        EXPECT_EQ(Cell("a1", kBlack).rowFromTop, 0);
        EXPECT_EQ(Cell("h8", kBlack).column, 0);
        EXPECT_EQ(Cell("h8", kBlack).rowFromTop, 7);
        EXPECT_EQ(Cell("e2", kBlack).column, 3);
        EXPECT_EQ(Cell("e2", kBlack).rowFromTop, 1);
        EXPECT_EQ(Cell("e7", kBlack).column, 3);
        EXPECT_EQ(Cell("e7", kBlack).rowFromTop, 6);
    }

    TEST(BoardOrientationTest, TheBlackLayoutIsTheWhiteOneTurnedBy180Degrees) {
        for (int index = 0; index < kSquareCount; ++index) {
            const Square square = Square::FromIndex(index);
            const BoardCell white = CellOf(square, kWhite);
            const BoardCell black = CellOf(square, kBlack);
            EXPECT_EQ(black.column, 7 - white.column) << index;
            EXPECT_EQ(black.rowFromTop, 7 - white.rowFromTop) << index;
        }
    }

    TEST(BoardOrientationTest, SquareAtInvertsCellOfAndEveryCellIsHitOnce) {
        for (const PieceColor bottom : {kWhite, kBlack}) {
            eastl::array<bool, kSquareCount> hit{};
            int hitCount = 0;
            for (int index = 0; index < kSquareCount; ++index) {
                const Square square = Square::FromIndex(index);
                const BoardCell cell = CellOf(square, bottom);
                EXPECT_TRUE(Square::IsInside(cell.column, cell.rowFromTop)) << index;
                const auto back = SquareAt(cell.column, cell.rowFromTop, bottom);
                ASSERT_TRUE(back.has_value()) << index;
                EXPECT_TRUE(*back == square) << index;
            }
            for (int row = 0; row < kBoardSize; ++row) {
                for (int column = 0; column < kBoardSize; ++column) {
                    const auto square = SquareAt(column, row, bottom);
                    ASSERT_TRUE(square.has_value());
                    if (!hit[square->Index()]) {
                        hit[square->Index()] = true;
                        ++hitCount;
                    }
                }
            }
            EXPECT_EQ(hitCount, kSquareCount);
        }
    }

    TEST(BoardOrientationTest, ACellOutsideTheBoardIsNoSquare) {
        for (const PieceColor bottom : {kWhite, kBlack}) {
            EXPECT_FALSE(SquareAt(-1, 0, bottom).has_value());
            EXPECT_FALSE(SquareAt(kBoardSize, 0, bottom).has_value());
            EXPECT_FALSE(SquareAt(0, -1, bottom).has_value());
            EXPECT_FALSE(SquareAt(0, kBoardSize, bottom).has_value());
        }
    }

}  // namespace BECore::Chess::Tests
