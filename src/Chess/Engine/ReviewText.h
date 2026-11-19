#pragma once

#include <Chess/Engine/GameReviewer.h>

namespace BECore::Chess {

    /// The words of the review of a finished game: the Review section's status line and the HUD's. Pure, like OpponentText; here because Game/ must not know the engine
    namespace ReviewText {

        /// The longest sentence Status() makes, in characters ("Reviewing 9999 of 9999": the layout tests lay out four-digit counts)
        inline constexpr size_t kMaxStatusLength = 22;

        /// The line of the Review section. Idle says why a start is refused (@p refusal, as ReviewRefusalFor() answers), and a puzzle on wins over it; the other states say how far the
        /// review is. The detail of a failure is for the log and the control API
        [[nodiscard]] String Status(ReviewState state, ReviewProgress progress, Optional<ReviewRefusal> refusal, bool puzzleOn);

        /// The status line of the HUD while a review runs: "Reviewing 12/40"
        [[nodiscard]] String Hud(ReviewProgress progress);

    }  // namespace ReviewText

}  // namespace BECore::Chess
