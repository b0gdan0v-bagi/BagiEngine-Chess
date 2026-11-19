#include "ChessReviewBarComponent.h"

#include <BECore/UI/UICanvasComponent.h>
#include <Chess/ChessGame.h>
#include <Events/SceneEvents.h>
#include <Generated/ChessReviewBarComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        struct Ids {
            static constexpr auto reviewBar = "ReviewBar"_intern;
        };

    }  // namespace

    void ChessReviewBarComponent::OnAttached() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &ChessReviewBarComponent::OnUpdate>(this);
    }

    void ChessReviewBarComponent::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        Sync();
    }

    void ChessReviewBarComponent::OnDraw() {
        Sync();
    }

    void ChessReviewBarComponent::Sync() {
        const ChessGame* game = ChessGame::GetActive();
        // The cheap reads first: the analyst's is asked only on a finished game, so a game in progress asks nothing of the reviewer per frame
        const ChessGameModel* model = game ? &game->GetModel() : nullptr;
        const bool wanted = model && model->IsGameOver() && !model->IsViewing() && !model->IsDrafting() && game->ReviewStartAllowedNow();
        if (wanted != _shown) {
            _shown = wanted;
            UICanvasComponent::SetVisibleByName(GetScene(), Ids::reviewBar, wanted);
        }
    }

}  // namespace BECore::Chess
