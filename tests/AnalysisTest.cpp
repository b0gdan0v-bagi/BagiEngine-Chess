#include "ChessTestUtil.h"

#include <Chess/Engine/Analysis.h>
#include <Chess/Engine/Evaluation.h>
#include <Chess/Engine/SearchRequest.h>

/// The analysis types: pure, no engine. The score of a search as a score of an analysis, its text, and the default of IChessEngine::Analyse()
namespace BECore::Chess::Tests {

    namespace {

        /// An engine that only plays: it keeps the default Analyse() of the interface
        class PlayingEngine final : public IChessEngine {
        public:
            std::expected<Move, EngineError> FindMove(const SearchRequest&, const CancellationToken&) override {
                return Mv("e2e4");
            }
        };

        constexpr AnalysisScore Cp(int value) {
            return {AnalysisScoreKind::Centipawns, value};
        }

        constexpr AnalysisScore Mate(int moves) {
            return {AnalysisScoreKind::Mate, moves};
        }

        struct MateRow {
            int ply;
            int moves;  ///< for a win at that ply; a loss is the same number with the other sign
        };

    }  // namespace

    TEST(AnalysisTest, AnEvaluationIsCentipawnsAsItIs) {
        EXPECT_EQ(ScoreFromSearch(0), Cp(0));
        EXPECT_EQ(ScoreFromSearch(35), Cp(35));
        EXPECT_EQ(ScoreFromSearch(-120), Cp(-120));
        EXPECT_EQ(ScoreFromSearch(900), Cp(900));
        EXPECT_EQ(ScoreFromSearch(-kQueenValue), Cp(-kQueenValue));
    }

    TEST(AnalysisTest, AMateTheSideToMoveGivesIsPositiveInMoves) {
        EXPECT_EQ(ScoreFromSearch(MateIn(1)), Mate(1));
        EXPECT_EQ(ScoreFromSearch(MateIn(3)), Mate(2));
        EXPECT_EQ(ScoreFromSearch(MateIn(5)), Mate(3));
    }

    TEST(AnalysisTest, AMateTheSideToMoveSuffersIsNegativeInMoves) {
        EXPECT_EQ(ScoreFromSearch(-MateIn(2)), Mate(-1));
        EXPECT_EQ(ScoreFromSearch(-MateIn(4)), Mate(-2));
        EXPECT_EQ(ScoreFromSearch(-MateIn(6)), Mate(-3));
    }

    TEST(AnalysisTest, TheConversionFollowsTheTableOfPlies) {
        // Written out from moves = (plies + 1) / 2. A win lands on an odd ply and a loss on an even one in a real search; the even wins and the odd losses
        // are in the table too, so the formula is pinned for every ply and not only for those
        constexpr MateRow kTable[] = {{1, 1}, {2, 1}, {3, 2}, {4, 2}, {5, 3}, {6, 3}};
        for (const MateRow& row : kTable) {
            EXPECT_EQ(ScoreFromSearch(MateIn(row.ply)), Mate(row.moves)) << "win at ply " << row.ply;
            EXPECT_EQ(ScoreFromSearch(-MateIn(row.ply)), Mate(-row.moves)) << "loss at ply " << row.ply;
        }
    }

    TEST(AnalysisTest, TheFarthestMateStillIsAMateAndTheNextOneIsNot) {
        EXPECT_EQ(ScoreFromSearch(MateIn(kMaxMatePly)), Mate(kMaxMatePly / 2));
        EXPECT_EQ(ScoreFromSearch(-MateIn(kMaxMatePly)), Mate(-kMaxMatePly / 2));
        EXPECT_EQ(ScoreFromSearch(MateIn(kMaxMatePly + 1)), Cp(MateIn(kMaxMatePly + 1)));
        EXPECT_EQ(ScoreFromSearch(-MateIn(kMaxMatePly + 1)), Cp(-MateIn(kMaxMatePly + 1)));
    }

