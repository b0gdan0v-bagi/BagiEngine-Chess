#include <BECore/Profiler/ScopedTrace.h>
#include <Chess/Engine/EngineOpponent.h>
#include <Chess/Engine/GameReviewer.h>
#include <Chess/Engine/OwnEngine.h>
#include <Chess/Engine/PositionAnalyst.h>
#include <Chess/Game/ChessGameModel.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <tests/TestEngines.h>

/// The zones and task labels of the opponent, the analysis and the ratings, read back from the Chrome trace of a `ScopedTrace`. Only the names are checked, never a
/// time. The zones exist only in a profile build (ENGINE_PROFILER); elsewhere every case skips
namespace BECore::Chess::Tests {

    namespace {

        constexpr int kShallowDepth = 2;
        constexpr StringView kSearchSliceZone = "\"Chess search slice\"";
        constexpr StringView kMoveSearchZone = "\"Chess move search\"";
        constexpr StringView kAnalysisZone = "\"Chess analysis\"";
        constexpr StringView kRatingZone = "\"Chess rating\"";
        constexpr StringView kOpponentUpdateZone = "\"Chess opponent update\"";
        constexpr StringView kAnalystUpdateZone = "\"Chess analyst update\"";
        constexpr StringView kRaterUpdateZone = "\"Chess rater update\"";
        constexpr StringView kReviewZone = "\"Chess review\"";
        constexpr StringView kReviewerUpdateZone = "\"Chess reviewer update\"";

        bool Contains(const String& text, StringView name) {
            return text.find(name.data(), 0, name.size()) != String::npos;
        }

        /// What a game loop does: one frame of the task system (a single-thread build resumes a yielded search there), then @p step, until @p done
        template <typename Step, typename Condition>
        bool RunFramesUntil(Step step, Condition done) {
            return WaitUntil([&] {
                PumpTaskFrame();
                step();
                return done();
            });
        }

    }  // namespace

    class EngineProfileZonesTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ::BECore::Tests::EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
#if !BE_PROFILER
            GTEST_SKIP() << "needs ENGINE_PROFILER: without it the zone macros compile out";
#endif
            _directory = ::BECore::Tests::TestTempDirectory("EngineProfileZones");
            ::BECore::Tests::MakeEmptyDirectory(_directory);
        }

        void TearDown() override {
            ::BECore::Tests::RemoveDirectory(_directory);
        }

        [[nodiscard]] Path TracePath() const {
            return _directory / "trace.json";
        }

        /// The trace is written when the `ScopedTrace` ends, so a case ends it before it reads
        [[nodiscard]] String ReadTrace() const {
            return ::BECore::Tests::ReadFile(TracePath());
        }

        /// The tasks of the pool have ended, so every zone of them is closed in the file
        static bool PoolIsIdle() {
            return WaitUntil([] { return TaskManager::GetInstance().GetPendingTaskCount() == 0; });
        }

        Path _directory;
    };

    TEST_F(EngineProfileZonesTest, ABlockingSearchRunsInSliceZones) {
        {
            ScopedTrace trace(String(TracePath().View()));
            OwnEngine engine;
            const auto found = engine.FindMove(SearchRequest::FromPosition(Position::Start(), SearchLimits{0, kShallowDepth}), CancellationToken::Create());
            ASSERT_TRUE(found.has_value());
        }
        EXPECT_TRUE(Contains(ReadTrace(), kSearchSliceZone));
    }

    TEST_F(EngineProfileZonesTest, TheOpponentsSearchHasItsLabelAndItsFrameUpdateAZone) {
        ChessGameModel model;
        {
            ScopedTrace trace(String(TracePath().View()));
            EngineOpponent opponent(New<OwnEngine>(), PieceColor::White, SearchLimits{0, kShallowDepth});
            ASSERT_TRUE(RunFramesUntil([&] { opponent.Update(model); }, [&] { return !model.GetMoves().empty(); })) << "the opponent never played";
            ASSERT_TRUE(PoolIsIdle());
        }
        const String text = ReadTrace();
        EXPECT_TRUE(Contains(text, kMoveSearchZone));
        EXPECT_TRUE(Contains(text, kOpponentUpdateZone));
        EXPECT_TRUE(Contains(text, kSearchSliceZone)) << "the slices of the search run inside the task";
    }

    TEST_F(EngineProfileZonesTest, TheAnalysisAndTheRatingAreNamedApart) {
        ChessGameModel model;
        {
            const auto played = MoveGenerator::ParseUci(model.GetPosition(), "e2e4");
            ASSERT_TRUE(played.has_value());
            ASSERT_TRUE(model.TryMove(*played).has_value());
        }
        {
            ScopedTrace trace(String(TracePath().View()));
            PositionAnalyst analyst;
            analyst.SetEngineFactory([](OpponentEngine) -> MadeEngine { return ChessEnginePtr(New<OwnEngine>()); });
            analyst.SetEvaluation(true);
            analyst.SetRatings(true);
            const auto answered = [&] {
                const MoveRatings ratings = analyst.GetRatings(model);
                return analyst.GetEvaluation(model).has_value() && !ratings.empty() && ratings.front().has_value();
            };
            ASSERT_TRUE(RunFramesUntil([&] { analyst.Update(model, OpponentEngine::Own); }, answered)) << DescribeError(analyst.GetError()).c_str();
            ASSERT_TRUE(PoolIsIdle());
            analyst.Shutdown();
        }
        const String text = ReadTrace();
        EXPECT_TRUE(Contains(text, kAnalysisZone));
        EXPECT_TRUE(Contains(text, kRatingZone));
        EXPECT_TRUE(Contains(text, kAnalystUpdateZone));
        EXPECT_TRUE(Contains(text, kRaterUpdateZone));
    }

    TEST_F(EngineProfileZonesTest, TheReviewHasItsLabelAndItsFrameUpdateAZone) {
        ChessGameModel model;
        for (const StringView uci : {"e2e4", "e7e5"}) {
            const auto played = MoveGenerator::ParseUci(model.GetPosition(), uci);
            ASSERT_TRUE(played.has_value());
            ASSERT_TRUE(model.TryMove(*played).has_value());
        }
        ASSERT_TRUE(model.Resign(PieceColor::White).has_value());
        {
            ScopedTrace trace(String(TracePath().View()));
            GameReviewer reviewer;
            reviewer.SetEngineFactory([](OpponentEngine) -> MadeEngine { return ChessEnginePtr(New<OwnEngine>()); });
            ASSERT_TRUE(reviewer.Start(model).has_value());
            const auto done = [&] { return reviewer.GetState(model) == ReviewState::Done; };
            ASSERT_TRUE(RunFramesUntil([&] { reviewer.Update(model); }, done)) << DescribeError(reviewer.GetError()).c_str();
            ASSERT_TRUE(PoolIsIdle());
            reviewer.Shutdown();
        }
        const String text = ReadTrace();
        EXPECT_TRUE(Contains(text, kReviewZone));
        EXPECT_TRUE(Contains(text, kReviewerUpdateZone));
        EXPECT_TRUE(Contains(text, kSearchSliceZone)) << "the slices of the search run inside the task";
    }

}  // namespace BECore::Chess::Tests
