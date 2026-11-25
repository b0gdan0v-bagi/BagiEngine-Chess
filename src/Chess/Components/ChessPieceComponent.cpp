#include "ChessPieceComponent.h"

#include <BECore/Animation/Easing.h>
#include <BECore/Animation/Lerp.h>
#include <BECore/Animation/TweenManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ChessBoardComponent.h>
#include <Chess/Components/PieceSprite.h>
#include <Events/SceneEvents.h>
#include <Generated/ChessPieceComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        constexpr float kHiddenX = -10000.0f;
        constexpr float kHiddenY = -10000.0f;

        // A view and a draft both show a position that is not the game's, so a move seen across either is no slide
        bool ShowsOtherPosition(const ChessGameModel& model) {
            return model.IsViewing() || model.IsDrafting();
        }

    }  // namespace

    ChessPieceComponent::~ChessPieceComponent() {
        UnregisterFromModel();
    }

    void ChessPieceComponent::OnAttached() {
        RegisterWithModel();
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &ChessPieceComponent::OnUpdate>(this);
    }

    void ChessPieceComponent::OnUpdate(const SceneEvents::SceneUpdateEvent& event) {
        if (!_registered) {
            RegisterWithModel();
        }

        auto* model = ChessGame::GetActiveModel();
        auto* board = ChessBoardComponent::GetActive();
        auto transform = GetNode().GetComponent<TransformComponent>();
        if (!transform) {
            return;
        }

        if (!model || !board) {
            ForgetSeen();
            EndLift();
            transform->_x = kHiddenX;
            transform->_y = kHiddenY;
            return;
        }

        const Square cur = model->GetShownSquareOf(this);
        Observe(*model, cur, event.deltaTime);

        // The piece the person holds is under the pointer, over the others, and nothing slides it
        const Optional<ChessBoardComponent::LiftedPiece> lifted = board->GetLiftedPiece();
        if (lifted && cur.IsInside() && lifted->from == cur) {
            if (!_lifted) {
                _lifted = true;
                _layerBeforeLift = GetNode().GetLayer();
                GetNode().SetLayer(kDragLayer);
            }
            transform->_x = lifted->x;
            transform->_y = lifted->y;
            transform->_width = board->GetSquareSize();
            transform->_height = board->GetSquareSize();
            return;
        }
        EndLift();

        // The square drawn: the one the slide leaves while a slide runs (a captured piece waits there), else where the model has the piece.
        const Square shown = IsSliding() ? _slideFrom : cur;
        if (!shown.IsInside()) {
            transform->_x = kHiddenX;
            transform->_y = kHiddenY;
            return;
        }

        float px = 0.0f;
        float py = 0.0f;
        board->SquareToPixel(shown.file, shown.rank, px, py);
        if (IsSliding() && _slideTo.IsInside()) {
            // The pixels of both squares are taken now, so a turn of the board during the slide cannot leave the piece in a wrong place.
            float toX = 0.0f;
            float toY = 0.0f;
            board->SquareToPixel(_slideTo.file, _slideTo.rank, toX, toY);
            px = Lerp(px, toX, _slideProgress);
            py = Lerp(py, toY, _slideProgress);
        }
        transform->_x = px;
        transform->_y = py;
        const float size = board->GetSquareSize();
        transform->_width = size;
        transform->_height = size;

        // A promotion shows when the slide ends, not while the pawn is still on its way.
        if (!IsSliding() && cur.IsInside()) {
            ShowPieceType(model->GetShownPieceOf(this));
        }
    }

    void ChessPieceComponent::Observe(const ChessGameModel& model, Square current, float deltaTime) {
        const size_t moves = model.GetMoves().size();
        const bool sameGame = _seen && model.GetStartFen() == _seenStartFen;
        const bool unchanged = sameGame && moves == _seenMoves && current == _lastSquare;
        // One move is one more of both numbers: a reset adds a version too, so a reset and moves between two frames are not taken for one move. A view that begins or ends
        // jumps over any number of moves, so a move seen across one is placed at once
        const bool played = sameGame && !_seenOtherPosition && !ShowsOtherPosition(model) && moves == _seenMoves + 1 && model.GetVersion() == _seenVersion + 1;

        // A move of another piece (played, this piece on its square) leaves a slide of this one under way alone.
        if (!unchanged && !(played && current == _lastSquare)) {
            // A piece that was held in the last update was dropped where it stands now: a slide from the square it left would be a jump back and forth
            if (played && !_lifted && deltaTime > 0.0f && _slideDuration > 0.0f && _lastSquare.IsInside()) {
                StartSlide(_lastSquare, current);
            } else {
                // The first sighting, a reset, a new position, a frame with no time, a count that fell: at once.
                EndSlide();
            }
        }

        _seen = true;
        _lastSquare = current;
        _seenMoves = moves;
        _seenVersion = model.GetVersion();
        _seenOtherPosition = ShowsOtherPosition(model);
        if (!sameGame) {
            _seenStartFen = model.GetStartFen();
        }
    }

    void ChessPieceComponent::StartSlide(Square from, Square to) {
        _slideFrom = from;
        _slideTo = to;
        _slideProgress = 0.0f;
        // The tween first moves in the next frame, so this frame still draws the old square. A new slide replaces the one under way
        _slide.Reset(CoreManager::GetTweens().Start<float>(0.0f, 1.0f, {.duration = _slideDuration, .ease = EaseType::QuadOut}, [this](const float& progress) { _slideProgress = progress; }));
    }

    void ChessPieceComponent::EndSlide() {
        _slide.Reset();
    }

    void ChessPieceComponent::EndLift() {
        if (!_lifted) {
            return;
        }
        _lifted = false;
        GetNode().SetLayer(_layerBeforeLift);
    }

    bool ChessPieceComponent::IsSliding() const {
        return _slide.Get() && !_slide.Get()->IsFinished();
    }

    void ChessPieceComponent::ForgetSeen() {
        EndSlide();
        _seen = false;
    }

    void ChessPieceComponent::ShowPieceType(Piece piece) {
        if (piece.IsEmpty() || piece.type == _shownType) {
            return;
        }
        auto sprite = GetNode().GetComponent<SpriteRendererComponent>();
        if (!sprite) {
            return;
        }
        sprite->_spriteId = PieceSpriteId(piece, GraphicsPreset::Beer);
        sprite->ReloadTexture();
        _shownType = piece.type;
    }

    void ChessPieceComponent::RegisterWithModel() {
        auto* model = ChessGame::GetActiveModel();
        if (!model) {
            return;
        }
        if (!Square::IsInside(_file, _rank)) {
            return;
        }
        model->RegisterPiece(this, Square{static_cast<int8_t>(_file), static_cast<int8_t>(_rank)});
        _shownType = model->GetPieceOf(this).type;  // the sprite of the scene already shows it
        _registered = true;
    }

    void ChessPieceComponent::UnregisterFromModel() {
        if (!_registered) {
            return;
        }
        if (auto* model = ChessGame::GetActiveModel()) {
            model->UnregisterPiece(this);
        }
        _registered = false;
    }

}  // namespace BECore::Chess
