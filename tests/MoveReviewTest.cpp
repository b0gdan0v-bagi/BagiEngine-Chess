#include <Chess/Engine/MoveReview.h>
#include <tests/ChessTestUtil.h>

/// The rule that reviews a played move against the evaluations of the positions before and after it: pure, no engine
namespace BECore::Chess::Tests {

    namespace {

        constexpr int kMate3 = kMateCentipawns - 3 * kCentipawnsPerMateMove;
        constexpr int kMate5 = kMateCentipawns - 5 * kCentipawnsPerMateMove;

        /// A move other than the best one, which is e2e4
        MoveReview Played(int bestCentipawns, int afterCentipawns) {
            return ReviewMove(bestCentipawns, Mv("e2e4"), Mv("d2d4"), afterCentipawns);
        }

        /// The review of a move that gave up exactly @p loss centipawns from an equal position
        MoveReview GivingUp(int loss) {
            return Played(0, loss);  // the opponent stands better by `loss` after it, so the mover has -loss
        }

    }  // namespace

    TEST(MoveReviewTest, TheBestMoveHasNoLossWhateverTheNumbers) {
        EXPECT_EQ(ReviewMove(30, Mv("e2e4"), Mv("e2e4"), -30), (MoveReview{0, MoveQuality::Best}));
        EXPECT_EQ(ReviewMove(30, Mv("e2e4"), Mv("e2e4"), 900), (MoveReview{0, MoveQuality::Best})) << "engine noise on its own move";
        EXPECT_EQ(ReviewMove(-500, Mv("e2e4"), Mv("e2e4"), -kMateCentipawns), (MoveReview{0, MoveQuality::Best}));
    }

    TEST(MoveReviewTest, AnotherMoveThatScoresAboveLineOneHasLossZeroNotANegativeOne) {
        // The position after the move is worth -(-80) = +80 to the mover against line 1's +30
        const MoveReview review = Played(30, -80);

        EXPECT_EQ(review.loss, 0);
        EXPECT_EQ(review.quality, MoveQuality::Good) << "it is not the engine's move, so not Best";
    }

    TEST(MoveReviewTest, TheLossIsLineOneMinusWhatTheMoverHasAfterTheMove) {
        // Line 1 +100; after the move the other side stands at +40, so the mover has -40: a loss of 140
        EXPECT_EQ(Played(100, 40).loss, 140);
        EXPECT_EQ(Played(-200, 300).loss, 100);
        EXPECT_EQ(Played(-200, -100).loss, 0) << "a move that is better than line 1 by the numbers";
    }

    TEST(MoveReviewTest, TheScaleChangesAtFiftyAHundredAndThreeHundred) {
        EXPECT_EQ(kInaccuracyLoss, 50);
        EXPECT_EQ(kMistakeLoss, 100);
        EXPECT_EQ(kBlunderLoss, 300);

        EXPECT_EQ(GivingUp(0).quality, MoveQuality::Good);
        EXPECT_EQ(GivingUp(49).quality, MoveQuality::Good);
        EXPECT_EQ(GivingUp(50).quality, MoveQuality::Inaccuracy);
        EXPECT_EQ(GivingUp(99).quality, MoveQuality::Inaccuracy);
        EXPECT_EQ(GivingUp(100).quality, MoveQuality::Mistake);
        EXPECT_EQ(GivingUp(299).quality, MoveQuality::Mistake);
        EXPECT_EQ(GivingUp(300).quality, MoveQuality::Blunder);
        EXPECT_EQ(GivingUp(900).quality, MoveQuality::Blunder);
    }

    TEST(MoveReviewTest, TheLossIsKeptWithItsQuality) {
        EXPECT_EQ(GivingUp(49), (MoveReview{49, MoveQuality::Good}));
        EXPECT_EQ(GivingUp(300), (MoveReview{300, MoveQuality::Blunder}));
    }

    TEST(MoveReviewTest, AMateAgainstALaterMateIsNoLossBecauseOfTheClamp) {
        EXPECT_EQ(ReviewMove(kMate3, Mv("e2e4"), Mv("d2d4"), -kMate5).loss, 0) << "mate in 3 against mate in 5: the game stays won";
        EXPECT_EQ(ReviewMove(kMate3, Mv("e2e4"), Mv("d2d4"), -kMate5).quality, MoveQuality::Good);
    }

