#pragma once

#include <Chess/Game/Position.h>

namespace BECore::Chess {

    /// The 50-move draw in plies, as Position::HalfmoveClock() counts them
    inline constexpr int kFiftyMovePlies = 100;

    /// How a game stands. Everything but Ongoing ends it. Resigned and DrawAgreed are a person's decision, not a property of the position: only ChessGameModel sets them.
    CORE_ENUM(GameStatus, uint8_t, Ongoing, Checkmate, Stalemate, DrawInsufficientMaterial, DrawThreefold, DrawFiftyMoves, Resigned, DrawAgreed)

    /**
     * @brief The status of a game in @p position, which the side to move is about to play: the position's own, so never Resigned or DrawAgreed.
     *
     * The order matters: no legal move comes first (checkmate if the king is attacked, otherwise stalemate), so a mate
     * delivered on the last ply still counts over the 50-move draw. FIDE makes a player claim the repetition and the
     * 50-move draw; the window has no way to claim, so they end the game here.
     */
    [[nodiscard]] GameStatus EvaluateStatus(const Position& position);

}  // namespace BECore::Chess
