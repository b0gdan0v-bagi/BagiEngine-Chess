#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Game/ChessTypes.h>
#include <Chess/Game/MaterialLayout.h>

namespace BECore::Chess {

    class ChessGameModel;

    /**
     * @brief Gives the canvas of the node `Material` (the captured pieces and the balance in the left margin) its words and its sprites from the game. Keeps no game state.
     *
     * The canvas has two groups, mirrored about the center of the board: the lower one shows what the color at the bottom of the board has taken, the upper one what the other color has taken
     * (`BottomColor` of Game/BoardOrientation.h, the same function the board draws with, so the groups swap when the board turns). A group is a list of five rows (queen, rook, bishop, knight,
     * pawn) and a balance box. In OnDraw(), which runs before the canvas on the same node draws, it reads the model and the opponent and, only when the model, its version or the bottom color
     * changed, publishes the twelve `UIVars` (`material<Top|Bottom><Queen|Rook|Bishop|Knight|Pawn>`: "x3" or "", and `material<Top|Bottom>Balance`: "+N" for the group that is ahead, else ""),
     * and gives each of the ten icons the sprite `<Color><Type>` of the pieces its group has taken (the other color than the group's). With no game active the variables are empty and no row is shown.
     *
     * A row is shown when its count is above 0 and the lists fit: Game/MaterialLayout.h says whether the box of the lists lies inside the safe area of the canvas, and this is asked on every
     * draw (the safe area changes with the window, not with the model). Before the canvas has been laid out once the safe area is not known and the rows stay hidden, so nothing flashes for
     * one frame. The balance boxes are not hidden by this rule.
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

        std::optional<PieceColor> _publishedBottom;  ///< the bottom color the display was published for, none before the first draw
        const void* _publishedModel = nullptr;       ///< the model it was published for
        uint64_t _publishedVersion = 0;              ///< and the model version
        eastl::array<int, kRowCount> _counts{};      ///< the pieces gone per row (group * 5 + the index in CapturableTypes()) as last published
        eastl::array<bool, kRowCount> _rowShown{};   ///< what SyncRows() last set: the rows start hidden in the scene
    };

}  // namespace BECore::Chess
