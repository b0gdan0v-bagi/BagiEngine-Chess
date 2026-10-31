#pragma once

#include <Chess/Game/ChessTypes.h>

namespace BECore::Chess {

    /**
     * @brief Pure 8x8 board state. No rules — just storage and basic accessors.
     *
     * Origin (file=0, rank=0) is a1 (White's queenside corner). Indexing matches
     * Square::Index().
     */
    class ChessBoard {
    public:
        ChessBoard() = default;

        /// Wipe all squares to PieceType::None.
        void Clear() noexcept;

        /// Reset to the standard starting position.
        void ResetToStart() noexcept;

        [[nodiscard]] Piece Get(Square s) const noexcept;
        [[nodiscard]] Piece Get(int file, int rank) const noexcept;
        void Set(Square s, Piece p) noexcept;

        /**
         * @brief Move a piece from `from` to `to`, overwriting whatever stood there.
         *
         * No legality check — caller is expected to validate via MoveGenerator first.
         * The source square becomes empty.
         */
        void Move(Square from, Square to) noexcept;

    private:
        eastl::array<Piece, 64> _cells{};
    };

}  // namespace BECore::Chess
