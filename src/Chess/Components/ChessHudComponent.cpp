#include "ChessHudComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <Chess/ChessGame.h>
#include <Chess/Engine/OpponentText.h>
#include <Chess/Game/StatusText.h>
#include <Events/InputEvents.h>
#include <Generated/ChessHudComponent.gen.hpp>

namespace BECore::Chess {

    void ChessHudComponent::OnAttached() {
        SubscribeInScene<InputEvents::ActionEvent, &ChessHudComponent::OnAction>(this);
    }

    void ChessHudComponent::OnAction(const InputEvents::ActionEvent& event) {
        if (event.action != "ChessReset"_intern) {
            return;
        }
        if (ChessGameModel* model = ChessGame::GetActiveModel()) {
            model->Reset();
        }
    }

    void ChessHudComponent::OnDraw() {
        PublishUiVars();
    }

    void ChessHudComponent::PublishUiVars() {
        const ChessGame* game = ChessGame::GetActive();
        if (!game) {
            return;
        }
        const ChessGameModel& model = game->GetModel();
        const OpponentController& opponent = game->GetOpponent();
        const OpponentChoice& choice = game->GetChoice();
        // Read on every refresh, also when nothing else changed: it remembers the person's color, which a set_state of the control API could otherwise take with it
        const PieceColor personColor = choice.Color();
        // A search starts and ends without a move of the game, and a side or a level changes without one; the person's color changes without any of them while two people play
        const bool thinking = opponent.IsThinking();
        const bool opponentChanged = &model != _publishedModel || opponent.GetRevision() != _publishedRevision;
        if (!opponentChanged && model.GetVersion() == _publishedVersion && thinking == _publishedThinking && personColor == _publishedColor) {
            return;
        }
        _publishedModel = &model;
        _publishedVersion = model.GetVersion();
        _publishedRevision = opponent.GetRevision();
        _publishedThinking = thinking;
        _publishedColor = personColor;

        // While an error of the opponent stands the game is stalled by it, so its sentence wins over "Check"
        const Optional<EngineError>& error = opponent.GetError();
        const String turn = StatusText::Turn(model, thinking);
        const String status = error ? OpponentText::Error(*error) : StatusText::Status(model);
        const String opponentError = error ? OpponentText::Error(*error) : String();
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("turn"_intern, StringView(turn));
        vars.Set("status"_intern, StringView(status));
        // The ids of the opponent panel, those of the control API (the person's color, not the program's)
        vars.Set("opponentMode"_intern, choice.ModeId());
        vars.Set("opponentColor"_intern, choice.ColorId());
        vars.Set("opponentEngine"_intern, choice.EngineId());
        vars.Set("opponentLevel"_intern, choice.LevelId());
        vars.Set("opponentError"_intern, StringView(opponentError));
    }

}  // namespace BECore::Chess
