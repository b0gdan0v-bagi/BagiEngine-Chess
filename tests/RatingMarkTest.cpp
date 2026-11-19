#include <Chess/Game/RatingMark.h>
#include <tests/ChessTestUtil.h>

/// The geometry of the mark of a brilliant move: pure, every square size of the scene's matrix
namespace BECore::Chess::Tests {

    namespace {

        constexpr float kTolerance = 1e-4f;
        constexpr float kOneUnit = 1.0f;
        constexpr float kSquareX = 100.0f;
        constexpr float kSquareY = 260.0f;
        constexpr Array<float, 4> kSquareSizes = {80.0f, 64.0f, 40.0f, 24.0f};

        bool Inside(const RatingMarkRect& inner, float x, float y, float w, float h) {
            return inner.x >= x - kTolerance && inner.y >= y - kTolerance && inner.x + inner.w <= x + w + kTolerance && inner.y + inner.h <= y + h + kTolerance;
        }

        bool Touch(const RatingMarkRect& a, const RatingMarkRect& b) {
            return a.x <= b.x + b.w + kTolerance && b.x <= a.x + a.w + kTolerance && a.y <= b.y + b.h + kTolerance && b.y <= a.y + a.h + kTolerance;
        }

    }  // namespace

    TEST(RatingMarkTest, TheBadgeIsFirstAndTheGlyphIsTwoBarsAndTwoDots) {
        const auto rects = BrilliantMarkRects(kSquareX, kSquareY, 80.0f);
        EXPECT_EQ(rects[0].part, RatingMarkPart::Badge);
        for (size_t i = 1; i < rects.size(); ++i) {
            EXPECT_EQ(rects[i].part, RatingMarkPart::Glyph) << i;
        }
        // the bars are taller than the dots, which are as wide as the bars
        EXPECT_GT(rects[1].h, rects[2].h);
        EXPECT_GT(rects[3].h, rects[4].h);
        EXPECT_FLOAT_EQ(rects[1].w, rects[2].w);
        EXPECT_FLOAT_EQ(rects[2].w, rects[2].h);
        EXPECT_FLOAT_EQ(rects[1].w, rects[3].w);
    }

    TEST(RatingMarkTest, TheBadgeIsAThirdOfTheSquareAtItsTopRightCorner) {
        const float size = 80.0f;
        const RatingMarkRect badge = BrilliantMarkRects(kSquareX, kSquareY, size)[0];
        EXPECT_NEAR(badge.w, size / 3.0f, kTolerance);
        EXPECT_FLOAT_EQ(badge.w, badge.h);
        // the right and the top edge are one inset from the corner, the same inset
        const float rightGap = kSquareX + size - (badge.x + badge.w);
        const float topGap = badge.y - kSquareY;
        EXPECT_GT(rightGap, 0.0f);
        EXPECT_NEAR(rightGap, topGap, kTolerance);
        EXPECT_LT(rightGap, badge.w) << "a small inset";
    }

    TEST(RatingMarkTest, EveryRectLiesInsideTheSquareAndTheGlyphInsideTheBadge) {
        for (const float size : kSquareSizes) {
            const auto rects = BrilliantMarkRects(kSquareX, kSquareY, size);
            for (const RatingMarkRect& rect : rects) {
                EXPECT_TRUE(Inside(rect, kSquareX, kSquareY, size, size)) << size;
            }
            for (size_t i = 1; i < rects.size(); ++i) {
                EXPECT_TRUE(Inside(rects[i], rects[0].x, rects[0].y, rects[0].w, rects[0].h)) << size << " " << i;
            }
        }
    }

    TEST(RatingMarkTest, NoTwoStrokesOfTheGlyphTouch) {
        for (const float size : kSquareSizes) {
            const auto rects = BrilliantMarkRects(kSquareX, kSquareY, size);
            for (size_t i = 1; i < rects.size(); ++i) {
                for (size_t j = i + 1; j < rects.size(); ++j) {
                    EXPECT_FALSE(Touch(rects[i], rects[j])) << size << " " << i << " " << j;
                }
            }
        }
    }

    TEST(RatingMarkTest, TheMarksOfTheTwoBangsAreTheSameSize) {
        const auto rects = BrilliantMarkRects(kSquareX, kSquareY, 64.0f);
        EXPECT_FLOAT_EQ(rects[1].w, rects[3].w);
        EXPECT_FLOAT_EQ(rects[1].h, rects[3].h);
        EXPECT_FLOAT_EQ(rects[2].w, rects[4].w);
        EXPECT_FLOAT_EQ(rects[2].h, rects[4].h);
        EXPECT_FLOAT_EQ(rects[1].y, rects[3].y);
        EXPECT_FLOAT_EQ(rects[2].y, rects[4].y);
        EXPECT_LT(rects[1].x, rects[3].x);
    }

    TEST(RatingMarkTest, TheLengthsScaleLinearlyWithTheSquare) {
        const auto big = BrilliantMarkRects(0.0f, 0.0f, 80.0f);
        for (const float size : kSquareSizes) {
            const auto small = BrilliantMarkRects(0.0f, 0.0f, size);
            const float factor = size / 80.0f;
            for (size_t i = 0; i < big.size(); ++i) {
                EXPECT_NEAR(small[i].x, big[i].x * factor, kTolerance) << size << " " << i;
                EXPECT_NEAR(small[i].y, big[i].y * factor, kTolerance) << size << " " << i;
                EXPECT_NEAR(small[i].w, big[i].w * factor, kTolerance) << size << " " << i;
                EXPECT_NEAR(small[i].h, big[i].h * factor, kTolerance) << size << " " << i;
            }
        }
    }

    TEST(RatingMarkTest, TheSquaresPositionOnlyMovesTheMark) {
        const auto home = BrilliantMarkRects(0.0f, 0.0f, 64.0f);
        const auto moved = BrilliantMarkRects(kSquareX, kSquareY, 64.0f);
        for (size_t i = 0; i < home.size(); ++i) {
            EXPECT_NEAR(moved[i].x - kSquareX, home[i].x, kTolerance) << i;
            EXPECT_NEAR(moved[i].y - kSquareY, home[i].y, kTolerance) << i;
            EXPECT_FLOAT_EQ(moved[i].w, home[i].w);
            EXPECT_FLOAT_EQ(moved[i].h, home[i].h);
        }
    }

    TEST(RatingMarkTest, AStrokeIsNeverNarrowerThanOneUnitAtTheSmallestSquare) {
        const auto rects = BrilliantMarkRects(0.0f, 0.0f, 24.0f);
        for (size_t i = 1; i < rects.size(); ++i) {
            EXPECT_GE(rects[i].w, kOneUnit) << i;
            EXPECT_GE(rects[i].h, kOneUnit) << i;
        }
    }

}  // namespace BECore::Chess::Tests
