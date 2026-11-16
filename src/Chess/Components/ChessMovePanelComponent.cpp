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

    void ChessMovePanelComponent::OnAttached() {
        SubscribeInScene<InputEvents::ActionEvent, &ChessMovePanelComponent::OnAction>(this);
    }

    void ChessMovePanelComponent::OnAction(const InputEvents::ActionEvent& event) {
        if (event.action != "ChessShowMove"_intern) {
            return;
        }
        ChessGameModel* model = ChessGame::GetActiveModel();
        if (!model) {
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
        if (layout.HasLayout()) {
            const Rect& safe = layout.GetResolved().safeBounds;
            static const PoolString tall = PoolString::Intern(kTallView);
            const MovePanelView view = GetScene().GetActiveView().name == tall ? MovePanelView::Tall : MovePanelView::Wide;
            fits = MovePanelFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h, view);
        }
        if (fits != _boxShown) {
            _boxShown = fits;
            static const PoolString box = PoolString::Intern("MovePanelBox");
            canvas->SetElementVisible(box, fits);
        }
    }

    void ChessMovePanelComponent::OnDraw() {
        const ChessGameModel* model = ChessGame::GetActiveModel();
        if (_published.Changed(model)) {
            Publish(model);
        }
        SyncBox();
    }

}  // namespace BECore::Chess