    TEST(MoveReviewTest, AMateThrownAwayForAPawnAndAHalfIsALossOfSevenHundred) {
        // Mate for the mover (clamped to +1000), and +300 after the move: 1000 - 300
        const MoveReview review = ReviewMove(kMate3, Mv("e2e4"), Mv("d2d4"), -300);

        EXPECT_EQ(review.loss, kReviewClamp - 300);
        EXPECT_EQ(review.quality, MoveQuality::Blunder);
    }

    TEST(MoveReviewTest, AWonGameThatStaysWonIsNoLossAtAnyHeight) {
        EXPECT_EQ(Played(1500, -1200).loss, 0) << "+1500 against +1200: both clamp to +1000";
        EXPECT_EQ(Played(1500, -1000).loss, 0);
        EXPECT_EQ(Played(1500, -900).loss, 100) << "the clamp ends at 1000, so +1500 is +1000 and +900 is a loss of 100";
    }

    TEST(MoveReviewTest, AMateAgainstTheMoverAfterTheMoveIsABlunder) {
        // Equal before; after the move the other side mates in 2 (the position after is +mate for ITS side to move)
        const MoveReview review = Played(0, kMateCentipawns - 2 * kCentipawnsPerMateMove);

        EXPECT_EQ(review.loss, kReviewClamp);
        EXPECT_EQ(review.quality, MoveQuality::Blunder);
    }

    TEST(MoveReviewTest, AMateAgainstTheMoverThatWasComingAnywayIsNoLoss) {
        const MoveReview review = Played(-kMate3, kMate5);

        EXPECT_EQ(review.loss, 0);
        EXPECT_EQ(review.quality, MoveQuality::Good);
    }

    TEST(MoveReviewTest, OnlyAnInaccuracyOrWorseIsAMistakeForTheList) {
        EXPECT_FALSE(IsMistake(MoveQuality::Best));
        EXPECT_FALSE(IsMistake(MoveQuality::Good));
        EXPECT_TRUE(IsMistake(MoveQuality::Inaccuracy));
        EXPECT_TRUE(IsMistake(MoveQuality::Mistake));
        EXPECT_TRUE(IsMistake(MoveQuality::Blunder));
    }

    TEST(MoveReviewTest, TheIdsOfTheQualitiesArePinned) {
        EXPECT_EQ(MoveQualityId(MoveQuality::Best), "best");
        EXPECT_EQ(MoveQualityId(MoveQuality::Good), "good");
        EXPECT_EQ(MoveQualityId(MoveQuality::Inaccuracy), "inaccuracy");
        EXPECT_EQ(MoveQualityId(MoveQuality::Mistake), "mistake");
        EXPECT_EQ(MoveQualityId(MoveQuality::Blunder), "blunder");
    }

    TEST(MoveReviewTest, TheIdsAreLowerCaseSnakeCaseAndUnique) {
        Vector<String> seen;
        for (const MoveQuality quality : EnumUtils<MoveQuality>::Values()) {
            const String id(MoveQualityId(quality));
            EXPECT_FALSE(id.empty());
            for (const char c : id) {
                EXPECT_TRUE((c >= 'a' && c <= 'z') || c == '_') << id.c_str();
            }
            for (const String& other : seen) {
                EXPECT_NE(id, other);
            }
            seen.push_back(id);
        }
        EXPECT_EQ(seen.size(), EnumUtils<MoveQuality>::Values().size());
    }

    TEST(MoveReviewTest, ACheckmatedPositionIsWorthMinusAMateToItsSideToMove) {
        // Fool's mate: White is mated
        const Position mated = PositionFromFen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");

        EXPECT_EQ(TerminalCentipawns(mated), -kMateCentipawns);
    }

    TEST(MoveReviewTest, AStalematedPositionIsWorthZero) {
        const Position stalemate = PositionFromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");

        EXPECT_EQ(TerminalCentipawns(stalemate), 0);
    }

    TEST(MoveReviewTest, APositionWithAMoveIsNotTerminal) {
        EXPECT_FALSE(TerminalCentipawns(Position::Start()).has_value());
        EXPECT_FALSE(TerminalCentipawns(PositionFromFen("k7/8/1K6/8/8/8/8/7R b - - 0 1")).has_value()) << "one legal move is still a move";
    }

}  // namespace BECore::Chess::Tests
