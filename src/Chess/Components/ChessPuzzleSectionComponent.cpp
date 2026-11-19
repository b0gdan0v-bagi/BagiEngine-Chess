#include "ChessPuzzleSectionComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIVars.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ClickerDisabled.h>
#include <Chess/Components/MenuActions.h>
#include <Chess/Game/StatusText.h>
#include <Events/InputEvents.h>
#include <Generated/ChessPuzzleSectionComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        struct Ids {
            static constexpr auto chessPuzzleLeave = "ChessPuzzleLeave"_intern;
            static constexpr auto chessPuzzleNext = "ChessPuzzleNext"_intern;
            static constexpr auto chessPuzzleRetry = "ChessPuzzleRetry"_intern;
            static constexpr auto puzzleNextWord = "puzzleNextWord"_intern;
            static constexpr auto puzzleNumber = "puzzleNumber"_intern;
            static constexpr auto puzzleRating = "puzzleRating"_intern;
            static constexpr auto puzzleSectionLeave = "PuzzleSectionLeave"_intern;
            static constexpr auto puzzleSectionNext = "PuzzleSectionNext"_intern;
            static constexpr auto puzzleSectionRetry = "PuzzleSectionRetry"_intern;
            static constexpr auto puzzleState = "puzzleState"_intern;
        };

        /// Writes @p disabled to the button only when it differs from @p known, which then follows it
        void Grey(UICanvasComponent& canvas, PoolString name, bool disabled, bool& known) {
            if (disabled != known) {
                known = disabled;
                SetClickerDisabled(canvas, name, disabled);
            }
        }

    }  // namespace

    void ChessPuzzleSectionComponent::OnAttached() {
        SubscribeInScene<InputEvents::ActionEvent, &ChessPuzzleSectionComponent::OnAction>(this);
    }

    void ChessPuzzleSectionComponent::OnAction(const InputEvents::ActionEvent& event) {
        const PoolString action = event.action;
        const bool next = action == Ids::chessPuzzleNext;
        const bool retry = action == Ids::chessPuzzleRetry;
        const bool leave = action == Ids::chessPuzzleLeave;
        ChessGame* game = (next || retry || leave) ? ChessGame::GetActive() : nullptr;
        if (!game) {
            return;
        }
        const bool done = next ? game->NextPuzzle() : (retry ? game->RetryPuzzle() : game->LeavePuzzle());
        if (done) {
            InputEvents::ActionEvent::Emit(MenuActions::closeMenu);
        }
    }

    void ChessPuzzleSectionComponent::OnDraw() {
        const auto canvas = GetNode().GetComponent<UICanvasComponent>();
        if (!canvas) {
            return;
        }
        const ChessGame* game = ChessGame::GetActive();
        const ChessGameModel* model = game ? &game->GetModel() : nullptr;
        const uint64_t version = model ? model->GetVersion() : 0;
        const uint64_t revision = game ? game->GetPuzzle().GetRevision() : 0;
        const size_t count = game ? game->GetPuzzle().GetPuzzles().Size() : 0;
        const bool on = game && game->IsPuzzleOn();
        if (_published && game == _publishedGame && model == _publishedModel && version == _publishedVersion && revision == _publishedRevision && count == _publishedCount && on == _publishedOn) {
            return;
        }
        _published = true;
        _publishedGame = game;
        _publishedModel = model;
        _publishedVersion = version;
        _publishedRevision = revision;
        _publishedCount = count;
        _publishedOn = on;

        String number;
        String rating;
        String state;
        if (on) {
            const PuzzleSession& puzzle = game->GetPuzzle();
            const Puzzle* current = puzzle.GetCurrent();
            number = StatusText::PuzzleNumber(*puzzle.GetIndex(), count);
            rating = StatusText::PuzzleRating(current->rating);
            state = StatusText::PuzzleProgress(puzzle.GetPhase(*model), puzzle.WasWrong(), puzzle.GetMistakes());
        } else if (count == 0) {
            state = StatusText::NoPuzzles();
        }
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set(Ids::puzzleNumber, StringView(number));
        vars.Set(Ids::puzzleRating, StringView(rating));
        vars.Set(Ids::puzzleState, StringView(state));
        vars.Set(Ids::puzzleNextWord, StringView(StatusText::PuzzleNextWord(on)));

        Grey(*canvas, Ids::puzzleSectionNext, count == 0, _nextDisabled);
        Grey(*canvas, Ids::puzzleSectionRetry, !on, _retryDisabled);
        Grey(*canvas, Ids::puzzleSectionLeave, !on, _leaveDisabled);
    }

}  // namespace BECore::Chess
