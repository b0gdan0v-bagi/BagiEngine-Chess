#pragma once

#include <Chess/Game/MoveList.h>

namespace BECore::Chess {

    /// The box of the move panel (the node `MovePanel`, config/nodes/ChessMovePanel.xml) in the Wide view of the scene (1120 x 800 units): right of the board, as high as it. The panel is
    /// shown only while this whole box is inside the safe area of its canvas; ChessMovePanel.xml has to agree with these numbers. In view units
    constexpr float kMovePanelLeft = 760.0f;
    constexpr float kMovePanelTop = 80.0f;
    constexpr float kMovePanelRight = 1080.0f;
    constexpr float kMovePanelBottom = 720.0f;

    /// The box of the panel in the Tall view of the scene (720 x 1204 units): under the board and the lower row of the captured material, as wide as the board; the rows are two columns of
    /// four and three there. ChessMovePanel.xml has to agree with these numbers too. In view units
    constexpr float kMovePanelTallLeft = 40.0f;
    constexpr float kMovePanelTallTop = 840.0f;
    constexpr float kMovePanelTallRight = 680.0f;
    constexpr float kMovePanelTallBottom = 1128.0f;

    /// The view of the scene whose box the rule asks for; the default and the Portrait view hold neither box, so either answer there is no panel
    CORE_ENUM(MovePanelView, uint8_t, Wide, Tall)

    /// The rows of the list the panel has room for: one page of the game (BuildMovePanelRows())
    constexpr size_t kMovePanelRows = 7;

    /// What a row says in the place of the White move of a game that began with Black to move
    constexpr StringView kMovePanelAbsentMove = "...";

    /// Whether the box of the panel in @p view lies inside the safe area (its edges in view units; the edge itself counts as inside). A view that does not hold the box and a window whose
    /// insets reach into it leave no room, and the panel is then hidden
    [[nodiscard]] constexpr bool MovePanelFits(float safeLeft, float safeTop, float safeRight, float safeBottom, MovePanelView view = MovePanelView::Wide) noexcept {
        if (view == MovePanelView::Tall) {
            return kMovePanelTallLeft >= safeLeft && kMovePanelTallTop >= safeTop && kMovePanelTallRight <= safeRight && kMovePanelTallBottom <= safeBottom;
        }
        return kMovePanelLeft >= safeLeft && kMovePanelTop >= safeTop && kMovePanelRight <= safeRight && kMovePanelBottom <= safeBottom;
    }

    static_assert(MovePanelFits(0.0f, 0.0f, 1120.0f, 800.0f), "the Wide view has room for the panel");
    static_assert(!MovePanelFits(0.0f, 0.0f, 840.0f, 800.0f), "the default view is narrower than the box");
    static_assert(!MovePanelFits(0.0f, 0.0f, 720.0f, 960.0f), "so is the Portrait view");
    // UIPreview::SimulatedSafeArea(SideInsets) takes 9 % of the short side at the sides and 4 % at the bottom; at the fit scale 0.6 of a 480 px window the view is 672 px wide
    static_assert(!MovePanelFits(48.7f, 0.0f, 1071.3f, 768.0f), "a 700 x 480 window with side insets: the right inset (1071.3) cuts the box");
    static_assert(MovePanelFits(32.0f, 0.0f, 1088.0f, 768.0f), "a 720 x 480 window with side insets keeps the box");
    static_assert(MovePanelFits(0.0f, 0.0f, 720.0f, 1204.0f, MovePanelView::Tall), "the Tall view has room for its box");
    static_assert(!MovePanelFits(0.0f, 0.0f, 720.0f, 1204.0f), "and not for the box of the Wide view");
    static_assert(!MovePanelFits(0.0f, 0.0f, 720.0f, 960.0f, MovePanelView::Tall), "the Portrait view is too low for the box of the Tall view");
    static_assert(!MovePanelFits(0.0f, 0.0f, 1120.0f, 800.0f, MovePanelView::Tall), "so is the Wide view");
    // In the Tall view the width is the limit, so side insets of 9 % of the short side are always 64.8 units of it, more than the 40 the box keeps from the edge
    static_assert(!MovePanelFits(64.8f, 0.0f, 655.2f, 1175.2f, MovePanelView::Tall), "a Tall window with side insets cuts the box");

    /// One cell of a row of the panel: the text of the move, whether it is the move the board shows, and its ply in ChessGameModel::GetMoves() (-1: a place with no move)
    struct MovePanelCell {
        String text;
        bool current = false;
        int ply = -1;
    };

    /// One row of the panel; an entry that is not `shown` is empty and the panel hides its row
    struct MovePanelRow {
        String number;  ///< "<n>."
        MovePanelCell white;
        MovePanelCell black;
        bool shown = false;
    };

    /**
     * @brief The text of the rows the panel shows: exactly @p visibleRows entries, so a component writes every variable the same way.
     *
     * The list is cut into pages of @p visibleRows rows counted from its end (the last rows, the ones before them, ...; the first page holds what is left), and the window is the page
     * of the row that holds @p currentPly, the ply of ChessGameModel::GetMoves() the board shows, so a click on a row of the window keeps the window. -1 is the position before the
     * first move (the first page, nothing current); a ply no row has marks nothing and shows the last page. The entries after the window are empty and not shown. An absent White
     * move (a game that began with Black to move) reads kMovePanelAbsentMove and is never current; an absent Black move is empty.
     */
    [[nodiscard]] Vector<MovePanelRow> BuildMovePanelRows(const Vector<MoveListRow>& rows, int currentPly, size_t visibleRows = kMovePanelRows);

    /// What a press in the move list asks for: the cell of a move, one move back or forward, or the game's own position
    CORE_ENUM(MoveListPressKind, uint8_t, White, Black, Back, Forward, Latest)

    struct MoveListPress {
        MoveListPressKind kind = MoveListPressKind::Latest;
        size_t row = 0;  ///< The entry of BuildMovePanelRows() whose cell was pressed (White and Black only)
    };

    /// The `value` of a clicker of the move list (ChessMovePanel.xml, ChessMovesSection.xml): `white<i>` and `black<i>` for the cells of entry i, `back`, `forward`, `latest`; nothing
    /// for any other text
    [[nodiscard]] Optional<MoveListPress> ParseMoveListPress(StringView value);

    /**
     * @brief The number of moves of the position to show after @p press (ChessGameModel::ShowMoves()).
     *
     * A cell gives the position after its move (nothing for a cell with no move or an entry @p rows does not have), `back` and `forward` one move from @p shownMoves, kept within 0 and
     * @p gameMoves, and `latest` @p gameMoves, the game itself.
     */
    [[nodiscard]] Optional<size_t> ShownMovesAfter(const MoveListPress& press, const Vector<MovePanelRow>& rows, size_t shownMoves, size_t gameMoves);

}  // namespace BECore::Chess
