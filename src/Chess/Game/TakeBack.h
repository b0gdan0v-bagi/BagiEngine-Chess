#pragma once

#include <Chess/Game/ChessTypes.h>

namespace BECore::Chess {

    /**
     * @brief How many moves one takeback of the person takes back, so that the person is on the move again: the argument of ChessGameModel::TakeBack().
     *
     * @p programPlays is the one color the program plays (OpponentController::GetPlays()): nothing for two people and for two programs, where one move goes back. Against the program,
     * with the person to move, its answer and the person's move go back together (2), and nothing when the only move is the program's (the person has none to take back); with the
     * program to move (it thinks, or the person has just ended the game) only the person's last move goes back, and the search in flight becomes stale. 0: nothing can be taken back.
     */
    [[nodiscard]] constexpr size_t TakeBackPlies(size_t movesPlayed, PieceColor sideToMove, Optional<PieceColor> programPlays) noexcept {
        if (movesPlayed == 0) {
            return 0;
        }
        if (!programPlays || sideToMove == *programPlays) {
            return 1;
        }
        constexpr size_t kAnswerAndMove = 2;
        return movesPlayed >= kAnswerAndMove ? kAnswerAndMove : 0;
    }

}  // namespace BECore::Chess
