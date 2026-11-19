#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Components/AnalysisRows.h>
#include <Chess/Components/MoveRows.h>

namespace BECore::InputEvents {
    struct ActionEvent;
}

namespace BECore::Chess {

    class ChessGameModel;

    /**
     * @brief Gives the canvas of the node `MovePanel` (the move list right of the board in the Wide view, under it in the Tall view) its words from the game. Keeps no game state.
     *
     * OnDraw() runs before the canvas on the same node draws: only when the rows changed (MoveRowsKey) it rebuilds them (BuildMoveList and BuildMovePanelRows, the page of
     * the move the board shows), publishes the three `UIVars` of every row and shows the rows and the marks the rows say. While the analyst's ratings are on, a brilliant move's cell reads `<san>!!`
     * (CurrentMovePanelRows()), and the rows are rebuilt when the analyst's revision moves too: a rating arrives with no new version of the model. With no game active every row is empty and hidden.
     *
     * The panel is shown only while the box of the active view lies inside the safe area of the canvas (MovePanelFits), asked on every draw: the safe area changes with the window, not with the model.
     * Before the canvas has been laid out once it stays hidden, so nothing flashes.
     *
     * It answers `ChessShowMove`, the press of a cell or a step button of the move list (`value`: ParseMoveListPress()), for this canvas and for the menu section "Moves", whose rows
     * are the same: the model shows the position the press asks for (ChessGameModel::ShowMoves()). A value of no press logs one warning; a cell with no move does nothing.
     *
     * It answers `ChessTakeBack` of both canvases too: ChessGameModel::TakeBack() of TakeBackPliesNow() moves (nothing when that is 0), which ends a view, drops the selection and the
     * premoves and moves the version (the program's search in flight goes stale). Each canvas's own component greys its button (TakeBackOnCanvas).
     *
     * The analysis band (the Wide band and the Tall twins) is published from the game's analyst (PublishAnalysisWords) when AnalysisKey says it changed, and the Hint buttons are greyed
     * while HintRefusalFor() refuses (HintOnCanvas). `ChessHint`: when the refusal does not hold, a view of an earlier position ends (the hint is for the game's position), the pick of the
     * opponent is told to the analyst and the hint is asked, as the control API's `set_state analysis` does. `ChessSetEvaluation` (`value` "true" / "false", what the toggle says): the same pick,
     * then the evaluation is switched; any other value logs one warning. The evaluation is of the game's position, not of the one a view shows. `ChessSetRatings` (`value` "true" / "false") switches
     * PositionAnalyst::SetRatings() with no engine pick (the rater is the own engine); any other value logs one warning.
     */
    class ChessMovePanelComponent : public IComponent {
        BE_CLASS(ChessMovePanelComponent)
    public:
        ChessMovePanelComponent() = default;
        ~ChessMovePanelComponent() override = default;

        void OnAttached() override;
        void OnDraw() override;

    private:
        void OnAction(const InputEvents::ActionEvent& event);

        /// ChessHint: ends a view, tells the analyst the pick and asks for the hint of the game's position, unless HintRefusalFor() refuses
        void AskForHint();

        /// ChessSetEvaluation: switches the evaluation to @p value ("true" or "false"); another value only logs
        void SetEvaluation(StringView value);

        /// ChessSetRatings: switches the ratings (the mark and the "!!" of the brilliant moves) to @p value ("true" or "false"); another value only logs
        void SetRatings(StringView value);

        /// Writes the variables and the visibility of the rows and marks for the game of @p model (none: empty rows)
        void Publish(const ChessGameModel* model);

        /// Shows or hides the box after the safe area of the canvas
        void SyncBox();

        MoveRowsKey _published;
        AnalysisKey _analysisPublished;
        HintOnCanvas _hint{"MovePanelHint"};
        HintOnCanvas _tallHint{"MovePanelTallHint"};
        MoveRowsOnCanvas _rows{"MovePanel"};
        TakeBackOnCanvas _takeBack{"MovePanel"};
        bool _boxShown = false;  ///< The scene starts with the box hidden
    };

}  // namespace BECore::Chess
