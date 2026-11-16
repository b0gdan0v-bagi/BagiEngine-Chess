#include "ChessMovePanelComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ChessBoardComponent.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveList.h>
#include <Generated/ChessMovePanelComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        constexpr int kNoCurrentPly = -1;

        // Interned on the first use, not per frame; indexed by row
        struct MovePanelNames {
            Array<PoolString, kMovePanelRows> numberVars;
            Array<PoolString, kMovePanelRows> whiteVars;
            Array<PoolString, kMovePanelRows> blackVars;
            Array<PoolString, kMovePanelRows> rows;
            Array<PoolString, kMovePanelRows> whiteMarks;
            Array<PoolString, kMovePanelRows> blackMarks;
            PoolString box;
        };

        const MovePanelNames& GetNames() {
            static const MovePanelNames names = [] {
                MovePanelNames made;
                for (size_t row = 0; row < kMovePanelRows; ++row) {
                    made.numberVars[row] = PoolString::Intern(Format("movePanelNumber{}", row).c_str());
                    made.whiteVars[row] = PoolString::Intern(Format("movePanelWhite{}", row).c_str());
                    made.blackVars[row] = PoolString::Intern(Format("movePanelBlack{}", row).c_str());
                    made.rows[row] = PoolString::Intern(Format("MovePanelRow{}", row).c_str());
                    made.whiteMarks[row] = PoolString::Intern(Format("MovePanelWhiteMark{}", row).c_str());
                    made.blackMarks[row] = PoolString::Intern(Format("MovePanelBlackMark{}", row).c_str());
                }
                made.box = PoolString::Intern("MovePanelBox");
                return made;
            }();
            return names;
        }

    }  // namespace

    void ChessMovePanelComponent::Publish(const ChessGameModel* model) {
        const MovePanelNames& names = GetNames();
        Vector<MoveListRow> list;
        int lastPly = kNoCurrentPly;
        if (model) {
            list = BuildMoveList(model->GetStartFen(), model->GetMoves());
            lastPly = static_cast<int>(model->GetMoves().size()) - 1;
        }
        const Vector<MovePanelRow> rows = BuildMovePanelRows(list, lastPly);
        ASSERT(rows.size() == kMovePanelRows);

        UIVars& vars = CoreManager::GetUIVars();
        const auto canvas = GetNode().GetComponent<UICanvasComponent>();
        for (size_t row = 0; row < kMovePanelRows && row < rows.size(); ++row) {
            const MovePanelRow& entry = rows[row];
            vars.Set(names.numberVars[row], StringView(entry.number));
            vars.Set(names.whiteVars[row], StringView(entry.white.text));
            vars.Set(names.blackVars[row], StringView(entry.black.text));
            if (!canvas) {
                continue;
            }
            if (entry.shown != _rowShown[row]) {
                _rowShown[row] = entry.shown;
                canvas->SetElementVisible(names.rows[row], entry.shown);
            }
            if (entry.white.current != _whiteMarked[row]) {
                _whiteMarked[row] = entry.white.current;
                canvas->SetElementVisible(names.whiteMarks[row], entry.white.current);
            }
            if (entry.black.current != _blackMarked[row]) {
                _blackMarked[row] = entry.black.current;
                canvas->SetElementVisible(names.blackMarks[row], entry.black.current);
            }
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
            canvas->SetElementVisible(GetNames().box, fits);
        }
    }

    void ChessMovePanelComponent::OnDraw() {
        const ChessGameModel* model = ChessGame::GetActiveModel();
        const uint64_t version = model ? model->GetVersion() : 0;
        if (!_published || model != _publishedModel || version != _publishedVersion) {
            _published = true;
            _publishedModel = model;
            _publishedVersion = version;
            Publish(model);
        }
        SyncBox();
    }

}  // namespace BECore::Chess
