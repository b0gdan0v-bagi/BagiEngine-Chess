#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Game/ChessTypes.h>

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
     * The HUD is not drawn here: it is a UI canvas of the scene (`config/SceneConfig.xml`) whose labels follow the variables `turn`, `status`
     * and, for the opponent panel, the ids `opponentMode`, `opponentColor`, `opponentEngine`, `opponentLevel` and the sentence `opponentError`, which this component publishes
     * to `CoreManager::GetUIVars()` whenever the game or the opponent changed (StatusText, OpponentText): `turn` says "Computer is thinking" while the program searches, `status` is the sentence of
     * the opponent's error while one stands and the status of the game otherwise. Its buttons emit the actions `ChessReset` (a new game), `ChessOpenOpponent` and `ChessCloseOpponent` (show and hide
     * the modal canvas of the node `OpponentPanel`, the `Menu` button and the OK button), which this component answers; so are the four actions of the panel's choice groups, `ChessSetMode`,
     * `ChessSetColor`, `ChessSetEngine` and `ChessSetLevel`, whose `value` is an id (OpponentChoice picks it; an id that is in no table is ignored with one warning). `opponentColor` is the PERSON's
     * color, the opposite of the one the control API's `opponent.plays` names. No group of the panel is greyed: a color or a level picked while two people play is remembered
     * and applies when Computer is picked. It reads the person's color on every refresh so a side set from outside is remembered.
     *
     * The promotion picker is the modal canvas of the node `PromotionPanel` (Queen, Rook, Bishop, Knight, Cancel). The model owns the state (a click on a promotion target leaves it pending), so this
     * component shows the canvas while ChessGameModel::HasPendingPromotion() and hides it otherwise, after a click on the board and on every SceneUpdateEvent (a reset, a set_state or a move of the
     * program or of the control API closes it too). The actions are `ChessPromote` (`value` is an id of PromotionChoices: "queen", "rook", "bishop", "knight"; an id in no table is ignored with one
     * warning) and `ChessCancelPromotion` (drops the choice, the pawn stays selected).
     *
     * It is also the frame of the computer opponent and of the analysis: IGame has no per-frame hook, so on every SceneUpdateEvent it runs
     * ChessGame::GetOpponent().Update() and ChessGame::GetAnalyst().Update() (the one place that does either; the first changes the model, so neither can be done in OnDraw()). While the program plays
     * the side to move, a click on the board does nothing: a half-made move of the person would only cancel the program's own search. After the game has ended a click still starts the next one. The
     * result of a game is logged once, whoever ended it.
     *
     * The hint (PositionAnalyst::GetHint()) is drawn here too: when the analyst holds a hint for the position on the board, the two squares of its move are filled, after the check and before the
     * selection (so a selection and its legal moves stay readable on top), through the same SquareToPixel(). Nothing is played; the hint is bound to the version of the model, so a move, a reset
     * or a position set from outside takes it away at the next frame, and a finished game shows none.
     *
     * The board is drawn from White's side, and from Black's side (turned by 180 degrees) while the program plays White, so the person who plays Black sees their own pieces at the bottom.
     * The pieces, the highlights and the clicks all go through SquareToPixel() / OnMouseDown() and BoardOrientation.h; only the geometry turns, the model and the control API do not.
     *
     * It also publishes the letters and numbers at the edges of the board (`coordFile0..7`, the file drawn in the bottom-row cell of each column, and `coordRank0..7`, the rank drawn in the
     * left-column cell of each row; Game/BoardCoordinates.h) to `CoreManager::GetUIVars()` from the orientation it draws, also when no game is active. The labels are the 16 `UILabel`s of the canvas
     * of the node `BoardCoordinates` in SceneConfig.xml (RankLabel0..7, FileLabel0..7), which show these variables; their boxes stay at the same cells of the screen and their text turns with the
     * board.
     *
     * A piece can also be dragged, next to the two-click move. The press acts at once, exactly as a click (OnMouseDown), and when it left the piece of the side to move selected it also arms a
     * drag; motion past kDragThreshold lifts the piece (GetLiftedPiece() tells the picture where the pointer holds it); the release over a legal target of the selected piece plays the move through
     * ChessGameModel::OnSquareClicked(), the very path of a second click. A release anywhere else (the start square, another square, off the board) calls nothing on the model, so the piece goes back
     * and stays selected with its legal moves and the move can be finished by a click. The drag is dropped by the release, by a motion with the button no longer held (a release that a canvas took
     * never reaches the board), by the next press and as soon as the game moved under it (another model, another version, a game over, the selection gone, a promotion pending, the program to move).
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

        /// The color drawn at the bottom of the board, read live from the opponent on every call (no cached flag): Black exactly when the program plays White (the person is Black and
        /// sits at the bottom), White for two people and when the program plays Black. See BoardOrientation.h
        [[nodiscard]] PieceColor GetBottomColor() const;

        /// World-space pixel position (top-left) of the square (file, rank), for the orientation of GetBottomColor(): with White at the bottom a1 is the bottom left cell,
        /// with Black at the bottom the grid is turned by 180 degrees and a1 is the top right cell.
        void SquareToPixel(int file, int rank, float& outX, float& outY) const;

        /// The same for an orientation the caller has read once (OnDraw places 64 squares and more for one frame).
        void SquareToPixel(int file, int rank, PieceColor bottom, float& outX, float& outY) const;

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

        /// A piece held by the pointer: the square it was lifted from and the top-left of the piece in scene units (the pointer minus half a square)
        struct LiftedPiece {
            Square from;
            float x = 0.0f;
            float y = 0.0f;
        };

        /// The piece the person is dragging, engaged only while a drag is armed, still valid (see IsDragValid()) and lifted past kDragThreshold; the piece component asks it on every update
        [[nodiscard]] std::optional<LiftedPiece> GetLiftedPiece() const;

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
        [[nodiscard]] std::optional<Square> SquareUnder(float screenX, float screenY) const;

        /// Whether the drag still holds: the same game at the same version, not over, the dragged square still selected, no promotion pending and the program not to move
        [[nodiscard]] static bool IsDragValid(const Drag& drag, const ChessGameModel* model);
        void OnAction(const InputEvents::ActionEvent&);

        /// The four actions of the choice groups of the opponent panel (ChessSetMode, ChessSetColor, ChessSetEngine, ChessSetLevel, the id in `value`); any other action is ignored
        void AnswerSetAction(const InputEvents::ActionEvent& event);

        /// The two actions of the promotion panel, ChessPromote (the id of the piece in `value`; an id in no table is ignored with one warning) and ChessCancelPromotion; false for any other action
        bool AnswerPromotionAction(const InputEvents::ActionEvent& event);

        /// Shows the canvas of the node `PromotionPanel` while the model has a promotion pending and hides it otherwise (only when that differs from what was set last)
        void SyncPromotionPanel(const ChessGameModel& model);
        void OnUpdate(const SceneEvents::SceneUpdateEvent&);

        /// Without Stockfish in this build (CHESS_NO_STOCKFISH) hides its segment of the opponent panel, `EngineStockfish`, so Own engine fills the group; once, at the first update or draw, when
        /// every node of the scene is attached. The scene file is the same in every build; a build with Stockfish does nothing
        void HideUnavailableEngines();

        /// One line in the log when a game has ended, the first time it is seen: whether the person, the program or the control API played the last move
        void LogResultOnce(const ChessGameModel& model);

        /// Gives the UI labels the words for the state of the game and of the opponent; only when something they say changed, since a label checks the version of the
        /// variables every frame
        void PublishUiVars();

        /// Gives the 16 labels at the edges of the board their letters and numbers (`coordFile0..7`, `coordRank0..7`) for the orientation read in OnDraw(); only when the bottom color
        /// changed since the last time, since the labels sit at fixed cells of the screen and only their text turns with the board
        void PublishCoordinates(PieceColor bottom);

        std::optional<PieceColor> _publishedBottom;  // the bottom color the board coordinates were published for, none before the first draw

        uint64_t _publishedVersion = 0;                  // the model version PublishUiVars() last published, 0 for none
        const void* _publishedModel = nullptr;           // the model it was published for: another game starts the count again
        uint64_t _publishedRevision = 0;                 // the OpponentController::GetRevision() it was published for
        bool _publishedThinking = false;                 // whether the program was searching then: a search starts and ends without a move of the game
        PieceColor _publishedColor = PieceColor::White;  // the person's color it was published for: it changes with no revision of the controller while two people play

        uint64_t _loggedVersion = 0;         // the model version of the ended game that LogResultOnce() last logged, 0 for none
        const void* _loggedModel = nullptr;  // the model it was logged for

        bool _promotionPanelShown = false;  // what SyncPromotionPanel() last set: the canvas starts hidden
        bool _enginesChecked = false;       // HideUnavailableEngines() has run

        std::optional<Drag> _drag;  // armed by a press that left a piece selected, cleared by every release, a motion without the button, the next press and a failed IsDragValid()

        static ChessBoardComponent* s_active;
    };

}  // namespace BECore::Chess
