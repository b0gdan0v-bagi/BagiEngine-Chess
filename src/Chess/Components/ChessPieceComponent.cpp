#include "ChessPieceComponent.h"

#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ChessBoardComponent.h>
#include <Events/SceneEvents.h>
#include <Games/GameManager.h>
#include <Generated/ChessPieceComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        // Off-screen position used to hide captured pieces without removing the node.
        constexpr float kHiddenX = -10000.0f;
        constexpr float kHiddenY = -10000.0f;

        ChessGameModel* GetActiveModel() {
            auto* active = GameManager::GetInstance().GetActive();
            if (!active) {
                return nullptr;
            }
            auto* chess = active->Cast<ChessGame>();
            if (!chess) {
                return nullptr;
            }
            return &chess->GetModel();
        }

    }  // namespace

    ChessPieceComponent::~ChessPieceComponent() {
        UnregisterFromModel();
    }

    void ChessPieceComponent::OnAttached() {
        RegisterWithModel();
        Subscribe<SceneEvents::SceneUpdateEvent, &ChessPieceComponent::OnUpdate>(this);
    }

    void ChessPieceComponent::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        // Late-register if the model wasn't ready at OnAttached time.
        if (!_registered) {
            RegisterWithModel();
        }

        auto* model = GetActiveModel();
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
    }

    void ChessPieceComponent::RegisterWithModel() {
        auto* model = GetActiveModel();
        if (!model) {
            return;
        }
        if (!Square::IsInside(_file, _rank)) {
            return;
        }
        model->RegisterPiece(this, Square{static_cast<int8_t>(_file), static_cast<int8_t>(_rank)});
        _registered = true;
    }

    void ChessPieceComponent::UnregisterFromModel() {
        if (!_registered) {
            return;
        }
        if (auto* model = GetActiveModel()) {
            model->UnregisterPiece(this);
        }
        _registered = false;
    }

}  // namespace BECore::Chess
