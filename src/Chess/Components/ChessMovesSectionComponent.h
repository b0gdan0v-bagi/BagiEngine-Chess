#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Components/MoveRows.h>

namespace BECore::Chess {

    class ChessGameModel;

    /**
     * @brief Gives the canvas of the node `MovesSection` (the menu section "Moves", for the windows with no move panel on the screen) the rows of the move list. Keeps no game state.
     *
     * OnDraw() runs before the canvas on the same node draws: only when the model or its version changed it writes the words the move panel's canvas binds too (the same rows, so the
     * two never disagree) and shows the rows and marks of this canvas. The menu shows and hides the canvas (Game/MenuSections.*).
     */
    class ChessMovesSectionComponent : public IComponent {
        BE_CLASS(ChessMovesSectionComponent)
    public:
        ChessMovesSectionComponent() = default;
        ~ChessMovesSectionComponent() override = default;

        void OnDraw() override;

    private:
        bool _published = false;  ///< Whether the rows were written at least once
        const ChessGameModel* _publishedModel = nullptr;
        uint64_t _publishedVersion = 0;
        MoveRowsOnCanvas _rows{"MovesSection"};
    };

}  // namespace BECore::Chess
