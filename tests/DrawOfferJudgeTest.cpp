#include <Chess/Engine/DrawOfferJudge.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>
#include <Common/EngineEnvironment.h>
#include <tests/TestEngines.h>

/// DrawOfferJudge with the real TaskManager and engines made in the test (no process). Every wait polls a condition with a time limit; none is a fixed sleep, and what must not block is
/// shown by a gate and Answered(), not by a clock
namespace BECore::Chess::Tests {

    namespace {

        constexpr int kManyUpdates = 1000;
        constexpr PieceColor kWhite = PieceColor::White;
        constexpr PieceColor kBlack = PieceColor::Black;

        const EngineError kBroken{EngineErrorKind::ProcessDied, "the engine fell over"};

        AnalysisScore Cp(int value) {
            return AnalysisScore{AnalysisScoreKind::Centipawns, value};
        }

        AnalysisScore Mate(int moves) {
            return AnalysisScore{AnalysisScoreKind::Mate, moves};
        }

        bool Play(ChessGameModel& model, StringView uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        /// What a game loop does: Update every frame until a verdict comes; nothing when none does within the give-up limit
        Optional<DrawVerdict> AwaitVerdict(DrawOfferJudge& judge, const ChessGameModel& model) {
            Optional<DrawVerdict> verdict;
            WaitUntil([&] {
                verdict = judge.Update(model);
                return verdict.has_value();
            });
            return verdict;
        }

        /// Update many frames and say whether any of them gave a verdict
        bool GivesAVerdictWithin(DrawOfferJudge& judge, const ChessGameModel& model, int frames = kManyUpdates) {
            for (int frame = 0; frame < frames; ++frame) {
                if (judge.Update(model).has_value()) {
                    return true;
                }
            }
            return false;
        }

        IntrusivePtr<AnalysingEngine> MakeEngine(AnalysisScore score) {
            return New<AnalysingEngine>(score);
        }

        /**
         * @brief Answers for the position after `n` moves: the score scripted for n (centipawns, from the side to move's view), 0 where none is. Gated and deaf to its token on demand, so a
         * test can hold two searches of different positions at once and tell whose answer reached the judge.
         */
        class PlyEngine final : public IChessEngine {
        public:
            explicit PlyEngine(Vector<int> scores) : _scores(std::move(scores)) {}

            void Gate() {
                _gated = true;
            }

            void Open() {
                _open.store(true);
            }

            std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken&) override {
                return FirstLegalMove(request);
            }

            std::expected<Analysis, EngineError> Analyse(const SearchRequest& request, const CancellationToken&) override {
                _calls.fetch_add(1);
                if (_gated) {
                    WaitUntil([&] { return _open.load(); }, kGateGiveUp);
                }
                const std::expected<Move, EngineError> first = FirstLegalMove(request);
                if (!first) {
                    _answered.fetch_add(1);
                    return std::unexpected(first.error());
                }
                Analysis analysis;
                const size_t ply = request.moves.size();
                analysis.lines.push_back(AnalysisLine{*first, Cp(ply < _scores.size() ? _scores[ply] : 0), 5});
                _answered.fetch_add(1);
                return analysis;
            }

            [[nodiscard]] int Calls() const {
                return _calls.load();
            }

            [[nodiscard]] int Answered() const {
                return _answered.load();
            }

        private:
            Vector<int> _scores;
            bool _gated = false;
            std::atomic<bool> _open{false};
            std::atomic<int> _calls{0};
            std::atomic<int> _answered{0};
        };

        /// One offer at @p score from the side to move's view, judged to its end
        Optional<DrawVerdict> Judged(ChessGameModel& model, PieceColor program, AnalysisScore score) {
            const auto engine = MakeEngine(score);
            CountingAnalysisFactory factory{engine};
            DrawOfferJudge judge;
            judge.SetEngineFactory(factory.AsFactory());
            judge.Offer(model, program);
            return AwaitVerdict(judge, model);
        }

    }  // namespace

