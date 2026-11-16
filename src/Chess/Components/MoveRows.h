#pragma once

#include <Chess/Game/MovePanel.h>

namespace BECore {
    class UICanvasComponent;
}

namespace BECore::Chess {

    class ChessGameModel;

    /// The rows the move list shows for the game of @p model: the last kMovePanelRows rows up to the last move, that move current; no game: every entry empty and not shown
    [[nodiscard]] Vector<MovePanelRow> CurrentMovePanelRows(const ChessGameModel* model);

    /// Writes the words of @p rows to the UIVars `movePanelNumber<i>`, `movePanelWhite<i>` and `movePanelBlack<i>`, which every canvas that shows the list binds
    void PublishMovePanelWords(const Vector<MovePanelRow>& rows);

    /**
     * @brief The rows of the move list on one canvas: shows the elements `<prefix>Row<i>`, `<prefix>WhiteMark<i>` and `<prefix>BlackMark<i>` that the entries say.
     *
     * Remembers what it showed, so a call changes only what differs; the canvas starts with every row and mark hidden. The names are interned once, by the constructor.
     */
    class MoveRowsOnCanvas {
    public:
        explicit MoveRowsOnCanvas(StringView prefix);

        void Show(UICanvasComponent& canvas, const Vector<MovePanelRow>& rows);

    private:
        Array<PoolString, kMovePanelRows> _rows;
        Array<PoolString, kMovePanelRows> _whiteMarks;
        Array<PoolString, kMovePanelRows> _blackMarks;
        Array<bool, kMovePanelRows> _rowShown{};
        Array<bool, kMovePanelRows> _whiteMarked{};
        Array<bool, kMovePanelRows> _blackMarked{};
    };

}  // namespace BECore::Chess
