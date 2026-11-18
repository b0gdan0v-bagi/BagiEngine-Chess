#include "ChessHudComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <Chess/ChessGame.h>
#include <Chess/Engine/OpponentText.h>
#include <Chess/Game/StatusText.h>
#include <Events/InputEvents.h>
#include <Generated/ChessHudComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        struct Ids {
            static constexpr auto chessReset = "ChessReset"_intern;
            static constexpr auto opponentColor = "opponentColor"_intern;
            static constexpr auto opponentEngine = "opponentEngine"_intern;
            static constexpr auto opponentError = "opponentError"_intern;
            static constexpr auto opponentLevel = "opponentLevel"_intern;
            static constexpr auto opponentMode = "opponentMode"_intern;
            static constexpr auto resign = "Resign"_intern;
            static constexpr auto status = "status"_intern;
            static constexpr auto turn = "turn"_intern;
        };

    }  // namespace

    void ChessHudComponent::OnAttached() {
        SubscribeInScene<InputEvents::ActionEvent, &ChessHudComponent::OnAction>(this);
    }

    void ChessHudComponent::OnAction(const InputEvents::ActionEvent& event) {
        if (event.action != Ids::chessReset) {
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
        const DrawOfferJudge& judge = game->GetDrawJudge();
        const DrawOfferState offer = judge.GetState(model);
        const bool thinking = opponent.IsThinking() || offer == DrawOfferState::Pending;
        // A view starts, steps and ends without a version step of the game
        const Optional<size_t> shown = model.IsViewing() ? Optional<size_t>(model.GetShownMoveCount()) : std::nullopt;
        const bool opponentChanged = &model != _publishedModel || opponent.GetRevision() != _publishedRevision || judge.GetRevision() != _publishedJudgeRevision;
        if (!opponentChanged && model.GetVersion() == _publishedVersion && thinking == _publishedThinking && personColor == _publishedColor && shown == _publishedShown) {
            return;
        }
        _publishedModel = &model;
        _publishedVersion = model.GetVersion();
        _publishedRevision = opponent.GetRevision();
        _publishedJudgeRevision = judge.GetRevision();
        _publishedThinking = thinking;
        _publishedColor = personColor;
        _publishedShown = shown;

        // While an error of the opponent stands the game is stalled by it, so its sentence wins over the others; a refused draw offer wins over the game's own status ("Check"); a view
        // wins over all: the board shows another position and the line says how to get back to the game (the panel keeps the error, and the line has it again when the view ends)
        const Optional<EngineError>& error = opponent.GetError();
        const String turn = StatusText::Turn(model, thinking);
        String status;
        if (shown) {
            status = StatusText::Status(model);
        } else if (error) {
            status = OpponentText::Error(*error);
        } else if (offer == DrawOfferState::Declined) {
            status = StatusText::DrawDeclined();
        } else {
            status = StatusText::Status(model);
        }
        const String opponentError = error ? OpponentText::Error(*error) : String();
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set(Ids::turn, StringView(turn));
        vars.Set(Ids::status, StringView(status));
        // The ids of the opponent panel, those of the control API (the person's color, not the program's)
        vars.Set(Ids::opponentMode, choice.ModeId());
        vars.Set(Ids::opponentColor, choice.ColorId());
        vars.Set(Ids::opponentEngine, choice.EngineId());
        vars.Set(Ids::opponentLevel, choice.LevelId());
        vars.Set(Ids::opponentError, StringView(opponentError));

        const bool resignDisabled = !game->ResigningNow();
        if (resignDisabled != _resignDisabled) {
            _resignDisabled = resignDisabled;
            const auto canvas = GetNode().GetComponent<UICanvasComponent>();
            UIElement* button = canvas ? canvas->GetRoot().FindByName(Ids::resign) : nullptr;
            if (UIInputClicker* clicker = button ? button->GetComponent<UIInputClicker>() : nullptr) {
                clicker->disabled = resignDisabled;
            }
        }
    }

}  // namespace BECore::Chess
