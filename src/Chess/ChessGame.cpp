#include "ChessGame.h"

#include <Games/GameManager.h>
#include <Generated/ChessGame.gen.hpp>

namespace BECore::Chess {

    ChessGameModel* ChessGame::GetActiveModel() {
        auto* active = GameManager::GetInstance().GetActive();
        if (!active) {
            return nullptr;
        }
        auto* chess = active->Cast<ChessGame>();
        return chess ? &chess->GetModel() : nullptr;
    }

    ChessGame::ChessGame() : _control(_model) {}

    IControllableGame* ChessGame::AsControllable() {
        return &_control;
    }

    PoolString ChessGame::GetName() const {
        return "Chess"_intern;
    }

    PoolString ChessGame::GetSceneName() const {
        return "ChessScene"_intern;
    }

    void ChessGame::Start() {
        _model.Reset();
    }

    void ChessGame::Stop() {
        // Nothing scene-specific yet — components own their own subscriptions.
    }

    void ChessGame::Reset() {
        _model.Reset();
    }

}  // namespace BECore::Chess
