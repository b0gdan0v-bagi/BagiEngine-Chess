#pragma once

#include <Chess/Engine/OpponentLevels.h>
#include <Chess/Game/ChessTypes.h>

namespace BECore::Chess {

    /// The words of the HUD for the computer opponent, next to StatusText (which says them for the game). Pure: they read their arguments and make text, nothing else.
    /// They live in Engine/ and not in Game/ because Game/ must not know the engine
    namespace OpponentText {

        /// The longest sentence Error() makes, in characters. The Status label shows one text at a time, the error sentence or the status of the game, so it has to hold the
        /// widest of both: "Draw: insufficient material" (27 characters) is the longest status
        inline constexpr size_t kMaxErrorLength = 28;

        /// What Error() says for a kind it has no sentence for (a kind added to EngineErrorKind and not to the switch, which the compiler warns about). A test fails when any
        /// kind that exists gets it
        inline constexpr eastl::string_view kUnknownErrorSentence = "Engine failed";

        /// What the Side button says, from the person's point of view: @p computerPlays is the side the program plays, so the program as Black is "You: White".
        /// Nobody is "Two players"
        [[nodiscard]] eastl::string SideLabel(std::optional<PieceColor> computerPlays);

        /// What the Level button says: "Level: Medium"
        [[nodiscard]] eastl::string LevelLabel(const OpponentLevel& level);

        /// A short sentence for a failure of the engine, at most kMaxErrorLength characters ("Engine not found", "Engine did not answer"). It is what the Status label shows while
        /// the error stands; the detail of the error (paths, the engine's own words) is for the log and the control API
        [[nodiscard]] eastl::string Error(const EngineError& error);

    }  // namespace OpponentText

}  // namespace BECore::Chess