    TEST(AnalysisTest, EveryMateScoreOfTheSearchGivesAMateOfAtLeastOneMove) {
        for (int ply = 1; ply <= kMaxMatePly; ++ply) {
            const AnalysisScore win = ScoreFromSearch(MateIn(ply));
            const AnalysisScore loss = ScoreFromSearch(-MateIn(ply));
            EXPECT_EQ(win.kind, AnalysisScoreKind::Mate) << ply;
            EXPECT_EQ(loss.kind, AnalysisScoreKind::Mate) << ply;
            EXPECT_GE(win.value, 1) << ply;
            EXPECT_EQ(loss.value, -win.value) << ply;
        }
    }

    TEST(AnalysisTest, ACentipawnScoreIsShownInPawnsWithTwoDecimalsAndASign) {
        EXPECT_EQ(ScoreText(Cp(35)), "+0.35");
        EXPECT_EQ(ScoreText(Cp(-120)), "-1.20");
        EXPECT_EQ(ScoreText(Cp(100)), "+1.00");
        EXPECT_EQ(ScoreText(Cp(1234)), "+12.34");
        EXPECT_EQ(ScoreText(Cp(-5)), "-0.05");
        EXPECT_EQ(ScoreText(Cp(5)), "+0.05");
        EXPECT_EQ(ScoreText(Cp(-99)), "-0.99");
    }

    TEST(AnalysisTest, NothingIsRoundedAndZeroHasNoSign) {
        // A centipawn already is a hundredth of a pawn, so the text is exact
        EXPECT_EQ(ScoreText(Cp(1)), "+0.01");
        EXPECT_EQ(ScoreText(Cp(-1)), "-0.01");
        EXPECT_EQ(ScoreText(Cp(0)), "0.00");
        EXPECT_EQ(ScoreText(Cp(kQueenValue)), "+9.00");
    }

    TEST(AnalysisTest, AMateIsHashAndTheSignedNumberOfMoves) {
        EXPECT_EQ(ScoreText(Mate(3)), "#3");
        EXPECT_EQ(ScoreText(Mate(1)), "#1");
        EXPECT_EQ(ScoreText(Mate(-2)), "#-2");
        EXPECT_EQ(ScoreText(Mate(kMaxMatePly / 2)), "#128");
    }

    TEST(AnalysisTest, TheTextOfASearchScoreIsTheTextOfItsConversion) {
        EXPECT_EQ(ScoreText(ScoreFromSearch(35)), "+0.35");
        EXPECT_EQ(ScoreText(ScoreFromSearch(MateIn(3))), "#2");
        EXPECT_EQ(ScoreText(ScoreFromSearch(-MateIn(2))), "#-1");
    }

    TEST(AnalysisTest, TheBestLineIsTheFirstOne) {
        Analysis analysis;
        analysis.lines.push_back(AnalysisLine{Mv("e2e4"), Cp(30), 12});
        analysis.lines.push_back(AnalysisLine{Mv("d2d4"), Cp(20), 12});

        EXPECT_EQ(analysis.Best().move, Mv("e2e4"));
        EXPECT_EQ(analysis.Best().score, Cp(30));
        EXPECT_EQ(analysis.Best().depth, 12);
    }

    TEST(AnalysisTest, AnEngineThatOnlyPlaysDoesNotAnalyse) {
        PlayingEngine engine;
        IChessEngine& asInterface = engine;
        const SearchRequest request = SearchRequest::FromPosition(PositionFromFen(Position::kStartFen), SearchLimits{100, 0});

        const std::expected<Analysis, EngineError> analysis = asInterface.Analyse(request, CancellationToken::Create());

        ASSERT_FALSE(analysis.has_value());
        EXPECT_EQ(analysis.error().kind, EngineErrorKind::Unsupported);
        EXPECT_FALSE(analysis.error().detail.empty());
    }

}  // namespace BECore::Chess::Tests
