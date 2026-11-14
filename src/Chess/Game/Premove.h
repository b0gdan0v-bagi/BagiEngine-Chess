#pragma once

#include <Chess/Game/Bitboard.h>
#include <Chess/Game/ChessTypes.h>
#include <Chess/Game/Position.h>

namespace BECore::Chess {

    /// A move the person queued while the program thinks: a piece from one square to another, nothing else (a promotion is always a queen)
    struct Premove {
        Square from;
        Square to;

        constexpr bool operator==(const Premove& other) const noexcept {
            return from == other.from && to == other.to;
        }
    };

    /**
     * @brief The board a premove is made on: the real position with the queued premoves already played over it, so that the
     * second premove can move the piece the first one moved. Pure, no scene and no model.
     *
     * Like lichess, the opponent's answer is not known when a premove is made, so the geometry is its own and not the
     * rules' (the real legality is checked when the premove's turn comes, in `ChessGameModel::PlayPremove()`):
     *  - a piece reaches the squares of its own movement, **blocked by the pieces of its own color only** (an opponent's
     *    piece never blocks: it may be gone by then, and a square that holds one is a target);
     *  - **check and pins are ignored**;
     *  - a square with an own piece is no target;
     *  - a pawn goes one step forward and, from its start rank, two (both squares free of own pieces), and to the two
     *    diagonal squares whether or not something stands there now;
     *  - **not here:** castling (the king's two-file step is no target) and the choice of a promotion piece: a premoved
     *    promotion is a queen, `Apply()` shows it as one.
     *
     * Only the cells and the pieces of each color are kept (no turn, no castling rights, no en passant): `Apply()` is
     * not `Position::DoMove()`.
     */
    class PremoveBoard {
    public:
        explicit PremoveBoard(const Position& position);

        /// The piece on a square, or an empty piece for a square outside the board
        [[nodiscard]] Piece GetPieceAt(Square square) const noexcept;

        /// Whether a piece of @p color stands on @p square
        [[nodiscard]] bool IsOwnPiece(Square square, PieceColor color) const noexcept;

        /// The squares the piece on @p from can be premoved to, each once, by ascending index; empty for an empty cell and for a piece that is not of @p color
        [[nodiscard]] eastl::vector<Square> Targets(Square from, PieceColor color) const;

        /// Lifts the piece from `from` to `to`: what stood on `to` is gone, the cell of `from` is empty, a pawn that reaches the last rank becomes a queen. Does nothing for an empty `from` or a
        /// square outside the board
        void Apply(const Premove& premove);

    private:
        static constexpr size_t kColorCount = 2;

        eastl::array<Piece, kSquareCount> _cells{};
        eastl::array<Bitboard, kColorCount> _byColor{};
    };

}  // namespace BECore::Chess
