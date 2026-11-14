#pragma once

#include <Chess/Engine/ChessEngine.h>
#include <Chess/Game/ChessTypes.h>

namespace BECore::Chess {

    /// The words of the HUD for the computer opponent, next to StatusText (which says them for the game). Pure: they read their arguments and make text
    namespace OpponentText {

        /// The longest sentence Error() makes, in characters. The Status label shows the error sentence or the status of the game, so it has to hold the widest of both
        /// ("Draw: insufficient material" is the longest status of the game)
        inline constexpr size_t kMaxErrorLength = 28;

        /// What Error() says for a kind it has no sentence for; a test fails when any kind that exists gets it
        inline constexpr StringView kUnknownErrorSentence = "Engine failed";

        /// The segment of the opponent panel's first choice: "Computer" when @p computer, else "Two players". The one source of the words
        [[nodiscard]] String ModeLabel(bool computer);

        /// The segment of the color choice, the color the PERSON plays
        [[nodiscard]] String ColorLabel(PieceColor color);

        /// A short sentence for a failure of the engine, at most kMaxErrorLength characters, for the Status label while the error stands; the detail of the error is for the log and the control API
        [[nodiscard]] String Error(const EngineError& error);

    }  // namespace OpponentText

}  // namespace BECore::Chess
