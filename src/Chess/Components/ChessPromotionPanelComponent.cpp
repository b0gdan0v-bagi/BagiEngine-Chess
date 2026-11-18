#include "ChessPromotionPanelComponent.h"

#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Chess/ChessGame.h>
#include <Chess/Game/PromotionChoices.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/ChessPromotionPanelComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        struct Ids {
            static constexpr auto chessCancelPromotion = "ChessCancelPromotion"_intern;
            static constexpr auto chessPromote = "ChessPromote"_intern;
            static constexpr auto promotionPanel = "PromotionPanel"_intern;
        };

    }  // namespace

    ChessPromotionPanelComponent* ChessPromotionPanelComponent::s_active = nullptr;

    ChessPromotionPanelComponent::~ChessPromotionPanelComponent() {
        if (s_active == this) {
            s_active = nullptr;
        }
    }

    void ChessPromotionPanelComponent::OnAttached() {
        s_active = this;
        SubscribeInScene<InputEvents::ActionEvent, &ChessPromotionPanelComponent::OnAction>(this);
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &ChessPromotionPanelComponent::OnUpdate>(this);
    }

    void ChessPromotionPanelComponent::SyncActive() {
        if (s_active) {
            s_active->Sync();
        }
    }

    void ChessPromotionPanelComponent::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        Sync();
    }

    void ChessPromotionPanelComponent::OnDraw() {
        Sync();
    }

    void ChessPromotionPanelComponent::Sync() {
        const ChessGameModel* model = ChessGame::GetActiveModel();
        const bool wanted = model && model->HasPendingPromotion();
        if (wanted == _shown) {
            return;
        }
        _shown = wanted;
        UICanvasComponent::SetVisibleByName(GetScene(), Ids::promotionPanel, wanted);
    }

    void ChessPromotionPanelComponent::OnAction(const InputEvents::ActionEvent& event) {
        const bool promote = event.action == Ids::chessPromote;
        if (!promote && event.action != Ids::chessCancelPromotion) {
            return;
        }
        ChessGameModel* model = ChessGame::GetActiveModel();
        if (!model) {
            return;
        }
        if (!promote) {
            model->CancelPromotion();
        } else if (const Optional<PieceType> piece = FindPromotion(event.value.ToStringView())) {
            // Nothing pending (a press that came late) is an IllegalMove of the model and changes nothing; a game this move ended is logged by the board's next update
            static_cast<void>(model->ChoosePromotion(*piece));
        } else {
            LOG_WARNING("ChessPromote ignored: '{}' is not a piece a pawn can become"_format(event.value.ToStringView()));
        }
        Sync();
    }

}  // namespace BECore::Chess
