#pragma once

#include <Chess/Game/ChessGameModel.h>

namespace BECore::Chess {

    /// The words of the HUD for the state of a game. Pure: they read the model and make text, nothing else
    namespace StatusText {

        /// Whose move it is: "White to move" or "Black to move"; once the game is over, who won or that it is a draw ("White wins", "Draw").
        /// While the game is on and the computer opponent is searching for its move (@p opponentThinking) it says "Computer is thinking" instead; a finished game never does
        [[nodiscard]] eastl::string Turn(const ChessGameModel& model, bool opponentThinking = false);

        /// How the game stands, a line under the board: "" while it goes on, "Check" for a king in check, and the reason once it is over
        /// ("Checkmate", "Stalemate", "Draw: insufficient material", "Draw: threefold repetition", "Draw: fifty-move rule")
        [[nodiscard]] eastl::string Status(const ChessGameModel& model);

    }  // namespace StatusText

}  // namespace BECore::Chess
