#include "ChessGame.h"

#include <Generated/ChessGame.gen.hpp>

namespace BECore::Chess {

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
