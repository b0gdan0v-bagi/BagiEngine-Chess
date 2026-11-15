#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Game/MenuSections.h>

namespace BECore::InputEvents {
    struct ActionEvent;
}

namespace BECore::Chess {

    /**
     * @brief The menu of the scene, on the node `MenuPanel`: answers `ChessOpenMenu`, `ChessOpenSection` (the section id in `value`) and `ChessCloseMenu`.
     *
     * Opening the list hides every section, opening a section swaps the list for the section's canvas (the table Game/MenuSections.*), closing hides all. An id in no table changes nothing
     * and logs one warning. The menu is no state: what is shown is the visibility of the canvases.
     */
    class ChessMenuComponent : public IComponent {
        BE_CLASS(ChessMenuComponent)
    public:
        ChessMenuComponent() = default;
        ~ChessMenuComponent() override = default;

        void OnAttached() override;

    private:
        void OnAction(const InputEvents::ActionEvent& event);

        /// Shows the canvas of this section of the menu and hides the canvases of the others (none: hides them all); the list `MenuPanel` is not touched
        void ShowMenuSection(Optional<MenuSection> shown);
    };

}  // namespace BECore::Chess
