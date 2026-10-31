#pragma once

namespace BECore::Chess {

    /// Six standard piece kinds + None for empty cells.
    CORE_ENUM(PieceType, uint8_t, None, Pawn, Knight, Bishop, Rook, Queen, King)

    /// Two players. White moves first.
    CORE_ENUM(PieceColor, uint8_t, White, Black)

    /**
     * @brief A board cell coordinate, in algebraic-axis style.
     *
     * file: column 0..7 (a..h). rank: row 0..7 (1..8 — White's back rank is 0).
     * Stored as int8 so that off-board math (Square{-1, 8}) is representable
     * without UB; use IsInside() to validate.
     */
    struct Square {
        int8_t file = 0;
        int8_t rank = 0;

        constexpr bool operator==(const Square& other) const noexcept {
            return file == other.file && rank == other.rank;
        }
        constexpr bool operator!=(const Square& other) const noexcept {
            return !(*this == other);
        }

        [[nodiscard]] static constexpr bool IsInside(int file, int rank) noexcept {
            return file >= 0 && file < 8 && rank >= 0 && rank < 8;
        }

        [[nodiscard]] constexpr bool IsInside() const noexcept {
            return IsInside(file, rank);
        }

        [[nodiscard]] constexpr int Index() const noexcept {
            return rank * 8 + file;
        }

        static constexpr Square FromIndex(int index) noexcept {
            return Square{static_cast<int8_t>(index % 8), static_cast<int8_t>(index / 8)};
        }
    };

    struct Piece {
        PieceType type = PieceType::None;
        PieceColor color = PieceColor::White;

        [[nodiscard]] constexpr bool IsEmpty() const noexcept {
            return type == PieceType::None;
        }

        constexpr bool operator==(const Piece& other) const noexcept {
            return type == other.type && (IsEmpty() || color == other.color);
        }
    };

}  // namespace BECore::Chess
