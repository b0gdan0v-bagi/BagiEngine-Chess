#include <Chess/Engine/AnalysisText.h>
#include <Chess/Engine/Evaluation.h>
#include <climits>
#include <tests/ChessTestUtil.h>

/// The words and the bar of the evaluation cell: pure, no engine
namespace BECore::Chess::Tests {

    namespace {

        constexpr AnalysisScore Cp(int value) {
            return {AnalysisScoreKind::Centipawns, value};
        }

        constexpr AnalysisScore Mate(int moves) {
            return {AnalysisScoreKind::Mate, moves};
        }

        Optional<AnalysisLine> LineOf(const AnalysisScore& score) {
            AnalysisLine line;
            line.move = Mv("e2e4");
            line.score = score;
            line.depth = 12;
            return line;
        }

        String Words(StringView text) {
            return String(text.data(), text.size());
        }

        /// What the cell says for a line, with the evaluation on and nothing else the matter
        String Said(const AnalysisScore& score) {
            return AnalysisText::Evaluation(LineOf(score), true, false, false);
        }

    }  // namespace

    TEST(AnalysisTextTest, EachStateOfTheCellHasItsWord) {
        const Optional<AnalysisLine> none;
        EXPECT_EQ(AnalysisText::Evaluation(none, false, false, false), "Eval off");
        EXPECT_EQ(AnalysisText::Evaluation(none, true, false, false), "...");
        EXPECT_EQ(AnalysisText::Evaluation(none, true, true, false), "-");
        EXPECT_EQ(AnalysisText::Evaluation(none, true, false, true), "No analysis");
    }

    TEST(AnalysisTextTest, TheWordConstantsAreTheWordsOfTheCell) {
        const Optional<AnalysisLine> none;
        EXPECT_EQ(AnalysisText::Evaluation(none, false, false, false), Words(AnalysisText::kEvaluationOff));
        EXPECT_EQ(AnalysisText::Evaluation(none, true, false, false), Words(AnalysisText::kEvaluationPending));
        EXPECT_EQ(AnalysisText::Evaluation(none, true, true, false), Words(AnalysisText::kEvaluationGameOver));
        EXPECT_EQ(AnalysisText::Evaluation(none, true, false, true), Words(AnalysisText::kEvaluationFailed));
    }

    TEST(AnalysisTextTest, ALineOfWhitesViewIsSaidAsTheControlApiSaysIt) {
        EXPECT_EQ(Said(Cp(35)), "+0.35");
        EXPECT_EQ(Said(Cp(-120)), "-1.20");
        EXPECT_EQ(Said(Cp(0)), "0.00");
        EXPECT_EQ(Said(Mate(3)), "#3");
        EXPECT_EQ(Said(Mate(-2)), "#-2");
        for (const AnalysisScore& score : {Cp(7), Cp(-4000), Mate(1), Mate(-30)}) {
            EXPECT_EQ(Said(score), ScoreText(score));
        }
    }

    TEST(AnalysisTextTest, TheOrderOfTheRulesIsOffFailedGameOverTheLine) {
        const Optional<AnalysisLine> line = LineOf(Cp(35));
        EXPECT_EQ(AnalysisText::Evaluation(line, false, true, true), "Eval off") << "off wins over everything";
        EXPECT_EQ(AnalysisText::Evaluation(Optional<AnalysisLine>{}, false, true, true), "Eval off");
        EXPECT_EQ(AnalysisText::Evaluation(line, true, true, true), "No analysis") << "a failed search wins over a finished game";
        EXPECT_EQ(AnalysisText::Evaluation(line, true, true, false), "-") << "a finished game wins over a line that is still held";
        EXPECT_EQ(AnalysisText::Evaluation(line, true, false, false), "+0.35");
    }

    TEST(AnalysisTextTest, TheShareOfAnEvenPositionIsHalf) {
        EXPECT_DOUBLE_EQ(AnalysisText::Share(Cp(0)), 0.5);
    }

