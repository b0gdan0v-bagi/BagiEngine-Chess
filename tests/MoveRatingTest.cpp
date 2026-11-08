#include "ChessTestUtil.h"

#include <Chess/Engine/Evaluation.h>
#include <Chess/Engine/MoveRating.h>

/// The rule that rates a played move against the analysis of the position before it: pure, no engine
namespace BECore::Chess::Tests {

    namespace {

        constexpr AnalysisScore Cp(int value) {
            return {AnalysisScoreKind::Centipawns, value};
        }

        constexpr AnalysisScore Mate(int moves) {
            return {AnalysisScoreKind::Mate, moves};
        }

        /// An analysis of the start position with e2e4 first and d2d4 second
        Analysis TwoLines(AnalysisScore first, AnalysisScore second) {
            Analysis analysis;
            analysis.lines.push_back(AnalysisLine{Mv("e2e4"), first, 5});
            analysis.lines.push_back(AnalysisLine{Mv("d2d4"), second, 5});
            return analysis;
        }

        /// The reason for the move of the first line, with the second line `gap` centipawns lower
        MoveRatingReason ReasonForGap(int gap) {
            return RateMove(TwoLines(Cp(100), Cp(100 - gap)), Mv("e2e4")).reason;
        }

    }  // namespace

    TEST(MoveRatingTest, TheMarginIsTwoPawns) {
        EXPECT_EQ(kForcedMargin, 200);
        EXPECT_EQ(kMateCentipawns, 100000);
    }

    TEST(MoveRatingTest, AGapBelowTheMarginIsBrilliantAndTheMarginItselfIsObvious) {
        EXPECT_EQ(ReasonForGap(199), MoveRatingReason::Brilliant);
        EXPECT_EQ(ReasonForGap(200), MoveRatingReason::Obvious);
        EXPECT_EQ(ReasonForGap(201), MoveRatingReason::Obvious);
        EXPECT_EQ(ReasonForGap(1500), MoveRatingReason::Obvious);
    }

    TEST(MoveRatingTest, TheMarginCountsFromNegativeScoresToo) {
        // Only the difference counts, not where on the scale the two lines are
        EXPECT_EQ(RateMove(TwoLines(Cp(-300), Cp(-499)), Mv("e2e4")).reason, MoveRatingReason::Brilliant);
        EXPECT_EQ(RateMove(TwoLines(Cp(-300), Cp(-500)), Mv("e2e4")).reason, MoveRatingReason::Obvious);
        EXPECT_EQ(RateMove(TwoLines(Cp(150), Cp(-49)), Mv("e2e4")).reason, MoveRatingReason::Brilliant);
        EXPECT_EQ(RateMove(TwoLines(Cp(150), Cp(-50)), Mv("e2e4")).reason, MoveRatingReason::Obvious);
    }

    TEST(MoveRatingTest, ATieMakesTheMoveOfTheFirstLineBrilliantAndTheOtherNotBest) {
        const Analysis tie = TwoLines(Cp(25), Cp(25));

        EXPECT_EQ(RateMove(tie, Mv("e2e4")).reason, MoveRatingReason::Brilliant);
        EXPECT_TRUE(RateMove(tie, Mv("e2e4")).IsBrilliant());
        EXPECT_EQ(RateMove(tie, Mv("d2d4")).reason, MoveRatingReason::NotBest);
        EXPECT_FALSE(RateMove(tie, Mv("d2d4")).IsBrilliant());
    }

    TEST(MoveRatingTest, ASecondLineAboveTheFirstIsAGapOfZero) {
        // The own engine searches each line on its own, so line 2 may score above line 1: that is not a failure
        const Analysis analysis = TwoLines(Cp(20), Cp(60));

        EXPECT_EQ(RateMove(analysis, Mv("e2e4")).reason, MoveRatingReason::Brilliant);
        EXPECT_EQ(RateMove(analysis, Mv("d2d4")).reason, MoveRatingReason::NotBest) << "the first line is the best move even when line 2 scores more";
    }

