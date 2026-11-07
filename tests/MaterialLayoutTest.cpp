#include "ChessTestUtil.h"

#include <Chess/Game/MaterialLayout.h>

/// The rule that hides the lists of captured pieces in a margin that insets have made too thin: pure, nothing to start
namespace BECore::Chess::Tests {

    namespace {

        constexpr float kViewWidth = 840.0f;
        constexpr float kViewHeight = 800.0f;
        constexpr float kNarrowestSafeLeft = 75.6f;  // a 480 px window with the side insets of a phone held sideways (9 % of the width, at the fit scale 0.571)
        constexpr float kBeyond = 0.01f;
        constexpr float kBoardLeft = 100.0f;  // where the margin ends

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

    TEST(MaterialLayoutTest, AFullViewFitsTheLists) {
        EXPECT_TRUE(FitsIn(0.0f, 0.0f, kViewWidth, kViewHeight));
    }

    TEST(MaterialLayoutTest, TheEdgesOfTheBoxAreInsideAndOneStepBeyondEachEdgeIsNot) {
        // exactly the box
        EXPECT_TRUE(FitsIn(kMaterialListLeft, kMaterialListTop, kMaterialListRight, kMaterialListBottom));
        // each edge of the safe area a little inside the box: the box leaves it
        EXPECT_FALSE(FitsIn(kMaterialListLeft + kBeyond, kMaterialListTop, kMaterialListRight, kMaterialListBottom));
        EXPECT_FALSE(FitsIn(kMaterialListLeft, kMaterialListTop + kBeyond, kMaterialListRight, kMaterialListBottom));
        EXPECT_FALSE(FitsIn(kMaterialListLeft, kMaterialListTop, kMaterialListRight - kBeyond, kMaterialListBottom));
        EXPECT_FALSE(FitsIn(kMaterialListLeft, kMaterialListTop, kMaterialListRight, kMaterialListBottom - kBeyond));
        // each edge a little outside the box: it fits
        EXPECT_TRUE(FitsIn(kMaterialListLeft - kBeyond, kMaterialListTop - kBeyond, kMaterialListRight + kBeyond, kMaterialListBottom + kBeyond));
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
    }

}  // namespace BECore::Chess::Tests
