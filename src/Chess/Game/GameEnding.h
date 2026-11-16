#pragma once

#include <Chess/Game/ChessTypes.h>

namespace BECore::Chess {

    /**
     * @brief The color the person resigns for, or nothing when nobody can.
     *
     * Against the program (@p programPlays: the one color it plays) it is the person's color, whoever is to move. With two people it is the side to move: one screen,
     * and the person at the board whose turn it is gives up. Two programs have nobody to resign.
     */
    [[nodiscard]] constexpr Optional<PieceColor> ResigningColor(PieceColor sideToMove, Optional<PieceColor> programPlays, bool twoPrograms) noexcept {
        if (twoPrograms) {
            return std::nullopt;
        }
        if (programPlays) {
            return Opposite(*programPlays);
        }
        return sideToMove;
    }

}  // namespace BECore::Chess