    TEST(MoveRatingTest, AnyOtherMoveThanTheFirstLineIsNotBest) {
        const Analysis analysis = TwoLines(Cp(30), Cp(10));

        EXPECT_EQ(RateMove(analysis, Mv("d2d4")).reason, MoveRatingReason::NotBest) << "the second line";
        EXPECT_EQ(RateMove(analysis, Mv("g1f3")).reason, MoveRatingReason::NotBest) << "a move that is in no line";
        EXPECT_EQ(RateMove(analysis, Move{}).reason, MoveRatingReason::NotBest) << "the null move";
    }

    TEST(MoveRatingTest, NotBestIsCheckedBeforeTheOthers) {
        Analysis oneLine;
        oneLine.lines.push_back(AnalysisLine{Mv("e2e4"), Cp(30), 5});

        EXPECT_EQ(RateMove(oneLine, Mv("d2d4")).reason, MoveRatingReason::NotBest) << "not forced: it is not the move of the line";
        EXPECT_EQ(RateMove(TwoLines(Cp(900), Cp(0)), Mv("g1f3")).reason, MoveRatingReason::NotBest) << "not obvious either";
    }

    TEST(MoveRatingTest, OneLineMeansNoChoice) {
        Analysis oneLine;
        oneLine.lines.push_back(AnalysisLine{Mv("e2e4"), Cp(30), 5});

        const MoveRating rating = RateMove(oneLine, Mv("e2e4"));

        EXPECT_EQ(rating.reason, MoveRatingReason::Forced);
        EXPECT_FALSE(rating.IsBrilliant());
    }

    TEST(MoveRatingTest, OnlyBrilliantIsBrilliant) {
        EXPECT_TRUE((MoveRating{MoveRatingReason::Brilliant}).IsBrilliant());
        EXPECT_FALSE((MoveRating{MoveRatingReason::NotBest}).IsBrilliant());
        EXPECT_FALSE((MoveRating{MoveRatingReason::Forced}).IsBrilliant());
        EXPECT_FALSE((MoveRating{MoveRatingReason::Obvious}).IsBrilliant());
    }

    TEST(MoveRatingTest, AMateInOneAgainstAMoveThatDoesNothingIsObvious) {
        EXPECT_EQ(RateMove(TwoLines(Mate(1), Cp(0)), Mv("e2e4")).reason, MoveRatingReason::Obvious);
        EXPECT_EQ(RateMove(TwoLines(Mate(1), Cp(900)), Mv("e2e4")).reason, MoveRatingReason::Obvious) << "even against a queen up";
        EXPECT_EQ(RateMove(TwoLines(Mate(3), Cp(900)), Mv("e2e4")).reason, MoveRatingReason::Obvious);
    }

    TEST(MoveRatingTest, AMateAgainstAMateOnlyOneMoveSlowerIsBrilliant) {
        // The scale gives 99900 against 99800: a gap of 100, below the margin (a mate in 1 and a mate in 2 are both good moves)
        EXPECT_EQ(RateMove(TwoLines(Mate(1), Mate(2)), Mv("e2e4")).reason, MoveRatingReason::Brilliant);
        // Three moves slower is a gap of 300
        EXPECT_EQ(RateMove(TwoLines(Mate(1), Mate(4)), Mv("e2e4")).reason, MoveRatingReason::Obvious);
        // Two moves slower is a gap of exactly 200
        EXPECT_EQ(RateMove(TwoLines(Mate(2), Mate(4)), Mv("e2e4")).reason, MoveRatingReason::Obvious);
    }

    TEST(MoveRatingTest, BeingMatedOnlyOneMoveSoonerIsBrilliant) {
        // Both lines lose: -99800 (mated in 2) against -99900 (mated in 1), a gap of 100
        EXPECT_EQ(RateMove(TwoLines(Mate(-2), Mate(-1)), Mv("e2e4")).reason, MoveRatingReason::Brilliant);
        // Escaping a mate in 1 to a mate in 4 is 300 apart: the only move that holds out
        EXPECT_EQ(RateMove(TwoLines(Mate(-4), Mate(-1)), Mv("e2e4")).reason, MoveRatingReason::Obvious);
    }

    TEST(MoveRatingTest, AMoveThatAvoidsAMateAgainstAMateIsObvious) {
        EXPECT_EQ(RateMove(TwoLines(Cp(-50), Mate(-1)), Mv("e2e4")).reason, MoveRatingReason::Obvious) << "the only move that does not lose at once";
        EXPECT_EQ(RateMove(TwoLines(Cp(-900), Mate(-2)), Mv("e2e4")).reason, MoveRatingReason::Obvious);
    }

