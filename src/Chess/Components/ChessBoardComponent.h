#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace SDLEvents {
        struct MouseButtonDownEvent;
    }

    namespace InputEvents {
        struct ActionEvent;
    }

    namespace SceneEvents {
        struct SceneUpdateEvent;
    }

}  // namespace BECore

namespace BECore::Chess {

    class ChessGameModel;

    /**
     * @brief Renders the 8x8 board background and routes square clicks
     *        to the active ChessGame's model.
     *
     * Board geometry (origin + cell size) is reflected so a level designer can
     * tweak placement in the editor. Owns no game state itself — all logic
     * lives in ChessGameModel and is shared with ChessPieceComponent through
     * GameManager::GetActive().
     *
     * The HUD is not drawn here: it is a UI canvas of the scene (`config/SceneConfig.xml`) whose labels and buttons follow the variables `turn`, `status`, `side` and
     * `level`, which this component publishes to `CoreManager::GetUIVars()` whenever the game or the opponent changed (StatusText, OpponentText): `turn` says
     * "Computer is thinking" while the program searches, `status` is the sentence of the opponent's error while one stands and the status of the game otherwise.
     * It also shows the element `Level` of the HUD while a computer opponent plays and hides it for two people. Its buttons emit the actions `ChessReset`
     * (a new game), `ChessCycleSide` and `ChessCycleLevel` (OpponentController::CycleSide() and CycleLevel()), which this component answers.
     *
     * It is also the frame of the computer opponent: IGame has no per-frame hook, so on every SceneUpdateEvent it runs
     * ChessGame::GetOpponent().Update() (the one place that does; it changes the model, so it cannot be done in OnDraw()). While the program plays the side to move,
     * a click on the board does nothing: a half-made move of the person would only cancel the program's own search. After the game has ended a click still starts
     * the next one. The result of a game is logged once, whoever ended it.
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
        void OnUpdate(const SceneEvents::SceneUpdateEvent&);

        /// One line in the log when a game has ended, the first time it is seen: whether the person, the program or the control API played the last move
        void LogResultOnce(const ChessGameModel& model);

        /// Gives the UI labels the words for the state of the game and of the opponent; only when something they say changed, since a label checks the version of the
        /// variables every frame
        void PublishUiVars();

        uint64_t _publishedVersion = 0;         // the model version PublishUiVars() last published, 0 for none
        const void* _publishedModel = nullptr;  // the model it was published for: another game starts the count again
        uint64_t _publishedRevision = 0;        // the OpponentController::GetRevision() it was published for
        bool _publishedThinking = false;        // whether the program was searching then: a search starts and ends without a move of the game

        uint64_t _loggedVersion = 0;         // the model version of the ended game that LogResultOnce() last logged, 0 for none
        const void* _loggedModel = nullptr;  // the model it was logged for

        static ChessBoardComponent* s_active;
    };

}  // namespace BECore::Chess
