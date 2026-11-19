#include "RatingMark.h"

namespace BECore::Chess {

    namespace {

        // Shares of the square
        constexpr float kBadgeShare = 1.0f / 3.0f;
        constexpr float kInsetShare = 1.0f / 20.0f;

        // Shares of the badge: the glyph is 1/2 wide and 3/5 high, centred; a stroke is 1/6 wide, so at the smallest square of the size matrix (24) it is still more than one unit
        constexpr float kGlyphLeftShare = 1.0f / 4.0f;
        constexpr float kStrokeShare = 1.0f / 6.0f;
        constexpr float kGlyphTopShare = 1.0f / 5.0f;
        constexpr float kBarHeightShare = 7.0f / 20.0f;
        constexpr float kBarToDotShare = 1.0f / 12.0f;
        constexpr size_t kBangCount = 2;

    }  // namespace

    Array<RatingMarkRect, kRatingMarkRectCount> BrilliantMarkRects(float squareX, float squareY, float squareSize) noexcept {
        Array<RatingMarkRect, kRatingMarkRectCount> rects;
        const float badge = squareSize * kBadgeShare;
        const float badgeX = squareX + squareSize - squareSize * kInsetShare - badge;
        const float badgeY = squareY + squareSize * kInsetShare;
        rects[0] = RatingMarkRect{badgeX, badgeY, badge, badge, RatingMarkPart::Badge};

        const float stroke = badge * kStrokeShare;
        const float barY = badgeY + badge * kGlyphTopShare;
        const float barHeight = badge * kBarHeightShare;
        const float dotY = barY + barHeight + badge * kBarToDotShare;
        for (size_t i = 0; i < kBangCount; ++i) {
            // the two strokes of a row are one stroke apart
            const float x = badgeX + badge * kGlyphLeftShare + 2.0f * stroke * static_cast<float>(i);
            rects[1 + 2 * i] = RatingMarkRect{x, barY, stroke, barHeight, RatingMarkPart::Glyph};
            rects[2 + 2 * i] = RatingMarkRect{x, dotY, stroke, stroke, RatingMarkPart::Glyph};
        }
        return rects;
    }

}  // namespace BECore::Chess
