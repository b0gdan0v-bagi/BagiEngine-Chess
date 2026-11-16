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
    /// four there. ChessMovePanel.xml has to agree with these numbers too. In view units
    constexpr float kMovePanelTallLeft = 40.0f;
    constexpr float kMovePanelTallTop = 840.0f;
    constexpr float kMovePanelTallRight = 680.0f;
    constexpr float kMovePanelTallBottom = 1128.0f;

    /// The view of the scene whose box the rule asks for; the default and the Portrait view hold neither box, so either answer there is no panel
    CORE_ENUM(MovePanelView, uint8_t, Wide, Tall)

    /// The rows of the list the panel has room for: it shows the last ones of the game
    constexpr size_t kMovePanelRows = 8;

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

    /// One cell of a row of the panel: the text of the move and whether it is the move the board shows
    struct MovePanelCell {
        String text;
        bool current = false;
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
     * The window is the last @p visibleRows rows of @p rows up to the row that holds @p currentPly (the ply of ChessGameModel::GetMoves(); -1 or a ply no row has: no
     * current move, and the window ends at the last row). The entries after the window are empty and not shown. An absent White move (a game that began with Black to move)
     * reads kMovePanelAbsentMove and is never current; an absent Black move is empty.
     */
    [[nodiscard]] Vector<MovePanelRow> BuildMovePanelRows(const Vector<MoveListRow>& rows, int currentPly, size_t visibleRows = kMovePanelRows);

}  // namespace BECore::Chess
