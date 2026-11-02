#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace SDLEvents {
        struct MouseButtonDownEvent;
    }

    namespace InputEvents {
        struct ActionEvent;
    }

}  // namespace BECore

namespace BECore::Chess {

    /**
     * @brief Renders the 8x8 board background and routes square clicks
     *        to the active ChessGame's model.
     *
     * Board geometry (origin + cell size) is reflected so a level designer can
     * tweak placement in the editor. Owns no game state itself — all logic
     * lives in ChessGameModel and is shared with ChessPieceComponent through
     * GameManager::GetActive().
     *
     * The HUD is not drawn here: it is a UI canvas of the scene (`config/SceneConfig.xml`) whose labels follow the variables `turn` and `status`, which this
     * component publishes to `CoreManager::GetUIVars()` whenever the model changed (StatusText). Its Reset button emits the action `ChessReset`, which this
     * component answers by starting a new game.
     *
     * Its node sits on a layer below the pieces (`layer` in SceneConfig.xml), so the
     * squares and highlights are drawn first and the piece sprites on top.
     */
    class ChessBoardComponent : public IComponent {
        BE_CLASS(ChessBoardComponent)
    public:
        ChessBoardComponent() = default;
        ~ChessBoardComponent() override;

        BE_REFLECT_FIELD float _originX = 100.0f;
        BE_REFLECT_FIELD float _originY = 100.0f;
        BE_REFLECT_FIELD float _squareSize = 80.0f;

        void OnAttached() override;
        void OnDraw() override;

        /// World-space pixel position (top-left) of a board cell.
        void SquareToPixel(int file, int rank, float& outX, float& outY) const;

        float GetSquareSize() const noexcept {
            return _squareSize;
        }
        float GetOriginX() const noexcept {
            return _originX;
        }
        float GetOriginY() const noexcept {
            return _originY;
        }

        /// The currently-attached board, if any. Chess assumes a single board
        /// is alive at any time; piece components use this to query layout
        /// without traversing the scene graph or duplicating geometry fields.
        static ChessBoardComponent* GetActive() noexcept {
            return s_active;
        }

    private:
        void OnMouseDown(const SDLEvents::MouseButtonDownEvent&);
        void OnAction(const InputEvents::ActionEvent&);

        /// Gives the UI labels the words for the state of the game; only when the model changed, since a label checks the version of the variables every frame
        void PublishUiVars();

        uint64_t _publishedVersion = 0;         // the model version PublishUiVars() last published, 0 for none
        const void* _publishedModel = nullptr;  // the model it was published for: another game starts the count again

        static ChessBoardComponent* s_active;
    };

}  // namespace BECore::Chess
