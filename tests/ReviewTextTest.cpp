#include <Chess/Engine/ReviewText.h>
#include <tests/ChessTestUtil.h>

/// The words of the review: pure, they read their arguments and make text
namespace BECore::Chess::Tests {

    namespace {

        constexpr ReviewProgress kNoProgress{};

        String StatusOf(ReviewState state, ReviewProgress progress, Optional<ReviewRefusal> refusal, bool puzzleOn) {
            return ReviewText::Status(state, progress, refusal, puzzleOn);
        }

    }  // namespace

    TEST(ReviewTextTest, AnIdleReviewSaysWhyItCannotStartOrThatItHasNotStarted) {
        EXPECT_EQ(StatusOf(ReviewState::Idle, kNoProgress, std::nullopt, false), "Not started");
        EXPECT_EQ(StatusOf(ReviewState::Idle, kNoProgress, ReviewRefusal::NotFinished, false), "The game is not over");
        EXPECT_EQ(StatusOf(ReviewState::Idle, kNoProgress, ReviewRefusal::NoMoves, false), "No moves to review");
    }

    TEST(ReviewTextTest, APuzzleOnWinsOverTheRefusalWhileIdleOnly) {
        for (const Optional<ReviewRefusal> refusal : {Optional<ReviewRefusal>(), Optional<ReviewRefusal>(ReviewRefusal::NotFinished), Optional<ReviewRefusal>(ReviewRefusal::NoMoves)}) {
            EXPECT_EQ(StatusOf(ReviewState::Idle, kNoProgress, refusal, true), "Not during a puzzle");
        }
        // A review that is held is a review of a finished game: the puzzle's word is never its status
        EXPECT_EQ(StatusOf(ReviewState::Running, {12, 40}, std::nullopt, true), "Reviewing 12 of 40");
        EXPECT_EQ(StatusOf(ReviewState::Done, {40, 40}, std::nullopt, true), "Review done");
        EXPECT_EQ(StatusOf(ReviewState::Failed, {3, 40}, std::nullopt, true), "Review failed");
    }

    TEST(ReviewTextTest, ARunningReviewSaysHowFarItIsAndTheOtherStatesSayTheirWord) {
        EXPECT_EQ(StatusOf(ReviewState::Running, {12, 40}, std::nullopt, false), "Reviewing 12 of 40");
        EXPECT_EQ(StatusOf(ReviewState::Running, {0, 40}, std::nullopt, false), "Reviewing 0 of 40");
        EXPECT_EQ(StatusOf(ReviewState::Done, {40, 40}, std::nullopt, false), "Review done");
        EXPECT_EQ(StatusOf(ReviewState::Failed, {3, 40}, std::nullopt, false), "Review failed");
    }

    TEST(ReviewTextTest, TheHudSaysTheProgressInTheShortForm) {
        EXPECT_EQ(ReviewText::Hud({12, 40}), "Reviewing 12/40");
        EXPECT_EQ(ReviewText::Hud({0, 7}), "Reviewing 0/7");
    }

    TEST(ReviewTextTest, EveryCombinationIsAnOwnNonEmptySentenceThatFitsTheLineTheLayoutIsMadeFor) {
        constexpr ReviewProgress kWorst{9999, 9999};
        size_t longest = 0;
        for (const ReviewState state : EnumUtils<ReviewState>::Values()) {
            for (const Optional<ReviewRefusal> refusal : {Optional<ReviewRefusal>(), Optional<ReviewRefusal>(ReviewRefusal::NotFinished), Optional<ReviewRefusal>(ReviewRefusal::NoMoves)}) {
                for (const bool puzzleOn : {false, true}) {
                    const String words = StatusOf(state, kWorst, refusal, puzzleOn);
                    EXPECT_FALSE(words.empty());
                    EXPECT_LE(words.size(), ReviewText::kMaxStatusLength) << words.c_str();
                    longest = eastl::max(longest, words.size());
                }
            }
        }
        EXPECT_EQ(longest, ReviewText::kMaxStatusLength) << "the constant is the longest sentence, not a larger guess";
        EXPECT_EQ(StatusOf(ReviewState::Running, kWorst, std::nullopt, false), "Reviewing 9999 of 9999");
        EXPECT_LE(ReviewText::Hud(kWorst).size(), ReviewText::kMaxStatusLength);
    }

    TEST(ReviewTextTest, TheStatesOfAReviewSayDifferentWords) {
        Vector<String> words;
        for (const ReviewState state : EnumUtils<ReviewState>::Values()) {
            words.push_back(StatusOf(state, {1, 2}, std::nullopt, false));
        }
        for (size_t a = 0; a < words.size(); ++a) {
            for (size_t b = a + 1; b < words.size(); ++b) {
                EXPECT_NE(words[a], words[b]);
            }
        }
    }

}  // namespace BECore::Chess::Tests
