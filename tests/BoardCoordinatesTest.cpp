#include "ChessTestUtil.h"

#include <Chess/Game/BoardCoordinates.h>

/// What is written at the edge of a drawn cell, for each orientation: pure maths, nothing to start
namespace BECore::Chess::Tests {

    namespace {

        constexpr PieceColor kWhite = PieceColor::White;
        constexpr PieceColor kBlack = PieceColor::Black;
        constexpr int kBoardSize = 8;  // files and ranks

        String Joined(bool files, PieceColor bottom) {
            String text;
            for (int index = 0; index < kBoardSize; ++index) {
                const StringView label = files ? FileLabel(index, bottom) : RankLabel(index, bottom);
                text.append(label.data(), label.size());
            }
            return text;
        }

        // The letter and the digit a square is named with
        char FileLetter(Square square) {
            return static_cast<char>('a' + square.file);
        }

        char RankDigit(Square square) {
            return static_cast<char>('1' + square.rank);
        }

    }  // namespace

    TEST(BoardCoordinatesTest, WhiteAtTheBottomReadsAToHFromTheLeftAndEightToOneFromTheTop) {
        EXPECT_EQ(Joined(true, kWhite), "abcdefgh");
        EXPECT_EQ(Joined(false, kWhite), "87654321");
    }

    TEST(BoardCoordinatesTest, BlackAtTheBottomReadsHToAFromTheLeftAndOneToEightFromTheTop) {
        EXPECT_EQ(Joined(true, kBlack), "hgfedcba");
        EXPECT_EQ(Joined(false, kBlack), "12345678");
    }

    TEST(BoardCoordinatesTest, EachBlackLabelIsTheWhiteOneOfTheOppositeCell) {
        for (int index = 0; index < kBoardSize; ++index) {
            EXPECT_EQ(FileLabel(index, kBlack), FileLabel(kBoardSize - 1 - index, kWhite)) << index;
            EXPECT_EQ(RankLabel(index, kBlack), RankLabel(kBoardSize - 1 - index, kWhite)) << index;
        }
    }

    TEST(BoardCoordinatesTest, ALabelOfACellOutsideTheBoardIsEmpty) {
        for (const PieceColor bottom : {kWhite, kBlack}) {
            for (const int outside : {-1, kBoardSize, 100}) {
                EXPECT_TRUE(FileLabel(outside, bottom).empty()) << outside;
                EXPECT_TRUE(RankLabel(outside, bottom).empty()) << outside;
            }
        }
    }

    TEST(BoardCoordinatesTest, EveryLabelIsOneCharacter) {
        for (const PieceColor bottom : {kWhite, kBlack}) {
            for (int index = 0; index < kBoardSize; ++index) {
                EXPECT_EQ(FileLabel(index, bottom).size(), 1u) << index;
                EXPECT_EQ(RankLabel(index, bottom).size(), 1u) << index;
            }
        }
    }

    TEST(BoardCoordinatesTest, TheLabelsOfTheEdgeCellOfASquareNameThatSquare) {
        for (const PieceColor bottom : {kWhite, kBlack}) {
            for (int index = 0; index < kSquareCount; ++index) {
                const Square square = Square::FromIndex(index);
                const BoardCell cell = CellOf(square, bottom);
                if (cell.rowFromTop == kLabelRow) {
                    ASSERT_EQ(FileLabel(cell.column, bottom).size(), 1u) << index;
                    EXPECT_EQ(FileLabel(cell.column, bottom)[0], FileLetter(square)) << "file of square " << index;
                }
                if (cell.column == kLabelColumn) {
                    ASSERT_EQ(RankLabel(cell.rowFromTop, bottom).size(), 1u) << index;
                    EXPECT_EQ(RankLabel(cell.rowFromTop, bottom)[0], RankDigit(square)) << "rank of square " << index;
                }
            }
        }
    }

    TEST(BoardCoordinatesTest, TheCornerLabelsAgreeWithTheCornerSquares) {
        // White at the bottom: a1 is the bottom left cell, h8 the top right one
        EXPECT_EQ(FileLabel(0, kWhite), "a");
        EXPECT_EQ(RankLabel(kLabelRow, kWhite), "1");
        EXPECT_EQ(RankLabel(0, kWhite), "8");
        EXPECT_EQ(FileLabel(kLastBoardIndex, kWhite), "h");
        // Black at the bottom: h8 is the bottom left cell, a1 the top left one in its column
        EXPECT_EQ(FileLabel(0, kBlack), "h");
        EXPECT_EQ(RankLabel(kLabelRow, kBlack), "8");
        EXPECT_EQ(RankLabel(0, kBlack), "1");
        EXPECT_EQ(FileLabel(kLastBoardIndex, kBlack), "a");
    }

    TEST(BoardCoordinatesTest, TheEdgeCellsAreTheLeftColumnAndTheBottomRow) {
        EXPECT_EQ(kLabelColumn, 0);
        EXPECT_EQ(kLabelRow, kLastBoardIndex);
    }

    TEST(BoardCoordinatesTest, ACellHasTheSameColorInBothOrientationsAndTheBoardsOwnRule) {
        for (int row = 0; row < kBoardSize; ++row) {
            for (int column = 0; column < kBoardSize; ++column) {
                // The rule ChessBoardComponent paints with, for the square drawn in this cell
                for (const PieceColor bottom : {kWhite, kBlack}) {
                    const Optional<Square> square = SquareAt(column, row, bottom);
                    ASSERT_TRUE(square.has_value());
                    EXPECT_EQ(IsLightCell(column, row), ((square->file + square->rank) % 2) != 0) << column << "," << row;
                }
            }
        }
    }

    TEST(BoardCoordinatesTest, A1IsDarkAndH1IsLightWhereverTheyAreDrawn) {
        for (const PieceColor bottom : {kWhite, kBlack}) {
            const BoardCell a1 = CellOf(Sq("a1"), bottom);
            const BoardCell h1 = CellOf(Sq("h1"), bottom);
            EXPECT_FALSE(IsLightCell(a1.column, a1.rowFromTop));
            EXPECT_TRUE(IsLightCell(h1.column, h1.rowFromTop));
        }
    }

    TEST(BoardCoordinatesTest, TheColorsAlternateAlongTheEdgesAndACellOutsideTheBoardIsNotLight) {
        for (int index = 0; index < kLastBoardIndex; ++index) {
            EXPECT_NE(IsLightCell(kLabelColumn, index), IsLightCell(kLabelColumn, index + 1));
            EXPECT_NE(IsLightCell(index, kLabelRow), IsLightCell(index + 1, kLabelRow));
        }
        EXPECT_FALSE(IsLightCell(-1, 0));
        EXPECT_FALSE(IsLightCell(0, kBoardSize));
    }

}  // namespace BECore::Chess::Tests
