#include "StockfishUtil.h"
#include "TestEngines.h"

#include <Chess/Engine/OpponentLevels.h>
#include <Chess/Engine/PositionAnalyst.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>
#include <Common/EngineEnvironment.h>
#include <chrono>

/// PositionAnalyst with the real TaskManager and engines made in the test (TestEngines.h, no process), plus the OwnEngine for the mate in one and the default factory, and a
/// real Stockfish when there is one. Every wait polls a condition with a time limit; none is a fixed sleep
namespace BECore::Chess::Tests {

    namespace {

        using std::chrono::milliseconds;
        using Clock = std::chrono::steady_clock;

        constexpr milliseconds kUpdateBudget{2000};  // an Update that waits for the gated engine would take 10 s (kGateGiveUp)
        constexpr int kManyUpdates = 1000;
        constexpr AnalysisScore kScore{AnalysisScoreKind::Centipawns, 35};
        constexpr eastl::string_view kMateInOneFen = "6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1";  // Ra8#
        constexpr eastl::string_view kMatedBlackFen = "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1";

        bool Play(ChessGameModel& model, eastl::string_view uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        /// What a game loop does: Update every frame until @p done
        template <typename Condition>
        bool UpdateUntil(PositionAnalyst& analyst, const ChessGameModel& model, OpponentEngine picked, Condition done) {
            return WaitUntil([&] {
                analyst.Update(model, picked);
                return done();
            });
        }

        void UpdateMany(PositionAnalyst& analyst, const ChessGameModel& model, OpponentEngine picked, int times = kManyUpdates) {
            for (int frame = 0; frame < times; ++frame) {
                analyst.Update(model, picked);
            }
        }

        eastl::shared_ptr<AnalysingEngine> MakeEngine(AnalysisScore score = kScore, std::optional<Move> move = std::nullopt) {
            return eastl::make_shared<AnalysingEngine>(score, move);
        }

        /// An engine that only plays: it keeps the default Analyse() of the interface
        class PlayingEngine final : public IChessEngine {
        public:
            std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken&) override {
                return FirstLegalMove(request);
            }
        };

    }  // namespace

