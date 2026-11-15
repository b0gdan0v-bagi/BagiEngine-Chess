#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Game/ChessTypes.h>
#include <Chess/Game/MenuSections.h>

namespace BECore {

    namespace SDLEvents {
        struct MouseButtonDownEvent;
        struct MouseButtonUpEvent;
        struct MouseMotionEvent;
    }  // namespace SDLEvents

    namespace InputEvents {
        struct ActionEvent;
    }

    namespace SceneEvents {
        struct SceneUpdateEvent;
        struct SceneViewChangedEvent;
    }  // namespace SceneEvents

}  // namespace BECore

namespace BECore::Chess {

    class ChessGameModel;

    /// The name of the second view of SceneConfig.xml (`viewVariants`), which the components that differ per view pick by
    inline constexpr const char* kPortraitView = "Portrait";

    /**
     * @brief The board of the scene: draws the squares and the highlights of the active model and turns clicks and drags into its moves.
     *
     * Owns no game state: the model is shared with ChessPieceComponent through GameManager::GetActive(). The geometry (origin and cell size) is reflected, in scene units: the engine scales
     * the scene to the window. The pieces, the HUD, the menu and the panels are other nodes of the scene (`Components/CLAUDE.md` tells them).
     *
     * The scene has two views, so the geometry has two sets: the default fields and the `portrait*` ones, for the view named kPortraitView. Which set holds is changed only on
     * SceneViewChangedEvent (sent from Scene::Update(), before every update of the frame) and every accessor reads that one choice: the squares drawn, the pieces placed from the same
     * update and the result bar then always agree, which they would not if each asked the scene for its active view at its own moment (a draw may run after the view was picked and before
     * the pieces have moved). Before the first event the default set holds. The event comes one Update after the Draw that picked the view, so for one frame the canvases are already
     * laid out in the new view while the board still draws in the old one.
     *
     * It publishes the UIVars that the HUD, the panels and the coordinate labels show, only when something they say changed, and answers their actions: the HUD and the menu (`ChessReset`,
     * `ChessOpenMenu`, `ChessOpenSection`, `ChessCloseMenu`), the four of the opponent panel's groups (`ChessSetMode`, `ChessSetColor`, `ChessSetEngine`, `ChessSetLevel`) and the promotion pair
     * (`ChessPromote`, `ChessCancelPromotion`). An id in no table changes nothing and logs one warning. `opponentColor` is the PERSON's color, the opposite of the control API's `opponent.plays`.
     *
     * It is also the frame of the computer opponent and of the analysis: IGame has no per-frame hook, so on every SceneUpdateEvent it runs ChessGame::GetOpponent().Update() and
     * ChessGame::GetAnalyst().Update() (the one place that does either). The opponent plays into the model, so neither can run in OnDraw(). The result of a game is logged once, whoever
     * ended it; a click on the board after the game has ended starts the next one.
     *
     * The board is drawn from Black's side (turned by 180 degrees) exactly when the program plays White, read live (GetBottomColor()); the model and the control API do not turn.
     */
    class ChessBoardComponent : public IComponent {
        BE_CLASS(ChessBoardComponent)
    public:
        ChessBoardComponent() = default;
        ~ChessBoardComponent() override;

        BE_REFLECT_FIELD float _originX = 100.0f;
        BE_REFLECT_FIELD float _originY = 100.0f;
        BE_REFLECT_FIELD float _squareSize = 80.0f;
        BE_REFLECT_FIELD float _portraitOriginX = 40.0f;
        BE_REFLECT_FIELD float _portraitOriginY = 144.0f;
        BE_REFLECT_FIELD float _portraitSquareSize = 80.0f;

        void OnAttached() override;
        void OnDraw() override;

        /// The color drawn at the bottom of the board, read live from the opponent on every call (no cached flag): Black exactly when the program plays White. See BoardOrientation.h
        [[nodiscard]] PieceColor GetBottomColor() const;

        /// World-space pixel position (top-left) of the square (file, rank), for the orientation of GetBottomColor() (a1 is the bottom left cell with White at the bottom, the top right with Black)
        void SquareToPixel(int file, int rank, float& outX, float& outY) const;

        /// The same for an orientation the caller has read once (OnDraw places many squares per frame)
        void SquareToPixel(int file, int rank, PieceColor bottom, float& outX, float& outY) const;

        /// The geometry of the view in effect (see the class comment), in scene units of that view
        float GetSquareSize() const noexcept {
            return _portraitActive ? _portraitSquareSize : _squareSize;
        }
        float GetOriginX() const noexcept {
            return _portraitActive ? _portraitOriginX : _originX;
        }
        float GetOriginY() const noexcept {
            return _portraitActive ? _portraitOriginY : _originY;
        }

