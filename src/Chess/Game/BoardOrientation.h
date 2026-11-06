#pragma once

#include <Chess/Game/ChessTypes.h>

namespace BECore::Chess {

    /// The last index of a file, of a rank, of a column and of a row: the board is 8 x 8
    constexpr int kLastIndex = 7;

    /// A cell of the drawn 8x8 grid: the column from the left and the row from the top, 0..7 each
    struct BoardCell {
        int column = 0;
        int rowFromTop = 0;
    };

    /// The color drawn at the bottom of the board, given the color the PROGRAM plays (`OpponentController::GetPlays()`).
    /// The person is the other color and sits at the bottom, so Black is at the bottom exactly when the program plays White;
    /// two people on one screen, or the program as Black, keep White at the bottom
    [[nodiscard]] constexpr PieceColor BottomColor(std::optional<PieceColor> programPlays) noexcept {
        return (programPlays.has_value() && *programPlays == PieceColor::White) ? PieceColor::Black : PieceColor::White;
    }

    /// Where a square is drawn. White at the bottom: a1 at the bottom left, h8 at the top right.
    /// Black at the bottom: the same grid turned by 180 degrees, a1 at the top right, h8 at the bottom left
    [[nodiscard]] constexpr BoardCell CellOf(Square square, PieceColor bottom) noexcept {
        if (bottom == PieceColor::White) {
            return BoardCell{square.file, kLastIndex - square.rank};
        }
        return BoardCell{kLastIndex - square.file, square.rank};
    }

    /// The square drawn in a cell (the inverse of `CellOf`), or nothing for a cell outside the board
    [[nodiscard]] constexpr std::optional<Square> SquareAt(int column, int rowFromTop, PieceColor bottom) noexcept {
        if (!Square::IsInside(column, rowFromTop)) {
            return std::nullopt;
        }
        if (bottom == PieceColor::White) {
            return Square{static_cast<int8_t>(column), static_cast<int8_t>(kLastIndex - rowFromTop)};
        }
        return Square{static_cast<int8_t>(kLastIndex - column), static_cast<int8_t>(rowFromTop)};
    }

    static_assert(CellOf(Square{0, 0}, PieceColor::White).rowFromTop == kLastIndex);
    static_assert(CellOf(Square{0, 0}, PieceColor::Black).column == kLastIndex);
    static_assert(!SquareAt(8, 0, PieceColor::White).has_value());
    static_assert(SquareAt(7, 0, PieceColor::Black)->file == 0);

}  // namespace BECore::Chess
