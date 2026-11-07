#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Game/ChessTypes.h>

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
     * It is also the frame of the computer opponent: IGame has no per-frame hook, so on every SceneUpdateEvent it runs
     * ChessGame::GetOpponent().Update() (the one place that does; it changes the model, so it cannot be done in OnDraw()). While the program plays the side to move,
     * a click on the board does nothing: a half-made move of the person would only cancel the program's own search. After the game has ended a click still starts
     * the next one. The result of a game is logged once, whoever ended it.
     *
     * The board is drawn from White's side, and from Black's side (turned by 180 degrees) while the program plays White, so the person who plays Black sees their own pieces at the bottom.
     * The pieces, the highlights and the clicks all go through SquareToPixel() / OnMouseDown() and BoardOrientation.h; only the geometry turns, the model and the control API do not.
     *
     * It also publishes the letters and numbers at the edges of the board (`coordFile0..7`, the file drawn in the bottom-row cell of each column, and `coordRank0..7`, the rank drawn in the
     * left-column cell of each row; Game/BoardCoordinates.h) to `CoreManager::GetUIVars()` from the orientation it draws, also when no game is active. The labels are the UI elements of the scene;
     * they stay at the same cells of the screen and their text turns with the board.
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

    private:
        void OnMouseDown(const SDLEvents::MouseButtonDownEvent&);
        void OnAction(const InputEvents::ActionEvent&);

        /// The four actions of the choice groups of the opponent panel (ChessSetMode, ChessSetColor, ChessSetEngine, ChessSetLevel, the id in `value`); any other action is ignored
        void AnswerSetAction(const InputEvents::ActionEvent& event);

        /// The two actions of the promotion panel, ChessPromote (the id of the piece in `value`; an id in no table is ignored with one warning) and ChessCancelPromotion; false for any other action
        bool AnswerPromotionAction(const InputEvents::ActionEvent& event);

        /// Shows the canvas of the node `PromotionPanel` while the model has a promotion pending and hides it otherwise (only when that differs from what was set last)
        void SyncPromotionPanel(const ChessGameModel& model);
        void OnUpdate(const SceneEvents::SceneUpdateEvent&);

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

        static ChessBoardComponent* s_active;
    };

}  // namespace BECore::Chess
