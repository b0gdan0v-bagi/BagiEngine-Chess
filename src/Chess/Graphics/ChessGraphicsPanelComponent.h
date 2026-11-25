#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }

    namespace SceneEvents {
        struct SceneUpdateEvent;
    }

}  // namespace BECore

namespace BECore::Chess {

    /**
     * @brief The Graphics section, on the node `GraphicsPanel` before its canvas: answers `ChessSetGraphics` (a preset id in `value`) with the game's GraphicsChoice, publishes
     * `graphicsPreset` for the segmented group and keeps the board theme of `UIThemes` at the choice's preset.
     *
     * The theme follows the choice, not the click: a pick by the menu, by the control API or a restored save all end in `SetTheme()` at the next update. The variable is written in `OnDraw` and
     * the theme set in `OnUpdate` only when the choice's revision differs from the last one handled (and the first time), so a steady frame does neither. An id in no table changes nothing and
     * logs one warning.
     */
    class ChessGraphicsPanelComponent : public IComponent {
        BE_CLASS(ChessGraphicsPanelComponent)
    public:
        ChessGraphicsPanelComponent() = default;
        ~ChessGraphicsPanelComponent() override = default;

        void OnAttached() override;
        void OnDraw() override;

    private:
        void OnAction(const InputEvents::ActionEvent& event);
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);

        uint64_t _publishedRevision = 0;  ///< GraphicsChoice::GetRevision() of the variable last written; 0 is none yet (revisions start at 1)
        uint64_t _themeRevision = 0;      ///< the same for the theme
    };

}  // namespace BECore::Chess
