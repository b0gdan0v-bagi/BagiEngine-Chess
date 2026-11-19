#include "ChessGameSectionComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/MenuActions.h>
#include <Chess/Engine/DrawOffer.h>
#include <Chess/Game/StatusText.h>
#include <Events/InputEvents.h>
#include <Generated/ChessGameSectionComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        struct Ids {
            static constexpr auto chessDraw = "ChessDraw"_intern;
            static constexpr auto chessGameNo = "ChessGameNo"_intern;
            static constexpr auto chessGameYes = "ChessGameYes"_intern;
            static constexpr auto chessResign = "ChessResign"_intern;
            static constexpr auto gameQuestion = "gameQuestion"_intern;
            static constexpr auto gameSectionAnswers = "GameSectionAnswers"_intern;
            static constexpr auto gameSectionConfirm = "GameSectionConfirm"_intern;
            static constexpr auto gameSectionDraw = "GameSectionDraw"_intern;
            static constexpr auto gameSectionPlate = "GameSectionPlate"_intern;
            static constexpr auto gameSectionQuestion = "GameSectionQuestion"_intern;
            static constexpr auto gameSectionResign = "GameSectionResign"_intern;
        };

        bool TwoPeoplePlay(const ChessGame& game) {
            const ProgramSides sides = game.GetOpponent().GetSides();
            return !sides.white && !sides.black;
        }

        void CloseTheMenu() {
            InputEvents::ActionEvent::Emit(MenuActions::closeMenu);
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

    void ChessGameSectionComponent::Ask(GameQuestion question, const ChessGame& game, bool fromHud) {
        _asking = question;
        _fromHud = fromHud;
        _askedModel = &game.GetModel();
        _askedVersion = game.GetModel().GetVersion();
        _askedRevision = game.GetOpponent().GetRevision();
    }

    void ChessGameSectionComponent::Answer(ChessGame& game) {
        const GameQuestion question = _asking;
        _asking = GameQuestion::None;
        _fromHud = false;
        if (question == GameQuestion::Resign) {
            if (const Optional<PieceColor> color = game.ResigningNow()) {
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
        if (action == Ids::chessGameNo && _asking != GameQuestion::None && _fromHud) {
            ShowCanvas(false);
        }
        if (action == MenuActions::openMenu || action == MenuActions::closeMenu || action == MenuActions::openSection || action == Ids::chessGameNo) {
            _asking = GameQuestion::None;
            _fromHud = false;
            return;
        }
        const bool resign = action == Ids::chessResign;
        const bool draw = action == Ids::chessDraw;
        const bool yes = action == Ids::chessGameYes;
        ChessGame* game = (resign || draw || yes) ? ChessGame::GetActive() : nullptr;
        if (!game) {
            return;
        }
        // The press may be older than the grey look of the buttons, so the rule is asked again
        if (yes) {
            Answer(*game);
        } else if (resign) {
            if (game->ResigningNow()) {
                // The HUD's Resign comes while no modal canvas is shown (they cover the HUD), so showing this one is all the menu would do; an action of the menu
                // would click a second time
                const auto canvas = GetNode().GetComponent<UICanvasComponent>();
                const bool fromHud = canvas && !canvas->_visible;
                ShowCanvas(true);
                Ask(GameQuestion::Resign, *game, fromHud);
            }
        } else if (game->CanOfferDraw()) {
            if (TwoPeoplePlay(*game)) {
                Ask(GameQuestion::Draw, *game);
            } else {
                static_cast<void>(game->OfferDraw());
                CloseTheMenu();
            }
        }
    }

    void ChessGameSectionComponent::ShowCanvas(bool shown) {
        UICanvasComponent::SetVisibleByName(GetScene(), GetNode().GetName(), shown);
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
            // The HUD's question came without the section: the game, not the section, is what is left when it goes
            if (_fromHud) {
                ShowCanvas(false);
            }
            _asking = GameQuestion::None;
            _fromHud = false;
        }
        if (_asking != _shownAsking || _fromHud != _shownFromHud) {
            const bool asking = _asking != GameQuestion::None;
            const bool confirmOnly = asking && _fromHud;
            canvas->SetElementVisible(Ids::gameSectionPlate, !confirmOnly);
            canvas->SetElementVisible(Ids::gameSectionConfirm, confirmOnly);
            _shownFromHud = _fromHud;
            if (asking) {
                const String question = _asking == GameQuestion::Resign ? StatusText::ResignQuestion() : StatusText::DrawQuestion();
                CoreManager::GetUIVars().Set(Ids::gameQuestion, StringView(question));
            }
            canvas->SetElementVisible(Ids::gameSectionQuestion, asking);
            canvas->SetElementVisible(Ids::gameSectionAnswers, asking);
            canvas->SetElementVisible(Ids::gameSectionResign, !asking);
            canvas->SetElementVisible(Ids::gameSectionDraw, !asking);
            _shownAsking = _asking;
        }

        if (_greyKnown && model == _greyModel && version == _greyVersion && revision == _greyRevision) {
            return;
        }
        _greyKnown = true;
        _greyModel = model;
        _greyVersion = version;
        _greyRevision = revision;
        const bool resignDisabled = !game || !game->ResigningNow();
        const bool drawDisabled = !game || !game->CanOfferDraw();
        if (resignDisabled != _resignDisabled) {
            _resignDisabled = resignDisabled;
            SetClickerDisabled(*canvas, Ids::gameSectionResign, resignDisabled);
        }
        if (drawDisabled != _drawDisabled) {
            _drawDisabled = drawDisabled;
            SetClickerDisabled(*canvas, Ids::gameSectionDraw, drawDisabled);
        }
    }

}  // namespace BECore::Chess
