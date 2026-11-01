#pragma once

#include <Chess/Game/Position.h>

namespace BECore::Chess {

    /// How a game stands. Everything but Ongoing ends it.
    CORE_ENUM(GameStatus, uint8_t, Ongoing, Checkmate, Stalemate, DrawInsufficientMaterial, DrawThreefold, DrawFiftyMoves)

    /**
     * @brief The status of a game in @p position, which the side to move is about to play.
     *
     * Checked in this order: no legal move (checkmate if the king is attacked, otherwise stalemate), so a mate delivered on
     * the last ply still counts; then dead position by insufficient material; then the third occurrence of the position and
     * 50 moves by each side without a capture or a pawn move. FIDE makes a player claim those two (it ends a game by itself
     * only at the fifth occurrence and 75 moves), but the window has no way to claim, so they end the game here.
     */
    [[nodiscard]] GameStatus EvaluateStatus(const Position& position);

}  // namespace BECore::Chess
