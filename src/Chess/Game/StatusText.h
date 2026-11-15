#pragma once

#include <Chess/Game/ChessGameModel.h>

namespace BECore::Chess {

    /// The words of the HUD. Pure: they read the model and make text, nothing else
    namespace StatusText {

        /// Whose move it is, or the result once the game is over ("White wins", "Draw"). While the game is on and the
        /// computer opponent is searching (@p opponentThinking) it says "Computer is thinking" instead; a finished game never does
        [[nodiscard]] String Turn(const ChessGameModel& model, bool opponentThinking = false);

        /// The line under the board: "" while the game goes on (but "Check" for a king in check), the reason once it is over
        [[nodiscard]] String Status(const ChessGameModel& model);

    }  // namespace StatusText

}  // namespace BECore::Chess
