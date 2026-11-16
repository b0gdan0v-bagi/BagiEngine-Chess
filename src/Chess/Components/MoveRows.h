#pragma once

#include <Chess/Game/MovePanel.h>

namespace BECore {
    class UICanvasComponent;
}

namespace BECore::Chess {

    class ChessGameModel;

    /// The rows the move list shows for the game of @p model: the page of the move the board shows (the last one, or the viewed one), that move current; no game: every entry empty
    /// and not shown
    [[nodiscard]] Vector<MovePanelRow> CurrentMovePanelRows(const ChessGameModel* model);

    /// What the rows of a canvas were last built from: the model, its version and the moves of the shown position (a view moves no version)
    class MoveRowsKey {
    public:
        /// Whether the rows of @p model are other than the ones last built (always on the first call); remembers @p model as built
        bool Changed(const ChessGameModel* model);

    private:
        bool _built = false;
        const ChessGameModel* _model = nullptr;
        uint64_t _version = 0;
        size_t _shownMoves = 0;
    };

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

    /// How many moves one press of the takeback takes back in the game of @p model now (TakeBackPlies() with the side the active opponent plays); 0: nothing, also with no game
    [[nodiscard]] size_t TakeBackPliesNow(const ChessGameModel* model);

    /**
     * @brief The takeback button `<prefix>TakeBack` of one canvas: greyed (its clicker `disabled`) while TakeBackPliesNow() is 0, so a press that would do nothing looks it.
     *
     * The node file authors it disabled, as the game starts with no move. Remembers what it set, so a call changes the clicker only when the answer changes.
     */
    class TakeBackOnCanvas {
    public:
        explicit TakeBackOnCanvas(StringView prefix);

        void Sync(UICanvasComponent& canvas, const ChessGameModel* model);

    private:
        PoolString _name;
        bool _disabled = true;
    };

}  // namespace BECore::Chess
