#include "MovePanel.h"

namespace BECore::Chess {

    namespace {

        bool HoldsPly(const MoveListRow& row, int ply) {
            return ply >= 0 && (row.white.ply == ply || row.black.ply == ply);
        }

        /// One past the last row of the window: the row of @p currentPly, else the end of the list
        size_t WindowEnd(const Vector<MoveListRow>& rows, int currentPly) {
            for (size_t i = 0; i < rows.size(); ++i) {
                if (HoldsPly(rows[i], currentPly)) {
                    return i + 1;
                }
            }
            return rows.size();
        }

        MovePanelRow MakeRow(const MoveListRow& row, int currentPly) {
            MovePanelRow shown;
            shown.number = ToString(row.number);
            shown.number.push_back('.');
            shown.white.text = row.white.san.empty() ? String(kMovePanelAbsentMove) : row.white.san;
            shown.white.current = row.white.ply >= 0 && row.white.ply == currentPly;
            shown.black.text = row.black.san;
            shown.black.current = row.black.ply >= 0 && row.black.ply == currentPly;
            shown.shown = true;
            return shown;
        }

    }  // namespace

    Vector<MovePanelRow> BuildMovePanelRows(const Vector<MoveListRow>& rows, int currentPly, size_t visibleRows) {
        Vector<MovePanelRow> panel(visibleRows);
        const size_t end = WindowEnd(rows, currentPly);
        const size_t first = end > visibleRows ? end - visibleRows : 0;
        for (size_t i = first; i < end; ++i) {
            panel[i - first] = MakeRow(rows[i], currentPly);
        }
        return panel;
    }

}  // namespace BECore::Chess
