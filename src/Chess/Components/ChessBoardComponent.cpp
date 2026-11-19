#include "ChessBoardComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ChessPromotionPanelComponent.h>
#include <Chess/Engine/PositionAnalyst.h>
#include <Chess/Game/BoardCoordinates.h>
#include <Chess/Game/BoardOrientation.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/ChessBoardComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        // How far, in scene units, the pointer must go from the press before the piece is lifted: a click with a shaky hand never lifts one. It decides only the picture, not the move
        constexpr float kDragThreshold = 6.0f;

        // One line in the log when a game ends; the board has no text to show it in.
        void LogResult(const ChessGameModel& model) {
            const auto status = EnumUtils<GameStatus>::ToString(model.GetStatus());
            if (const auto winner = model.GetWinner()) {
                LOG_INFO("{}, {} wins"_format(status, EnumUtils<PieceColor>::ToString(*winner)));
            } else {
                LOG_INFO("Game over: {}"_format(status));
            }
        }

        // Interned on the first use, not per frame
        struct CoordinateVars {
            Array<PoolString, kLastBoardIndex + 1> files;  // by column
            Array<PoolString, kLastBoardIndex + 1> ranks;  // by row from the top
        };

        const CoordinateVars& GetCoordinateVars() {
            static const CoordinateVars vars = [] {
                CoordinateVars made;
                for (int index = 0; index <= kLastBoardIndex; ++index) {
                    made.files[index] = PoolString::Intern("coordFile{}"_format(index).c_str());
                    made.ranks[index] = PoolString::Intern("coordRank{}"_format(index).c_str());
                }
                return made;
            }();
            return vars;
        }

    }  // namespace

    ChessBoardComponent* ChessBoardComponent::s_active = nullptr;

    ChessBoardComponent::~ChessBoardComponent() {
        if (s_active == this) {
            s_active = nullptr;
        }
    }

    void ChessBoardComponent::OnAttached() {
        s_active = this;
        SubscribeInScene<SDLEvents::MouseButtonDownEvent, &ChessBoardComponent::OnMouseDown>(this);
        SubscribeInScene<SDLEvents::MouseButtonUpEvent, &ChessBoardComponent::OnMouseUp>(this);
        SubscribeInScene<SDLEvents::MouseMotionEvent, &ChessBoardComponent::OnMouseMotion>(this);
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &ChessBoardComponent::OnUpdate>(this);
        SubscribeInScene<SceneEvents::SceneViewChangedEvent, &ChessBoardComponent::OnViewChanged>(this);
    }

    void ChessBoardComponent::OnViewChanged(const SceneEvents::SceneViewChangedEvent& event) {
        _portraitActive = HasPortraitPlaces(event.view.name);
        _drag.reset();
    }

    void ChessBoardComponent::OnUpdate(const SceneEvents::SceneUpdateEvent& event) {
        ChessGame* game = ChessGame::GetActive();
        if (!game) {
            return;
        }
        // The opponent plays here, on the main thread, and not in OnDraw(): it changes the model. While a position is edited it and the puzzle's judge wait: a move of either (the program's reply, the
        // takeback of a wrong move) would end the draft. While a puzzle is on the opponent gets no frame: the judge plays the other side, and a search that was in flight is stale by the time the
        // puzzle ends
        const bool editing = game->IsEditing();
        if (!editing) {
            game->UpdatePuzzle(event.deltaTime);
        }
        if (!editing && !game->IsPuzzleOn()) {
            game->GetOpponent().Update();
        }
        game->GetAnalyst().Update(game->GetModel(), game->GetOpponent().GetEngine());  // also steps the rater of the moves; the searches run on the pool and never wait
        if (!editing && game->GetDrawJudge().Update(game->GetModel()) == DrawVerdict::Accepted) {
            static_cast<void>(game->GetModel().AgreeDraw());  // the judge gives a verdict only for the version it was asked at, so the game is still on
        }
        static_cast<void>(game->GetChoice().Color());  // reading it remembers the person's color of a side set from outside (the control API) before that side is cleared
        game->UpdateKeeper();                          // after the opponent and the choice: a move the program just played is saved in this frame's key
        LogResultOnce(game->GetModel());
    }

    void ChessBoardComponent::LogResultOnce(const ChessGameModel& model) {
        if (!model.IsGameOver() || (&model == _loggedModel && model.GetVersion() == _loggedVersion)) {
            return;
        }
        _loggedModel = &model;
        _loggedVersion = model.GetVersion();
        LogResult(model);
    }

    void ChessBoardComponent::PublishCoordinates(PieceColor bottom) {
        if (_publishedBottom == bottom) {
            return;
        }
        _publishedBottom = bottom;
        const CoordinateVars& names = GetCoordinateVars();
        UIVars& vars = CoreManager::GetUIVars();
        for (int index = 0; index <= kLastBoardIndex; ++index) {
            vars.Set(names.files[index], FileLabel(index, bottom));
            vars.Set(names.ranks[index], RankLabel(index, bottom));
        }
    }

    PieceColor ChessBoardComponent::GetBottomColor() const {
        const ChessGame* game = ChessGame::GetActive();
        return game ? game->BoardBottomColor() : PieceColor::White;
    }

    void ChessBoardComponent::SquareToPixel(int file, int rank, float& outX, float& outY) const {
        SquareToPixel(file, rank, GetBottomColor(), outX, outY);
    }

    void ChessBoardComponent::SquareToPixel(int file, int rank, PieceColor bottom, float& outX, float& outY) const {
        const BoardCell cell = CellOf(Square{static_cast<int8_t>(file), static_cast<int8_t>(rank)}, bottom);
        const float square = GetSquareSize();
        outX = GetOriginX() + static_cast<float>(cell.column) * square;
        outY = GetOriginY() + static_cast<float>(cell.rowFromTop) * square;
    }

    void ChessBoardComponent::OnDraw() {
        _palette.Refresh(CoreManager::GetUIThemes());  // a theme switch shows at this draw; an unchanged table costs a compare
        auto& renderer = *CoreManager::GetRenderer();
        const float squareSize = GetSquareSize();    // once per draw, as the orientation
        const PieceColor bottom = GetBottomColor();  // once per draw: every square of this frame is placed for the same orientation
        PublishCoordinates(bottom);                  // before the model check: the labels show also when no game is active

        for (int rank = 0; rank < 8; ++rank) {
            for (int file = 0; file < 8; ++file) {
                float px = 0.0f;
                float py = 0.0f;
                SquareToPixel(file, rank, bottom, px, py);
                const bool isLight = IsLightSquare(file, rank);
                renderer.DrawFilledRect(px, py, squareSize, squareSize, isLight ? _palette.LightSquare() : _palette.DarkSquare());
            }
        }

        const ChessGameModel* model = ChessGame::GetActiveModel();
        if (!model) {
            return;
        }

        // A draft is no position of the game: nothing of the game is marked on it
        if (model->IsDrafting()) {
            return;
        }

        // Everything below is of the shown position: an earlier one while it is viewed, which has no hint, premove or selection of its own
        const Position& shown = model->GetShownPosition();
        const bool viewing = model->IsViewing();

        // The last move, under the check, the hint and the selection; no move yet (a new game, a reset, a position set from a FEN) marks nothing
        if (const size_t shownMoves = model->GetShownMoveCount(); shownMoves > 0) {
            const Move last = model->GetMoves()[shownMoves - 1];
            float mx = 0.0f;
            float my = 0.0f;
            SquareToPixel(last.From().file, last.From().rank, bottom, mx, my);
            renderer.DrawFilledRect(mx, my, squareSize, squareSize, _palette.LastMove());
            SquareToPixel(last.To().file, last.To().rank, bottom, mx, my);
            renderer.DrawFilledRect(mx, my, squareSize, squareSize, _palette.LastMove());
        }

        if (shown.IsInCheck()) {
            const Square king = Square::FromIndex(shown.KingSquare(shown.SideToMove()));
            float kx = 0.0f;
            float ky = 0.0f;
            SquareToPixel(king.file, king.rank, bottom, kx, ky);
            // Only a mate looks like one: a game resigned or agreed drawn while in check ends with the king merely in check
            renderer.DrawFilledRect(kx, ky, squareSize, squareSize, !viewing && model->GetStatus() == GameStatus::Checkmate ? _palette.Mate() : _palette.Check());
        }

        // The hint, under the selection, once the engine has answered for this very position: the analyst gives nothing for another version, so any change of the position takes it off at the next
        // frame. A viewed position has none of its own; a held review marks its best move there in the hint's colors (only a view asks the reviewer, a normal frame never does)
        Optional<Move> hinted;
        if (const PositionAnalyst* analyst = ChessGame::GetActiveAnalyst()) {
            if (!viewing && !model->IsGameOver()) {
                if (const Optional<AnalysisLine> hint = analyst->GetHint(*model)) {
                    hinted = hint->move;
                }
            } else if (viewing) {
                if (const Optional<ReviewedPosition> reviewed = analyst->GetReviewedPosition(*model, model->GetShownMoveCount()); reviewed && !reviewed->best.IsNull()) {
                    hinted = reviewed->best;
                }
            }
        }
        if (hinted) {
            float hx = 0.0f;
            float hy = 0.0f;
            SquareToPixel(hinted->From().file, hinted->From().rank, bottom, hx, hy);
            renderer.DrawFilledRect(hx, hy, squareSize, squareSize, _palette.HintFrom());
            SquareToPixel(hinted->To().file, hinted->To().rank, bottom, hx, hy);
            renderer.DrawFilledRect(hx, hy, squareSize, squareSize, _palette.HintTo());
        }

        // The queued premoves stand until the program has answered, so they are drawn under the selection of the next one
        for (const Premove& premove : model->GetPremoves()) {
            if (viewing) {
                break;  // the queue is made for the game's position
            }
            float qx = 0.0f;
            float qy = 0.0f;
            SquareToPixel(premove.from.file, premove.from.rank, bottom, qx, qy);
            renderer.DrawFilledRect(qx, qy, squareSize, squareSize, _palette.Premove());
            SquareToPixel(premove.to.file, premove.to.rank, bottom, qx, qy);
            renderer.DrawFilledRect(qx, qy, squareSize, squareSize, _palette.Premove());
        }

        if (model->HasSelection()) {
            const Square sel = model->GetSelected();
            float px = 0.0f;
            float py = 0.0f;
            SquareToPixel(sel.file, sel.rank, bottom, px, py);
            renderer.DrawFilledRect(px, py, squareSize, squareSize, _palette.Selected());

            for (const auto target : model->GetLegalMoves()) {
                SquareToPixel(target.file, target.rank, bottom, px, py);
                const bool isCapture = !model->GetPieceAt(target).IsEmpty();
                renderer.DrawFilledRect(px, py, squareSize, squareSize, isCapture ? _palette.LegalCapture() : _palette.LegalMove());
            }
        }
    }

    Optional<Square> ChessBoardComponent::SquareUnder(float screenX, float screenY) const {
        const float squareSize = GetSquareSize();
        const float boardSize = squareSize * 8.0f;
        const float localX = GetScene().ScreenToSceneX(screenX) - GetOriginX();
        const float localY = GetScene().ScreenToSceneY(screenY) - GetOriginY();
        if (localX < 0.0f || localY < 0.0f || localX >= boardSize || localY >= boardSize) {
            return std::nullopt;
        }
        return SquareAt(static_cast<int>(localX / squareSize), static_cast<int>(localY / squareSize), GetBottomColor());
    }

    void ChessBoardComponent::OnMouseDown(const SDLEvents::MouseButtonDownEvent& e) {
        _drag.reset();  // any press ends a drag that is still armed (a lost release); the press below may arm a new one
        ChessGameModel* model = ChessGame::GetActiveModel();
        if (!model) {
            return;
        }

        const Optional<Square> clicked = SquareUnder(e.button.x, e.button.y);
        if (!clicked) {
            return;
        }

        // While a position is edited the press puts or takes off a piece and nothing of the game's click flow runs
        if (ChessGame* game = ChessGame::GetActive(); game && game->IsEditing()) {
            EditSquare(*game, *clicked, e.button.button);
            return;
        }

        // A press on an earlier position only brings the game back: nothing is played, selected or started on a board the game is not at
        if (model->EndViewing()) {
            return;
        }

        // After the game has ended a click on the board starts the next one
        if (model->IsGameOver()) {
            model->Reset();
            return;
        }

        // The program's turn: the person has no move to make. When the program plays this side only, the click is a premove for the other color (no drag is armed, no promotion is asked: it
        // plays as a queen); while two programs play the board takes no click on either turn. In a puzzle the program's turn is the reply of the line, which takes no click and no premove
        ChessGame* game = ChessGame::GetActive();
        if (game && game->ProgramToMoveNow()) {
            const OpponentController& opponent = game->GetOpponent();
            const PieceColor person = Opposite(model->GetTurn());
            if (!game->IsPuzzleOn() && !opponent.PlaysFor(person)) {
                model->OnPremoveClicked(person, *clicked);
            }
            return;
        }

        if (model->OnSquareClicked(*clicked)) {
            LogResultOnce(*model);
        }
        // The click may have left a promotion pending or dropped one: the picker is shown before anything else reads the scene
        ChessPromotionPanelComponent::SyncActive();

        // The press has acted as a click. When it left the piece on the pressed square selected (a press that played a move leaves no selection) the left button also arms a drag; the
        // release decides whether it plays a move
        if (e.button.button == SDL_BUTTON_LEFT && !model->IsGameOver() && !model->HasPendingPromotion() && model->HasSelection() && model->GetSelected() == *clicked) {
            const float sceneX = GetScene().ScreenToSceneX(e.button.x);
            const float sceneY = GetScene().ScreenToSceneY(e.button.y);
            _drag = Drag{*clicked, model, model->GetVersion(), sceneX, sceneY, sceneX, sceneY, false};
        }
    }

    void ChessBoardComponent::EditSquare(ChessGame& game, Square square, uint8_t button) {
        PositionEditor& editor = game.GetEditor();
        if (button == SDL_BUTTON_LEFT) {
            editor.Apply(square);
        } else if (button == SDL_BUTTON_RIGHT) {
            editor.Remove(square);
        } else {
            return;
        }
        game.EditChanged();
    }

    bool ChessBoardComponent::IsDragValid(const Drag& drag, const ChessGameModel* model) {
        if (model != drag.model || model->GetVersion() != drag.version || model->IsGameOver() || model->IsViewing() || model->IsDrafting() || !model->HasSelection() ||
            model->GetSelected() != drag.from || model->HasPendingPromotion()) {
            return false;
        }
        const ChessGame* game = ChessGame::GetActive();
        return !(game && game->ProgramToMoveNow());
    }

    void ChessBoardComponent::OnMouseMotion(const SDLEvents::MouseMotionEvent& e) {
        if (!_drag) {
            return;
        }
        // The button is no longer held: its release went to a canvas (over a HUD button, say) and never reached the board
        if ((e.motion.state & SDL_BUTTON_LMASK) == 0 || !IsDragValid(*_drag, ChessGame::GetActiveModel())) {
            _drag.reset();
            return;
        }
        _drag->pointerX = GetScene().ScreenToSceneX(e.motion.x);
        _drag->pointerY = GetScene().ScreenToSceneY(e.motion.y);
        const float dx = _drag->pointerX - _drag->pressX;
        const float dy = _drag->pointerY - _drag->pressY;
        if (dx * dx + dy * dy > kDragThreshold * kDragThreshold) {
            _drag->lifted = true;
        }
    }

    void ChessBoardComponent::OnMouseUp(const SDLEvents::MouseButtonUpEvent& e) {
        if (e.button.button != SDL_BUTTON_LEFT) {
            return;
        }
        const Optional<Drag> drag = std::exchange(_drag, std::nullopt);  // a release ends the drag in every case below
        ChessGameModel* model = ChessGame::GetActiveModel();
        if (!drag || !model || !IsDragValid(*drag, model)) {
            return;
        }
        // Only a legal target of the selected piece plays. Anywhere else nothing is called on the model: the piece goes back and the selection stays with its legal moves (and a release
        // on another own piece must not select it)
        const Optional<Square> target = SquareUnder(e.button.x, e.button.y);
        const auto& legalMoves = model->GetLegalMoves();
        if (!target || eastl::find(legalMoves.begin(), legalMoves.end(), *target) == legalMoves.end()) {
            return;
        }
        if (model->OnSquareClicked(*target)) {
            LogResultOnce(*model);
        }
        ChessPromotionPanelComponent::SyncActive();
    }

    Optional<ChessBoardComponent::LiftedPiece> ChessBoardComponent::GetLiftedPiece() const {
        if (!_drag || !_drag->lifted || !IsDragValid(*_drag, ChessGame::GetActiveModel())) {
            return std::nullopt;
        }
        const float half = GetSquareSize() * 0.5f;
        return LiftedPiece{_drag->from, _drag->pointerX - half, _drag->pointerY - half};
    }

}  // namespace BECore::Chess
