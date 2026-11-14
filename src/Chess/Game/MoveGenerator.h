#pragma once

#include <Chess/Game/Move.h>
#include <Chess/Game/Position.h>

namespace BECore::Chess {

    /// No position has more than 218 legal moves; the pseudo-legal list before filtering stays well below this.
    constexpr size_t kMaxMoves = 256;
    using MoveList = eastl::fixed_vector<Move, kMaxMoves, false>;

    /**
     * @brief Legal move generation for the side to move: the standard rules of chess, Chess960 excluded.
     *
     * Moves are produced per piece type from the attack tables (pawn pushes and captures by bitboard shifts), restricted
     * to the squares that answer a check when the king is attacked, and then filtered by Position::IsLegal() for pins,
     * king safety and the castling path. Castling, en passant and promotion (four moves per promoting pawn, queen first)
     * are included.
     */
    class MoveGenerator {
    public:
        /// Every legal move of the side to move. Empty means checkmate or stalemate.
        [[nodiscard]] static MoveList GenerateLegal(const Position& position);

        /// The legal move spelled @p text in UCI ("e2e4", "e7e8q", "e1g1"), or nothing if no legal move has that spelling.
        [[nodiscard]] static Optional<Move> ParseUci(const Position& position, eastl::string_view text);
    };

}  // namespace BECore::Chess
