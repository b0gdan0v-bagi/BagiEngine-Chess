#pragma once

namespace BECore::Chess {

    /// What a rect of the mark of a brilliant move is: the plate or a stroke of the "!!" on it
    CORE_ENUM(RatingMarkPart, uint8_t, Badge, Glyph)

    struct RatingMarkRect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        RatingMarkPart part = RatingMarkPart::Badge;
    };

    /// The badge and, on it, two bars and two dots
    constexpr size_t kRatingMarkRectCount = 5;

    /**
     * @brief The rects of the mark of a brilliant move in the top right corner of the square at (@p squareX, @p squareY) with the side @p squareSize, in the unit of the arguments.
     *
     * The badge is first, then the glyph "!!" as two bars and two dots, to be drawn dark on it. The renderer draws no text and the scene has no sprite for the mark, so the glyph is
     * rects too. Everything lies inside the square and the glyph inside the badge, every length is a fixed share of @p squareSize, and the strokes do not touch each other.
     */
    [[nodiscard]] Array<RatingMarkRect, kRatingMarkRectCount> BrilliantMarkRects(float squareX, float squareY, float squareSize) noexcept;

}  // namespace BECore::Chess
