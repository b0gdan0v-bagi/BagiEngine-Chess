#pragma once

#include <Chess/Game/BoardOrientation.h>

namespace BECore::Chess {

    /// The column of cells that carries the rank numbers: the left edge of the drawn grid, in both orientations
    constexpr int kLabelColumn = 0;

    /// The row of cells that carries the file letters: the bottom edge of the drawn grid, in both orientations
    constexpr int kLabelRow = kLastBoardIndex;

    /// The names of the files and of the ranks, indexed by `Square::file` and `Square::rank`
    constexpr eastl::string_view kFileLetters = "abcdefgh";
    constexpr eastl::string_view kRankDigits = "12345678";

    /// The letter of the file drawn in the bottom-row cell of a column ("a".."h"), given the color at the bottom; empty for a column outside the board.
    /// White at the bottom reads a..h from the left, Black at the bottom (the grid turned by 180 degrees) h..a
    [[nodiscard]] constexpr eastl::string_view FileLabel(int column, PieceColor bottom) noexcept {
        const Optional<Square> square = SquareAt(column, kLabelRow, bottom);
        return square ? kFileLetters.substr(square->file, 1) : eastl::string_view();
    }

    /// The number of the rank drawn in the left-column cell of a row ("1".."8"), given the color at the bottom; empty for a row outside the board.
    /// White at the bottom reads 8..1 from the top, Black at the bottom 1..8
    [[nodiscard]] constexpr eastl::string_view RankLabel(int rowFromTop, PieceColor bottom) noexcept {
        const Optional<Square> square = SquareAt(kLabelColumn, rowFromTop, bottom);
        return square ? kRankDigits.substr(square->rank, 1) : eastl::string_view();
    }

    /// Whether a square is painted light (a1 is dark, h1 is light): the one rule of the checkered base, which the board paints with and the labels take their contrast from
    [[nodiscard]] constexpr bool IsLightSquare(int file, int rank) noexcept {
        return ((file + rank) % 2) != 0;
    }

    /// Whether the drawn cell is a light one, by the square it shows and the rule the board paints with (a1 is dark, h1 is light); false outside the board.
    /// The turn by 180 degrees keeps the parity of the grid, so a cell has the same color in both orientations
    [[nodiscard]] constexpr bool IsLightCell(int column, int rowFromTop) noexcept {
        const Optional<Square> square = SquareAt(column, rowFromTop, PieceColor::White);
        return square && IsLightSquare(square->file, square->rank);
    }

    static_assert(FileLabel(0, PieceColor::White) == "a" && FileLabel(kLastBoardIndex, PieceColor::White) == "h");
    static_assert(FileLabel(0, PieceColor::Black) == "h" && FileLabel(kLastBoardIndex, PieceColor::Black) == "a");
    static_assert(RankLabel(0, PieceColor::White) == "8" && RankLabel(kLastBoardIndex, PieceColor::White) == "1");
    static_assert(RankLabel(0, PieceColor::Black) == "1" && RankLabel(kLastBoardIndex, PieceColor::Black) == "8");
    static_assert(FileLabel(8, PieceColor::White).empty() && RankLabel(-1, PieceColor::Black).empty());
    static_assert(!IsLightCell(kLabelColumn, kLabelRow) && IsLightCell(kLastBoardIndex, kLabelRow));

}  // namespace BECore::Chess
