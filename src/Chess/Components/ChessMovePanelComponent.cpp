#include "ChessMovePanelComponent.h"

#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ChessBoardComponent.h>
#include <Chess/Game/ChessGameModel.h>
#include <Events/InputEvents.h>
#include <Generated/ChessMovePanelComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        struct Ids {
            static constexpr auto chessHint = "ChessHint"_intern;
            static constexpr auto chessSetEvaluation = "ChessSetEvaluation"_intern;
            static constexpr auto chessShowMove = "ChessShowMove"_intern;
            static constexpr auto chessTakeBack = "ChessTakeBack"_intern;
            static constexpr auto movePanelBox = "MovePanelBox"_intern;
        };

    }  // namespace

    void ChessMovePanelComponent::OnAttached() {
        SubscribeInScene<InputEvents::ActionEvent, &ChessMovePanelComponent::OnAction>(this);
    }

    void ChessMovePanelComponent::OnAction(const InputEvents::ActionEvent& event) {
        if (event.action == Ids::chessHint) {
            AskForHint();
            return;
        }
        if (event.action == Ids::chessSetEvaluation) {
            SetEvaluation(event.value.ToStringView());
            return;
        }
        const bool takeBack = event.action == Ids::chessTakeBack;
        if (!takeBack && event.action != Ids::chessShowMove) {
            return;
        }
        ChessGameModel* model = ChessGame::GetActiveModel();
        if (!model) {
            return;
        }
        if (takeBack) {
            // The rule is asked again: the button's grey look is from the last draw, and the game may have moved since
            if (const size_t plies = TakeBackPliesNow(model)) {
                model->TakeBack(plies);
            }
            return;
        }
        // A shown draft is the editor's: ending it by a view would be a change of the state with no step of the version
        if (model->IsDrafting()) {
            return;
        }
        const Optional<MoveListPress> press = ParseMoveListPress(event.value.ToStringView());
        if (!press) {
            LOG_WARNING("ChessShowMove ignored: '{}' is not a press of the move list"_format(event.value.ToStringView()));
            return;
        }
        // The rows of the press are the ones on the screen: the words were published from the same model in the last draw
        if (const Optional<size_t> shown = ShownMovesAfter(*press, CurrentMovePanelRows(model), model->GetShownMoveCount(), model->GetMoves().size())) {
            model->ShowMoves(*shown);
        }
    }

    void ChessMovePanelComponent::AskForHint() {
        ChessGameModel* model = ChessGame::GetActiveModel();
        PositionAnalyst* analyst = ChessGame::GetActiveAnalyst();
        // The rule is asked again: the button's grey look is from the last draw, and the game may have moved since
        if (!analyst || !HintAllowedNow(model)) {
            return;
        }
        if (model->IsViewing()) {
            model->EndViewing();
        }
        analyst->SetPicked(ChessGame::GetActiveOpponent()->GetEngine());
        const auto asked = analyst->RequestHint(*model);
        EXPECT(asked.has_value(), "ChessMovePanelComponent: HintRefusalFor and RequestHint disagree");
    }

    void ChessMovePanelComponent::SetEvaluation(StringView value) {
        PositionAnalyst* analyst = ChessGame::GetActiveAnalyst();
        const OpponentController* opponent = ChessGame::GetActiveOpponent();
        if (!analyst || !opponent) {
            return;
        }
        if (value != "true" && value != "false") {
            LOG_WARNING("ChessSetEvaluation ignored: '{}' is neither true nor false"_format(value));
            return;
        }
        analyst->SetPicked(opponent->GetEngine());
        analyst->SetEvaluation(value == "true");
    }

    void ChessMovePanelComponent::Publish(const ChessGameModel* model) {
        const Vector<MovePanelRow> rows = CurrentMovePanelRows(model);
        PublishMovePanelWords(rows);
        if (const auto canvas = GetNode().GetComponent<UICanvasComponent>()) {
            _rows.Show(*canvas, rows);
        }
    }

    void ChessMovePanelComponent::SyncBox() {
        const auto canvas = GetNode().GetComponent<UICanvasComponent>();
        if (!canvas) {
            return;
        }
        // Before the first layout of the canvas the safe area is not known, and the box stays hidden: the next draw has it. The scene's active view is already the one of this draw while
        // the layout may still be the last view's for this one frame; no box lies inside a view that is not its own, so that frame shows none
        const UICanvasLayout& layout = canvas->GetLayout();
        bool fits = false;
        // The editor's bar stands in the margin the box would share, and a draft has no moves to list
        const ChessGameModel* model = ChessGame::GetActiveModel();
        if (layout.HasLayout() && !(model && model->IsDrafting())) {
            const Rect& safe = layout.GetResolved().safeBounds;
            static const PoolString tall = PoolString::Intern(kTallView);
            const MovePanelView view = GetScene().GetActiveView().name == tall ? MovePanelView::Tall : MovePanelView::Wide;
            fits = MovePanelFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h, view);
        }
        if (fits != _boxShown) {
            _boxShown = fits;
            canvas->SetElementVisible(Ids::movePanelBox, fits);
        }
    }

    void ChessMovePanelComponent::OnDraw() {
        const ChessGameModel* model = ChessGame::GetActiveModel();
        if (_published.Changed(model)) {
            Publish(model);
        }
        if (_analysisPublished.Changed(model, ChessGame::GetActiveAnalyst())) {
            PublishAnalysisWords(model, ChessGame::GetActiveAnalyst());
        }
        if (const auto canvas = GetNode().GetComponent<UICanvasComponent>()) {
            _takeBack.Sync(*canvas, model);
            _hint.Sync(*canvas, model);
            _tallHint.Sync(*canvas, model);
        }
        SyncBox();
    }

}  // namespace BECore::Chess
