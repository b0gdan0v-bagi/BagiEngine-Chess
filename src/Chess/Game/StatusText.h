#pragma once

#include <Chess/Game/ChessGameModel.h>

namespace BECore::Chess {

    /// The words of the HUD. Pure: they read the model and make text, nothing else
    namespace StatusText {

        /// Whose move it is, or the result once the game is over ("White wins", "Draw"). While the game is on and the
        /// computer opponent is searching (@p opponentThinking) it says "Computer is thinking" instead; a finished game never does
        [[nodiscard]] String Turn(const ChessGameModel& model, bool opponentThinking = false);

        /// The line under the board: while an earlier position is viewed (ChessGameModel::IsViewing()), which one and how to get back (Viewing()); else "" while the game goes
        /// on (but "Check" for a king in check), the reason once it is over ("Checkmate", "White resigned", "Draw agreed", ...)
        [[nodiscard]] String Status(const ChessGameModel& model);

        /// The line a refused draw offer puts on the status line (a person offered, the program said no); the game's own status is not meant by it
        [[nodiscard]] String DrawDeclined();

        /// The questions the menu section "Game" asks before it ends the game: a person resigning, and two people agreeing a draw
        [[nodiscard]] String ResignQuestion();
        [[nodiscard]] String DrawQuestion();

        /// "At move 12 - tap board" (the score-sheet number of the last shown move, the same for White's and Black's) or "At the start - tap board"; "" when nothing earlier is viewed.
        /// Short on purpose: a longer line, "Viewing move 12" with the whole hint, does not fit the Status of the narrowest window
        [[nodiscard]] String Viewing(const ChessGameModel& model);

    }  // namespace StatusText

}  // namespace BECore::Chess