    class DrawOfferJudgeTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ::BECore::Tests::EngineEnvironment::EnsureCoreServices();  // TaskManager, which the judge's search runs on
        }
    };

    TEST_F(DrawOfferJudgeTest, AcceptsAtAnEvenScoreAndAsksTheOwnEngineOnceAtTheFixedDepth) {
        const auto engine = MakeEngine(Cp(0));
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(Play(model, "e7e5"));
        DrawOfferJudge judge;
        judge.SetEngineFactory(factory.AsFactory());

        EXPECT_EQ(judge.GetState(model), DrawOfferState::Idle);
        judge.Offer(model, kBlack);
        EXPECT_EQ(judge.GetState(model), DrawOfferState::Pending);
        EXPECT_TRUE(judge.IsThinking());
        const Optional<DrawVerdict> verdict = AwaitVerdict(judge, model);

        ASSERT_TRUE(verdict.has_value()) << DescribeError(judge.GetError()).c_str();
        EXPECT_EQ(*verdict, DrawVerdict::Accepted);
        EXPECT_EQ(judge.GetState(model), DrawOfferState::Idle) << "an accepted offer ends the game, there is nothing to show";
        EXPECT_FALSE(judge.IsThinking());
        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Engines()[0], "own") << "whatever the person picked";
        ASSERT_EQ(engine->Calls(), 1);
        const SearchRequest request = engine->LastRequest();
        EXPECT_EQ(request.startFen, model.GetStartFen());
        EXPECT_EQ(request.moves.size(), 2u) << "the position the offer was made in";
        EXPECT_EQ(request.limits.depth, kDrawOfferDepth);
        EXPECT_EQ(request.limits.moveTimeMs, 0) << "a fixed depth and no clock: the answer repeats";
        EXPECT_EQ(request.lines, 1);
        EXPECT_FALSE(GivesAVerdictWithin(judge, model)) << "the verdict comes once";
    }

    TEST_F(DrawOfferJudgeTest, TheThresholdDecidesForTheProgramWhoPlaysBlack) {
        // The person (White) is to move: the engine's score is theirs, so the program has the opposite
        for (const auto& [score, expected] :
             {Pair<int, DrawVerdict>{-kDrawAcceptCentipawns, DrawVerdict::Accepted}, Pair<int, DrawVerdict>{-kDrawAcceptCentipawns - 1, DrawVerdict::Declined},
              Pair<int, DrawVerdict>{0, DrawVerdict::Accepted}, Pair<int, DrawVerdict>{900, DrawVerdict::Accepted}, Pair<int, DrawVerdict>{-900, DrawVerdict::Declined}}) {
            ChessGameModel model;
            const Optional<DrawVerdict> verdict = Judged(model, kBlack, Cp(score));
            ASSERT_TRUE(verdict.has_value()) << score;
            EXPECT_EQ(*verdict, expected) << score;
        }
    }

    TEST_F(DrawOfferJudgeTest, ABlackPersonsOfferIsJudgedWithTheSignsTurned) {
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));  // Black, the person, is to move; the program plays White
        for (const auto& [score, expected] : {Pair<int, DrawVerdict>{-kDrawAcceptCentipawns, DrawVerdict::Accepted}, Pair<int, DrawVerdict>{-kDrawAcceptCentipawns - 1, DrawVerdict::Declined},
                                              Pair<int, DrawVerdict>{kDrawAcceptCentipawns + 1, DrawVerdict::Accepted}}) {
            const Optional<DrawVerdict> verdict = Judged(model, kWhite, Cp(score));
            ASSERT_TRUE(verdict.has_value()) << score;
            EXPECT_EQ(*verdict, expected) << score;
        }
    }

    TEST_F(DrawOfferJudgeTest, ABlackProgramJudgesWithTheSignsTurned) {
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(Play(model, "e7e5"));
        ASSERT_TRUE(Play(model, "g1f3"));  // Black is to move and it is the program: the offer is not allowed by the rule, but the judge's sums stay right for either side
        const Optional<DrawVerdict> better = Judged(model, kBlack, Cp(kDrawAcceptCentipawns + 1));
        const Optional<DrawVerdict> level = Judged(model, kBlack, Cp(kDrawAcceptCentipawns));
        ASSERT_TRUE(better.has_value());
        ASSERT_TRUE(level.has_value());
        EXPECT_EQ(*better, DrawVerdict::Declined) << "the side to move is the program, +51 for it";
        EXPECT_EQ(*level, DrawVerdict::Accepted);
    }

    TEST_F(DrawOfferJudgeTest, AMateForTheProgramDeclinesAndAMateAgainstItAccepts) {
        ChessGameModel model;  // White, the person, to move; the program plays Black
        const Optional<DrawVerdict> personMates = Judged(model, kBlack, Mate(3));
        const Optional<DrawVerdict> personIsMated = Judged(model, kBlack, Mate(-3));
        ASSERT_TRUE(personMates.has_value());
        ASSERT_TRUE(personIsMated.has_value());
        EXPECT_EQ(*personMates, DrawVerdict::Accepted);
        EXPECT_EQ(*personIsMated, DrawVerdict::Declined);
    }

    TEST_F(DrawOfferJudgeTest, TheDeclinedOfferIsShownForItsVersionOnlyAndNotAskedAgain) {
        const auto engine = MakeEngine(Cp(-300));
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        DrawOfferJudge judge;
        judge.SetEngineFactory(factory.AsFactory());

        judge.Offer(model, kBlack);
        const Optional<DrawVerdict> verdict = AwaitVerdict(judge, model);
        ASSERT_TRUE(verdict.has_value());
        EXPECT_EQ(*verdict, DrawVerdict::Declined);
        EXPECT_EQ(judge.GetState(model), DrawOfferState::Declined);
        EXPECT_FALSE(judge.GetError().has_value()) << "a refusal is an answer, not an error";
        EXPECT_FALSE(GivesAVerdictWithin(judge, model));
        EXPECT_EQ(judge.GetState(model), DrawOfferState::Declined) << "it stays until the game moves";

        judge.Offer(model, kBlack);
        EXPECT_EQ(judge.GetState(model), DrawOfferState::Declined) << "the same version is not offered twice";
        EXPECT_FALSE(GivesAVerdictWithin(judge, model));
        EXPECT_EQ(engine->Calls(), 1);

        ASSERT_TRUE(Play(model, "e2e4"));
        EXPECT_EQ(judge.GetState(model), DrawOfferState::Idle) << "read before the next Update()";
        EXPECT_FALSE(GivesAVerdictWithin(judge, model));
        EXPECT_EQ(judge.GetState(model), DrawOfferState::Idle);
    }

    TEST_F(DrawOfferJudgeTest, AFinishedGameIsNotOffered) {
        const auto engine = MakeEngine(Cp(0));
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(model.Resign(kWhite).has_value());
        DrawOfferJudge judge;
        judge.SetEngineFactory(factory.AsFactory());

        judge.Offer(model, kBlack);

        EXPECT_EQ(judge.GetState(model), DrawOfferState::Idle);
        EXPECT_EQ(factory.Calls(), 0);
        EXPECT_EQ(judge.GetRevision(), 0u);
        EXPECT_FALSE(GivesAVerdictWithin(judge, model));
    }

    TEST_F(DrawOfferJudgeTest, AMoveDuringTheSearchDropsTheAnswerOfAnEngineThatIgnoresItsToken) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeEngine(Cp(0));
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        DrawOfferJudge judge;
        judge.SetEngineFactory(factory.AsFactory());
        judge.Offer(model, kBlack);
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));

        ASSERT_TRUE(Play(model, "e2e4"));
        EXPECT_FALSE(judge.Update(model).has_value());
        EXPECT_EQ(judge.GetState(model), DrawOfferState::Idle);
        EXPECT_FALSE(judge.IsThinking()) << "the stale search is cancelled";
        EXPECT_EQ(engine->Answered(), 0);
        engine->Open();  // the old worker answers now, for the old position
        ASSERT_TRUE(WaitUntil([&] { return engine->Answered() == 1; }));

        EXPECT_FALSE(GivesAVerdictWithin(judge, model)) << "the late answer reaches nobody";
        EXPECT_EQ(judge.GetState(model), DrawOfferState::Idle);
        EXPECT_FALSE(judge.GetError().has_value());
        EXPECT_EQ(engine->Calls(), 1) << "nothing asks again by itself";
    }

    TEST_F(DrawOfferJudgeTest, AMoveDuringTheSearchCancelsAnEngineThatHonorsItsToken) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeEngine(Cp(0));
        engine->Gate(true);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        DrawOfferJudge judge;
        judge.SetEngineFactory(factory.AsFactory());
        judge.Offer(model, kBlack);
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));

        ASSERT_TRUE(Play(model, "e2e4"));
        EXPECT_FALSE(judge.Update(model).has_value());

        ASSERT_TRUE(WaitUntil([&] { return engine->WasCancelled(); })) << "the token reached the engine";
        ASSERT_TRUE(WaitUntil([&] { return engine->Answered() == 1; }));
        EXPECT_FALSE(GivesAVerdictWithin(judge, model));
        EXPECT_FALSE(judge.GetError().has_value()) << "a search the judge cancelled is not an error";
        EXPECT_EQ(judge.GetState(model), DrawOfferState::Idle);
    }

    TEST_F(DrawOfferJudgeTest, TheAnswerOfAnOlderOfferNeverReachesTheNewOne) {
        SKIP_WITHOUT_WORKER_THREADS();
        // The person is White. At the start the engine says -300 for them (+300 for the program: declined); after e2e4 e7e5 it says 0 (accepted)
        const auto engine = New<PlyEngine>(Vector<int>{-300, 0, 0});
        engine->Gate();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        DrawOfferJudge judge;
        judge.SetEngineFactory(factory.AsFactory());
        judge.Offer(model, kBlack);
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));

        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(Play(model, "e7e5"));
        judge.Offer(model, kBlack);
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 2; }));
        engine->Open();
        ASSERT_TRUE(WaitUntil([&] { return engine->Answered() == 2; }));
        const Optional<DrawVerdict> verdict = AwaitVerdict(judge, model);

        ASSERT_TRUE(verdict.has_value());
        EXPECT_EQ(*verdict, DrawVerdict::Accepted) << "the answer of the first position (declined) was dropped";
        EXPECT_EQ(factory.Calls(), 1) << "the engine is made once and kept";
    }

    TEST_F(DrawOfferJudgeTest, AnEngineThatFailsDeclinesOnceAndIsNotAskedAgain) {
        const auto engine = MakeEngine(Cp(0));
        engine->FailWith(kBroken);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        DrawOfferJudge judge;
        judge.SetEngineFactory(factory.AsFactory());

        judge.Offer(model, kBlack);
        const Optional<DrawVerdict> verdict = AwaitVerdict(judge, model);

        ASSERT_TRUE(verdict.has_value());
        EXPECT_EQ(*verdict, DrawVerdict::Declined) << "a draw the program could not judge is not given";
        ASSERT_TRUE(judge.GetError().has_value());
        EXPECT_EQ(judge.GetError()->kind, EngineErrorKind::ProcessDied);
        EXPECT_EQ(judge.GetState(model), DrawOfferState::Declined);
        EXPECT_FALSE(GivesAVerdictWithin(judge, model));
        judge.Offer(model, kBlack);
        EXPECT_FALSE(GivesAVerdictWithin(judge, model));
        EXPECT_EQ(engine->Calls(), 1) << "no try per frame and none by itself";

        ASSERT_TRUE(Play(model, "e2e4"));
        EXPECT_FALSE(GivesAVerdictWithin(judge, model, 10));
        EXPECT_FALSE(judge.GetError().has_value()) << "the error goes with the version";
    }

    TEST_F(DrawOfferJudgeTest, AnEngineThatCannotBeMadeDeclinesAtOnceAndOnce) {
        CountingAnalysisFactory factory{EngineError{EngineErrorKind::LaunchFailed, "no engine here"}};
        ChessGameModel model;
        DrawOfferJudge judge;
        judge.SetEngineFactory(factory.AsFactory());

        judge.Offer(model, kBlack);

        EXPECT_EQ(judge.GetState(model), DrawOfferState::Declined) << "no worker is needed to say no";
        EXPECT_FALSE(judge.IsThinking());
        ASSERT_TRUE(judge.GetError().has_value());
        EXPECT_EQ(judge.GetError()->kind, EngineErrorKind::LaunchFailed);
        const Optional<DrawVerdict> verdict = judge.Update(model);
        ASSERT_TRUE(verdict.has_value());
        EXPECT_EQ(*verdict, DrawVerdict::Declined);
        EXPECT_FALSE(GivesAVerdictWithin(judge, model));
        judge.Offer(model, kBlack);
        EXPECT_EQ(factory.Calls(), 1) << "the version was offered; a try per frame would show here";

        ASSERT_TRUE(Play(model, "e2e4"));
        judge.Offer(model, kWhite);
        EXPECT_EQ(factory.Calls(), 2) << "the next version is asked again";
    }

    TEST_F(DrawOfferJudgeTest, AFactoryThatMakesNoEngineIsAnError) {
        CountingAnalysisFactory factory{ChessEnginePtr{}};
        ChessGameModel model;
        DrawOfferJudge judge;
        judge.SetEngineFactory(factory.AsFactory());

        judge.Offer(model, kBlack);

        EXPECT_EQ(judge.GetState(model), DrawOfferState::Declined);
        ASSERT_TRUE(judge.GetError().has_value());
        EXPECT_EQ(judge.GetError()->kind, EngineErrorKind::LaunchFailed);
    }

    TEST_F(DrawOfferJudgeTest, AnEmptyFactoryMakesTheOwnEngine) {
        ChessGameModel model;
        DrawOfferJudge judge;
        judge.SetEngineFactory({});

        judge.Offer(model, kBlack);
        const Optional<DrawVerdict> verdict = AwaitVerdict(judge, model);

        ASSERT_TRUE(verdict.has_value());
        EXPECT_FALSE(judge.GetError().has_value()) << DescribeError(judge.GetError()).c_str();
        EXPECT_EQ(*verdict, DrawVerdict::Accepted) << "the opening position is level at depth 5";
    }

    TEST_F(DrawOfferJudgeTest, ASecondOfferForTheSameVersionDoesNothing) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeEngine(Cp(0));
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        DrawOfferJudge judge;
        judge.SetEngineFactory(factory.AsFactory());
        judge.Offer(model, kBlack);
        const uint64_t revision = judge.GetRevision();

        judge.Offer(model, kBlack);
        judge.Offer(model, kBlack);

        EXPECT_EQ(judge.GetRevision(), revision);
        engine->Open();
        const Optional<DrawVerdict> verdict = AwaitVerdict(judge, model);
        ASSERT_TRUE(verdict.has_value());
        EXPECT_EQ(engine->Calls(), 1) << "one search for one version";
    }

    TEST_F(DrawOfferJudgeTest, UpdateDoesNotWaitForTheEngine) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeEngine(Cp(0));
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        DrawOfferJudge judge;
        judge.SetEngineFactory(factory.AsFactory());
        judge.Offer(model, kBlack);

        EXPECT_FALSE(GivesAVerdictWithin(judge, model, 200)) << "the gate is opened only after this: an Update that waited for it would end in the give-up of the gate";

        EXPECT_TRUE(judge.IsThinking());
        EXPECT_EQ(judge.GetState(model), DrawOfferState::Pending);
        engine->Open();
        const Optional<DrawVerdict> verdict = AwaitVerdict(judge, model);
        ASSERT_TRUE(verdict.has_value());
        EXPECT_EQ(*verdict, DrawVerdict::Accepted);
    }

    TEST_F(DrawOfferJudgeTest, ShutdownDoesNotWaitForASearchThatIgnoresItsToken) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeEngine(Cp(0));
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        DrawOfferJudge judge;
        judge.SetEngineFactory(factory.AsFactory());
        judge.Offer(model, kBlack);
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));

        judge.Shutdown();

        EXPECT_EQ(engine->Answered(), 0) << "a join would have waited for the gate";
        EXPECT_EQ(judge.GetState(model), DrawOfferState::Idle);
        EXPECT_FALSE(judge.IsThinking());
        engine->Open();
        ASSERT_TRUE(WaitUntil([&] { return engine->Answered() == 1; }));
        EXPECT_FALSE(GivesAVerdictWithin(judge, model));

        judge.Offer(model, kBlack);
        EXPECT_EQ(factory.Calls(), 2) << "Shutdown() dropped the engine too";
        ASSERT_TRUE(AwaitVerdict(judge, model).has_value());
    }

    TEST_F(DrawOfferJudgeTest, TheDestructorDoesNotWaitForASearchThatIgnoresItsToken) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeEngine(Cp(0));
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        {
            DrawOfferJudge judge;
            judge.SetEngineFactory(factory.AsFactory());
            judge.Offer(model, kBlack);
            ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));
        }

        EXPECT_EQ(engine->Answered(), 0) << "a join would have waited for the gate";
        engine->Open();  // the worker finishes into a state nobody reads, and must not touch the dead judge
        ASSERT_TRUE(WaitUntil([&] { return engine->Answered() == 1; }));
    }

    TEST_F(DrawOfferJudgeTest, CancelDropsTheOfferAndTheLateAnswer) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeEngine(Cp(0));
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        DrawOfferJudge judge;
        judge.SetEngineFactory(factory.AsFactory());
        judge.Offer(model, kBlack);
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));

        judge.Cancel();

        EXPECT_EQ(judge.GetState(model), DrawOfferState::Idle);
        EXPECT_EQ(engine->Answered(), 0);
        engine->Open();
        ASSERT_TRUE(WaitUntil([&] { return engine->Answered() == 1; }));
        EXPECT_FALSE(GivesAVerdictWithin(judge, model));
        judge.Offer(model, kBlack);
        EXPECT_EQ(judge.GetState(model), DrawOfferState::Pending) << "a cancelled offer may be made again at the same version";
        ASSERT_TRUE(AwaitVerdict(judge, model).has_value());
        EXPECT_EQ(factory.Calls(), 1) << "Cancel() keeps the engine";
    }

    TEST_F(DrawOfferJudgeTest, TheRevisionGrowsWithAnOfferAnAnswerAndADropAndNeverWithASearchThatStarts) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeEngine(Cp(-300));
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        DrawOfferJudge judge;
        judge.SetEngineFactory(factory.AsFactory());
        EXPECT_EQ(judge.GetRevision(), 0u);

        judge.Offer(model, kBlack);
        const uint64_t offered = judge.GetRevision();
        EXPECT_GT(offered, 0u);
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));
        EXPECT_FALSE(GivesAVerdictWithin(judge, model, 100));
        EXPECT_EQ(judge.GetRevision(), offered) << "a search that starts is not shown";
        engine->Open();
        ASSERT_TRUE(AwaitVerdict(judge, model).has_value());
        const uint64_t answered = judge.GetRevision();
        EXPECT_GT(answered, offered);
        EXPECT_FALSE(GivesAVerdictWithin(judge, model));
        EXPECT_EQ(judge.GetRevision(), answered) << "and nothing else moves it";

        ASSERT_TRUE(Play(model, "e2e4"));
        EXPECT_FALSE(judge.Update(model).has_value());
        EXPECT_GT(judge.GetRevision(), answered) << "a display must see the refusal go";
        const uint64_t dropped = judge.GetRevision();
        EXPECT_FALSE(GivesAVerdictWithin(judge, model));
        EXPECT_EQ(judge.GetRevision(), dropped);
    }

}  // namespace BECore::Chess::Tests
