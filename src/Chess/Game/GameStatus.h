#pragma once

#include <Chess/Game/Position.h>

namespace BECore::Chess {

    /// How a game stands. Everything but Ongoing ends it.
    CORE_ENUM(GameStatus, uint8_t, Ongoing, Checkmate, Stalemate, DrawInsufficientMaterial, DrawFivefold, DrawSeventyFiveMoves)

    /**
     * @brief The status of a game in @p position, which the side to move is about to play.
     *
     * Checked in this order: no legal move (checkmate if the king is attacked, otherwise stalemate), so a mate delivered on
     * the last ply still counts; then dead position by insufficient material; then the draws FIDE applies by itself, the
     * fifth occurrence of the position and 75 moves by each side without a capture or a pawn move. The shorter threefold and
     * 50-move draws have to be claimed by a player and are not reported here.
     */
    [[nodiscard]] GameStatus EvaluateStatus(const Position& position);

}  // namespace BECore::Chess