    TEST(MoveRatingTest, CentipawnsAreTheirOwnScore) {
        EXPECT_EQ(ScoreToCentipawns(Cp(0)), 0);
        EXPECT_EQ(ScoreToCentipawns(Cp(35)), 35);
        EXPECT_EQ(ScoreToCentipawns(Cp(-120)), -120);
    }

    TEST(MoveRatingTest, AMateIsOnTheScaleOfMateCentipawnsMinusAHundredPerMove) {
        EXPECT_EQ(ScoreToCentipawns(Mate(1)), 99900);
        EXPECT_EQ(ScoreToCentipawns(Mate(2)), 99800);
        EXPECT_EQ(ScoreToCentipawns(Mate(-1)), -99900);
        EXPECT_EQ(ScoreToCentipawns(Mate(-2)), -99800);
    }

    TEST(MoveRatingTest, AMateBeatsAnyCentipawnScoreAndAMateAgainstLosesToAny) {
        // Every score the search keeps below the mates, and the farthest mate it can give
        const int farthest = kMaxMatePly / 2;
        const int largestCentipawns = kMateScore - kMaxMatePly - 1;
        EXPECT_GT(ScoreToCentipawns(Mate(farthest)), ScoreToCentipawns(Cp(kQueenValue * 10)));
        EXPECT_GT(ScoreToCentipawns(Mate(farthest)), ScoreToCentipawns(Cp(largestCentipawns)));
        EXPECT_LT(ScoreToCentipawns(Mate(-farthest)), ScoreToCentipawns(Cp(-kQueenValue * 10)));
        EXPECT_LT(ScoreToCentipawns(Mate(-farthest)), ScoreToCentipawns(Cp(-largestCentipawns)));
        EXPECT_GT(ScoreToCentipawns(Mate(1)), ScoreToCentipawns(Mate(2)));
    }

    TEST(MoveRatingTest, ACloserMateIsLargerAndACloserMateAgainstIsSmaller) {
        for (int moves = 1; moves < kMaxMatePly / 2; ++moves) {
            EXPECT_GT(ScoreToCentipawns(Mate(moves)), ScoreToCentipawns(Mate(moves + 1))) << moves;
            EXPECT_LT(ScoreToCentipawns(Mate(-moves)), ScoreToCentipawns(Mate(-(moves + 1)))) << moves;
        }
    }

    TEST(MoveRatingTest, TheSignOfAMateFollowsWhoMates) {
        for (int moves = 1; moves <= kMaxMatePly / 2; ++moves) {
            EXPECT_GT(ScoreToCentipawns(Mate(moves)), 0) << moves;
            EXPECT_EQ(ScoreToCentipawns(Mate(-moves)), -ScoreToCentipawns(Mate(moves))) << moves;
        }
    }

    TEST(MoveRatingTest, TheIdsOfTheReasonsArePinned) {
        EXPECT_EQ(MoveRatingReasonId(MoveRatingReason::Brilliant), "brilliant");
        EXPECT_EQ(MoveRatingReasonId(MoveRatingReason::NotBest), "not_best");
        EXPECT_EQ(MoveRatingReasonId(MoveRatingReason::Forced), "forced");
        EXPECT_EQ(MoveRatingReasonId(MoveRatingReason::Obvious), "obvious");
    }

    TEST(MoveRatingTest, TheIdsAreLowerCaseSnakeCaseAndUnique) {
        eastl::vector<eastl::string> seen;
        for (const MoveRatingReason reason : EnumUtils<MoveRatingReason>::Values()) {
            const eastl::string id(MoveRatingReasonId(reason));
            EXPECT_FALSE(id.empty());
            for (const char c : id) {
                EXPECT_TRUE((c >= 'a' && c <= 'z') || c == '_') << id.c_str();
            }
            for (const eastl::string& other : seen) {
                EXPECT_NE(id, other);
            }
            seen.push_back(id);
        }
        EXPECT_EQ(seen.size(), 4u);
    }

}  // namespace BECore::Chess::Tests
