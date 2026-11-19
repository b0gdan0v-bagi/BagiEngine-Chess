#include "ChessRatingMarkComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ChessBoardComponent.h>
#include <Chess/Engine/PositionAnalyst.h>
#include <Chess/Game/RatingMark.h>
#include <Generated/ChessRatingMarkComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        constexpr Color kBadgeColor{250, 204, 51, 255};
        constexpr Color kGlyphColor{34, 26, 10, 255};

    }  // namespace

    void ChessRatingMarkComponent::OnDraw() {
        const ChessGameModel* model = ChessGame::GetActiveModel();
        const ChessBoardComponent* board = ChessBoardComponent::GetActive();
        const PositionAnalyst* analyst = ChessGame::GetActiveAnalyst();
        // A draft is no position of the game, and the ratings are shown only while the person switched them on
        if (!model || !board || !analyst || !analyst->AreRatingsOn() || model->IsDrafting()) {
            return;
        }
        const size_t shownMoves = model->GetShownMoveCount();
        if (shownMoves == 0) {
            return;
        }
        const size_t ply = shownMoves - 1;
        // The rating is empty for a ply the rater does not hold (not rated yet, or another game's), so the index of GetMoves() below is in range
        const Optional<MoveRating> rating = analyst->GetRating(*model, ply);
        if (!rating || !rating->IsBrilliant()) {
            return;
        }

        const Square target = model->GetMoves()[ply].To();
        float squareX = 0.0f;
        float squareY = 0.0f;
        board->SquareToPixel(target.file, target.rank, board->GetBottomColor(), squareX, squareY);
        auto& renderer = *CoreManager::GetRenderer();
        for (const RatingMarkRect& rect : BrilliantMarkRects(squareX, squareY, board->GetSquareSize())) {
            renderer.DrawFilledRect(rect.x, rect.y, rect.w, rect.h, rect.part == RatingMarkPart::Badge ? kBadgeColor : kGlyphColor);
        }
    }

}  // namespace BECore::Chess