    class PositionAnalystTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ::BECore::Tests::EngineEnvironment::EnsureCoreServices();  // TaskManager
        }
    };

    TEST_F(PositionAnalystTest, NothingStartsWhileNothingIsSwitchedOnOrAsked) {
        const auto engine = MakeEngine();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());

        UpdateMany(analyst, model, OpponentEngine::Stockfish);
        ASSERT_TRUE(Play(model, "e2e4"));
        UpdateMany(analyst, model, OpponentEngine::Own);

        EXPECT_EQ(factory.Calls(), 0);
        EXPECT_EQ(engine->Calls(), 0);
        EXPECT_FALSE(analyst.IsThinking());
        EXPECT_FALSE(analyst.GetError().has_value());
        EXPECT_EQ(analyst.GetRevision(), 0u);
        EXPECT_FALSE(analyst.GetEvaluation(model).has_value());
        EXPECT_FALSE(analyst.GetHint(model).has_value());
    }

    TEST_F(PositionAnalystTest, TheEvaluationOnGivesTheLineOfThisVersion) {
        const auto engine = MakeEngine(kScore, Mv("e2e4"));
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());

        analyst.SetEvaluation(true);
        EXPECT_TRUE(analyst.IsEvaluationOn());
        EXPECT_FALSE(analyst.GetEvaluation(model).has_value()) << "nothing before the engine has answered";
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetEvaluation(model).has_value(); })) << DescribeError(analyst.GetError()).c_str();

        const std::optional<AnalysisLine> line = analyst.GetEvaluation(model);
        EXPECT_EQ(line->score, kScore);
        EXPECT_EQ(line->move, Mv("e2e4"));
        EXPECT_EQ(line->depth, 7);
        EXPECT_FALSE(analyst.GetHint(model).has_value()) << "no hint was asked for";
        EXPECT_FALSE(analyst.IsThinking());
        UpdateMany(analyst, model, OpponentEngine::Stockfish);
        EXPECT_EQ(engine->Calls(), 1) << "the answer is kept for the version, no search per frame";
        EXPECT_EQ(factory.Calls(), 1);
    }

    TEST_F(PositionAnalystTest, AHintAskedOnTheStartPositionGivesTheScriptedMove) {
        const auto engine = MakeEngine(kScore, Mv("d2d4"));
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());

        ASSERT_TRUE(analyst.RequestHint(model).has_value());
        EXPECT_TRUE(analyst.IsHintRequested(model));
        EXPECT_FALSE(analyst.GetHint(model).has_value());
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetHint(model).has_value(); })) << DescribeError(analyst.GetError()).c_str();

        EXPECT_EQ(analyst.GetHint(model)->move, Mv("d2d4"));
        EXPECT_FALSE(analyst.GetEvaluation(model).has_value()) << "the evaluation is off, so it is not shown, though the search had a score";
    }

    TEST_F(PositionAnalystTest, AMoveDropsTheHintAndTheEvaluationGetsANewSearch) {
        const auto engine = MakeEngine();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        analyst.SetEvaluation(true);
        ASSERT_TRUE(analyst.RequestHint(model).has_value());
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetHint(model).has_value(); }));
        EXPECT_TRUE(analyst.GetEvaluation(model).has_value());

        ASSERT_TRUE(Play(model, "e2e4"));
        EXPECT_FALSE(analyst.GetHint(model).has_value()) << "the hint belongs to the position it was asked at";
        EXPECT_FALSE(analyst.GetEvaluation(model).has_value());
        EXPECT_FALSE(analyst.IsHintRequested(model));
        analyst.Update(model, OpponentEngine::Stockfish);
        EXPECT_FALSE(analyst.IsHintRequested(model));
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetEvaluation(model).has_value(); }));

        EXPECT_FALSE(analyst.GetHint(model).has_value()) << "the hint is not asked again by itself";
        EXPECT_EQ(engine->Calls(), 2);
        EXPECT_EQ(engine->LastRequest().moves.size(), 1u) << "the second search is about the position after the move";
        EXPECT_EQ(factory.Calls(), 1) << "the engine is made once";
    }

    TEST_F(PositionAnalystTest, TheOldAnswerOfAnEngineThatIgnoresItsTokenIsDropped) {
        const auto engine = MakeEngine();
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        analyst.SetEvaluation(true);
        analyst.Update(model, OpponentEngine::Stockfish);
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));
        EXPECT_TRUE(analyst.IsThinking());

        ASSERT_TRUE(Play(model, "e2e4"));
        analyst.Update(model, OpponentEngine::Stockfish);
        EXPECT_FALSE(analyst.IsThinking()) << "the stale search is cancelled";
        EXPECT_FALSE(analyst.GetEvaluation(model).has_value());
        engine->Open();  // the old worker answers now, for the position before the move
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetEvaluation(model).has_value(); })) << DescribeError(analyst.GetError()).c_str();

        const std::optional<AnalysisLine> line = analyst.GetEvaluation(model);
        EXPECT_TRUE(MoveGenerator::ParseUci(model.GetPosition(), line->move.ToUci()).has_value()) << "a move of Black, from the new search, not the White move of the old one";
        EXPECT_EQ(engine->Calls(), 2);
        EXPECT_EQ(engine->LastRequest().moves.size(), 1u);
    }

    TEST_F(PositionAnalystTest, UpdateDoesNotWaitForTheEngine) {
        const auto engine = MakeEngine();
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        analyst.SetEvaluation(true);

        const Clock::time_point begin = Clock::now();
        UpdateMany(analyst, model, OpponentEngine::Stockfish, 200);
        const Clock::duration took = Clock::now() - begin;

        EXPECT_LT(took, kUpdateBudget);
        EXPECT_TRUE(analyst.IsThinking());
        EXPECT_FALSE(analyst.GetEvaluation(model).has_value());
        engine->Open();
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetEvaluation(model).has_value(); }));
        EXPECT_EQ(engine->Calls(), 1);
    }

    TEST_F(PositionAnalystTest, AHintOnAFinishedGameIsRefusedAndTheEngineIsNeverAsked) {
        const auto engine = MakeEngine();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        model.SetPosition(PositionFromFen(kMatedBlackFen));
        ASSERT_TRUE(model.IsGameOver());
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());

        const std::expected<void, HintRefusal> hint = analyst.RequestHint(model);
        analyst.SetEvaluation(true);
        UpdateMany(analyst, model, OpponentEngine::Stockfish);

        ASSERT_FALSE(hint.has_value());
        EXPECT_EQ(hint.error(), HintRefusal::GameOver);
        EXPECT_FALSE(analyst.IsHintRequested(model));
        EXPECT_EQ(engine->Calls(), 0) << "not even the evaluation asks about a finished game";
        EXPECT_EQ(factory.Calls(), 0);
        EXPECT_FALSE(analyst.GetError().has_value());
    }

    TEST_F(PositionAnalystTest, TheHintRuleIsOnePureFunction) {
        ChessGameModel model;  // White to move
        EXPECT_EQ(HintRefusalFor(model, std::nullopt), std::nullopt) << "two people: either side may ask";
        EXPECT_EQ(HintRefusalFor(model, PieceColor::Black), std::nullopt) << "the program plays Black, it is the person's turn";
        EXPECT_EQ(HintRefusalFor(model, PieceColor::White), HintRefusal::ProgramsTurn);

        ASSERT_TRUE(Play(model, "e2e4"));  // Black to move
        EXPECT_EQ(HintRefusalFor(model, std::nullopt), std::nullopt);
        EXPECT_EQ(HintRefusalFor(model, PieceColor::White), std::nullopt);
        EXPECT_EQ(HintRefusalFor(model, PieceColor::Black), HintRefusal::ProgramsTurn);

        ChessGameModel finished;
        finished.SetPosition(PositionFromFen(kMatedBlackFen));
        EXPECT_EQ(HintRefusalFor(finished, std::nullopt), HintRefusal::GameOver);
        EXPECT_EQ(HintRefusalFor(finished, PieceColor::Black), HintRefusal::GameOver) << "the end is named before the turn";
        EXPECT_EQ(HintRefusalFor(finished, PieceColor::White), HintRefusal::GameOver);
    }

    TEST_F(PositionAnalystTest, AFailingEngineIsOneErrorForTheVersionNotATryPerFrame) {
        const auto engine = MakeEngine();
        engine->FailWith(EngineError{EngineErrorKind::ProcessDied, "the engine died"});
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        analyst.SetEvaluation(true);

        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetError().has_value(); }));
        EXPECT_EQ(analyst.GetError()->kind, EngineErrorKind::ProcessDied);
        EXPECT_EQ(analyst.GetError()->detail, "the engine died");
        UpdateMany(analyst, model, OpponentEngine::Stockfish);
        EXPECT_EQ(engine->Calls(), 1) << "no retry while the error stands";
        EXPECT_FALSE(analyst.GetEvaluation(model).has_value());

        ASSERT_TRUE(Play(model, "e2e4"));
        analyst.Update(model, OpponentEngine::Stockfish);
        EXPECT_FALSE(analyst.GetError().has_value()) << "an error goes with the next version";
        EXPECT_TRUE(analyst.IsThinking()) << "and the engine is asked about the new position";
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetError().has_value(); }));
        EXPECT_EQ(engine->Calls(), 2);
    }

    TEST_F(PositionAnalystTest, ANewHintRequestIsTheRetryAfterAnError) {
        const auto engine = MakeEngine();
        engine->FailWith(EngineError{EngineErrorKind::Timeout, "slow"});
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(analyst.RequestHint(model).has_value());
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetError().has_value(); }));
        UpdateMany(analyst, model, OpponentEngine::Stockfish);
        ASSERT_EQ(engine->Calls(), 1);

        ASSERT_TRUE(analyst.RequestHint(model).has_value());
        EXPECT_FALSE(analyst.GetError().has_value()) << "the request clears the error";
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetError().has_value(); }));
        EXPECT_EQ(engine->Calls(), 2);
        EXPECT_TRUE(analyst.IsHintRequested(model));
    }

    TEST_F(PositionAnalystTest, AFailingFactoryIsOneErrorForTheVersionToo) {
        CountingAnalysisFactory factory{EngineError{EngineErrorKind::LaunchFailed, "no engine here"}};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        analyst.SetEvaluation(true);

        UpdateMany(analyst, model, OpponentEngine::Stockfish);

        ASSERT_TRUE(analyst.GetError().has_value());
        EXPECT_EQ(analyst.GetError()->kind, EngineErrorKind::LaunchFailed);
        EXPECT_EQ(factory.Calls(), 1) << "asked once, not per frame";
        EXPECT_FALSE(analyst.IsThinking());

        ASSERT_TRUE(Play(model, "e2e4"));
        UpdateMany(analyst, model, OpponentEngine::Stockfish);
        EXPECT_EQ(factory.Calls(), 2) << "the next version tries again, once";
        ASSERT_TRUE(analyst.RequestHint(model).has_value());
        UpdateMany(analyst, model, OpponentEngine::Stockfish);
        EXPECT_EQ(factory.Calls(), 3) << "so does a new request";
    }

    TEST_F(PositionAnalystTest, AnEngineThatDoesNotAnalyseIsAnErrorNotACrash) {
        CountingAnalysisFactory factory{eastl::make_shared<PlayingEngine>()};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(analyst.RequestHint(model).has_value());

        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Own, [&] { return analyst.GetError().has_value(); }));

        EXPECT_EQ(analyst.GetError()->kind, EngineErrorKind::Unsupported);
        EXPECT_FALSE(analyst.GetHint(model).has_value());
    }

    TEST_F(PositionAnalystTest, AChangeOfThePickedEngineDropsTheResultAndMakesANewEngine) {
        const auto stockfish = MakeEngine({AnalysisScoreKind::Centipawns, 10});
        const auto own = MakeEngine({AnalysisScoreKind::Centipawns, 20});
        CountingAnalysisFactory factory{stockfish, own};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        analyst.SetEvaluation(true);
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetEvaluation(model).has_value(); }));
        EXPECT_EQ(analyst.GetEvaluation(model)->score.value, 10);

        analyst.Update(model, OpponentEngine::Own);
        EXPECT_FALSE(analyst.GetEvaluation(model).has_value()) << "the answer of the other engine is not shown";
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Own, [&] { return analyst.GetEvaluation(model).has_value(); }));

        EXPECT_EQ(analyst.GetEvaluation(model)->score.value, 20);
        ASSERT_EQ(factory.Calls(), 2);
        EXPECT_EQ(factory.Engines()[0], "stockfish");
        EXPECT_EQ(factory.Engines()[1], "own");
        EXPECT_EQ(stockfish->Calls(), 1);
        EXPECT_EQ(own->Calls(), 1);
    }

    TEST_F(PositionAnalystTest, SetPickedDropsTheOldEnginesResultErrorAndSearchAtOnce) {
        const auto stockfish = MakeEngine({AnalysisScoreKind::Centipawns, 10});
        const auto own = MakeEngine({AnalysisScoreKind::Centipawns, 20});
        CountingAnalysisFactory factory{stockfish, own};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        analyst.SetEvaluation(true);
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetEvaluation(model).has_value(); }));

        // The caller that changes the pick between two frames tells the analyst, and the reads are right with no Update() in between
        const uint64_t revision = analyst.GetRevision();
        analyst.SetPicked(OpponentEngine::Own);
        EXPECT_FALSE(analyst.GetEvaluation(model).has_value());
        EXPECT_GT(analyst.GetRevision(), revision) << "what was shown went";
        analyst.SetPicked(OpponentEngine::Own);
        EXPECT_EQ(analyst.GetRevision(), revision + 1) << "the same pick again does nothing";

        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Own, [&] { return analyst.GetEvaluation(model).has_value(); }));
        EXPECT_EQ(analyst.GetEvaluation(model)->score.value, 20);
        ASSERT_EQ(factory.Calls(), 2);
        EXPECT_EQ(factory.Engines()[1], "own");
    }

    TEST_F(PositionAnalystTest, SetPickedCancelsTheSearchOfTheOtherEngineAndDropsItsError) {
        const auto stockfish = MakeEngine({AnalysisScoreKind::Centipawns, 10});
        stockfish->Gate(true);
        const auto own = MakeEngine({AnalysisScoreKind::Centipawns, 20});
        own->FailWith(EngineError{EngineErrorKind::Timeout, "slow"});
        CountingAnalysisFactory factory{stockfish, own};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        analyst.SetEvaluation(true);
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return stockfish->Calls() == 1; }));
        ASSERT_TRUE(analyst.IsThinking());

        analyst.SetPicked(OpponentEngine::Own);
        EXPECT_FALSE(analyst.IsThinking()) << "the search of the other engine is cancelled at once";
        ASSERT_TRUE(WaitUntil([&] { return stockfish->WasCancelled(); }));

        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Own, [&] { return analyst.GetError().has_value(); }));
        analyst.SetPicked(OpponentEngine::Stockfish);
        EXPECT_FALSE(analyst.GetError().has_value()) << "the error of the engine that is no longer picked goes at once";
    }

    TEST_F(PositionAnalystTest, AnAnswerOfTheOldEngineIsDroppedWhenThePickChangesWhileItThinks) {
        const auto stockfish = MakeEngine({AnalysisScoreKind::Centipawns, 10});
        stockfish->Gate(false);
        const auto own = MakeEngine({AnalysisScoreKind::Centipawns, 20});
        CountingAnalysisFactory factory{stockfish, own};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        analyst.SetEvaluation(true);
        analyst.Update(model, OpponentEngine::Stockfish);
        ASSERT_TRUE(WaitUntil([&] { return stockfish->Calls() == 1; }));

        analyst.Update(model, OpponentEngine::Own);
        EXPECT_FALSE(analyst.IsThinking());
        stockfish->Open();
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Own, [&] { return analyst.GetEvaluation(model).has_value(); }));

        EXPECT_EQ(analyst.GetEvaluation(model)->score.value, 20) << "the late answer of Stockfish did not become the evaluation";
        EXPECT_EQ(own->Calls(), 1);
    }

    TEST_F(PositionAnalystTest, AHintAndTheEvaluationTogetherAreOneSearch) {
        const auto engine = MakeEngine();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        analyst.SetEvaluation(true);
        ASSERT_TRUE(analyst.RequestHint(model).has_value());

        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetHint(model).has_value(); }));
        UpdateMany(analyst, model, OpponentEngine::Stockfish);

        ASSERT_TRUE(analyst.GetEvaluation(model).has_value());
        EXPECT_EQ(analyst.GetEvaluation(model)->move, analyst.GetHint(model)->move);
        EXPECT_EQ(analyst.GetEvaluation(model)->score, analyst.GetHint(model)->score);
        EXPECT_EQ(engine->Calls(), 1);
    }

    TEST_F(PositionAnalystTest, AHintAskedAfterTheEvaluationHasComeNeedsNoNewSearch) {
        const auto engine = MakeEngine();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        analyst.SetEvaluation(true);
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetEvaluation(model).has_value(); }));

        ASSERT_TRUE(analyst.RequestHint(model).has_value());

        EXPECT_TRUE(analyst.GetHint(model).has_value()) << "the answer for this very position is held";
        UpdateMany(analyst, model, OpponentEngine::Stockfish);
        EXPECT_EQ(engine->Calls(), 1);
    }

    TEST_F(PositionAnalystTest, SwitchingTheEvaluationOffStopsTheSearchNobodyWantsAnyMore) {
        const auto engine = MakeEngine();
        engine->Gate(true);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        analyst.SetEvaluation(true);
        analyst.Update(model, OpponentEngine::Stockfish);
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));

        analyst.SetEvaluation(false);
        analyst.Update(model, OpponentEngine::Stockfish);

        EXPECT_FALSE(analyst.IsThinking());
        ASSERT_TRUE(WaitUntil([&] { return engine->WasCancelled(); }));
        UpdateMany(analyst, model, OpponentEngine::Stockfish);
        EXPECT_EQ(engine->Calls(), 1);
        EXPECT_FALSE(analyst.GetError().has_value()) << "a search that was cancelled by the analyst is not an error";
    }

    TEST_F(PositionAnalystTest, CancelAndTheDestructorReturnAtOnceWithASearchThatIgnoresItsToken) {
        const auto engine = MakeEngine();
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        {
            PositionAnalyst analyst;
            analyst.SetEngineFactory(factory.AsFactory());
            analyst.SetEvaluation(true);
            analyst.Update(model, OpponentEngine::Stockfish);
            ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));

            const Clock::time_point begin = Clock::now();
            analyst.Cancel();
            EXPECT_FALSE(analyst.IsThinking());
            EXPECT_LT(Clock::now() - begin, kUpdateBudget);
            EXPECT_FALSE(analyst.GetEvaluation(model).has_value());

            // The switch stays: once the old worker has returned, the next Update asks again
            engine->Open();
            ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetEvaluation(model).has_value(); }));
            EXPECT_EQ(engine->Calls(), 2);
        }

        // And the destructor with a search under way
        const auto second = MakeEngine();
        second->Gate(false);
        CountingAnalysisFactory secondFactory{second};
        const Clock::time_point begin = Clock::now();
        {
            PositionAnalyst analyst;
            analyst.SetEngineFactory(secondFactory.AsFactory());
            analyst.SetEvaluation(true);
            analyst.Update(model, OpponentEngine::Stockfish);
            ASSERT_TRUE(WaitUntil([&] { return second->Calls() == 1; }));
        }
        EXPECT_LT(Clock::now() - begin, kUpdateBudget);
        second->Open();
        ASSERT_TRUE(WaitUntil([&] { return second->Answered() == 1; }));
    }

    TEST_F(PositionAnalystTest, ShutdownThenUpdateStartsNothing) {
        const auto engine = MakeEngine();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        analyst.SetEvaluation(true);
        ASSERT_TRUE(analyst.RequestHint(model).has_value());
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetHint(model).has_value(); }));

        analyst.Shutdown();

        EXPECT_FALSE(analyst.IsEvaluationOn());
        EXPECT_FALSE(analyst.IsHintRequested(model));
        EXPECT_FALSE(analyst.GetEvaluation(model).has_value());
        EXPECT_FALSE(analyst.GetHint(model).has_value());
        ASSERT_TRUE(Play(model, "e2e4"));
        UpdateMany(analyst, model, OpponentEngine::Stockfish);
        EXPECT_EQ(engine->Calls(), 1);
        EXPECT_FALSE(analyst.IsThinking());

        // A switch brings it back
        analyst.SetEvaluation(true);
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetEvaluation(model).has_value(); }));
        EXPECT_EQ(factory.Calls(), 2) << "the engine was dropped by the shutdown and is made again";
    }

    TEST_F(PositionAnalystTest, TheRevisionGrowsWithWhatADisplayShowsAndNeverGoesBack) {
        const auto engine = MakeEngine();
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        uint64_t last = analyst.GetRevision();
        const auto grew = [&] {
            const bool did = analyst.GetRevision() > last;
            last = analyst.GetRevision();
            return did;
        };
        const auto stayed = [&] {
            const bool did = analyst.GetRevision() == last;
            last = analyst.GetRevision();
            return did;
        };

        analyst.SetEvaluation(true);
        EXPECT_TRUE(grew()) << "a switch";
        analyst.SetEvaluation(true);
        EXPECT_TRUE(stayed()) << "the same switch again";
        ASSERT_TRUE(analyst.RequestHint(model).has_value());
        EXPECT_TRUE(grew()) << "a hint asked";
        ASSERT_TRUE(analyst.RequestHint(model).has_value());
        EXPECT_TRUE(stayed()) << "the same request again";

        analyst.Update(model, OpponentEngine::Stockfish);  // the search starts
        EXPECT_TRUE(stayed()) << "a search that starts";
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));
        engine->Open();
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetHint(model).has_value(); }));
        EXPECT_TRUE(grew()) << "a result that arrives";

        analyst.CancelHint();
        EXPECT_TRUE(grew()) << "a hint dropped";
        analyst.CancelHint();
        EXPECT_TRUE(stayed());

        engine->FailWith(EngineError{EngineErrorKind::Timeout, "slow"});
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetError().has_value(); }));
        EXPECT_TRUE(grew()) << "an error that appears";
        ASSERT_TRUE(Play(model, "e7e5"));
        analyst.Update(model, OpponentEngine::Stockfish);
        EXPECT_TRUE(grew()) << "an error that goes";
    }

    TEST_F(PositionAnalystTest, TheRequestCarriesTheLimitsOfTheStrongestLevelAndTheHistoryOfTheGame) {
        const auto engine = MakeEngine();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(Play(model, "e7e5"));
        PositionAnalyst analyst;
        analyst.SetEngineFactory(factory.AsFactory());
        analyst.SetEvaluation(true);

        for (const OpponentEngine picked : {OpponentEngine::Stockfish, OpponentEngine::Own}) {
            const int callsBefore = engine->Calls();
            ASSERT_TRUE(UpdateUntil(analyst, model, picked, [&] { return engine->Calls() > callsBefore; }));
            const SearchRequest request = engine->LastRequest();
            const SearchLimits expected = LimitsOf(picked, GetOpponentLevels().back());
            EXPECT_EQ(request.limits.moveTimeMs, expected.moveTimeMs) << OpponentEngineId(picked).data();
            EXPECT_EQ(request.limits.depth, expected.depth) << OpponentEngineId(picked).data();
            EXPECT_EQ(request.startFen, model.GetStartFen());
            ASSERT_EQ(request.moves.size(), 2u);
            EXPECT_EQ(request.moves[0].ToUci(), "e2e4");
            EXPECT_EQ(request.moves[1].ToUci(), "e7e5");
            ASSERT_TRUE(UpdateUntil(analyst, model, picked, [&] { return analyst.GetEvaluation(model).has_value(); }));
        }
        EXPECT_EQ(GetOpponentLevels().back().id, "max");
    }

    TEST_F(PositionAnalystTest, TheDefaultFactoryMakesTheOwnEngineWithoutAPath) {
        const std::expected<eastl::shared_ptr<IChessEngine>, EngineError> engine = MakeAnalysisEngine(OpponentEngine::Own, OpponentSettings{});

        ASSERT_TRUE(engine.has_value());
        ASSERT_NE(*engine, nullptr);
        const SearchRequest request = SearchRequest::FromPosition(PositionFromFen(kMateInOneFen), SearchLimits{0, 3});
        const std::expected<Analysis, EngineError> analysis = (*engine)->Analyse(request, CancellationToken::Create());
        ASSERT_TRUE(analysis.has_value());
        EXPECT_EQ(analysis->Best().move, Mv("a1a8"));
    }

    TEST_F(PositionAnalystTest, AMissingStockfishFileIsLaunchFailedNamingThePathAndNothingFallsBack) {
        OpponentSettings settings;
        settings.stockfishPath = "/no/such/file";

        const std::expected<eastl::shared_ptr<IChessEngine>, EngineError> engine = MakeAnalysisEngine(OpponentEngine::Stockfish, settings);

        ASSERT_FALSE(engine.has_value());
        EXPECT_EQ(engine.error().kind, EngineErrorKind::LaunchFailed);
        EXPECT_NE(engine.error().detail.find("/no/such/file"), eastl::string::npos) << engine.error().detail.c_str();
    }

    TEST_F(PositionAnalystTest, TheOwnEngineGivesTheMatingMoveAsTheHintAndMateInOne) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen(kMateInOneFen));
        PositionAnalyst analyst;  // the default factory
        analyst.SetEvaluation(true);
        ASSERT_TRUE(analyst.RequestHint(model).has_value());

        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Own, [&] { return analyst.GetHint(model).has_value(); })) << DescribeError(analyst.GetError()).c_str();

        const std::optional<AnalysisLine> hint = analyst.GetHint(model);
        EXPECT_EQ(hint->move, Mv("a1a8"));
        EXPECT_EQ(hint->score, (AnalysisScore{AnalysisScoreKind::Mate, 1}));
        EXPECT_EQ(analyst.GetEvaluation(model)->score, hint->score);
    }

    TEST_F(PositionAnalystTest, ARealStockfishGivesTheMatingMoveAsTheHint) {
        SKIP_WITHOUT_STOCKFISH(path);
        ChessGameModel model;
        model.SetPosition(PositionFromFen(kMateInOneFen));
        PositionAnalyst analyst;  // the default factory: the Stockfish that FindStockfish finds
        ASSERT_TRUE(analyst.RequestHint(model).has_value());

        ASSERT_TRUE(UpdateUntil(analyst, model, OpponentEngine::Stockfish, [&] { return analyst.GetHint(model).has_value() || analyst.GetError().has_value(); }));

        ASSERT_TRUE(analyst.GetHint(model).has_value()) << DescribeError(analyst.GetError()).c_str();
        EXPECT_EQ(analyst.GetHint(model)->move, Mv("a1a8"));
        EXPECT_EQ(analyst.GetHint(model)->score, (AnalysisScore{AnalysisScoreKind::Mate, 1}));
        analyst.Shutdown();
    }

}  // namespace BECore::Chess::Tests
