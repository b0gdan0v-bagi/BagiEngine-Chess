#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Components/BoardPalette.h>
#include <Chess/Game/ChessTypes.h>

namespace BECore {

    namespace SDLEvents {
        struct MouseButtonDownEvent;
        struct MouseButtonUpEvent;
        struct MouseMotionEvent;
    }  // namespace SDLEvents

    namespace SceneEvents {
        struct SceneUpdateEvent;
        struct SceneViewChangedEvent;
    }  // namespace SceneEvents

}  // namespace BECore

namespace BECore::Chess {

    class ChessGame;
    class ChessGameModel;

    /// The name of the second view of SceneConfig.xml (`viewVariants`), which the components that differ per view pick by
    inline constexpr const char* kPortraitView = "Portrait";

    /// The name of the view that is Portrait made higher for the narrow phones held upright: every place of Portrait holds in it, and the room under the board is the move panel's
    inline constexpr const char* kTallView = "Tall";

    /// Whether the view named @p view has the board and the places of Portrait (Portrait and Tall)
    [[nodiscard]] inline bool HasPortraitPlaces(PoolString view) {
        static const PoolString portrait = PoolString::Intern(kPortraitView);
        static const PoolString tall = PoolString::Intern(kTallView);
        return view == portrait || view == tall;
    }

    /**
     * @brief The board of the scene: draws the squares and the highlights of the active model and turns clicks and drags into its moves.
     *
     * Owns no game state: the model is shared with ChessPieceComponent through GameManager::GetActive(). The geometry (origin and cell size) is reflected, in scene units: the engine scales
     * the scene to the window. The pieces, the HUD, the menu and the panels are other nodes of the scene (`Components/CLAUDE.md` tells them).
     *
     * The board has two places, so the geometry has two sets: the default fields (the default and the Wide view) and the `portrait*` ones (HasPortraitPlaces()). Which set holds is changed only on
     * SceneViewChangedEvent (sent from Scene::Update(), before every update of the frame) and every accessor reads that one choice: the squares drawn, the pieces placed from the same
     * update and the result bar then always agree, which they would not if each asked the scene for its active view at its own moment (a draw may run after the view was picked and before
     * the pieces have moved). Before the first event the default set holds. The event comes one Update after the Draw that picked the view, so for one frame the canvases are already
     * laid out in the new view while the board still draws in the old one.
     *
     * It publishes the UIVars of the coordinate labels, only when the bottom color changed. The HUD, the menu and the panels answer their own actions, each a component on its own node
     * (ChessHudComponent, ChessMenuComponent, ChessOpponentPanelComponent, ChessPromotionPanelComponent); after a click the board asks the promotion picker to look at the model at once.
     *
     * It is also the frame of the computer opponent and of the analysis: IGame has no per-frame hook, so on every SceneUpdateEvent it runs ChessGame::GetOpponent().Update() and
     * ChessGame::GetAnalyst().Update() (the one place that does either). The opponent plays into the model, so neither can run in OnDraw(). The result of a game is logged once, whoever
     * ended it; a click on the board after the game has ended starts the next one.
     *
     * The squares, the marks and the pieces are of ChessGameModel::GetShownPosition(): while an earlier position is viewed the board marks its move and check and draws no hint or
     * premove, and a press on the board only returns to the game. With a review held it marks that position's best move (PositionAnalyst::GetReviewedPosition()) in the hint's two colors.
     *
     * While a position is edited (ChessGame::IsEditing()) the board draws the squares and none of the game's marks, a left press puts the picked piece on the square under it (the same
     * piece again takes it off), a right press takes the piece off, and nothing of the select, drag, premove and promotion flow runs. The opponent and the draw judge are not updated then.
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

        /// The press of a mouse @p button on @p square while a position is edited: the left puts or takes off the picked piece, the right takes the piece off, any other does nothing
        static void EditSquare(ChessGame& game, Square square, uint8_t button);
        void OnMouseUp(const SDLEvents::MouseButtonUpEvent&);
        void OnMouseMotion(const SDLEvents::MouseMotionEvent&);

        /// The square drawn under a point of the screen (scene units through Scene::ScreenToSceneX/Y, the orientation of GetBottomColor()), none off the board
        [[nodiscard]] Optional<Square> SquareUnder(float screenX, float screenY) const;

        /// Whether the drag still holds: the same game at the same version, not over, the dragged square still selected, no promotion pending and the program not to move
        [[nodiscard]] static bool IsDragValid(const Drag& drag, const ChessGameModel* model);
        void OnUpdate(const SceneEvents::SceneUpdateEvent&);

        /// Takes the geometry of the new view; a drag in the hand ends, since its scene units were those of the old one
        void OnViewChanged(const SceneEvents::SceneViewChangedEvent&);

        /// One line in the log when a game has ended, the first time it is seen: whether the person, the program or the control API played the last move
        void LogResultOnce(const ChessGameModel& model);

        /// Gives the 16 labels at the edges of the board their letters and numbers for the orientation read in OnDraw(), only when the bottom color changed: the labels sit at fixed cells and
        /// only their text turns with the board
        void PublishCoordinates(PieceColor bottom);

        Optional<PieceColor> _publishedBottom;

        BoardPalette _palette;

        uint64_t _loggedVersion = 0;
        const void* _loggedModel = nullptr;

        bool _portraitActive = false;  // the geometry in effect: the `portrait*` fields, else the default ones

        Optional<Drag> _drag;

        static ChessBoardComponent* s_active;
    };

}  // namespace BECore::Chess
