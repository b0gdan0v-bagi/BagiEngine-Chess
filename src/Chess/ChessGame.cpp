#include "ChessGame.h"

#include <Games/GameManager.h>
#include <Generated/ChessGame.gen.hpp>

namespace BECore::Chess {

    ChessGame* ChessGame::GetActive() {
        auto* active = GameManager::GetInstance().GetActive();
        return active ? active->Cast<ChessGame>() : nullptr;
    }

    ChessGameModel* ChessGame::GetActiveModel() {
        ChessGame* chess = GetActive();
        return chess ? &chess->GetModel() : nullptr;
    }

    OpponentController* ChessGame::GetActiveOpponent() {
        ChessGame* chess = GetActive();
        return chess ? &chess->GetOpponent() : nullptr;
    }

    ChessGame::ChessGame() : _opponent(_model), _control(_model) {}

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
        _opponent.SetSettings(OpponentSettings::Load());
        // Two people: nothing is looked up or started until a side is picked. Neither call can fail without a side
        _opponent.SetSide(std::nullopt);
        _opponent.SetLevel(kDefaultOpponentLevel);
    }

    void ChessGame::Stop() {
        // Components own their own subscriptions. The opponent goes now, before TaskManager::Shutdown(): a search nobody cancelled would hold the join of the pool
        _opponent.Shutdown();
    }

    void ChessGame::Reset() {
        _model.Reset();
    }

}  // namespace BECore::Chess
