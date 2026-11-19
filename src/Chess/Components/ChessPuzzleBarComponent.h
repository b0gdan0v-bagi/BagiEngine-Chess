#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore::SceneEvents {
    struct SceneUpdateEvent;
}

namespace BECore::Chess {

    /**
     * @brief The bar of the puzzle mode, on the node `PuzzleBar` before its canvas: shows the canvas exactly while `ChessGame::IsPuzzleOn()`.
     *
     * It compares on every update and draw, like the editor bar, so a reset, a `set_state` or a leave that comes with no frame between hides a stale bar. It answers no action: its three
     * buttons emit the actions of `ChessPuzzleSectionComponent`, which answers them wherever they come from.
     */
    class ChessPuzzleBarComponent : public IComponent {
        BE_CLASS(ChessPuzzleBarComponent)
    public:
        ChessPuzzleBarComponent() = default;
        ~ChessPuzzleBarComponent() override = default;

        void OnAttached() override;
        void OnDraw() override;

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent&);

        /// Shows or hides the canvas as the puzzle wants, only when that changed
        void Sync();

        bool _shown = false;  // the canvas starts hidden
    };

}  // namespace BECore::Chess
