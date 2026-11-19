#include "ReviewText.h"

namespace BECore::Chess {

    namespace ReviewText {

        String Status(ReviewState state, ReviewProgress progress, Optional<ReviewRefusal> refusal, bool puzzleOn) {
            switch (state) {
                case ReviewState::Idle:
                    if (puzzleOn) {
                        return "Not during a puzzle";
                    }
                    if (refusal) {
                        return *refusal == ReviewRefusal::NotFinished ? "The game is not over" : "No moves to review";
                    }
                    return "Not started";
                case ReviewState::Running:
                    return Format("Reviewing {} of {}", progress.done, progress.total);
                case ReviewState::Done:
                    return "Review done";
                case ReviewState::Failed:
                    return "Review failed";
            }
            return "Not started";
        }

        String Hud(ReviewProgress progress) {
            return Format("Reviewing {}/{}", progress.done, progress.total);
        }

    }  // namespace ReviewText

}  // namespace BECore::Chess
