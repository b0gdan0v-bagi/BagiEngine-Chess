#include "ChessMenuComponent.h"

#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Chess/Components/MenuActions.h>
#include <Events/InputEvents.h>
#include <Generated/ChessMenuComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        struct Ids {
            static constexpr auto menuPanel = "MenuPanel"_intern;
        };

    }  // namespace

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
        const PoolString menuNode = Ids::menuPanel;
        if (event.action == MenuActions::openMenu) {
            ShowMenuSection(std::nullopt);
            UICanvasComponent::SetVisibleByName(GetScene(), menuNode, true);
        } else if (event.action == MenuActions::closeMenu) {
            ShowMenuSection(std::nullopt);
            UICanvasComponent::SetVisibleByName(GetScene(), menuNode, false);
        } else if (event.action == MenuActions::openSection) {
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
