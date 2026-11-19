#include "MovePanel.h"

namespace BECore::Chess {

    namespace {

        constexpr StringView kWhitePress = "white";
        constexpr StringView kBlackPress = "black";
        constexpr size_t kDecimalBase = 10;
        /// More than any row of a game, few enough that the number cannot wrap
        constexpr size_t kMaxRowDigits = 4;

        bool HoldsPly(const MoveListRow& row, int ply) {
            return ply >= 0 && (row.white.ply == ply || row.black.ply == ply);
        }

        /// The row the window has to show: the one of @p currentPly, the first for the position before the first move, else the last; @p rows is not empty
        size_t RowOfPly(const Vector<MoveListRow>& rows, int currentPly) {
            if (currentPly < 0) {
                return 0;
            }
            for (size_t i = 0; i < rows.size(); ++i) {
                if (HoldsPly(rows[i], currentPly)) {
                    return i;
                }
            }
            return rows.size() - 1;
        }

        /// One past the last row of the page of @p row, the pages counted from the end of the list
        size_t PageEnd(size_t rowCount, size_t row, size_t visibleRows) {
            const size_t pagesAfter = (rowCount - 1 - row) / visibleRows;
            return rowCount - pagesAfter * visibleRows;
        }

        MovePanelRow MakeRow(const MoveListRow& row, int currentPly) {
            MovePanelRow shown;
            shown.number = ToString(row.number);
            shown.number.push_back('.');
            shown.white.text = row.white.san.empty() ? String(kMovePanelAbsentMove) : row.white.san;
            shown.white.current = row.white.ply >= 0 && row.white.ply == currentPly;
            shown.white.ply = row.white.ply;
            shown.black.text = row.black.san;
            shown.black.current = row.black.ply >= 0 && row.black.ply == currentPly;
            shown.black.ply = row.black.ply;
            shown.shown = true;
            return shown;
        }

    }  // namespace

    Vector<MovePanelRow> BuildMovePanelRows(const Vector<MoveListRow>& rows, int currentPly, size_t visibleRows) {
        Vector<MovePanelRow> panel(visibleRows);
        if (rows.empty() || visibleRows == 0) {
            return panel;
        }
        const size_t end = PageEnd(rows.size(), RowOfPly(rows, currentPly), visibleRows);
        const size_t first = end > visibleRows ? end - visibleRows : 0;
        for (size_t i = first; i < end; ++i) {
            panel[i - first] = MakeRow(rows[i], currentPly);
        }
        return panel;
    }

    void MarkMovePanelRows(Vector<MovePanelRow>& rows, const Vector<bool>& brilliantByPly) {
        const auto mark = [&brilliantByPly](MovePanelCell& cell) {
            if (cell.ply >= 0 && static_cast<size_t>(cell.ply) < brilliantByPly.size() && brilliantByPly[static_cast<size_t>(cell.ply)]) {
                cell.text.append(kMovePanelBrilliantSuffix.data(), kMovePanelBrilliantSuffix.size());
            }
        };
        for (MovePanelRow& row : rows) {
            mark(row.white);
            mark(row.black);
        }
    }

    Optional<MoveListPress> ParseMoveListPress(StringView value) {
        if (value == "back") {
            return MoveListPress{MoveListPressKind::Back};
        }
        if (value == "forward") {
            return MoveListPress{MoveListPressKind::Forward};
        }
        if (value == "latest") {
            return MoveListPress{MoveListPressKind::Latest};
        }
        MoveListPress press;
        StringView digits;
        if (value.starts_with(kWhitePress)) {
            press.kind = MoveListPressKind::White;
            digits = value.substr(kWhitePress.size());
        } else if (value.starts_with(kBlackPress)) {
            press.kind = MoveListPressKind::Black;
            digits = value.substr(kBlackPress.size());
        } else {
            return std::nullopt;
        }
        if (digits.empty() || digits.size() > kMaxRowDigits) {
            return std::nullopt;
        }
        for (const char digit : digits) {
            if (digit < '0' || digit > '9') {
                return std::nullopt;
            }
            press.row = press.row * kDecimalBase + static_cast<size_t>(digit - '0');
        }
        return press;
    }

    Optional<size_t> ShownMovesAfter(const MoveListPress& press, const Vector<MovePanelRow>& rows, size_t shownMoves, size_t gameMoves) {
        const size_t shown = eastl::min(shownMoves, gameMoves);
        switch (press.kind) {
            case MoveListPressKind::Back:
                return shown > 0 ? shown - 1 : 0;
            case MoveListPressKind::Forward:
                return eastl::min(shown + 1, gameMoves);
            case MoveListPressKind::Latest:
                return gameMoves;
            case MoveListPressKind::White:
            case MoveListPressKind::Black:
                break;
        }
        if (press.row >= rows.size()) {
            return std::nullopt;
        }
        const MovePanelCell& cell = press.kind == MoveListPressKind::White ? rows[press.row].white : rows[press.row].black;
        if (cell.ply < 0) {
            return std::nullopt;
        }
        return static_cast<size_t>(cell.ply) + 1;
    }

}  // namespace BECore::Chess
