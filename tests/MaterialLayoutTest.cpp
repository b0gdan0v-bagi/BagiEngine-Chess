#include <Chess/Game/MaterialLayout.h>
#include <tests/ChessTestUtil.h>

/// The rule that hides the lists of captured pieces when insets have taken their box (the margin of the default view, the two rows of Portrait): pure, nothing to start
namespace BECore::Chess::Tests {

    namespace {

        constexpr float kViewWidth = 840.0f;
        constexpr float kViewHeight = 800.0f;
        constexpr float kNarrowestSafeLeft = 75.6f;  // a 480 px window with the side insets of a phone held sideways (9 % of the width, at the fit scale 0.571)
        constexpr float kBeyond = 0.01f;
        constexpr float kBoardLeft = 100.0f;
        constexpr float kPortraitViewWidth = 720.0f;
        constexpr float kPortraitViewHeight = 960.0f;
        constexpr float kNarrowestPortraitSafeLeft = 64.8f;  // side insets of 9 % of a 720-unit width: the inset of every portrait window of the size matrix with them
        constexpr float kPortraitNotch = 50.4f;              // the top inset of a notch (7 % of the width of the view) when the view fills the window

        bool FitsInPortrait(float left, float top, float right, float bottom) {
            return MaterialListFits(left, top, right, bottom, MaterialView::Portrait);
        }

        bool FitsIn(float left, float top, float right, float bottom) {
            return MaterialListFits(left, top, right, bottom);
        }

    }  // namespace

    TEST(MaterialLayoutTest, TheBoxIsTheOneTheSceneDrawsTheListsIn) {
        EXPECT_FLOAT_EQ(kMaterialListLeft, 8.0f);
        EXPECT_FLOAT_EQ(kMaterialListTop, 84.0f);
        EXPECT_FLOAT_EQ(kMaterialListRight, 72.0f);
        EXPECT_FLOAT_EQ(kMaterialListBottom, 716.0f);
        EXPECT_EQ(kMaterialGroupCount, 2u);
        EXPECT_EQ(kMaterialRowsPerGroup, 5u);
    }

    TEST(MaterialLayoutTest, ThePortraitBoxIsTheOneTheSceneDrawsTheTwoRowsIn) {
        EXPECT_FLOAT_EQ(kMaterialPortraitListLeft, 40.0f);
        EXPECT_FLOAT_EQ(kMaterialPortraitListTop, 92.0f);
        EXPECT_FLOAT_EQ(kMaterialPortraitListRight, 376.0f);
        EXPECT_FLOAT_EQ(kMaterialPortraitListBottom, 832.0f);
    }

    TEST(MaterialLayoutTest, AFullViewFitsTheLists) {
        EXPECT_TRUE(FitsIn(0.0f, 0.0f, kViewWidth, kViewHeight));
        EXPECT_TRUE(FitsInPortrait(0.0f, 0.0f, kPortraitViewWidth, kPortraitViewHeight));
    }

    TEST(MaterialLayoutTest, TheViewIsPartOfTheQuestion) {
        // the default view's box is not the Portrait one: a safe area that is the one box does not hold the other
        EXPECT_FALSE(FitsInPortrait(kMaterialListLeft, kMaterialListTop, kMaterialListRight, kMaterialListBottom));
        EXPECT_FALSE(FitsIn(kMaterialPortraitListLeft, kMaterialPortraitListTop, kMaterialPortraitListRight, kMaterialPortraitListBottom));
        EXPECT_TRUE(MaterialListFits(0.0f, 0.0f, kViewWidth, kViewHeight, MaterialView::Default)) << "the default view is what is asked when no view is given";
    }

    TEST(MaterialLayoutTest, TheEdgesOfTheBoxAreInsideAndOneStepBeyondEachEdgeIsNot) {
        EXPECT_TRUE(FitsIn(kMaterialListLeft, kMaterialListTop, kMaterialListRight, kMaterialListBottom));
        // each edge of the safe area a little inside the box: the box leaves it
        EXPECT_FALSE(FitsIn(kMaterialListLeft + kBeyond, kMaterialListTop, kMaterialListRight, kMaterialListBottom));
        EXPECT_FALSE(FitsIn(kMaterialListLeft, kMaterialListTop + kBeyond, kMaterialListRight, kMaterialListBottom));
        EXPECT_FALSE(FitsIn(kMaterialListLeft, kMaterialListTop, kMaterialListRight - kBeyond, kMaterialListBottom));
        EXPECT_FALSE(FitsIn(kMaterialListLeft, kMaterialListTop, kMaterialListRight, kMaterialListBottom - kBeyond));
        // each edge a little outside the box: it fits
        EXPECT_TRUE(FitsIn(kMaterialListLeft - kBeyond, kMaterialListTop - kBeyond, kMaterialListRight + kBeyond, kMaterialListBottom + kBeyond));
    }

