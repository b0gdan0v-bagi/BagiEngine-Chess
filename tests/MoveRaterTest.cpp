#include "TestEngines.h"

#include <Chess/Engine/MoveRater.h>
#include <Chess/Engine/OwnEngine.h>
#include <Chess/Engine/PositionAnalyst.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>
#include <Common/EngineEnvironment.h>
#include <chrono>

/// MoveRater with the real TaskManager and engines made in the test (no process), then the real OwnEngine at depth 5 on positions found by running it. Every wait polls a condition with a time
/// limit; none is a fixed sleep
namespace BECore::Chess::Tests {

    namespace {

        using std::chrono::milliseconds;
        using Clock = std::chrono::steady_clock;

        constexpr milliseconds kUpdateBudget{2000};  // an Update that waits for the gated engine would take 10 s (kGateGiveUp)
        constexpr int kManyUpdates = 1000;
        constexpr eastl::string_view kOneMoveFen = "k7/8/1K6/8/8/8/8/7R b - - 0 1";  // Black has one legal move, Kb8

        bool Play(ChessGameModel& model, eastl::string_view uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        /// A game from the opening: the moves of @p ucis in order
        bool PlayAll(ChessGameModel& model, std::initializer_list<eastl::string_view> ucis) {
            for (const eastl::string_view uci : ucis) {
                if (!Play(model, uci)) {
                    return false;
                }
            }
            return true;
        }

        /// What a game loop does: Update every frame until @p done
        template <typename Condition>
        bool UpdateUntil(MoveRater& rater, const ChessGameModel& model, Condition done) {
            return WaitUntil([&] {
                rater.Update(model);
                return done();
            });
        }

        void UpdateMany(MoveRater& rater, const ChessGameModel& model, int times = kManyUpdates) {
            for (int frame = 0; frame < times; ++frame) {
                rater.Update(model);
            }
        }

        /// Every move of the model is rated
        bool AllRated(const MoveRater& rater, const ChessGameModel& model) {
            const MoveRatings ratings = rater.GetRatings(model);
            return ratings.size() == model.GetMoves().size() && eastl::all_of(ratings.begin(), ratings.end(), [](const std::optional<MoveRating>& rating) { return rating.has_value(); });
        }

        bool RatedAt(const MoveRater& rater, const ChessGameModel& model, size_t ply) {
            const MoveRatings ratings = rater.GetRatings(model);
            return ply < ratings.size() && ratings[ply].has_value();
        }

        MoveRatingReason ReasonAt(const MoveRater& rater, const ChessGameModel& model, size_t ply) {
            return rater.GetRatings(model)[ply]->reason;
        }

        /**
         * @brief The engine of these tests: Analyse() answers, for the request of ply `n` (n moves in it), the move scripted for ply n as line 1, with a second line (the first other legal move) that
         * is `gap` centipawns lower; a position with no other legal move has one line, as an engine gives it. A ply with no scripted move gets the first legal move.
         *
         * It keeps every request in order, can hold its answer at a gate until Open() (honoring the CancellationToken or ignoring it), and can fail one ply or all. Configure it while no search runs;
         * the reads are thread safe (a search runs on a pool worker).
         */
        class PlyEngine final : public IChessEngine {
        public:
            explicit PlyEngine(eastl::vector<Move> best = {}, int gap = 25) : _best(std::move(best)), _gap(gap) {}

            void Gate(bool honorsToken) {
                _gated = true;
                _honorsToken = honorsToken;
            }

            void Open() {
                _open.store(true);
            }

            /// Every search fails with @p error, or only the one for the request of @p ply moves
            void FailWith(EngineError error, std::optional<size_t> ply = std::nullopt) {
                _error = std::move(error);
                _failPly = ply;
            }

            std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken&) override {
                return FirstLegalMove(request);
            }

            std::expected<Analysis, EngineError> Analyse(const SearchRequest& request, const CancellationToken& token) override {
                {
                    const std::lock_guard lock(_mutex);
                    _requests.push_back(request);
                }
                _calls.fetch_add(1);  // after the request, so a test that waits for the call finds it
                std::expected<Analysis, EngineError> answer = Answer(request, token);
                _answered.fetch_add(1);
                return answer;
            }

            [[nodiscard]] int Calls() const {
                return _calls.load();
            }

            [[nodiscard]] int Answered() const {
                return _answered.load();
            }

            [[nodiscard]] eastl::vector<SearchRequest> Requests() const {
                const std::lock_guard lock(_mutex);
                return _requests;
            }

            [[nodiscard]] bool WasCancelled() const {
                return _cancelled.load();
            }

        private:
            std::expected<Analysis, EngineError> Answer(const SearchRequest& request, const CancellationToken& token) {
                if (_gated) {
                    const bool released = WaitUntil([&] { return _open.load() || (_honorsToken && token.IsCancelled()); }, kGateGiveUp);
                    if (_honorsToken && token.IsCancelled()) {
                        _cancelled.store(true);
                        return std::unexpected(EngineError{EngineErrorKind::Cancelled, "the search was cancelled at the gate"});
                    }
                    if (!released) {
                        return std::unexpected(EngineError{EngineErrorKind::Timeout, "the gate was never opened"});
                    }
                }
                if (_error && (!_failPly || *_failPly == request.moves.size())) {
                    return std::unexpected(*_error);
                }
                const std::expected<Position, EngineError> position = request.FinalPosition();
                if (!position) {
                    return std::unexpected(position.error());
                }
                const MoveList legal = MoveGenerator::GenerateLegal(*position);
                if (legal.empty()) {
                    return std::unexpected(EngineError{EngineErrorKind::NoMove, "the game is over"});
                }
                const Move first = request.moves.size() < _best.size() ? _best[request.moves.size()] : legal.front();
                Analysis analysis;
                analysis.lines.push_back(AnalysisLine{first, {AnalysisScoreKind::Centipawns, 50}, 5});
                if (request.lines >= 2) {
                    for (const Move other : legal) {
                        if (other != first) {
                            analysis.lines.push_back(AnalysisLine{other, {AnalysisScoreKind::Centipawns, 50 - _gap}, 5});
                            break;
                        }
                    }
                }
                return analysis;
            }

            eastl::vector<Move> _best;
            int _gap;
            bool _gated = false;
            bool _honorsToken = false;
            std::optional<EngineError> _error;
            std::optional<size_t> _failPly;
            mutable std::mutex _mutex;  ///< guards _requests
            eastl::vector<SearchRequest> _requests;
            std::atomic<bool> _open{false};
            std::atomic<bool> _cancelled{false};
            std::atomic<int> _calls{0};
            std::atomic<int> _answered{0};
        };

