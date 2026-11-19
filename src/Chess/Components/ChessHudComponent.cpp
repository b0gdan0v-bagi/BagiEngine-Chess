#include "ChessHudComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ClickerDisabled.h>
#include <Chess/Engine/OpponentText.h>
#include <Chess/Game/OpeningBook.h>
#include <Chess/Game/StatusText.h>
#include <Events/InputEvents.h>
#include <Generated/ChessHudComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        struct Ids {
            static constexpr auto chessReset = "ChessReset"_intern;
            static constexpr auto opening = "opening"_intern;
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
        // The Turn line of an edited position changes with no step of the game's version (an edit, a refused edit, a refused "Play from here")
        const bool editing = game->IsEditing();
        const Optional<EditRefusal> refusal = editing ? game->GetEditor().GetLastRefusal() : std::nullopt;
        const StringView editorMessage = game->GetEditorMessage();
        // A puzzle starts, ends and is judged without a step of the opponent's revision; its verdicts and its reply also move the model, but the start and the end of one may not
        const PuzzleSession& puzzle = game->GetPuzzle();
        const bool puzzleOn = game->IsPuzzleOn();
        const uint64_t puzzleRevision = puzzle.GetRevision();
        const bool opponentChanged = &model != _publishedModel || opponent.GetRevision() != _publishedRevision || judge.GetRevision() != _publishedJudgeRevision;
        if (!opponentChanged && model.GetVersion() == _publishedVersion && thinking == _publishedThinking && personColor == _publishedColor && shown == _publishedShown &&
            editing == _publishedEditing && refusal == _publishedRefusal && editorMessage == StringView(_publishedEditorMessage) && puzzleOn == _publishedPuzzleOn &&
            puzzleRevision == _publishedPuzzleRevision) {
            return;
        }
        _publishedModel = &model;
        _publishedVersion = model.GetVersion();
        _publishedRevision = opponent.GetRevision();
        _publishedJudgeRevision = judge.GetRevision();
        _publishedThinking = thinking;
        _publishedColor = personColor;
        _publishedShown = shown;
        _publishedEditing = editing;
        _publishedRefusal = refusal;
        _publishedEditorMessage = String(editorMessage);
        _publishedPuzzleOn = puzzleOn;
        _publishedPuzzleRevision = puzzleRevision;

        // While an error of the opponent stands the game is stalled by it, so its sentence wins over the others; a refused draw offer wins over the game's own status ("Check"); a view
        // wins over all: the board shows another position and the line says how to get back to the game (the panel keeps the error, and the line has it again when the view ends).
        // A puzzle parks the opponent, so its error and draw offers say nothing on the board: the Turn line is the puzzle's, and a wrong move stands over the game's status
        const Optional<EngineError>& error = opponent.GetError();
        // A position that is edited has no turn, status or opening of the game: the Turn line is the editor's, and the bottom margin is the editor bar's
        String turn;
        if (editing) {
            turn = StatusText::Editing(game->GetEditor(), editorMessage);
        } else if (puzzleOn) {
            turn = StatusText::PuzzleTurn(puzzle.GetPhase(model));
        } else {
            turn = StatusText::Turn(model, thinking);
        }
        String status;
        if (!editing) {
            if (shown) {
                status = StatusText::Status(model);
            } else if (puzzleOn) {
                status = StatusText::PuzzleStatus(puzzle.WasWrong());
                if (status.empty()) {
                    status = StatusText::Status(model);
                }
            } else if (error) {
                status = OpponentText::Error(*error);
            } else if (offer == DrawOfferState::Declined) {
                status = StatusText::DrawDeclined();
            } else {
                status = StatusText::Status(model);
            }
        }
        const String opponentError = error ? OpponentText::Error(*error) : String();
        // The name of the position the board shows: a view names the viewed one, as the board does. The puzzle bar stands in the place of the name, so none shows while a puzzle is on
        const Optional<OpeningEntry> opening = (editing || puzzleOn) ? std::nullopt : OpeningOf(game->GetOpenings(), model.GetStartFen(), model.GetMoves(), model.GetShownMoveCount());
        const String openingText = opening ? OpeningDisplayName(opening->name, kOpeningMaxChars) : String();
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set(Ids::turn, StringView(turn));
        vars.Set(Ids::status, StringView(status));
        vars.Set(Ids::opening, StringView(openingText));
        // The ids of the opponent panel, those of the control API (the person's color, not the program's)
        vars.Set(Ids::opponentMode, choice.ModeId());
        vars.Set(Ids::opponentColor, choice.ColorId());
        vars.Set(Ids::opponentEngine, choice.EngineId());
        vars.Set(Ids::opponentLevel, choice.LevelId());
        vars.Set(Ids::opponentError, StringView(opponentError));

        const bool resignDisabled = !game->ResigningNow();
        if (resignDisabled != _resignDisabled) {
            _resignDisabled = resignDisabled;
            if (const auto canvas = GetNode().GetComponent<UICanvasComponent>()) {
                SetClickerDisabled(*canvas, Ids::resign, resignDisabled);
            }
        }
    }

}  // namespace BECore::Chess
