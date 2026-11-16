#include "MoveRows.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveList.h>

namespace BECore::Chess {

    namespace {

        constexpr int kNoCurrentPly = -1;

        // Interned on the first use, not per frame; indexed by row
        struct MoveWordNames {
            Array<PoolString, kMovePanelRows> numbers;
            Array<PoolString, kMovePanelRows> whites;
            Array<PoolString, kMovePanelRows> blacks;
        };

        const MoveWordNames& GetWordNames() {
            static const MoveWordNames names = [] {
                MoveWordNames made;
                for (size_t row = 0; row < kMovePanelRows; ++row) {
                    made.numbers[row] = PoolString::Intern(Format("movePanelNumber{}", row).c_str());
                    made.whites[row] = PoolString::Intern(Format("movePanelWhite{}", row).c_str());
                    made.blacks[row] = PoolString::Intern(Format("movePanelBlack{}", row).c_str());
                }
                return made;
            }();
            return names;
        }

    }  // namespace

    Vector<MovePanelRow> CurrentMovePanelRows(const ChessGameModel* model) {
        Vector<MoveListRow> list;
        int lastPly = kNoCurrentPly;
        if (model) {
            list = BuildMoveList(model->GetStartFen(), model->GetMoves());
            lastPly = static_cast<int>(model->GetMoves().size()) - 1;
        }
        Vector<MovePanelRow> rows = BuildMovePanelRows(list, lastPly);
        ASSERT(rows.size() == kMovePanelRows);
        return rows;
    }

    void PublishMovePanelWords(const Vector<MovePanelRow>& rows) {
        const MoveWordNames& names = GetWordNames();
        UIVars& vars = CoreManager::GetUIVars();
        for (size_t row = 0; row < kMovePanelRows && row < rows.size(); ++row) {
            vars.Set(names.numbers[row], StringView(rows[row].number));
            vars.Set(names.whites[row], StringView(rows[row].white.text));
            vars.Set(names.blacks[row], StringView(rows[row].black.text));
        }
    }

    MoveRowsOnCanvas::MoveRowsOnCanvas(StringView prefix) {
        for (size_t row = 0; row < kMovePanelRows; ++row) {
            _rows[row] = PoolString::Intern(Format("{}Row{}", prefix, row).c_str());
            _whiteMarks[row] = PoolString::Intern(Format("{}WhiteMark{}", prefix, row).c_str());
            _blackMarks[row] = PoolString::Intern(Format("{}BlackMark{}", prefix, row).c_str());
        }
    }

    void MoveRowsOnCanvas::Show(UICanvasComponent& canvas, const Vector<MovePanelRow>& rows) {
        for (size_t row = 0; row < kMovePanelRows && row < rows.size(); ++row) {
            const MovePanelRow& entry = rows[row];
            if (entry.shown != _rowShown[row]) {
                _rowShown[row] = entry.shown;
                canvas.SetElementVisible(_rows[row], entry.shown);
            }
            if (entry.white.current != _whiteMarked[row]) {
                _whiteMarked[row] = entry.white.current;
                canvas.SetElementVisible(_whiteMarks[row], entry.white.current);
            }
            if (entry.black.current != _blackMarked[row]) {
                _blackMarked[row] = entry.black.current;
                canvas.SetElementVisible(_blackMarks[row], entry.black.current);
            }
        }
    }

}  // namespace BECore::Chess
