#include "ChessBoardComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Chess/ChessGame.h>
#include <Chess/Engine/OpponentEngines.h>
#include <Chess/Engine/OpponentText.h>
#include <Chess/Engine/PositionAnalyst.h>
#include <Chess/Game/BoardCoordinates.h>
#include <Chess/Game/BoardOrientation.h>
#include <Chess/Game/PromotionChoices.h>
#include <Chess/Game/StatusText.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/ChessBoardComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        constexpr Color kLightSquare{240, 217, 181, 255};
        constexpr Color kDarkSquare{181, 136, 99, 255};
        constexpr Color kSelectedSquare{120, 200, 120, 200};
        constexpr Color kLegalMoveSquare{120, 200, 120, 120};
        constexpr Color kLegalCaptureSquare{220, 90, 90, 160};
        constexpr Color kCheckSquare{230, 60, 60, 210};
        constexpr Color kMateSquare{150, 20, 20, 240};
        // The hint (the best move the engine found): the square the piece leaves in a lighter blue, the square it goes to in a stronger one. A first guess nobody has seen on screen: neither the
        // red of check nor the green of the selection, drawn under the selection so that one stays readable on top of it
        constexpr Color kHintFromSquare{60, 130, 230, 110};
        constexpr Color kHintToSquare{60, 130, 230, 200};
        // The last move (both squares, one color): an amber that is neither the green of the selection and the legal moves, nor the red of check, nor the blue of the hint. At alpha 140 it
        // turns the light square (240,217,181) to about (243,211,111) and the dark one (181,136,99) to about (214,171,76), so it reads on both; drawn under all of them. A first guess nobody has seen
        // on screen
        constexpr Color kLastMoveSquare{245, 205, 50, 140};

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

        // The interned names of the variables of the labels at the edges of the board, built on the first use (an interned name is not made per frame)
        struct CoordinateVars {
            eastl::array<PoolString, kLastBoardIndex + 1> files;  // coordFile0..7, by column
            eastl::array<PoolString, kLastBoardIndex + 1> ranks;  // coordRank0..7, by row from the top
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
        SubscribeInScene<InputEvents::ActionEvent, &ChessBoardComponent::OnAction>(this);
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &ChessBoardComponent::OnUpdate>(this);
    }

    void ChessBoardComponent::HideUnavailableEngines() {
        if (_enginesChecked) {
            return;
        }
        _enginesChecked = true;
        if (!IsOpponentEngineAvailable(OpponentEngine::Stockfish)) {
            // A scene without the panel has nothing to hide: the call finds no element and does nothing
            UICanvasComponent::SetElementVisibleByName(GetScene(), "EngineStockfish"_intern, false);
        }
    }

    void ChessBoardComponent::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        HideUnavailableEngines();
        ChessGame* game = ChessGame::GetActive();
        if (!game) {
            return;
        }
        // The opponent plays here, on the main thread, and not in OnDraw(): it changes the model
        game->GetOpponent().Update();
        game->GetAnalyst().Update(game->GetModel(), game->GetOpponent().GetEngine());  // the engine search for the evaluation and the hint, on the pool; never waits
        static_cast<void>(game->GetChoice().Color());  // reading it remembers the person's color of a side set from outside (the control API) before that side is cleared
        game->UpdateKeeper();                          // after the opponent and the choice: a move the program just played is saved in this frame's key
        LogResultOnce(game->GetModel());
        // A reset, a set_state, a move of the program or of the control API drops a pending promotion without a click on the board
        SyncPromotionPanel(game->GetModel());
    }

    void ChessBoardComponent::SyncPromotionPanel(const ChessGameModel& model) {
        const bool wanted = model.HasPendingPromotion();
        if (wanted == _promotionPanelShown) {
            return;
        }
        _promotionPanelShown = wanted;
        // A scene without the node has nothing to show
        UICanvasComponent::SetVisibleByName(GetScene(), "PromotionPanel"_intern, wanted);
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

    void ChessBoardComponent::PublishUiVars() {
        const ChessGame* game = ChessGame::GetActive();
        if (!game) {
            return;
        }
        const ChessGameModel& model = game->GetModel();
        const OpponentController& opponent = game->GetOpponent();
        const OpponentChoice& choice = game->GetChoice();
        // Read on every refresh, also when nothing else changed: it remembers the person's color, which a set_state of the control API could otherwise take with it
        const PieceColor personColor = choice.Color();
        // A search starts and ends without a move of the game, and a side or a level changes without one: all of it is a reason to say the words again. The person's
        // color changes without any of them while two people play
        const bool thinking = opponent.IsThinking();
        const bool opponentChanged = &model != _publishedModel || opponent.GetRevision() != _publishedRevision;
        if (!opponentChanged && model.GetVersion() == _publishedVersion && thinking == _publishedThinking && personColor == _publishedColor) {
            return;
        }
        _publishedModel = &model;
        _publishedVersion = model.GetVersion();
        _publishedRevision = opponent.GetRevision();
        _publishedThinking = thinking;
        _publishedColor = personColor;

        // While an error of the opponent stands the game is stalled by it, so its sentence wins over "Check"
        const std::optional<EngineError>& error = opponent.GetError();
        const eastl::string turn = StatusText::Turn(model, thinking);
        const eastl::string status = error ? OpponentText::Error(*error) : StatusText::Status(model);
        const eastl::string opponentError = error ? OpponentText::Error(*error) : eastl::string();
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("turn"_intern, eastl::string_view(turn.data(), turn.size()));
        vars.Set("status"_intern, eastl::string_view(status.data(), status.size()));
        // The ids of the opponent panel, those of the control API (the person's color, not the program's); the words are the text of the segments in the scene
        vars.Set("opponentMode"_intern, choice.ModeId());
        vars.Set("opponentColor"_intern, choice.ColorId());
        vars.Set("opponentEngine"_intern, choice.EngineId());
        vars.Set("opponentLevel"_intern, choice.LevelId());
        vars.Set("opponentError"_intern, eastl::string_view(opponentError.data(), opponentError.size()));
    }

    void ChessBoardComponent::AnswerSetAction(const InputEvents::ActionEvent& event) {
        // The actions of the segments of the opponent panel; `value` is the id the person chose
        OpponentChoice* choice = ChessGame::GetActiveChoice();
        if (!choice) {
            return;
        }
        const eastl::string_view value = event.value.ToStringView();
        std::expected<void, ChoiceError> picked;
        if (event.action == "ChessSetMode"_intern) {
            picked = choice->SetMode(value);
        } else if (event.action == "ChessSetColor"_intern) {
            picked = choice->SetColor(value);
        } else if (event.action == "ChessSetEngine"_intern) {
            picked = choice->SetEngine(value);
        } else if (event.action == "ChessSetLevel"_intern) {
            picked = choice->SetLevel(value);
        } else {
            return;
        }
        // A refused pick (no Stockfish found) is kept by the controller and shown through the variables, so only an id that is in no table (a hand-made press) is said here
        if (!picked && picked.error().kind == ChoiceErrorKind::BadId) {
            LOG_WARNING("{} ignored: {}"_format(event.action.ToStringView(), picked.error().detail));
        }
    }

    bool ChessBoardComponent::AnswerPromotionAction(const InputEvents::ActionEvent& event) {
        const bool promote = event.action == "ChessPromote"_intern;
        if (!promote && event.action != "ChessCancelPromotion"_intern) {
            return false;
        }
        ChessGameModel* model = ChessGame::GetActiveModel();
        if (!model) {
            return true;
        }
        if (!promote) {
            model->CancelPromotion();
        } else if (const std::optional<PieceType> piece = FindPromotion(event.value.ToStringView())) {
            // Nothing pending (a press that came late) is an IllegalMove of the model and changes nothing
            if (model->ChoosePromotion(*piece)) {
                LogResultOnce(*model);
            }
        } else {
            LOG_WARNING("ChessPromote ignored: '{}' is not a piece a pawn can become"_format(event.value.ToStringView()));
        }
        SyncPromotionPanel(*model);
        return true;
    }

    void ChessBoardComponent::OnAction(const InputEvents::ActionEvent& event) {
        // The actions of the buttons of the HUD in config/SceneConfig.xml
        const PoolString resetAction = "ChessReset"_intern;
        const PoolString openPanelAction = "ChessOpenOpponent"_intern;
        const PoolString closePanelAction = "ChessCloseOpponent"_intern;
        if (event.action == resetAction) {
            if (ChessGameModel* model = ChessGame::GetActiveModel()) {
                model->Reset();
            }
            return;
        }
        // The opponent panel is a modal canvas of the scene, hidden at the start; a scene without it has nothing to show or hide
        if (event.action == openPanelAction || event.action == closePanelAction) {
            UICanvasComponent::SetVisibleByName(GetScene(), "OpponentPanel"_intern, event.action == openPanelAction);
            return;
        }
        if (AnswerPromotionAction(event)) {
            return;
        }
        AnswerSetAction(event);
    }

    PieceColor ChessBoardComponent::GetBottomColor() const {
        const OpponentController* opponent = ChessGame::GetActiveOpponent();
        return BottomColor(opponent ? opponent->GetPlays() : std::nullopt);
    }

    void ChessBoardComponent::SquareToPixel(int file, int rank, float& outX, float& outY) const {
        SquareToPixel(file, rank, GetBottomColor(), outX, outY);
    }

    void ChessBoardComponent::SquareToPixel(int file, int rank, PieceColor bottom, float& outX, float& outY) const {
        // The cell comes from BoardOrientation.h: White's back rank is at the bottom unless the program plays White.
        const BoardCell cell = CellOf(Square{static_cast<int8_t>(file), static_cast<int8_t>(rank)}, bottom);
        outX = _originX + static_cast<float>(cell.column) * _squareSize;
        outY = _originY + static_cast<float>(cell.rowFromTop) * _squareSize;
    }

    void ChessBoardComponent::OnDraw() {
        HideUnavailableEngines();  // the panel is drawn after the board, so a frame with no update before it already lacks the segment
        PublishUiVars();
        auto& renderer = *CoreManager::GetRenderer();
        const PieceColor bottom = GetBottomColor();  // once per draw: every square of this frame is placed for the same orientation
        PublishCoordinates(bottom);                  // before the model check: the labels show also when no game is active

        // Checkered base.
        for (int rank = 0; rank < 8; ++rank) {
            for (int file = 0; file < 8; ++file) {
                float px = 0.0f;
                float py = 0.0f;
                SquareToPixel(file, rank, bottom, px, py);
                const bool isLight = IsLightSquare(file, rank);
                renderer.DrawFilledRect(px, py, _squareSize, _squareSize, isLight ? kLightSquare : kDarkSquare);
            }
        }

        const ChessGameModel* model = ChessGame::GetActiveModel();
        if (!model) {
            return;
        }

        // The last move, read from the model's history (the component keeps nothing): the two squares of the move, under the check, the hint and the selection. Castling is the king's
        // move, en passant and a promotion the pawn's; no move yet (a new game, a reset, a position set from a FEN) marks nothing
        if (!model->GetMoves().empty()) {
            const Move last = model->GetMoves().back();
            float mx = 0.0f;
            float my = 0.0f;
            SquareToPixel(last.From().file, last.From().rank, bottom, mx, my);
            renderer.DrawFilledRect(mx, my, _squareSize, _squareSize, kLastMoveSquare);
            SquareToPixel(last.To().file, last.To().rank, bottom, mx, my);
            renderer.DrawFilledRect(mx, my, _squareSize, _squareSize, kLastMoveSquare);
        }

        // The king that is attacked: bright red while the game goes on, dark red once it is mate.
        if (model->IsInCheck()) {
            const Square king = model->GetKingSquare(model->GetTurn());
            float kx = 0.0f;
            float ky = 0.0f;
            SquareToPixel(king.file, king.rank, bottom, kx, ky);
            renderer.DrawFilledRect(kx, ky, _squareSize, _squareSize, model->IsGameOver() ? kMateSquare : kCheckSquare);
        }

        // The hint, once the engine has answered for this very position: the two squares of the move, under the selection. It is never played, and the analyst gives nothing for another
        // version, so any change of the position takes it off at the next frame
        if (const PositionAnalyst* analyst = ChessGame::GetActiveAnalyst(); analyst && !model->IsGameOver()) {
            if (const std::optional<AnalysisLine> hint = analyst->GetHint(*model)) {
                float hx = 0.0f;
                float hy = 0.0f;
                SquareToPixel(hint->move.From().file, hint->move.From().rank, bottom, hx, hy);
                renderer.DrawFilledRect(hx, hy, _squareSize, _squareSize, kHintFromSquare);
                SquareToPixel(hint->move.To().file, hint->move.To().rank, bottom, hx, hy);
                renderer.DrawFilledRect(hx, hy, _squareSize, _squareSize, kHintToSquare);
            }
        }

        if (model->HasSelection()) {
            const Square sel = model->GetSelected();
            float px = 0.0f;
            float py = 0.0f;
            SquareToPixel(sel.file, sel.rank, bottom, px, py);
            renderer.DrawFilledRect(px, py, _squareSize, _squareSize, kSelectedSquare);

            for (const auto target : model->GetLegalMoves()) {
                SquareToPixel(target.file, target.rank, bottom, px, py);
                const bool isCapture = !model->GetPieceAt(target).IsEmpty();
                renderer.DrawFilledRect(px, py, _squareSize, _squareSize, isCapture ? kLegalCaptureSquare : kLegalMoveSquare);
            }
        }
    }

    std::optional<Square> ChessBoardComponent::SquareUnder(float screenX, float screenY) const {
        const float boardSize = _squareSize * 8.0f;
        const float localX = GetScene().ScreenToSceneX(screenX) - _originX;
        const float localY = GetScene().ScreenToSceneY(screenY) - _originY;
        if (localX < 0.0f || localY < 0.0f || localX >= boardSize || localY >= boardSize) {
            return std::nullopt;
        }
        return SquareAt(static_cast<int>(localX / _squareSize), static_cast<int>(localY / _squareSize), GetBottomColor());
    }

    void ChessBoardComponent::OnMouseDown(const SDLEvents::MouseButtonDownEvent& e) {
        _drag.reset();  // any press ends a drag that is still armed (a lost release); the press below may arm a new one
        ChessGameModel* model = ChessGame::GetActiveModel();
        if (!model) {
            return;
        }

        const std::optional<Square> clicked = SquareUnder(e.button.x, e.button.y);
        if (!clicked) {
            return;
        }

        // After the game has ended a click on the board starts the next one.
        if (model->IsGameOver()) {
            model->Reset();
            return;
        }

        // The program's turn: the person has nothing to move, and a selection would only fight the search that is under way. PlaysFor is true for either seat, so while two
        // programs play (a watch) the board takes no click on either turn
        const OpponentController* opponent = ChessGame::GetActiveOpponent();
        if (opponent && opponent->PlaysFor(model->GetTurn())) {
            return;
        }

        if (model->OnSquareClicked(*clicked)) {
            LogResultOnce(*model);
        }
        // The click may have left a promotion pending (the panel asks for the piece) or dropped one
        SyncPromotionPanel(*model);

        // The press has acted as a click. When it left the piece on the pressed square selected (it selected or re-selected a piece of the side to move; a press that played a move
        // leaves no selection) the left button also arms a drag; the release decides whether it plays a move
        if (e.button.button == SDL_BUTTON_LEFT && !model->IsGameOver() && !model->HasPendingPromotion() && model->HasSelection() && model->GetSelected() == *clicked) {
            const float sceneX = GetScene().ScreenToSceneX(e.button.x);
            const float sceneY = GetScene().ScreenToSceneY(e.button.y);
            _drag = Drag{*clicked, model, model->GetVersion(), sceneX, sceneY, sceneX, sceneY, false};
        }
    }

    bool ChessBoardComponent::IsDragValid(const Drag& drag, const ChessGameModel* model) {
        if (model != drag.model || model->GetVersion() != drag.version || model->IsGameOver() || !model->HasSelection() || model->GetSelected() != drag.from || model->HasPendingPromotion()) {
            return false;
        }
        const OpponentController* opponent = ChessGame::GetActiveOpponent();
        return !(opponent && opponent->PlaysFor(model->GetTurn()));
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
        const std::optional<Drag> drag = std::exchange(_drag, std::nullopt);  // a release ends the drag in every case below
        ChessGameModel* model = ChessGame::GetActiveModel();
        if (!drag || !model || !IsDragValid(*drag, model)) {
            return;
        }
        // Only a legal target of the selected piece plays. Anywhere else nothing is called on the model: the piece goes back and the selection stays with its legal moves (and a release
        // on another own piece must not select it)
        const std::optional<Square> target = SquareUnder(e.button.x, e.button.y);
        const auto& legalMoves = model->GetLegalMoves();
        if (!target || eastl::find(legalMoves.begin(), legalMoves.end(), *target) == legalMoves.end()) {
            return;
        }
        if (model->OnSquareClicked(*target)) {
            LogResultOnce(*model);
        }
        SyncPromotionPanel(*model);
    }

    std::optional<ChessBoardComponent::LiftedPiece> ChessBoardComponent::GetLiftedPiece() const {
        if (!_drag || !_drag->lifted || !IsDragValid(*_drag, ChessGame::GetActiveModel())) {
            return std::nullopt;
        }
        const float half = _squareSize * 0.5f;
        return LiftedPiece{_drag->from, _drag->pointerX - half, _drag->pointerY - half};
    }

}  // namespace BECore::Chess
