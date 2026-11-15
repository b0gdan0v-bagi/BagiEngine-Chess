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
#include <Chess/Game/MenuSections.h>
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
        // The hint, the last move and the premoves each have a hue of their own, apart from the green of the selection and the red of check; the colors are not yet judged on screen.
        // The hint (the best move the engine found): the square the piece leaves lighter, the square it goes to stronger
        constexpr Color kHintFromSquare{60, 130, 230, 110};
        constexpr Color kHintToSquare{60, 130, 230, 200};
        // The last move (both squares, one color): the alpha is chosen so that it reads on both the light and the dark squares
        constexpr Color kLastMoveSquare{245, 205, 50, 140};
        // The queued premoves (the from and the to square of each, one color)
        constexpr Color kPremoveSquare{150, 90, 200, 150};

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
        SubscribeInScene<InputEvents::ActionEvent, &ChessBoardComponent::OnAction>(this);
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &ChessBoardComponent::OnUpdate>(this);
        SubscribeInScene<SceneEvents::SceneViewChangedEvent, &ChessBoardComponent::OnViewChanged>(this);
    }

    void ChessBoardComponent::OnViewChanged(const SceneEvents::SceneViewChangedEvent& event) {
        _portraitActive = event.view.name == PoolString::Intern(kPortraitView);
        _drag.reset();
    }

    void ChessBoardComponent::HideUnavailableEngines() {
        if (_enginesChecked) {
            return;
        }
        _enginesChecked = true;
        if (!IsOpponentEngineAvailable(OpponentEngine::Stockfish)) {
            // A scene without the panel has nothing to hide: the call does nothing
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
        game->GetAnalyst().Update(game->GetModel(), game->GetOpponent().GetEngine());  // the search runs on the pool; never waits
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
        // A search starts and ends without a move of the game, and a side or a level changes without one; the person's color changes without any of them while two people play
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
        const Optional<EngineError>& error = opponent.GetError();
        const String turn = StatusText::Turn(model, thinking);
        const String status = error ? OpponentText::Error(*error) : StatusText::Status(model);
        const String opponentError = error ? OpponentText::Error(*error) : String();
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("turn"_intern, StringView(turn));
        vars.Set("status"_intern, StringView(status));
        // The ids of the opponent panel, those of the control API (the person's color, not the program's)
        vars.Set("opponentMode"_intern, choice.ModeId());
        vars.Set("opponentColor"_intern, choice.ColorId());
        vars.Set("opponentEngine"_intern, choice.EngineId());
        vars.Set("opponentLevel"_intern, choice.LevelId());
        vars.Set("opponentError"_intern, StringView(opponentError));
    }

    void ChessBoardComponent::AnswerSetAction(const InputEvents::ActionEvent& event) {
        OpponentChoice* choice = ChessGame::GetActiveChoice();
        if (!choice) {
            return;
        }
        const StringView value = event.value.ToStringView();
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
        } else if (const Optional<PieceType> piece = FindPromotion(event.value.ToStringView())) {
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

    void ChessBoardComponent::ShowMenuSection(Optional<MenuSection> shown) {
        // A scene without a section canvas has nothing to show or hide
        for (const MenuSection& section : MenuSections()) {
            UICanvasComponent::SetVisibleByName(GetScene(), PoolString::Intern(section.node), shown && shown->id == section.id);
        }
    }

    bool ChessBoardComponent::AnswerMenuAction(const InputEvents::ActionEvent& event) {
        const PoolString menuNode = "MenuPanel"_intern;
        if (event.action == "ChessOpenMenu"_intern) {
            ShowMenuSection(std::nullopt);
            UICanvasComponent::SetVisibleByName(GetScene(), menuNode, true);
        } else if (event.action == "ChessCloseMenu"_intern) {
            ShowMenuSection(std::nullopt);
            UICanvasComponent::SetVisibleByName(GetScene(), menuNode, false);
        } else if (event.action == "ChessOpenSection"_intern) {
            const Optional<MenuSection> section = FindMenuSection(event.value.ToStringView());
            if (!section) {
                LOG_WARNING("ChessOpenSection ignored: '{}' is not a section of the menu"_format(event.value.ToStringView()));
                return true;
            }
            UICanvasComponent::SetVisibleByName(GetScene(), menuNode, false);
            ShowMenuSection(section);
        } else {
            return false;
        }
        return true;
    }

    void ChessBoardComponent::OnAction(const InputEvents::ActionEvent& event) {
        const PoolString resetAction = "ChessReset"_intern;
        if (event.action == resetAction) {
            if (ChessGameModel* model = ChessGame::GetActiveModel()) {
                model->Reset();
            }
            return;
        }
        if (AnswerMenuAction(event)) {
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
        const BoardCell cell = CellOf(Square{static_cast<int8_t>(file), static_cast<int8_t>(rank)}, bottom);
        const float square = GetSquareSize();
        outX = GetOriginX() + static_cast<float>(cell.column) * square;
        outY = GetOriginY() + static_cast<float>(cell.rowFromTop) * square;
    }

    void ChessBoardComponent::OnDraw() {
        HideUnavailableEngines();  // the panel is drawn after the board, so a frame with no update before it already lacks the segment
        PublishUiVars();
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
                renderer.DrawFilledRect(px, py, squareSize, squareSize, isLight ? kLightSquare : kDarkSquare);
            }
        }

        const ChessGameModel* model = ChessGame::GetActiveModel();
        if (!model) {
            return;
        }

        // The last move, under the check, the hint and the selection; no move yet (a new game, a reset, a position set from a FEN) marks nothing
        if (!model->GetMoves().empty()) {
            const Move last = model->GetMoves().back();
            float mx = 0.0f;
            float my = 0.0f;
            SquareToPixel(last.From().file, last.From().rank, bottom, mx, my);
            renderer.DrawFilledRect(mx, my, squareSize, squareSize, kLastMoveSquare);
            SquareToPixel(last.To().file, last.To().rank, bottom, mx, my);
            renderer.DrawFilledRect(mx, my, squareSize, squareSize, kLastMoveSquare);
        }

        if (model->IsInCheck()) {
            const Square king = model->GetKingSquare(model->GetTurn());
            float kx = 0.0f;
            float ky = 0.0f;
            SquareToPixel(king.file, king.rank, bottom, kx, ky);
            renderer.DrawFilledRect(kx, ky, squareSize, squareSize, model->IsGameOver() ? kMateSquare : kCheckSquare);
        }

        // The hint, under the selection, once the engine has answered for this very position: the analyst gives nothing for another version, so any change of the position takes it off at the next
        // frame
        if (const PositionAnalyst* analyst = ChessGame::GetActiveAnalyst(); analyst && !model->IsGameOver()) {
            if (const Optional<AnalysisLine> hint = analyst->GetHint(*model)) {
                float hx = 0.0f;
                float hy = 0.0f;
                SquareToPixel(hint->move.From().file, hint->move.From().rank, bottom, hx, hy);
                renderer.DrawFilledRect(hx, hy, squareSize, squareSize, kHintFromSquare);
                SquareToPixel(hint->move.To().file, hint->move.To().rank, bottom, hx, hy);
                renderer.DrawFilledRect(hx, hy, squareSize, squareSize, kHintToSquare);
            }
        }

        // The queued premoves stand until the program has answered, so they are drawn under the selection of the next one
        for (const Premove& premove : model->GetPremoves()) {
            float qx = 0.0f;
            float qy = 0.0f;
            SquareToPixel(premove.from.file, premove.from.rank, bottom, qx, qy);
            renderer.DrawFilledRect(qx, qy, squareSize, squareSize, kPremoveSquare);
            SquareToPixel(premove.to.file, premove.to.rank, bottom, qx, qy);
            renderer.DrawFilledRect(qx, qy, squareSize, squareSize, kPremoveSquare);
        }

        if (model->HasSelection()) {
            const Square sel = model->GetSelected();
            float px = 0.0f;
            float py = 0.0f;
            SquareToPixel(sel.file, sel.rank, bottom, px, py);
            renderer.DrawFilledRect(px, py, squareSize, squareSize, kSelectedSquare);

            for (const auto target : model->GetLegalMoves()) {
                SquareToPixel(target.file, target.rank, bottom, px, py);
                const bool isCapture = !model->GetPieceAt(target).IsEmpty();
                renderer.DrawFilledRect(px, py, squareSize, squareSize, isCapture ? kLegalCaptureSquare : kLegalMoveSquare);
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

        // After the game has ended a click on the board starts the next one
        if (model->IsGameOver()) {
            model->Reset();
            return;
        }

        // The program's turn: the person has no move to make. When the program plays this side only, the click is a premove for the other color (no drag is armed, no promotion is asked: it
        // plays as a queen); while two programs play the board takes no click on either turn
        const OpponentController* opponent = ChessGame::GetActiveOpponent();
        if (opponent && opponent->PlaysFor(model->GetTurn())) {
            const PieceColor person = Opposite(model->GetTurn());
            if (!opponent->PlaysFor(person)) {
                model->OnPremoveClicked(person, *clicked);
            }
            return;
        }

        if (model->OnSquareClicked(*clicked)) {
            LogResultOnce(*model);
        }
        // The click may have left a promotion pending or dropped one
        SyncPromotionPanel(*model);

        // The press has acted as a click. When it left the piece on the pressed square selected (a press that played a move leaves no selection) the left button also arms a drag; the
        // release decides whether it plays a move
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
        SyncPromotionPanel(*model);
    }

    Optional<ChessBoardComponent::LiftedPiece> ChessBoardComponent::GetLiftedPiece() const {
        if (!_drag || !_drag->lifted || !IsDragValid(*_drag, ChessGame::GetActiveModel())) {
            return std::nullopt;
        }
        const float half = GetSquareSize() * 0.5f;
        return LiftedPiece{_drag->from, _drag->pointerX - half, _drag->pointerY - half};
    }

}  // namespace BECore::Chess
