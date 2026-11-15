#include "ChessGame.h"

#include <Games/GameManager.h>
#include <Generated/ChessGame.gen.hpp>
#include <random>

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
        // A new game draws a new seed; a device per draw because the source has to be copyable and a Reset is rare
        _model.SetSeedSource([]() -> uint32_t {
            //-NoStd: the operating system's randomness is the real boundary of the seed
            std::random_device device;
            return device();
        });
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
        // Two people: nothing is looked up or started until a side is picked, so none of the three can be refused (kDefaultOpponentEngine is the one engine this build can run)
        [[maybe_unused]] const auto side = _opponent.SetSide(std::nullopt);
        [[maybe_unused]] const auto level = _opponent.SetLevel(kDefaultOpponentLevel);
        [[maybe_unused]] const auto engine = _opponent.SetEngine(kDefaultOpponentEngine);
        _choice.Reset();      // a new game starts with White remembered as the person's color
        _analyst.Shutdown();  // nothing is analysed or rated until the evaluation or the ratings are switched on or a hint is asked for
        // Last: the game it restores goes into the fresh model and choice above, and the key it takes is that of the game it leaves
        _keeper.Start(_model, _opponent, _choice);
        _stopped = false;
    }

    void ChessGame::UpdateKeeper() {
        if (_stopped) {
            return;  // frames that still run after Stop() must not save what Shutdown() did to the controller (two people)
        }
        _keeper.Update(_model, _opponent, _choice);
    }

    void ChessGame::Stop() {
        // Written first, while the opponent still holds what the person picked (Shutdown() forgets it); the opponent goes before TaskManager::Shutdown() (ChessGame.h)
        _keeper.Flush(_model, _opponent, _choice);
        _stopped = true;
        _opponent.Shutdown();
        _analyst.Shutdown();
    }

    void ChessGame::Reset() {
        _model.Reset();
    }

}  // namespace BECore::Chess
