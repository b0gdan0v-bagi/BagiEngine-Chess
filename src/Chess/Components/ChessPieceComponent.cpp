#include "ChessPieceComponent.h"

#include <BECore/Animation/Easing.h>
#include <BECore/Animation/Lerp.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ChessBoardComponent.h>
#include <Events/SceneEvents.h>
#include <Generated/ChessPieceComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        // Off-screen position used to hide captured pieces without removing the node.
        constexpr float kHiddenX = -10000.0f;
        constexpr float kHiddenY = -10000.0f;

        // The sprite of a piece is named after its color and type in TextureLibrary.xml: "WhiteQueen", "BlackKnight", ...
        PoolString SpriteIdOf(Piece piece) {
            return PoolString::Intern(Format("{}{}", EnumUtils<PieceColor>::ToString(piece.color), EnumUtils<PieceType>::ToString(piece.type)));
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
        // Late-register if the model wasn't ready at OnAttached time.
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
            transform->_x = kHiddenX;
            transform->_y = kHiddenY;
            return;
        }

        const Square cur = model->GetSquareOf(this);
        Observe(*model, cur, event.deltaTime);

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
            ShowPieceType(model->GetPieceOf(this));
        }
    }

    void ChessPieceComponent::Observe(const ChessGameModel& model, Square current, float deltaTime) {
        const size_t moves = model.GetMoves().size();
        const bool sameGame = _seen && model.GetStartFen() == _seenStartFen;
        const bool unchanged = sameGame && moves == _seenMoves && current == _lastSquare;
        // One move is one more of both numbers: a reset adds a version too, so a reset and moves between two frames are not taken for one move.
        const bool played = sameGame && moves == _seenMoves + 1 && model.GetVersion() == _seenVersion + 1;

        // A move of another piece (played, this piece on its square) leaves a slide of this one under way alone.
        if (!unchanged && !(played && current == _lastSquare)) {
            if (played && deltaTime > 0.0f && _slideDuration > 0.0f && _lastSquare.IsInside()) {
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
        if (!sameGame) {
            _seenStartFen = model.GetStartFen();
        }
    }

    void ChessPieceComponent::StartSlide(Square from, Square to) {
        _slideFrom = from;
        _slideTo = to;
        _slideProgress = 0.0f;
        // The tween first moves in the next frame (stage 2b), so this frame still draws the old square. A new slide replaces the one under way.
        _slide.Reset(CoreManager::GetTweens().Start<float>(0.0f, 1.0f, {.duration = _slideDuration, .ease = EaseType::QuadOut}, [this](const float& progress) { _slideProgress = progress; }));
    }

    void ChessPieceComponent::EndSlide() {
        _slide.Reset();
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
        // A promotion changes the pawn into another piece; a reset changes it back.
        sprite->_spriteId = SpriteIdOf(piece);
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
        _shownType = model->GetPieceOf(this).type;  // the sprite in SceneConfig.xml already shows it
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