    TEST(AnalysisTextTest, TheShareFallsSymmetricallyAroundHalf) {
        for (int cp : {1, 10, 35, 100, 300, 900, 2000, 29000, INT_MAX}) {
            EXPECT_EQ(AnalysisText::Share(Cp(-cp)), 1.0 - AnalysisText::Share(Cp(cp))) << cp;
        }
        EXPECT_EQ(AnalysisText::Share(Cp(INT_MIN)), 1.0 - AnalysisText::Share(Cp(INT_MAX)));
    }

    TEST(AnalysisTextTest, TheShareRisesWithTheCentipawnsUntilItSaturates) {
        constexpr int kStep = 25;
        constexpr int kRange = 2000;
        double previous = AnalysisText::Share(Cp(-kRange - kStep));
        for (int cp = -kRange; cp <= kRange; cp += kStep) {
            const double share = AnalysisText::Share(Cp(cp));
            EXPECT_GT(share, previous) << cp;
            previous = share;
        }
    }

    TEST(AnalysisTextTest, AMateFillsTheBarForWhiteOrEmptiesIt) {
        EXPECT_EQ(AnalysisText::Share(Mate(1)), 1.0);
        EXPECT_EQ(AnalysisText::Share(Mate(40)), 1.0);
        EXPECT_EQ(AnalysisText::Share(Mate(-1)), 0.0);
        EXPECT_EQ(AnalysisText::Share(Mate(-40)), 0.0);
        EXPECT_EQ(AnalysisText::Share(Mate(INT_MAX)), 1.0);
        EXPECT_EQ(AnalysisText::Share(Mate(INT_MIN)), 0.0);
        EXPECT_EQ(AnalysisText::Share(Mate(0)), 0.5) << "no engine gives it; the bar does not lean";
    }

    TEST(AnalysisTextTest, TheShareStaysInsideTheBarForAnyInt) {
        for (int cp : {INT_MIN, INT_MIN + 1, -kMateScore, -kQueenValue, -1, 0, 1, kQueenValue, kMateScore, INT_MAX - 1, INT_MAX}) {
            const double share = AnalysisText::Share(Cp(cp));
            EXPECT_GE(share, 0.0) << cp;
            EXPECT_LE(share, 1.0) << cp;
        }
    }

    TEST(AnalysisTextTest, AWinningQueenLeansTheBarFarAndAPawnLeansItLess) {
        const double pawn = AnalysisText::Share(Cp(kPawnValue));
        const double queen = AnalysisText::Share(Cp(kQueenValue));
        EXPECT_GT(pawn, 0.5);
        EXPECT_GT(queen, pawn);
        EXPECT_GT(queen, 0.9);
        EXPECT_LT(queen, 1.0);
    }

    TEST(AnalysisTextTest, NoWordOfAnEngineScoreIsLongerThanTheLengthTheCellIsSizedBy) {
        size_t longest = 0;
        const auto take = [&longest](const String& text) { longest = std::max(longest, text.size()); };

        const Optional<AnalysisLine> none;
        take(AnalysisText::Evaluation(none, false, false, false));
        take(AnalysisText::Evaluation(none, true, false, false));
        take(AnalysisText::Evaluation(none, true, true, false));
        take(AnalysisText::Evaluation(none, true, false, true));

        // The extremes a search can give: the farthest centipawns (below the mate scores) and the farthest mate, from either side
        const int farthestCentipawns = kMateScore - kMaxMatePly - 1;
        ASSERT_FALSE(IsMateScore(farthestCentipawns));
        for (int sign : {1, -1}) {
            take(Said(ScoreFromSearch(sign * farthestCentipawns)));
            take(Said(ScoreFromSearch(sign * MateIn(1))));
            take(Said(ScoreFromSearch(sign * MateIn(kMaxMatePly))));
        }
        EXPECT_EQ(longest, AnalysisText::kMaxEvaluationLength);
    }

}  // namespace BECore::Chess::Tests