        eastl::shared_ptr<PlyEngine> MakeEngine(eastl::vector<Move> best = {}, int gap = 25) {
            return eastl::make_shared<PlyEngine>(std::move(best), gap);
        }

        const EngineError kBroken{EngineErrorKind::ProcessDied, "the engine fell over"};

    }  // namespace

    class MoveRaterTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ::BECore::Tests::EngineEnvironment::EnsureCoreServices();  // TaskManager
        }
    };

    TEST_F(MoveRaterTest, NothingStartsWhileItIsOff) {
        const auto engine = MakeEngine();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());

        EXPECT_FALSE(rater.IsOn());
        ASSERT_TRUE(PlayAll(model, {"e2e4", "e7e5"}));
        UpdateMany(rater, model);

        EXPECT_EQ(factory.Calls(), 0);
        EXPECT_EQ(engine->Calls(), 0);
        EXPECT_FALSE(rater.IsThinking());
        EXPECT_FALSE(rater.GetError().has_value());
        EXPECT_EQ(rater.GetRevision(), 0u);
        const MoveRatings ratings = rater.GetRatings(model);
        ASSERT_EQ(ratings.size(), 2u) << "one entry per move, none rated";
        EXPECT_FALSE(ratings[0].has_value());
        EXPECT_FALSE(ratings[1].has_value());
    }

    TEST_F(MoveRaterTest, NothingStartsForAGameWithNoMove) {
        const auto engine = MakeEngine();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);

        UpdateMany(rater, model);

        EXPECT_EQ(factory.Calls(), 0) << "nothing to rate: the engine is not even made";
        EXPECT_FALSE(rater.IsThinking());
        EXPECT_TRUE(rater.GetRatings(model).empty());
    }

    TEST_F(MoveRaterTest, RatesTheMovesInOrderWithTheRequestOfThePositionBeforeEach) {
        const auto engine = MakeEngine({Mv("e2e4"), Mv("d7d5"), Mv("g1f3")});
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayAll(model, {"e2e4", "e7e5", "g1f3"}));

        rater.SetOn(true);
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); })) << DescribeError(rater.GetError()).c_str();

        EXPECT_EQ(ReasonAt(rater, model, 0), MoveRatingReason::Brilliant) << "e2e4 is the engine's move";
        EXPECT_EQ(ReasonAt(rater, model, 1), MoveRatingReason::NotBest) << "e7e5, the engine wanted d7d5";
        EXPECT_EQ(ReasonAt(rater, model, 2), MoveRatingReason::Brilliant);
        const eastl::vector<SearchRequest> requests = engine->Requests();
        ASSERT_EQ(requests.size(), 3u) << "one search per move";
        for (size_t ply = 0; ply < requests.size(); ++ply) {
            EXPECT_EQ(requests[ply].startFen, model.GetStartFen()) << ply;
            ASSERT_EQ(requests[ply].moves.size(), ply) << "the moves BEFORE move " << ply << ", so the oldest goes first";
            for (size_t i = 0; i < ply; ++i) {
                EXPECT_EQ(requests[ply].moves[i], model.GetMoves()[i]) << ply << "/" << i;
            }
            EXPECT_EQ(requests[ply].limits.depth, kRatingDepth) << ply;
            EXPECT_EQ(requests[ply].limits.moveTimeMs, 0) << "a fixed depth and no clock: the ratings repeat";
            EXPECT_EQ(requests[ply].lines, 2) << ply;
        }
        EXPECT_EQ(kRatingDepth, 5);
        ASSERT_EQ(factory.Calls(), 1) << "the engine is made once and kept";
        EXPECT_EQ(factory.Engines()[0], "own");
        UpdateMany(rater, model);
        EXPECT_EQ(engine->Calls(), 3) << "nothing is rated twice";
        EXPECT_FALSE(rater.IsThinking());
        EXPECT_FALSE(rater.GetError().has_value());
    }

    TEST_F(MoveRaterTest, AnEmptyFactoryMakesTheOwnEngine) {
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        MoveRater rater;
        rater.SetEngineFactory({});
        rater.SetOn(true);

        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); })) << DescribeError(rater.GetError()).c_str();

        EXPECT_FALSE(rater.GetError().has_value());
    }

    TEST_F(MoveRaterTest, ABestMoveWithNoSecondLineIsForced) {
        const auto engine = MakeEngine();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        model.SetPosition(PositionFromFen(kOneMoveFen));
        ASSERT_TRUE(Play(model, "a8b8"));
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);

        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); })) << DescribeError(rater.GetError()).c_str();

        EXPECT_EQ(ReasonAt(rater, model, 0), MoveRatingReason::Forced);
        EXPECT_FALSE(rater.GetRatings(model)[0]->IsBrilliant());
        EXPECT_EQ(engine->Requests()[0].startFen, "k7/8/1K6/8/8/8/8/7R b - - 0 1") << "the game began from the position set";
    }

    TEST_F(MoveRaterTest, AWideGapMakesTheBestMoveObviousAndTheMarginDecidesIt) {
        for (const auto& [gap, expected] : {std::pair{199, MoveRatingReason::Brilliant}, std::pair{200, MoveRatingReason::Obvious}, std::pair{900, MoveRatingReason::Obvious}}) {
            const auto engine = MakeEngine({Mv("e2e4")}, gap);
            CountingAnalysisFactory factory{engine};
            ChessGameModel model;
            ASSERT_TRUE(Play(model, "e2e4"));
            MoveRater rater;
            rater.SetEngineFactory(factory.AsFactory());
            rater.SetOn(true);

            ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); })) << gap;

            EXPECT_EQ(ReasonAt(rater, model, 0), expected) << gap;
        }
    }

    TEST_F(MoveRaterTest, RatingsSurviveANewMove) {
        const auto engine = MakeEngine({Mv("e2e4"), Mv("e7e5")});
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); }));
        const uint64_t revision = rater.GetRevision();

        ASSERT_TRUE(Play(model, "e7e5"));

        const MoveRatings meanwhile = rater.GetRatings(model);
        ASSERT_EQ(meanwhile.size(), 2u);
        EXPECT_TRUE(meanwhile[0].has_value()) << "the first rating is read before the next Update()";
        EXPECT_FALSE(meanwhile[1].has_value());
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); }));
        EXPECT_EQ(ReasonAt(rater, model, 0), MoveRatingReason::Brilliant);
        EXPECT_EQ(ReasonAt(rater, model, 1), MoveRatingReason::Brilliant);
        EXPECT_EQ(engine->Calls(), 2) << "only the new move was asked";
        EXPECT_EQ(engine->Requests()[1].moves.size(), 1u);
        EXPECT_GT(rater.GetRevision(), revision);
        EXPECT_EQ(factory.Calls(), 1);
    }

    TEST_F(MoveRaterTest, AResetDropsTheRatingsAndTheNextGameIsRatedAgain) {
        const auto engine = MakeEngine({Mv("e2e4")});
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); }));
        const uint64_t revision = rater.GetRevision();

        model.Reset();
        rater.Update(model);

        EXPECT_TRUE(rater.GetRatings(model).empty());
        EXPECT_GT(rater.GetRevision(), revision) << "a display must see the ratings go";
        EXPECT_TRUE(rater.IsOn()) << "the switch stays";
        ASSERT_TRUE(Play(model, "d2d4"));
        EXPECT_FALSE(rater.GetRatings(model)[0].has_value()) << "the old rating of ply 0 is not the new move's, even before the next Update()";
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); }));
        EXPECT_EQ(ReasonAt(rater, model, 0), MoveRatingReason::NotBest) << "d2d4, the engine wanted e2e4";
        EXPECT_EQ(engine->Calls(), 2);
    }

    TEST_F(MoveRaterTest, ASetPositionDropsTheRatingsToo) {
        const auto engine = MakeEngine();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);
        ASSERT_TRUE(PlayAll(model, {"e2e4", "e7e5"}));
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); }));

        model.SetPosition(PositionFromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));  // the same position, but not the same game
        rater.Update(model);

        EXPECT_TRUE(rater.GetRatings(model).empty());
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); }));
        EXPECT_EQ(engine->Calls(), 3) << "the new game was rated from the start";
    }

    TEST_F(MoveRaterTest, AnotherMoveAtTheSamePlyIsAnotherGameAndIsRatedFromTheStart) {
        const auto engine = MakeEngine();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);
        ASSERT_TRUE(PlayAll(model, {"e2e4", "e7e5"}));
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); }));

        model.Reset();
        ASSERT_TRUE(PlayAll(model, {"e2e4", "c7c5"}));  // as long as the old history, without an Update() between: the first move is shared, the second is not
        const MoveRatings ratings = rater.GetRatings(model);
        ASSERT_EQ(ratings.size(), 2u);
        EXPECT_FALSE(ratings[0].has_value()) << "a game that is no extension of the held one gets nothing, not even for the moves they share";
        EXPECT_FALSE(ratings[1].has_value());
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); }));
        EXPECT_EQ(engine->Calls(), 4) << "and it is rated from the start again";
        EXPECT_EQ(engine->Requests()[2].moves.size(), 0u);
    }

    TEST_F(MoveRaterTest, TheAnswerOfAnEngineThatIgnoresItsTokenIsDroppedAfterTheGameChanged) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeEngine({Mv("e2e4")});
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return engine->Calls() == 1; }));
        EXPECT_TRUE(rater.IsThinking());

        model.Reset();
        ASSERT_TRUE(Play(model, "d2d4"));
        rater.Update(model);
        EXPECT_FALSE(rater.IsThinking()) << "the stale search is cancelled";
        EXPECT_FALSE(RatedAt(rater, model, 0));
        engine->Open();  // the old worker answers now, for the old game
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); })) << DescribeError(rater.GetError()).c_str();

        EXPECT_EQ(engine->Calls(), 2) << "the old answer was thrown away and the new game was asked";
        EXPECT_EQ(ReasonAt(rater, model, 0), MoveRatingReason::NotBest) << "d2d4, not the e2e4 of the answer that arrived late";
    }

    TEST_F(MoveRaterTest, AnAnswerForAnOlderPlyOfAGrownGameIsStillKept) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeEngine({Mv("e2e4")});
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return engine->Calls() == 1; }));

        ASSERT_TRUE(Play(model, "e7e5"));
        UpdateMany(rater, model, 100);
        EXPECT_TRUE(rater.IsThinking()) << "a game that only grew keeps the search";
        engine->Open();
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); }));

        EXPECT_EQ(engine->Calls(), 2);
        EXPECT_EQ(ReasonAt(rater, model, 0), MoveRatingReason::Brilliant);
    }

    TEST_F(MoveRaterTest, UpdateDoesNotWaitForTheEngine) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeEngine();
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(PlayAll(model, {"e2e4", "e7e5"}));
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);

        const Clock::time_point begin = Clock::now();
        UpdateMany(rater, model, 200);
        const Clock::duration took = Clock::now() - begin;

        EXPECT_LT(took, kUpdateBudget);
        EXPECT_TRUE(rater.IsThinking());
        EXPECT_FALSE(RatedAt(rater, model, 0));
        engine->Open();
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); }));
        EXPECT_EQ(engine->Calls(), 2) << "one search at a time, one per move";
    }

    TEST_F(MoveRaterTest, SwitchingOffStopsTheSearchAndKeepsTheRatings) {
        const auto engine = MakeEngine({Mv("e2e4")});
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); }));

        rater.SetOn(false);
        ASSERT_TRUE(Play(model, "e7e5"));
        UpdateMany(rater, model);

        EXPECT_FALSE(rater.IsOn());
        EXPECT_EQ(engine->Calls(), 1) << "nothing starts while off";
        EXPECT_TRUE(RatedAt(rater, model, 0)) << "the ratings stay";
        EXPECT_FALSE(RatedAt(rater, model, 1));
        rater.SetOn(true);
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); }));
        EXPECT_EQ(engine->Calls(), 2);
    }

    TEST_F(MoveRaterTest, SwitchingOffCancelsARunningSearchAtTheNextUpdate) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeEngine();
        engine->Gate(true);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return engine->Calls() == 1; }));

        rater.SetOn(false);
        rater.Update(model);
        EXPECT_FALSE(rater.IsThinking());
        ASSERT_TRUE(WaitUntil([&] { return engine->WasCancelled(); }));
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return !rater.IsThinking() && engine->Answered() == 1; }));
        UpdateMany(rater, model, 100);

        EXPECT_FALSE(RatedAt(rater, model, 0));
        EXPECT_FALSE(rater.GetError().has_value()) << "a search the rater cancelled is not an error";
        EXPECT_EQ(engine->Calls(), 1);
    }

    TEST_F(MoveRaterTest, ANewRatingGrowsTheRevisionAndASearchStartingDoesNot) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeEngine();
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());

        EXPECT_EQ(rater.GetRevision(), 0u);
        rater.SetOn(true);
        const uint64_t afterSwitch = rater.GetRevision();
        EXPECT_GT(afterSwitch, 0u);
        rater.SetOn(true);
        EXPECT_EQ(rater.GetRevision(), afterSwitch) << "the same switch again does nothing";
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return engine->Calls() == 1; }));
        EXPECT_TRUE(rater.IsThinking());
        EXPECT_EQ(rater.GetRevision(), afterSwitch) << "a search that starts is not shown";
        engine->Open();
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); }));
        EXPECT_EQ(rater.GetRevision(), afterSwitch + 1) << "the rating arrived";
        UpdateMany(rater, model);
        EXPECT_EQ(rater.GetRevision(), afterSwitch + 1) << "and nothing else moves it";
    }

    TEST_F(MoveRaterTest, AFactoryThatFailsIsOneErrorAndNotATryPerFrame) {
        CountingAnalysisFactory factory{EngineError{EngineErrorKind::LaunchFailed, "no engine here"}};
        ChessGameModel model;
        ASSERT_TRUE(PlayAll(model, {"e2e4", "e7e5"}));
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);
        const uint64_t afterSwitch = rater.GetRevision();

        UpdateMany(rater, model);

        EXPECT_EQ(factory.Calls(), 1);
        ASSERT_TRUE(rater.GetError().has_value());
        EXPECT_EQ(rater.GetError()->kind, EngineErrorKind::LaunchFailed);
        EXPECT_EQ(rater.GetRevision(), afterSwitch + 1) << "the error appeared once";
        EXPECT_FALSE(rater.IsThinking());
        EXPECT_FALSE(RatedAt(rater, model, 0));

        rater.SetOn(false);
        EXPECT_FALSE(rater.GetError().has_value()) << "off and on is the retry";
        rater.SetOn(true);
        UpdateMany(rater, model, 10);
        EXPECT_EQ(factory.Calls(), 2);
        EXPECT_TRUE(rater.GetError().has_value());
    }

    TEST_F(MoveRaterTest, ANewGameClearsTheErrorOfAFactory) {
        CountingAnalysisFactory factory{EngineError{EngineErrorKind::LaunchFailed, "no engine here"}};
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);
        UpdateMany(rater, model, 10);
        ASSERT_TRUE(rater.GetError().has_value());

        model.Reset();
        rater.Update(model);

        EXPECT_FALSE(rater.GetError().has_value());
        ASSERT_TRUE(Play(model, "d2d4"));
        UpdateMany(rater, model, 10);
        EXPECT_EQ(factory.Calls(), 2) << "the new game is another try";
    }

    TEST_F(MoveRaterTest, AnEngineThatFailsIsOneErrorForThatMoveAndNotATryPerFrame) {
        const auto engine = MakeEngine();
        engine->FailWith(kBroken);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return rater.GetError().has_value(); }));
        const uint64_t revision = rater.GetRevision();

        UpdateMany(rater, model);

        EXPECT_EQ(engine->Calls(), 1);
        EXPECT_EQ(rater.GetError()->kind, EngineErrorKind::ProcessDied);
        EXPECT_FALSE(rater.IsThinking());
        EXPECT_FALSE(RatedAt(rater, model, 0));
        EXPECT_EQ(rater.GetRevision(), revision);
        rater.SetOn(false);
        rater.SetOn(true);
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return engine->Calls() == 2; })) << "off and on is the retry";
    }

    TEST_F(MoveRaterTest, ALaterMoveIsStillTriedAfterAFailedOne) {
        const auto engine = MakeEngine({Mv("e2e4"), Mv("e7e5")});
        engine->FailWith(kBroken, 0);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(PlayAll(model, {"e2e4", "e7e5"}));
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);

        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return RatedAt(rater, model, 1); }));
        UpdateMany(rater, model);

        EXPECT_FALSE(RatedAt(rater, model, 0)) << "the failed move stays empty";
        EXPECT_EQ(ReasonAt(rater, model, 1), MoveRatingReason::Brilliant);
        EXPECT_EQ(engine->Calls(), 2) << "the failed move is not asked again";
        ASSERT_TRUE(rater.GetError().has_value());
        EXPECT_EQ(rater.GetError()->kind, EngineErrorKind::ProcessDied);
    }

    TEST_F(MoveRaterTest, AnEngineThatDoesNotAnalyseIsAnErrorForTheMove) {
        class PlayingEngine final : public IChessEngine {
        public:
            std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken&) override {
                return FirstLegalMove(request);
            }
        };
        const eastl::shared_ptr<IChessEngine> engine = eastl::make_shared<PlayingEngine>();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);

        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return rater.GetError().has_value(); }));

        EXPECT_EQ(rater.GetError()->kind, EngineErrorKind::Unsupported);
    }

    TEST_F(MoveRaterTest, ShutdownClearsEverythingAndStartsNothingLater) {
        const auto engine = MakeEngine({Mv("e2e4")});
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); }));

        rater.Shutdown();

        EXPECT_FALSE(rater.IsOn());
        EXPECT_FALSE(RatedAt(rater, model, 0));
        ASSERT_TRUE(Play(model, "e7e5"));
        UpdateMany(rater, model);
        EXPECT_EQ(engine->Calls(), 1);
        EXPECT_FALSE(rater.IsThinking());
        EXPECT_EQ(factory.Calls(), 1);

        rater.SetOn(true);
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); }));
        EXPECT_EQ(factory.Calls(), 2) << "the engine was dropped, a new one is made";
        EXPECT_EQ(engine->Calls(), 3) << "and the game is rated from the start again";
    }

    TEST_F(MoveRaterTest, ShutdownWithASearchInFlightReturnsAtOnceAndDropsItsAnswer) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeEngine();
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return engine->Calls() == 1; }));

        const Clock::time_point begin = Clock::now();
        rater.Shutdown();
        const Clock::duration took = Clock::now() - begin;

        EXPECT_LT(took, kUpdateBudget);
        EXPECT_FALSE(rater.IsThinking());
        engine->Open();
        ASSERT_TRUE(WaitUntil([&] { return engine->Answered() == 1; }));
        UpdateMany(rater, model, 100);
        EXPECT_FALSE(RatedAt(rater, model, 0)) << "the late answer is never taken";
        EXPECT_EQ(engine->Calls(), 1);
    }

    TEST_F(MoveRaterTest, CancelStopsTheSearchAndTheNextUpdateAsksAgain) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeEngine();
        engine->Gate(true);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        MoveRater rater;
        rater.SetEngineFactory(factory.AsFactory());
        rater.SetOn(true);
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return engine->Calls() == 1; }));

        rater.Cancel();

        EXPECT_FALSE(rater.IsThinking());
        EXPECT_TRUE(rater.IsOn()) << "the switch stays";
        engine->Open();
        ASSERT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model); })) << DescribeError(rater.GetError()).c_str();
        EXPECT_EQ(engine->Calls(), 2);
    }

    TEST_F(MoveRaterTest, TheDestructorWithASearchInFlightReturnsAtOnce) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeEngine();
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        auto rater = std::make_unique<MoveRater>();
        rater->SetEngineFactory(factory.AsFactory());
        rater->SetOn(true);
        ASSERT_TRUE(UpdateUntil(*rater, model, [&] { return engine->Calls() == 1; }));

        const Clock::time_point begin = Clock::now();
        rater.reset();  // the worker is still at the gate
        const Clock::duration took = Clock::now() - begin;

        EXPECT_LT(took, kUpdateBudget);
        engine->Open();
        ASSERT_TRUE(WaitUntil([&] { return engine->Answered() == 1; })) << "the worker frees itself";
    }

    // With the real OwnEngine at depth 5 (the default factory): no double, no process. The answers below were found by running the engine at depth 5 with two lines and are pinned, like the
    // positions of StockfishPositionTest; a change of the search or of the evaluation that moves one of them is a change of the ratings and must be looked at
    namespace {

        /// The ratings of @p model by the real engine, waited for
        MoveRatings RateWithTheOwnEngine(const ChessGameModel& model) {
            MoveRater rater;
            rater.SetOn(true);
            EXPECT_TRUE(UpdateUntil(rater, model, [&] { return AllRated(rater, model) || rater.GetError().has_value(); })) << "no answer in time";
            EXPECT_FALSE(rater.GetError().has_value()) << DescribeError(rater.GetError()).c_str();
            return rater.GetRatings(model);
        }

        /// A game from @p fen: the moves of @p ucis rated, as the reasons in order
        eastl::vector<MoveRatingReason> ReasonsOf(eastl::string_view fen, std::initializer_list<eastl::string_view> ucis) {
            ChessGameModel model;
            if (!fen.empty()) {
                model.SetPosition(PositionFromFen(fen));
            }
            EXPECT_TRUE(PlayAll(model, ucis));
            eastl::vector<MoveRatingReason> reasons;
            for (const std::optional<MoveRating>& rating : RateWithTheOwnEngine(model)) {
                EXPECT_TRUE(rating.has_value());
                reasons.push_back(rating ? rating->reason : MoveRatingReason::NotBest);
            }
            return reasons;
        }

    }  // namespace

    TEST_F(MoveRaterTest, OwnEngineAMateInOneThatIsPlayedIsObvious) {
        // 6k1/5ppp/8/8/8/8/8/R3K3 w: Ra8# is Mate 1, line 2 (Kd2) is +2.40: a gap of 99660, "the only reasonable move"
        EXPECT_EQ(ReasonsOf("6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1", {"a1a8"}), (eastl::vector<MoveRatingReason>{MoveRatingReason::Obvious}));
    }

    TEST_F(MoveRaterTest, OwnEngineAnotherMoveThanTheMateInOneIsNotBest) {
        EXPECT_EQ(ReasonsOf("6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1", {"e1d2"}), (eastl::vector<MoveRatingReason>{MoveRatingReason::NotBest}));
    }

    TEST_F(MoveRaterTest, OwnEngineTakingAHangingQueenIsObvious) {
        // 4k3/8/8/3q4/8/8/8/3RK3 w: Rxd5 is +4.75, the best other move (Rd2) -4.05: a gap of 880
        EXPECT_EQ(ReasonsOf("4k3/8/8/3q4/8/8/8/3RK3 w - - 0 1", {"d1d5"}), (eastl::vector<MoveRatingReason>{MoveRatingReason::Obvious}));
        EXPECT_EQ(ReasonsOf("4k3/8/8/3q4/8/8/8/3RK3 w - - 0 1", {"d1d2"}), (eastl::vector<MoveRatingReason>{MoveRatingReason::NotBest}));
    }

    TEST_F(MoveRaterTest, OwnEngineTheOnlyLegalMoveIsForced) {
        EXPECT_EQ(ReasonsOf(kOneMoveFen, {"a8b8"}), (eastl::vector<MoveRatingReason>{MoveRatingReason::Forced}));
    }

    TEST_F(MoveRaterTest, OwnEngineABestMoveWithACloseSecondIsBrilliantAndALegalOtherMoveIsNotBest) {
        // The engine at depth 5 and two lines, per position before the move: the start position b1c3 / g1f3 (tied +0.40), after e4 e7e5 / b8c6 (tied 0.00), after e4 e5 b1c3 / g1f3 (tied +0.40),
        // after e4 e5 Nf3 b8c6 (0.00) / g8f6 (-0.20), after e4 e5 Nf3 Nc6 b1c3 / d2d4 (tied +0.30), after Bc4 g8f6 (+0.15) / f8c5 (0.00). So e4, Nf3 and Bc4 are legal moves that are not the
        // engine's pick, and e5, Nc6 and Nf6 are its pick with a second line within the margin (gaps 0, 20 and 15): brilliant
        using Reason = MoveRatingReason;
        EXPECT_EQ(ReasonsOf({}, {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6"}),
                  (eastl::vector<Reason>{Reason::NotBest, Reason::Brilliant, Reason::NotBest, Reason::Brilliant, Reason::NotBest, Reason::Brilliant}));
    }

    /// The rater awaits the async search (many slices at depth 5); its rating is the one the blocking Analyse of the same request gives
    TEST_F(MoveRaterTest, OwnEngineTheRatingEqualsTheRatingOfTheBlockingAnalysis) {
        ChessGameModel model;
        ASSERT_TRUE(PlayAll(model, {"e2e4", "e7e5"}));
        SearchRequest request;
        request.startFen = model.GetStartFen();
        request.limits = SearchLimits{0, kRatingDepth};
        request.lines = kRatingLines;
        const MoveRatings ratings = RateWithTheOwnEngine(model);
        ASSERT_EQ(ratings.size(), model.GetMoves().size());
        for (size_t ply = 0; ply < model.GetMoves().size(); ++ply) {
            const std::expected<Analysis, EngineError> analysis = OwnEngine().Analyse(request, CancellationToken::Create());
            ASSERT_TRUE(analysis.has_value());
            ASSERT_TRUE(ratings[ply].has_value());
            EXPECT_EQ(ratings[ply]->reason, RateMove(*analysis, model.GetMoves()[ply]).reason) << "ply " << ply;
            request.moves.push_back(model.GetMoves()[ply]);
        }
    }

    TEST_F(MoveRaterTest, OwnEngineTheSameGameRatedTwiceGivesEqualRatings) {
        ChessGameModel model;
        ASSERT_TRUE(PlayAll(model, {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6"}));

        const MoveRatings first = RateWithTheOwnEngine(model);
        const MoveRatings second = RateWithTheOwnEngine(model);

        ASSERT_EQ(first.size(), 6u);
        EXPECT_TRUE(first == second) << "a fixed depth and no clock: the same game, the same ratings";
        for (const std::optional<MoveRating>& rating : first) {
            EXPECT_TRUE(rating.has_value());
        }
    }

    TEST_F(MoveRaterTest, OwnEngineTheRatingsDoNotDependOnWhenTheMovesCameIn) {
        // Rating move by move as the game grows and rating the finished game at once give the same vector
        ChessGameModel grown;
        MoveRater rater;
        rater.SetOn(true);
        for (const eastl::string_view uci : {"e2e4", "e7e5", "g1f3", "b8c6"}) {
            ASSERT_TRUE(Play(grown, uci));
            ASSERT_TRUE(UpdateUntil(rater, grown, [&] { return AllRated(rater, grown); })) << DescribeError(rater.GetError()).c_str();
        }
        ChessGameModel whole;
        ASSERT_TRUE(PlayAll(whole, {"e2e4", "e7e5", "g1f3", "b8c6"}));

        EXPECT_TRUE(rater.GetRatings(grown) == RateWithTheOwnEngine(whole));
    }

}  // namespace BECore::Chess::Tests
