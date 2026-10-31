#include "ChessBoardComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <Chess/ChessGame.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/SceneEvents.h>
#include <Games/GameManager.h>
#include <Generated/ChessBoardComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        constexpr Color kLightSquare{240, 217, 181, 255};
        constexpr Color kDarkSquare{181, 136, 99, 255};
        constexpr Color kSelectedSquare{120, 200, 120, 200};
        constexpr Color kLegalMoveSquare{120, 200, 120, 120};
        constexpr Color kLegalCaptureSquare{220, 90, 90, 160};

        // Returns the active ChessGame's model, or nullptr if no chess game is active.
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

    ChessBoardComponent* ChessBoardComponent::s_active = nullptr;

    ChessBoardComponent::~ChessBoardComponent() {
        if (s_active == this) {
            s_active = nullptr;
        }
    }

    void ChessBoardComponent::OnAttached() {
        s_active = this;
        Subscribe<SceneEvents::SceneDrawEvent, &ChessBoardComponent::OnDraw>(this);
        Subscribe<SDLEvents::MouseButtonDownEvent, &ChessBoardComponent::OnMouseDown>(this);
    }

    void ChessBoardComponent::SquareToPixel(int file, int rank, float& outX, float& outY) const {
        // rank 0 (White's back rank) is drawn at the bottom of the board.
        outX = _originX + static_cast<float>(file) * _squareSize;
        outY = _originY + static_cast<float>(7 - rank) * _squareSize;
    }

    void ChessBoardComponent::OnDraw(const SceneEvents::SceneDrawEvent&) {
        auto& renderer = *CoreManager::GetRenderer();

        // Checkered base.
        for (int rank = 0; rank < 8; ++rank) {
            for (int file = 0; file < 8; ++file) {
                float px = 0.0f;
                float py = 0.0f;
                SquareToPixel(file, rank, px, py);
                const bool isLight = ((file + rank) % 2) != 0;
                renderer.DrawFilledRect(px, py, _squareSize, _squareSize, isLight ? kLightSquare : kDarkSquare);
            }
        }

        const ChessGameModel* model = GetActiveModel();
        if (!model) {
            return;
        }

        if (model->HasSelection()) {
            const Square sel = model->GetSelected();
            float px = 0.0f;
            float py = 0.0f;
            SquareToPixel(sel.file, sel.rank, px, py);
            renderer.DrawFilledRect(px, py, _squareSize, _squareSize, kSelectedSquare);

            for (const auto target : model->GetLegalMoves()) {
                SquareToPixel(target.file, target.rank, px, py);
                const bool isCapture = !model->GetPieceAt(target).IsEmpty();
                renderer.DrawFilledRect(px, py, _squareSize, _squareSize, isCapture ? kLegalCaptureSquare : kLegalMoveSquare);
            }
        }
    }

    void ChessBoardComponent::OnMouseDown(const SDLEvents::MouseButtonDownEvent& e) {
        ChessGameModel* model = GetActiveModel();
        if (!model) {
            return;
        }

        const float boardSize = _squareSize * 8.0f;
        const float localX = e.button.x - _originX;
        const float localY = e.button.y - _originY;
        if (localX < 0.0f || localY < 0.0f || localX >= boardSize || localY >= boardSize) {
            return;
        }

        const int file = static_cast<int>(localX / _squareSize);
        const int rankFromTop = static_cast<int>(localY / _squareSize);
        const int rank = 7 - rankFromTop;
        if (!Square::IsInside(file, rank)) {
            return;
        }

        model->OnSquareClicked(Square{static_cast<int8_t>(file), static_cast<int8_t>(rank)});
    }

}  // namespace BECore::Chess