        /// The currently-attached board, if any. Chess assumes a single board is alive at any time; piece components read the layout through it
        static ChessBoardComponent* GetActive() noexcept {
            return s_active;
        }

        /// A piece held by the pointer: the square it was lifted from and the top-left of the piece in scene units (the pointer minus half a square)
        struct LiftedPiece {
            Square from;
            float x = 0.0f;
            float y = 0.0f;
        };

        /// The piece the person is dragging, engaged only while a drag is armed, still valid (see IsDragValid()) and lifted past kDragThreshold; the piece component asks it on every update
        [[nodiscard]] Optional<LiftedPiece> GetLiftedPiece() const;

    private:
        /// A press that left a piece selected: where, for which game, at which version, and where the pointer is now (scene units)
        struct Drag {
            Square from;
            const ChessGameModel* model = nullptr;
            uint64_t version = 0;
            float pressX = 0.0f;
            float pressY = 0.0f;
            float pointerX = 0.0f;
            float pointerY = 0.0f;
            bool lifted = false;
        };

        void OnMouseDown(const SDLEvents::MouseButtonDownEvent&);
        void OnMouseUp(const SDLEvents::MouseButtonUpEvent&);
        void OnMouseMotion(const SDLEvents::MouseMotionEvent&);

        /// The square drawn under a point of the screen (scene units through Scene::ScreenToSceneX/Y, the orientation of GetBottomColor()), none off the board
        [[nodiscard]] Optional<Square> SquareUnder(float screenX, float screenY) const;

        /// Whether the drag still holds: the same game at the same version, not over, the dragged square still selected, no promotion pending and the program not to move
        [[nodiscard]] static bool IsDragValid(const Drag& drag, const ChessGameModel* model);
        void OnAction(const InputEvents::ActionEvent&);

        /// The four actions of the choice groups of the opponent panel (the id in `value`); any other action is ignored
        void AnswerSetAction(const InputEvents::ActionEvent& event);

        /// Shows the canvas of this section of the menu and hides the canvases of the others (none: hides them all); the list `MenuPanel` is not touched
        void ShowMenuSection(Optional<MenuSection> shown);

        /// The three actions of the menu (the id of the section in `value` of ChessOpenSection); false for any other action
        bool AnswerMenuAction(const InputEvents::ActionEvent& event);

        /// The two actions of the promotion panel (the id of the piece in `value` of ChessPromote); false for any other action
        bool AnswerPromotionAction(const InputEvents::ActionEvent& event);

        /// Shows the canvas of the node `PromotionPanel` while the model has a promotion pending and hides it otherwise (only when that differs from what was set last)
        void SyncPromotionPanel(const ChessGameModel& model);
        void OnUpdate(const SceneEvents::SceneUpdateEvent&);

        /// Takes the geometry of the new view; a drag in the hand ends, since its scene units were those of the old one
        void OnViewChanged(const SceneEvents::SceneViewChangedEvent&);

        /// Without Stockfish in this build (CHESS_NO_STOCKFISH) hides its segment of the opponent panel, so Own engine fills the group; once, at the first update or draw, when every node of the
        /// scene is attached. The scene file is the same in every build
        void HideUnavailableEngines();

        /// One line in the log when a game has ended, the first time it is seen: whether the person, the program or the control API played the last move
        void LogResultOnce(const ChessGameModel& model);

        /// Gives the UI labels the words for the state of the game and of the opponent, only when something they say changed (a label checks the version of the variables every frame)
        void PublishUiVars();

        /// Gives the 16 labels at the edges of the board their letters and numbers for the orientation read in OnDraw(), only when the bottom color changed: the labels sit at fixed cells and
        /// only their text turns with the board
        void PublishCoordinates(PieceColor bottom);

        Optional<PieceColor> _publishedBottom;

        uint64_t _publishedVersion = 0;
        const void* _publishedModel = nullptr;  // another game starts the count of versions again
        uint64_t _publishedRevision = 0;
        bool _publishedThinking = false;                 // a search starts and ends without a move of the game
        PieceColor _publishedColor = PieceColor::White;  // changes with no revision of the controller while two people play

        uint64_t _loggedVersion = 0;
        const void* _loggedModel = nullptr;

        bool _portraitActive = false;       // the geometry in effect: the `portrait*` fields, else the default ones
        bool _promotionPanelShown = false;  // the canvas starts hidden
        bool _enginesChecked = false;

        Optional<Drag> _drag;

        static ChessBoardComponent* s_active;
    };

}  // namespace BECore::Chess
