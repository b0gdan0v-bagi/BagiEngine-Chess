#include "ChessGameSectionComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <Chess/ChessGame.h>
#include <Chess/Engine/DrawOffer.h>
#include <Chess/Game/GameEnding.h>
#include <Chess/Game/StatusText.h>
#include <Events/InputEvents.h>
#include <Generated/ChessGameSectionComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        /// The color the person resigns for now, or nothing when nobody can (a finished game, two programs)
        Optional<PieceColor> ResigningNow(const ChessGame& game) {
            const ChessGameModel& model = game.GetModel();
            if (model.IsGameOver()) {
                return std::nullopt;
            }
            const ProgramSides sides = game.GetOpponent().GetSides();
            return ResigningColor(model.GetTurn(), game.GetOpponent().GetPlays(), sides.white && sides.black);
        }

        bool CanOfferDraw(const ChessGame& game) {
            return !DrawOfferRefusalFor(game.GetModel(), game.GetOpponent().GetSides()).has_value();
        }

        bool TwoPeoplePlay(const ChessGame& game) {
            const ProgramSides sides = game.GetOpponent().GetSides();
            return !sides.white && !sides.black;
        }

        void CloseTheMenu() {
            InputEvents::ActionEvent::Emit("ChessCloseMenu"_intern);
        }

        void SetClickerDisabled(UICanvasComponent& canvas, PoolString name, bool disabled) {
            UIElement* button = canvas.GetRoot().FindByName(name);
            if (UIInputClicker* clicker = button ? button->GetComponent<UIInputClicker>() : nullptr) {
                clicker->disabled = disabled;
            }
        }

    }  // namespace

    void ChessGameSectionComponent::OnAttached() {
        SubscribeInScene<InputEvents::ActionEvent, &ChessGameSectionComponent::OnAction>(this);
    }

    void ChessGameSectionComponent::Ask(GameQuestion question, const ChessGame& game) {
        _asking = question;
        _askedModel = &game.GetModel();
        _askedVersion = game.GetModel().GetVersion();
        _askedRevision = game.GetOpponent().GetRevision();
    }

    void ChessGameSectionComponent::Answer(ChessGame& game) {
        const GameQuestion question = _asking;
        _asking = GameQuestion::None;
        if (question == GameQuestion::Resign) {
            if (const Optional<PieceColor> color = ResigningNow(game)) {
                static_cast<void>(game.GetModel().Resign(*color));
            }
        } else if (question == GameQuestion::Draw) {
            static_cast<void>(game.OfferDraw());
        }
        if (question != GameQuestion::None) {
            CloseTheMenu();
        }
    }

    void ChessGameSectionComponent::OnAction(const InputEvents::ActionEvent& event) {
        const PoolString action = event.action;
        if (action == "ChessOpenMenu"_intern || action == "ChessCloseMenu"_intern || action == "ChessOpenSection"_intern || action == "ChessGameNo"_intern) {
            _asking = GameQuestion::None;
            return;
        }
        const bool resign = action == "ChessResign"_intern;
        const bool draw = action == "ChessDraw"_intern;
        const bool yes = action == "ChessGameYes"_intern;
        ChessGame* game = (resign || draw || yes) ? ChessGame::GetActive() : nullptr;
        if (!game) {
            return;
        }
        // The press may be older than the grey look of the buttons, so the rule is asked again
        if (yes) {
            Answer(*game);
        } else if (resign) {
            if (ResigningNow(*game)) {
                Ask(GameQuestion::Resign, *game);
            }
        } else if (CanOfferDraw(*game)) {
            if (TwoPeoplePlay(*game)) {
                Ask(GameQuestion::Draw, *game);
            } else {
                static_cast<void>(game->OfferDraw());
                CloseTheMenu();
            }
        }
    }

    void ChessGameSectionComponent::OnDraw() {
        const auto canvas = GetNode().GetComponent<UICanvasComponent>();
        if (!canvas) {
            return;
        }
        const ChessGame* game = ChessGame::GetActive();
        const ChessGameModel* model = game ? &game->GetModel() : nullptr;
        const uint64_t version = model ? model->GetVersion() : 0;
        const uint64_t revision = game ? game->GetOpponent().GetRevision() : 0;

        if (_asking != GameQuestion::None && (model != _askedModel || version != _askedVersion || revision != _askedRevision)) {
            _asking = GameQuestion::None;
        }
        if (_asking != _shownAsking) {
            const bool asking = _asking != GameQuestion::None;
            if (asking) {
                const String question = _asking == GameQuestion::Resign ? StatusText::ResignQuestion() : StatusText::DrawQuestion();
                CoreManager::GetUIVars().Set("gameQuestion"_intern, StringView(question));
            }
            canvas->SetElementVisible("GameSectionQuestion"_intern, asking);
            canvas->SetElementVisible("GameSectionAnswers"_intern, asking);
            canvas->SetElementVisible("GameSectionResign"_intern, !asking);
            canvas->SetElementVisible("GameSectionDraw"_intern, !asking);
            _shownAsking = _asking;
        }

        if (_greyKnown && model == _greyModel && version == _greyVersion && revision == _greyRevision) {
            return;
        }
        _greyKnown = true;
        _greyModel = model;
        _greyVersion = version;
        _greyRevision = revision;
        const bool resignDisabled = !game || !ResigningNow(*game);
        const bool drawDisabled = !game || !CanOfferDraw(*game);
        if (resignDisabled != _resignDisabled) {
            _resignDisabled = resignDisabled;
            SetClickerDisabled(*canvas, "GameSectionResign"_intern, resignDisabled);
        }
        if (drawDisabled != _drawDisabled) {
            _drawDisabled = drawDisabled;
            SetClickerDisabled(*canvas, "GameSectionDraw"_intern, drawDisabled);
        }
    }

}  // namespace BECore::Chess