    TEST(MaterialLayoutTest, TheEdgesOfThePortraitBoxAreInsideAndOneStepBeyondEachEdgeIsNot) {
        EXPECT_TRUE(FitsInPortrait(kMaterialPortraitListLeft, kMaterialPortraitListTop, kMaterialPortraitListRight, kMaterialPortraitListBottom));
        EXPECT_FALSE(FitsInPortrait(kMaterialPortraitListLeft + kBeyond, kMaterialPortraitListTop, kMaterialPortraitListRight, kMaterialPortraitListBottom));
        EXPECT_FALSE(FitsInPortrait(kMaterialPortraitListLeft, kMaterialPortraitListTop + kBeyond, kMaterialPortraitListRight, kMaterialPortraitListBottom));
        EXPECT_FALSE(FitsInPortrait(kMaterialPortraitListLeft, kMaterialPortraitListTop, kMaterialPortraitListRight - kBeyond, kMaterialPortraitListBottom));
        EXPECT_FALSE(FitsInPortrait(kMaterialPortraitListLeft, kMaterialPortraitListTop, kMaterialPortraitListRight, kMaterialPortraitListBottom - kBeyond));
        EXPECT_TRUE(FitsInPortrait(kMaterialPortraitListLeft - kBeyond, kMaterialPortraitListTop - kBeyond, kMaterialPortraitListRight + kBeyond, kMaterialPortraitListBottom + kBeyond));
    }

    TEST(MaterialLayoutTest, TheNarrowestPortraitWindowWithSideInsetsDoesNotFitTheRowsAndANotchOrAHomeBarDoes) {
        EXPECT_FALSE(FitsInPortrait(kNarrowestPortraitSafeLeft, 0.0f, kPortraitViewWidth - kNarrowestPortraitSafeLeft, kPortraitViewHeight));
        EXPECT_GT(kNarrowestPortraitSafeLeft, kMaterialPortraitListLeft) << "the inset reaches into the box";
        EXPECT_TRUE(FitsInPortrait(0.0f, kPortraitNotch, kPortraitViewWidth, kPortraitViewHeight)) << "a notch at the top stays above the upper row";
        EXPECT_FALSE(FitsInPortrait(0.0f, 100.0f, kPortraitViewWidth, kPortraitViewHeight)) << "the top of the safe area is below the upper row";
        EXPECT_FALSE(FitsInPortrait(0.0f, 0.0f, kPortraitViewWidth, 820.0f)) << "the bottom of the safe area is above the lower row";
        EXPECT_TRUE(FitsInPortrait(0.0f, 60.0f, kPortraitViewWidth, 900.0f)) << "an inset that stays clear of the box does not";
    }

    TEST(MaterialLayoutTest, TheNarrowestWindowWithSideInsetsDoesNotFitTheLists) {
        EXPECT_FALSE(FitsIn(kNarrowestSafeLeft, 0.0f, kViewWidth - kNarrowestSafeLeft, kViewHeight));
        // the margin that is left, 100 - 75.6 = 24.4 units, is less than the 64 of the lists
        EXPECT_LT(kBoardLeft - kNarrowestSafeLeft, kMaterialListRight - kMaterialListLeft);
    }

    TEST(MaterialLayoutTest, ANotchAtTheTopOrAnInsetAtTheBottomHidesThem) {
        EXPECT_FALSE(FitsIn(0.0f, 90.0f, kViewWidth, kViewHeight)) << "the top of the safe area is below the first row";
        EXPECT_FALSE(FitsIn(0.0f, 0.0f, kViewWidth, 700.0f)) << "the bottom of the safe area is above the last row";
        EXPECT_TRUE(FitsIn(0.0f, 60.0f, kViewWidth, 760.0f)) << "an inset that stays clear of the box does not";
    }

    TEST(MaterialLayoutTest, ASafeAreaThatIsEmptyOrBackwardsFitsNothing) {
        EXPECT_FALSE(FitsIn(0.0f, 0.0f, 0.0f, 0.0f));
        EXPECT_FALSE(FitsIn(500.0f, 500.0f, 100.0f, 100.0f));
        EXPECT_FALSE(FitsInPortrait(0.0f, 0.0f, 0.0f, 0.0f));
        EXPECT_FALSE(FitsInPortrait(500.0f, 500.0f, 100.0f, 100.0f));
    }

}  // namespace BECore::Chess::Tests
