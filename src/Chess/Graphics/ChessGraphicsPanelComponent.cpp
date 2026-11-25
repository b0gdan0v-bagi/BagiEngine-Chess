#include "ChessGraphicsPanelComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UIThemes.h>
#include <BECore/UI/UIVars.h>
#include <Chess/ChessGame.h>
#include <Chess/Graphics/GraphicsPreset.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/ChessGraphicsPanelComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        struct Ids {
            static constexpr auto chessSetGraphics = "ChessSetGraphics"_intern;
            static constexpr auto graphicsPreset = "graphicsPreset"_intern;
        };

    }  // namespace

    void ChessGraphicsPanelComponent::OnAttached() {
        SubscribeInScene<InputEvents::ActionEvent, &ChessGraphicsPanelComponent::OnAction>(this);
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &ChessGraphicsPanelComponent::OnUpdate>(this);
    }

    void ChessGraphicsPanelComponent::OnAction(const InputEvents::ActionEvent& event) {
        if (event.action != Ids::chessSetGraphics) {
            return;
        }
        GraphicsChoice* graphics = ChessGame::GetActiveGraphics();
        if (!graphics) {
            return;
        }
        if (const Optional<GraphicsPreset> preset = ParsePreset(event.value.ToStringView())) {
            graphics->Set(*preset);
            return;
        }
        LOG_WARNING("ChessSetGraphics ignored: '{}' is not a preset", event.value.ToStringView());
    }

    void ChessGraphicsPanelComponent::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        const GraphicsChoice* graphics = ChessGame::GetActiveGraphics();
        if (!graphics || graphics->GetRevision() == _themeRevision) {
            return;
        }
        _themeRevision = graphics->GetRevision();
        const StringView theme = ThemeOf(graphics->Get());
        if (!CoreManager::GetUIThemes().SetTheme(PoolString::Intern(theme))) {
            LOG_WARNING("The theme '{}' of the graphics preset '{}' is not in UIThemes.xml", theme, PresetId(graphics->Get()));
        }
    }

    void ChessGraphicsPanelComponent::OnDraw() {
        const GraphicsChoice* graphics = ChessGame::GetActiveGraphics();
        if (!graphics || graphics->GetRevision() == _publishedRevision) {
            return;
        }
        _publishedRevision = graphics->GetRevision();
        CoreManager::GetUIVars().Set(Ids::graphicsPreset, PresetId(graphics->Get()));
    }

}  // namespace BECore::Chess
