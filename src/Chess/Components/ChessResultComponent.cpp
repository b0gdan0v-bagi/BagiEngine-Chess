#include "ChessResultComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ChessBoardComponent.h>
#include <Generated/ChessResultComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        constexpr Color kBoardDim{0, 0, 0, 140};
        constexpr Color kBarEdge{205, 165, 60, 255};
        constexpr Color kWhiteWonBar{245, 245, 245, 245};
        constexpr Color kBlackWonBar{25, 25, 25, 245};
        constexpr Color kDrawBar{140, 140, 140, 245};

        constexpr float kBoardSquares = 8.0f;
        constexpr float kBarHeightInSquares = 1.0f;
        constexpr float kBarEdgeInSquares = 0.06f;

    }  // namespace

    void ChessResultComponent::OnDraw() {
        const ChessGameModel* model = ChessGame::GetActiveModel();
        const ChessBoardComponent* board = ChessBoardComponent::GetActive();
        if (!model || !board || !model->IsGameOver()) {
            return;
        }

        auto& renderer = *CoreManager::GetRenderer();
        const float square = board->GetSquareSize();
        const float side = square * kBoardSquares;
        const float left = board->GetOriginX();
        const float top = board->GetOriginY();
        renderer.DrawFilledRect(left, top, side, side, kBoardDim);

        Color barColor = kDrawBar;
        if (const auto winner = model->GetWinner()) {
            barColor = *winner == PieceColor::White ? kWhiteWonBar : kBlackWonBar;
        }
        const float barHeight = square * kBarHeightInSquares;
        const float edge = square * kBarEdgeInSquares;
        const float barTop = top + (side - barHeight) * 0.5f;
        renderer.DrawFilledRect(left, barTop - edge, side, barHeight + 2.0f * edge, kBarEdge);
        renderer.DrawFilledRect(left, barTop, side, barHeight, barColor);
    }

}  // namespace BECore::Chess
