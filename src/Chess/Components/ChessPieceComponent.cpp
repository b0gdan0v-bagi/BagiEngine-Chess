#include "ChessPieceComponent.h"

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

    void ChessPieceComponent::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
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
            transform->_x = kHiddenX;
            transform->_y = kHiddenY;
            return;
        }

        const Square cur = model->GetSquareOf(this);
        if (!cur.IsInside()) {
            transform->_x = kHiddenX;
            transform->_y = kHiddenY;
            return;
        }

        float px = 0.0f;
        float py = 0.0f;
        board->SquareToPixel(cur.file, cur.rank, px, py);
        transform->_x = px;
        transform->_y = py;
        const float size = board->GetSquareSize();
        transform->_width = size;
        transform->_height = size;

        ShowPieceType(model->GetPieceOf(this));
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
