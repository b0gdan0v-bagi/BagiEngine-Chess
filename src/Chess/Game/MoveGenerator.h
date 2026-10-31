#pragma once

#include <Chess/Game/ChessBoard.h>
#include <Chess/Game/ChessTypes.h>

namespace BECore::Chess {

    /**
     * @brief Pure-rules move generator. No check/checkmate, no castling,
     *        no en-passant, no promotion (MVP scope).
     *
     * For each piece type, generates the set of pseudo-legal target squares from
     * a given origin: empty squares it can step onto + opponent squares it can
     * capture. Does NOT verify whether the moving side leaves itself in check.
     */
    class MoveGenerator {
    public:
        /// Returns target squares for the piece on `from`. Empty if no piece.
        static eastl::vector<Square> Generate(const ChessBoard& board, Square from);
    };

}  // namespace BECore::Chess
