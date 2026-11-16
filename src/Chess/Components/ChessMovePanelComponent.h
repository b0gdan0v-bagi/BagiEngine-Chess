#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Components/MoveRows.h>

namespace BECore::Chess {

    class ChessGameModel;

    /**
     * @brief Gives the canvas of the node `MovePanel` (the move list right of the board in the Wide view, under it in the Tall view) its words from the game. Keeps no game state.
     *
     * OnDraw() runs before the canvas on the same node draws: only when the model or its version changed it rebuilds the rows (BuildMoveList and BuildMovePanelRows, the last
     * kMovePanelRows rows up to the last move), publishes the three `UIVars` of every row and shows the rows and the marks the rows say. With no game active every row is empty and hidden.
     *
     * The panel is shown only while the box of the active view lies inside the safe area of the canvas (MovePanelFits), asked on every draw: the safe area changes with the window, not with the model.
     * Before the canvas has been laid out once it stays hidden, so nothing flashes.
     */
    class ChessMovePanelComponent : public IComponent {
        BE_CLASS(ChessMovePanelComponent)
    public:
        ChessMovePanelComponent() = default;
        ~ChessMovePanelComponent() override = default;

        void OnDraw() override;

    private:
        /// Writes the variables and the visibility of the rows and marks for the game of @p model (none: empty rows)
        void Publish(const ChessGameModel* model);

        /// Shows or hides the box after the safe area of the canvas
        void SyncBox();

        bool _published = false;  ///< Whether the rows were written at least once
        const ChessGameModel* _publishedModel = nullptr;
        uint64_t _publishedVersion = 0;
        MoveRowsOnCanvas _rows{"MovePanel"};
        bool _boxShown = false;  ///< The scene starts with the box hidden
    };

}  // namespace BECore::Chess
