#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Game/ChessTypes.h>
#include <Chess/Game/MaterialLayout.h>

namespace BECore::Chess {

    class ChessGameModel;

    /**
     * @brief Gives the canvas of the node `Material` (the captured pieces and the balance: in the left margin, in the Portrait view above and below the board) its words and its sprites from the game.
     * Keeps no game state.
     *
     * The lower group shows what the color at the bottom of the board has taken, the upper one what the other color has taken, so the groups swap when the board turns. OnDraw() runs before
     * the canvas on the same node draws: only when the model, its version or the bottom color changed it publishes the twelve `UIVars`, and when the bottom color changed (also at the first
     * draw) it gives the ten icons their sprites. With no game active the variables are empty and no row is shown.
     *
     * A row is shown when its count is above 0 and the lists fit: Game/MaterialLayout.h says whether their box (the one of the active view) lies inside the safe area of the canvas, asked on every
     * draw (the safe area changes with the window, not with the model). Before the canvas has been laid out once the rows stay hidden, so nothing flashes. The balance boxes are not hidden by this
     * rule.
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
        void Publish(const ChessGameModel* model, PieceColor bottom, bool iconsToo);

        /// Shows or hides the rows after their counts and the safe area of the canvas; only the rows whose state differs from what was set last
        void SyncRows();

        Optional<PieceColor> _publishedBottom;
        const ChessGameModel* _publishedModel = nullptr;
        uint64_t _publishedVersion = 0;
        Array<int, kRowCount> _counts{};     ///< the pieces gone per row (a row is group * 5 + the index in CapturableTypes()), as last published
        Array<bool, kRowCount> _rowShown{};  ///< the rows start hidden in the scene
    };

}  // namespace BECore::Chess
