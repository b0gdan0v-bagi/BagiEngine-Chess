#include "ChessMenuComponent.h"

#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Events/InputEvents.h>
#include <Generated/ChessMenuComponent.gen.hpp>

namespace BECore::Chess {

    void ChessMenuComponent::OnAttached() {
        SubscribeInScene<InputEvents::ActionEvent, &ChessMenuComponent::OnAction>(this);
    }

    void ChessMenuComponent::ShowMenuSection(Optional<MenuSection> shown) {
        // A scene without a section canvas has nothing to show or hide
        for (const MenuSection& section : MenuSections()) {
            UICanvasComponent::SetVisibleByName(GetScene(), PoolString::Intern(section.node), shown && shown->id == section.id);
        }
    }

    void ChessMenuComponent::OnAction(const InputEvents::ActionEvent& event) {
        const PoolString menuNode = "MenuPanel"_intern;
        if (event.action == "ChessOpenMenu"_intern) {
            ShowMenuSection(std::nullopt);
            UICanvasComponent::SetVisibleByName(GetScene(), menuNode, true);
        } else if (event.action == "ChessCloseMenu"_intern) {
            ShowMenuSection(std::nullopt);
            UICanvasComponent::SetVisibleByName(GetScene(), menuNode, false);
        } else if (event.action == "ChessOpenSection"_intern) {
            const Optional<MenuSection> section = FindMenuSection(event.value.ToStringView());
            if (!section) {
                LOG_WARNING("ChessOpenSection ignored: '{}' is not a section of the menu"_format(event.value.ToStringView()));
                return;
            }
            UICanvasComponent::SetVisibleByName(GetScene(), menuNode, false);
            ShowMenuSection(section);
        }
    }

}  // namespace BECore::Chess
