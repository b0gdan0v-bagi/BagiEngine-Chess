#include "ChessPuzzleBarComponent.h"

#include <BECore/UI/UICanvasComponent.h>
#include <Chess/ChessGame.h>
#include <Events/SceneEvents.h>
#include <Generated/ChessPuzzleBarComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        struct Ids {
            static constexpr auto puzzleBar = "PuzzleBar"_intern;
        };

    }  // namespace

    void ChessPuzzleBarComponent::OnAttached() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &ChessPuzzleBarComponent::OnUpdate>(this);
    }

    void ChessPuzzleBarComponent::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        Sync();
    }

    void ChessPuzzleBarComponent::OnDraw() {
        Sync();
    }

    void ChessPuzzleBarComponent::Sync() {
        const ChessGame* game = ChessGame::GetActive();
        const bool wanted = game && game->IsPuzzleOn();
        if (wanted != _shown) {
            _shown = wanted;
            UICanvasComponent::SetVisibleByName(GetScene(), Ids::puzzleBar, wanted);
        }
    }

}  // namespace BECore::Chess
