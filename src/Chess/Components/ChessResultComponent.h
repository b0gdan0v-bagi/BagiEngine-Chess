#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore::Chess {

    /**
     * @brief Marks the end of a game over the board: the board is dimmed and a bar crosses its middle.
     *
     * The bar is white when White won, black when Black won and gray for a draw. The renderer has no text, so the colors are
     * the message; the reason (checkmate, stalemate, ...) is written to the log when the game ends.
     *
     * Draws nothing while the game is on. Its node sits on a layer above the pieces (`layer` in SceneConfig.xml), so the
     * dim covers the piece sprites as well. Owns no state: it reads the model and the board geometry.
     */
    class ChessResultComponent : public IComponent {
        BE_CLASS(ChessResultComponent)
    public:
        ChessResultComponent() = default;
        ~ChessResultComponent() override = default;

        void OnDraw() override;
    };

}  // namespace BECore::Chess
