#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Game/ChessTypes.h>
#include <Chess/Game/MaterialLayout.h>
#include <Chess/Graphics/GraphicsPreset.h>

namespace BECore::Chess {

    class ChessGameModel;

    /**
     * @brief Gives the canvas of the node `Material` (the captured pieces and the balance: in the left margin, in the Portrait view above and below the board, in the Wide view in the bands of
     * the move panel) its words and its sprites from the game. Keeps no game state.
     *
     * The lower group shows what the color at the bottom of the board has taken, the upper one what the other color has taken, so the groups swap when the board turns. OnDraw() runs before
     * the canvas on the same node draws: only when the model, its version or the bottom color changed it publishes the twelve `UIVars`, and when the bottom color or the graphics choice changed (also
     * at the first draw) it gives the icons of both sets their sprites. With no game active the variables are empty and no row is shown.
     *
     * The canvas has two sets of the groups bound to the same variables: the margin set and the panel set (`MaterialPanel*`, in the bands of the move panel). Exactly one is shown, asked on
     * every draw (the safe area changes with the window, not with the model): the panel set while MovePanelFits holds for the safe area of the canvas, which is when ChessMovePanelComponent
     * shows the panel; the margin set otherwise and before the first layout. In the margin set a row is shown when its count is above 0 and Game/MaterialLayout.h says the box of the lists
     * (the one of the active view) lies inside the safe area; its balance boxes stay whatever that says. In the panel set a row is shown when its count is above 0. Before the canvas has been
     * laid out once no row is shown, so nothing flashes.
     */
    class ChessMaterialComponent : public IComponent {
        BE_CLASS(ChessMaterialComponent)
    public:
        ChessMaterialComponent() = default;
        ~ChessMaterialComponent() override = default;

        void OnDraw() override;

    private:
        static constexpr size_t kRowCount = kMaterialGroupCount * kMaterialRowsPerGroup;

        /// Writes the twelve variables for the material of @p model (all empty for none) and the counts the rows follow; with @p iconsToo also the sprite of every icon
        void Publish(const ChessGameModel* model, PieceColor bottom, GraphicsPreset preset, bool iconsToo);

        /// Shows one set and hides the other, and the rows after their counts and the safe area of the canvas; only the elements whose state differs from what was set last
        void SyncRows();

        Optional<PieceColor> _publishedBottom;
        uint64_t _publishedGraphics = 0;  ///< GraphicsChoice::GetRevision() the icons were given for; 0 is none yet
        const ChessGameModel* _publishedModel = nullptr;
        uint64_t _publishedVersion = 0;
        Array<int, kRowCount> _counts{};                               ///< the pieces gone per row (a row is group * 5 + the index in CapturableTypes()), as last published
        Array<Array<bool, kRowCount>, kMaterialSetCount> _rowShown{};  ///< per set: the rows start hidden in the scene
        Array<bool, kMaterialSetCount> _setShown{true, false};         ///< the scene starts with the margin set shown and the panel set hidden
    };

}  // namespace BECore::Chess
