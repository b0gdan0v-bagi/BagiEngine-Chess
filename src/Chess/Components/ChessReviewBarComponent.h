#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore::SceneEvents {
    struct SceneUpdateEvent;
}

namespace BECore::Chess {

    /**
     * @brief The Review button on the result bar, on the node `ReviewBar` before its canvas: shows the canvas exactly while the game is over, shows its own position (no view, no draft)
     * and `ChessGame::ReviewStartAllowedNow()` holds.
     *
     * It compares on every update and draw, like the puzzle bar, so a takeback or a reset with no frame between hides a stale button. It answers no action: the button emits
     * `ChessReviewStart`, which `ChessReviewSectionComponent` answers wherever it comes from.
     */
    class ChessReviewBarComponent : public IComponent {
        BE_CLASS(ChessReviewBarComponent)
    public:
        ChessReviewBarComponent() = default;
        ~ChessReviewBarComponent() override = default;

        void OnAttached() override;
        void OnDraw() override;

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent&);

        /// Shows or hides the canvas as the game wants, only when that changed
        void Sync();

        bool _shown = false;  // the canvas starts hidden
    };

}  // namespace BECore::Chess
