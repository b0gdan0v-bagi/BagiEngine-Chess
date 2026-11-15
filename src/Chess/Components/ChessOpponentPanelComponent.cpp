#include "ChessOpponentPanelComponent.h"

#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Chess/ChessGame.h>
#include <Chess/Engine/OpponentEngines.h>
#include <Chess/Game/MenuSections.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/ChessOpponentPanelComponent.gen.hpp>

namespace BECore::Chess {

    void ChessOpponentPanelComponent::OnAttached() {
        SubscribeInScene<InputEvents::ActionEvent, &ChessOpponentPanelComponent::OnAction>(this);
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &ChessOpponentPanelComponent::OnUpdate>(this);
    }

    void ChessOpponentPanelComponent::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        SyncStockfishSegment();
    }

    void ChessOpponentPanelComponent::OnDraw() {
        SyncStockfishSegment();  // before the canvas of the node: a frame with no update before it already lacks the segment
    }

    void ChessOpponentPanelComponent::SyncStockfishSegment() {
        const OpponentController* opponent = ChessGame::GetActiveOpponent();
        const bool offered = opponent ? opponent->OffersStockfish() : kStockfishAvailable;
        if (offered == _stockfishShown) {
            return;
        }
        _stockfishShown = offered;
        UICanvasComponent::SetElementVisibleByName(GetScene(), "EngineStockfish"_intern, offered);
    }

    void ChessOpponentPanelComponent::OnAction(const InputEvents::ActionEvent& event) {
        if (event.action == "ChessOpenSection"_intern) {
            const Optional<MenuSection> section = FindMenuSection(event.value.ToStringView());
            if (section && section->id == kOpponentSectionId) {
                // The only look for a Stockfish installed since the game started; the panel is drawn from the same frame on
                if (OpponentController* opponent = ChessGame::GetActiveOpponent()) {
                    opponent->RefreshStockfish();
                }
                SyncStockfishSegment();
            }
            return;
        }
        OpponentChoice* choice = ChessGame::GetActiveChoice();
        if (!choice) {
            return;
        }
        const StringView value = event.value.ToStringView();
        std::expected<void, ChoiceError> picked;
        if (event.action == "ChessSetMode"_intern) {
            picked = choice->SetMode(value);
        } else if (event.action == "ChessSetColor"_intern) {
            picked = choice->SetColor(value);
        } else if (event.action == "ChessSetEngine"_intern) {
            picked = choice->SetEngine(value);
        } else if (event.action == "ChessSetLevel"_intern) {
            picked = choice->SetLevel(value);
        } else {
            return;
        }
        // A refused pick (no Stockfish found) is kept by the controller and shown through the variables, so only an id that is in no table (a hand-made press) is said here
        if (!picked && picked.error().kind == ChoiceErrorKind::BadId) {
            LOG_WARNING("{} ignored: {}"_format(event.action.ToStringView(), picked.error().detail));
        }
    }

}  // namespace BECore::Chess
