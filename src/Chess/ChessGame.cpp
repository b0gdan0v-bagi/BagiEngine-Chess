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

    OpponentChoice* ChessGame::GetActiveChoice() {
        ChessGame* chess = GetActive();
        return chess ? &chess->GetChoice() : nullptr;
    }

    PositionAnalyst* ChessGame::GetActiveAnalyst() {
        ChessGame* chess = GetActive();
        return chess ? &chess->GetAnalyst() : nullptr;
    }

    ChessGame::ChessGame() : _opponent(_model), _choice(_opponent), _control(_model, _opponent, _analyst) {
        // The settings are read at the time of the search, so the ones Start() loads are the ones used
        _analyst.SetEngineFactory([this](OpponentEngine engine) { return MakeAnalysisEngine(engine, _opponent.GetSettings()); });
    }

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
        // Two people: nothing is looked up or started until a side is picked. Neither call can fail without a side (kDefaultOpponentEngine is the one engine this build can run: Stockfish, or
        // Own with CHESS_NO_STOCKFISH, so the refusal of an engine that is not available never meets the default)
        [[maybe_unused]] const auto side = _opponent.SetSide(std::nullopt);
        [[maybe_unused]] const auto level = _opponent.SetLevel(kDefaultOpponentLevel);
        [[maybe_unused]] const auto engine = _opponent.SetEngine(kDefaultOpponentEngine);
        _choice.Reset();      // a new game starts with White remembered as the person's color
        _analyst.Shutdown();  // nothing is analysed or rated until the evaluation or the ratings are switched on or a hint is asked for
        // Last: the game it restores goes into the fresh model and choice above, and the key it takes is that of the game it leaves
        _keeper.Start(_model, _opponent, _choice);
    }

    void ChessGame::UpdateKeeper() {
        _keeper.Update(_model, _opponent, _choice);
    }

    void ChessGame::Stop() {
        // Components own their own subscriptions. The choice holds nothing but a reference to the controller and a color. The game is written first, while the opponent still holds what the person
        // picked (Shutdown() forgets it). The opponent goes now, before TaskManager::Shutdown(): a search nobody cancelled would hold the join of the pool
        _keeper.Flush(_model, _opponent, _choice);
        _opponent.Shutdown();
        _analyst.Shutdown();
    }

    void ChessGame::Reset() {
        _model.Reset();
    }

}  // namespace BECore::Chess
