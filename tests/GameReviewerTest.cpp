#include <Chess/Engine/GameReviewer.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>
#include <Common/EngineEnvironment.h>
#include <tests/TestEngines.h>

/// GameReviewer with the real TaskManager and a scripted double (no process), then the real OwnEngine on a short game. Every wait polls a condition with a time limit; none is a fixed sleep
namespace BECore::Chess::Tests {

    namespace {

        constexpr int kManyUpdates = 1000;
        constexpr StringView kMatedFen = "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3";  // fool's mate: White is mated

        constexpr AnalysisScore Cp(int value) {
            return {AnalysisScoreKind::Centipawns, value};
        }

        constexpr AnalysisScore Mate(int moves) {
            return {AnalysisScoreKind::Mate, moves};
        }

        bool Play(ChessGameModel& model, StringView uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        bool PlayAll(ChessGameModel& model, std::initializer_list<StringView> ucis) {
            for (const StringView uci : ucis) {
                if (!Play(model, uci)) {
                    return false;
                }
            }
            return true;
        }

        /// Four plies, a checkmate: the review has 5 positions and searches 4
        bool PlayFoolsMate(ChessGameModel& model) {
            return PlayAll(model, {"f2f3", "e7e5", "g2g4", "d8h4"}) && model.GetStatus() == GameStatus::Checkmate;
        }

        /// Two plies and White resigns: the review has 3 positions and searches all of them
        bool PlayResigned(ChessGameModel& model, StringView first = "e2e4", StringView second = "e7e5") {
            return PlayAll(model, {first, second}) && model.Resign(PieceColor::White).has_value();
        }

        /// What a game loop does: Update every frame until @p done
        template <typename Condition>
        bool UpdateUntil(GameReviewer& reviewer, const ChessGameModel& model, Condition done) {
            return WaitUntil([&] {
                reviewer.Update(model);
                return done();
            });
        }

        void UpdateMany(GameReviewer& reviewer, const ChessGameModel& model, int times = kManyUpdates) {
            for (int frame = 0; frame < times; ++frame) {
                reviewer.Update(model);
            }
        }

        bool IsDone(const GameReviewer& reviewer, const ChessGameModel& model) {
            return reviewer.GetState(model) == ReviewState::Done;
        }

        bool IsFailed(const GameReviewer& reviewer, const ChessGameModel& model) {
            return reviewer.GetState(model) == ReviewState::Failed;
        }

        size_t Evaluated(const ReviewedPositions& positions) {
            return static_cast<size_t>(eastl::count_if(positions.begin(), positions.end(), [](const Optional<ReviewedPosition>& position) { return position.has_value(); }));
        }

        /**
         * @brief The engine of these tests: Analyse() answers, for the request of ply `n` (n moves in it), the line scripted for ply n, or else the first legal move at 0 centipawns.
         *
         * It keeps every request in order, can hold its answer at a gate until Open() (honoring the CancellationToken or ignoring it), and can fail one ply or all, or answer with no line.
         * Configure it while no search runs; the reads are thread safe (a search runs on a pool worker).
         */
        class ScriptedEngine final : public IChessEngine {
        public:
            /// The line of the answer to the request of @p ply moves
            void Script(size_t ply, Move move, AnalysisScore score) {
                if (_script.size() <= ply) {
                    _script.resize(ply + 1);
                }
                _script[ply] = AnalysisLine{move, score, 5};
            }

            /// Holds the answer at a gate; of the request of @p ply moves only, or of every one
            void Gate(bool honorsToken, Optional<size_t> ply = std::nullopt) {
                _gated = true;
                _honorsToken = honorsToken;
                _gatePly = ply;
            }

            void Open() {
                _open.store(true);
            }

            /// Every search fails with @p error, or only the one for the request of @p ply moves
            void FailWith(EngineError error, Optional<size_t> ply = std::nullopt) {
                _error = std::move(error);
                _failPly = ply;
            }

            void StopFailing() {
                _error.reset();
                _noLines = false;
            }

            /// Every search succeeds with an analysis that has no line
            void AnswerNoLines() {
                _noLines = true;
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

            [[nodiscard]] Vector<SearchRequest> Requests() const {
                const std::lock_guard lock(_mutex);
                return _requests;
            }

            [[nodiscard]] bool WasCancelled() const {
                return _cancelled.load();
            }

        private:
            std::expected<Analysis, EngineError> Answer(const SearchRequest& request, const CancellationToken& token) {
                const size_t ply = request.moves.size();
                if (_gated && (!_gatePly || *_gatePly == ply)) {
                    const bool released = WaitUntil([&] { return _open.load() || (_honorsToken && token.IsCancelled()); }, kGateGiveUp);
                    if (_honorsToken && token.IsCancelled()) {
                        _cancelled.store(true);
                        return std::unexpected(EngineError{EngineErrorKind::Cancelled, "the search was cancelled at the gate"});
                    }
                    if (!released) {
                        return std::unexpected(EngineError{EngineErrorKind::Timeout, "the gate was never opened"});
                    }
                }
                if (_error && (!_failPly || *_failPly == ply)) {
                    return std::unexpected(*_error);
                }
                if (_noLines) {
                    return Analysis{};
                }
                const std::expected<Move, EngineError> first = FirstLegalMove(request);
                if (!first) {
                    return std::unexpected(first.error());
                }
                Analysis analysis;
                analysis.lines.push_back(ply < _script.size() && _script[ply] ? *_script[ply] : AnalysisLine{*first, Cp(0), 5});
                return analysis;
            }

            Vector<Optional<AnalysisLine>> _script;
            bool _gated = false;
            bool _honorsToken = false;
            Optional<size_t> _gatePly;
            Optional<EngineError> _error;
            Optional<size_t> _failPly;
            bool _noLines = false;
            mutable std::mutex _mutex;  ///< guards _requests
            Vector<SearchRequest> _requests;
            std::atomic<bool> _open{false};
            std::atomic<bool> _cancelled{false};
            std::atomic<int> _calls{0};
            std::atomic<int> _answered{0};
        };

        /// An engine that scripts the fool's mate: f3 is a small slip (the best was e4), e5 is the best move, g4 allows the mate in one, Qh4 is the mate
        IntrusivePtrAtomic<ScriptedEngine> MakeFoolsMateEngine() {
            auto engine = New<ScriptedEngine>();
            engine->Script(0, Mv("e2e4"), Cp(20));
            engine->Script(1, Mv("e7e5"), Cp(10));
            engine->Script(2, Mv("e2e4"), Cp(-10));
            engine->Script(3, Mv("d8h4"), Mate(1));
            return engine;
        }

        const EngineError kBroken{EngineErrorKind::ProcessDied, "the engine fell over"};

    }  // namespace

    class GameReviewerTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ::BECore::Tests::EngineEnvironment::EnsureCoreServices();  // TaskManager, which the reviewer's search runs on
        }
    };

    TEST_F(GameReviewerTest, AnOngoingGameAndAGameWithNoMovesAreRefused) {
        const auto engine = New<ScriptedEngine>();
        CountingAnalysisFactory factory{engine};
        GameReviewer reviewer;
        reviewer.SetEngineFactory(factory.AsFactory());

        ChessGameModel ongoing;
        ASSERT_TRUE(PlayAll(ongoing, {"e2e4", "e7e5"}));
        EXPECT_EQ(ReviewRefusalFor(ongoing), ReviewRefusal::NotFinished);
        const auto notFinished = reviewer.Start(ongoing);
        ASSERT_FALSE(notFinished.has_value());
        EXPECT_EQ(notFinished.error(), ReviewRefusal::NotFinished);

        ChessGameModel noMoves;
        noMoves.SetPosition(PositionFromFen(kMatedFen));
        ASSERT_TRUE(noMoves.IsGameOver());
        EXPECT_EQ(ReviewRefusalFor(noMoves), ReviewRefusal::NoMoves);
        const auto none = reviewer.Start(noMoves);
        ASSERT_FALSE(none.has_value());
        EXPECT_EQ(none.error(), ReviewRefusal::NoMoves);

        UpdateMany(reviewer, ongoing);
        EXPECT_EQ(reviewer.GetState(ongoing), ReviewState::Idle);
        EXPECT_EQ(reviewer.GetRevision(), 0u) << "a refusal changes nothing";
        EXPECT_EQ(engine->Calls(), 0);
        EXPECT_EQ(factory.Calls(), 0);

        ChessGameModel finished;
        ASSERT_TRUE(PlayResigned(finished));
        EXPECT_FALSE(ReviewRefusalFor(finished).has_value());
    }

    TEST_F(GameReviewerTest, PositionsComeOldestFirstEachWithTheRequestOfItsOwnGame) {
        const auto engine = New<ScriptedEngine>();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(PlayResigned(model));
        GameReviewer reviewer;
        reviewer.SetEngineFactory(factory.AsFactory());

        ASSERT_TRUE(reviewer.Start(model).has_value());
        EXPECT_EQ(reviewer.GetState(model), ReviewState::Running);
        EXPECT_EQ(reviewer.GetProgress(model).total, 3u);
        EXPECT_EQ(reviewer.GetProgress(model).done, 0u);
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return IsDone(reviewer, model); })) << DescribeError(reviewer.GetError()).c_str();

        const Vector<SearchRequest> requests = engine->Requests();
        ASSERT_EQ(requests.size(), 3u) << "the position after the resignation is searched like any other";
        for (size_t ply = 0; ply < requests.size(); ++ply) {
            EXPECT_EQ(requests[ply].startFen, model.GetStartFen()) << ply;
            ASSERT_EQ(requests[ply].moves.size(), ply);
            for (size_t i = 0; i < ply; ++i) {
                EXPECT_EQ(requests[ply].moves[i], model.GetMoves()[i]) << ply << " " << i;
            }
            EXPECT_EQ(requests[ply].limits.depth, kReviewDepth);
            EXPECT_EQ(requests[ply].limits.moveTimeMs, 0) << "no clock, so the same game gives the same review";
            EXPECT_EQ(requests[ply].lines, kReviewLines);
            EXPECT_EQ(requests[ply].seed, 0u);
        }
        EXPECT_EQ(reviewer.GetProgress(model).done, 3u);
        EXPECT_EQ(Evaluated(reviewer.GetPositions(model)), 3u);
    }

    TEST_F(GameReviewerTest, TheProgressGrowsOnePositionAtATimeAndTheMovesComeWithBothTheirPositions) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeFoolsMateEngine();
        engine->Gate(false, 2);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(PlayFoolsMate(model));
        GameReviewer reviewer;
        reviewer.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(reviewer.Start(model).has_value());

        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return engine->Calls() == 3; }));
        UpdateMany(reviewer, model, 100);

        EXPECT_EQ(reviewer.GetState(model), ReviewState::Running);
        EXPECT_EQ(reviewer.GetProgress(model).done, 2u);
        EXPECT_EQ(reviewer.GetProgress(model).total, 4u) << "the mated last position is not searched";
        const ReviewedPositions positions = reviewer.GetPositions(model);
        ASSERT_EQ(positions.size(), 5u);
        EXPECT_TRUE(positions[0].has_value());
        EXPECT_TRUE(positions[1].has_value());
        EXPECT_FALSE(positions[2].has_value());
        EXPECT_FALSE(positions[3].has_value());
        EXPECT_TRUE(positions[4].has_value()) << "the terminal position needs no search";
        const ReviewedMoves moves = reviewer.GetMoves(model);
        ASSERT_EQ(moves.size(), 4u);
        EXPECT_TRUE(moves[0].has_value()) << "positions 0 and 1 are there";
        EXPECT_FALSE(moves[1].has_value());
        EXPECT_FALSE(moves[2].has_value());
        EXPECT_FALSE(moves[3].has_value()) << "its position before it is not there, though the one after it is";

        engine->Open();
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return IsDone(reviewer, model); }));
        EXPECT_EQ(reviewer.GetProgress(model).done, 4u);
        EXPECT_EQ(Evaluated(reviewer.GetPositions(model)), 5u);
    }

    TEST_F(GameReviewerTest, ALastPositionWithNoLegalMoveIsNotSearchedAndHasItsOwnValue) {
        const auto engine = MakeFoolsMateEngine();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(PlayFoolsMate(model));
        GameReviewer reviewer;
        reviewer.SetEngineFactory(factory.AsFactory());

        ASSERT_TRUE(reviewer.Start(model).has_value());
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return IsDone(reviewer, model); }));
        UpdateMany(reviewer, model, 100);

        EXPECT_EQ(engine->Calls(), 4);
        const ReviewedPositions positions = reviewer.GetPositions(model);
        ASSERT_EQ(positions.size(), 5u);
        ASSERT_TRUE(positions[4].has_value());
        EXPECT_EQ(positions[4]->centipawns, -kMateCentipawns) << "White, to move, is mated";
        EXPECT_TRUE(positions[4]->best.IsNull());
        EXPECT_EQ(positions[4]->sideToMove, PieceColor::White);
    }

    TEST_F(GameReviewerTest, AStalematedLastPositionIsWorthZero) {
        const auto engine = New<ScriptedEngine>();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        model.SetPosition(PositionFromFen("7k/8/5K2/8/8/8/8/6Q1 w - - 0 1"));
        ASSERT_TRUE(PlayAll(model, {"g1g6"}));
        ASSERT_EQ(model.GetStatus(), GameStatus::Stalemate);
        GameReviewer reviewer;
        reviewer.SetEngineFactory(factory.AsFactory());

        ASSERT_TRUE(reviewer.Start(model).has_value());
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return IsDone(reviewer, model); }));

        EXPECT_EQ(engine->Calls(), 1);
        const ReviewedPositions positions = reviewer.GetPositions(model);
        ASSERT_EQ(positions.size(), 2u);
        ASSERT_TRUE(positions[1].has_value());
        EXPECT_EQ(positions[1]->centipawns, 0);
        EXPECT_EQ(positions[1]->score, Cp(0));
        EXPECT_EQ(positions[1]->sideToMove, PieceColor::Black);
    }

    TEST_F(GameReviewerTest, TheLossAndTheQualityOfEachMoveComeFromTheScriptedScores) {
        const auto engine = MakeFoolsMateEngine();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(PlayFoolsMate(model));
        GameReviewer reviewer;
        reviewer.SetEngineFactory(factory.AsFactory());

        ASSERT_TRUE(reviewer.Start(model).has_value());
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return IsDone(reviewer, model); }));

        const ReviewedPositions positions = reviewer.GetPositions(model);
        ASSERT_EQ(positions.size(), 5u);
        EXPECT_EQ(*positions[0], (ReviewedPosition{Cp(20), 20, Mv("e2e4"), PieceColor::White}));
        EXPECT_EQ(*positions[1], (ReviewedPosition{Cp(10), 10, Mv("e7e5"), PieceColor::Black})) << "as the engine gave it: the side to move's view";
        EXPECT_EQ(*positions[3], (ReviewedPosition{Mate(1), kMateCentipawns - kCentipawnsPerMateMove, Mv("d8h4"), PieceColor::Black}));
        const ReviewedMoves moves = reviewer.GetMoves(model);
        ASSERT_EQ(moves.size(), 4u);
        // f3: line 1 was +20, and after it Black stands at +10, so White has -10: a loss of 30
        EXPECT_EQ(*moves[0], (ReviewedMove{MoveReview{30, MoveQuality::Good}, Mv("e2e4")}));
        EXPECT_EQ(*moves[1], (ReviewedMove{MoveReview{0, MoveQuality::Best}, Mv("e7e5")})) << "the engine's own move";
        // g4: line 1 was -10, and after it Black mates in 1, which the clamp holds at -1000 for White
        EXPECT_EQ(*moves[2], (ReviewedMove{MoveReview{kReviewClamp - 10, MoveQuality::Blunder}, Mv("e2e4")}));
        EXPECT_EQ(*moves[3], (ReviewedMove{MoveReview{0, MoveQuality::Best}, Mv("d8h4")})) << "the mate itself";
    }

    TEST_F(GameReviewerTest, StartOnARunningOrADoneReviewOfTheSameGameChangesNothing) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeFoolsMateEngine();
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(PlayFoolsMate(model));
        GameReviewer reviewer;
        reviewer.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(reviewer.Start(model).has_value());
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return engine->Calls() == 1; }));
        const uint64_t revision = reviewer.GetRevision();

        EXPECT_TRUE(reviewer.Start(model).has_value());
        UpdateMany(reviewer, model, 100);

        EXPECT_EQ(reviewer.GetRevision(), revision);
        EXPECT_EQ(reviewer.GetState(model), ReviewState::Running);
        EXPECT_EQ(engine->Calls(), 1) << "the search in flight is kept, none is added";
        engine->Open();
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return IsDone(reviewer, model); }));
        const ReviewedPositions finished = reviewer.GetPositions(model);
        const uint64_t doneRevision = reviewer.GetRevision();

        EXPECT_TRUE(reviewer.Start(model).has_value());
        UpdateMany(reviewer, model, 100);

        EXPECT_EQ(reviewer.GetRevision(), doneRevision);
        EXPECT_EQ(engine->Calls(), 4);
        EXPECT_EQ(factory.Calls(), 1);
        EXPECT_TRUE(reviewer.GetPositions(model) == finished);
    }

    TEST_F(GameReviewerTest, AFailedReviewStartsAgainFromTheFirstPosition) {
        const auto engine = MakeFoolsMateEngine();
        engine->FailWith(kBroken, 2);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(PlayFoolsMate(model));
        GameReviewer reviewer;
        reviewer.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(reviewer.Start(model).has_value());
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return IsFailed(reviewer, model); }));

        engine->StopFailing();
        EXPECT_TRUE(reviewer.Start(model).has_value());

        EXPECT_EQ(reviewer.GetState(model), ReviewState::Running);
        EXPECT_EQ(Evaluated(reviewer.GetPositions(model)), 1u) << "only the terminal position: the first start's results are gone";
        EXPECT_FALSE(reviewer.GetError().has_value());
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return IsDone(reviewer, model); }));
        EXPECT_EQ(engine->Calls(), 3 + 4) << "the failed review asked three positions, the new one asks all four";
        EXPECT_EQ(Evaluated(reviewer.GetPositions(model)), 5u);
    }

    TEST_F(GameReviewerTest, AbandonGoesBackToIdleAndTheLateAnswerOfAnEngineThatIgnoresItsTokenIsNeverTaken) {
        SKIP_WITHOUT_WORKER_THREADS();
        for (const bool honorsToken : {false, true}) {
            const auto engine = MakeFoolsMateEngine();
            engine->Gate(honorsToken, 1);
            CountingAnalysisFactory factory{engine};
            ChessGameModel model;
            ASSERT_TRUE(PlayFoolsMate(model));
            GameReviewer reviewer;
            reviewer.SetEngineFactory(factory.AsFactory());
            ASSERT_TRUE(reviewer.Start(model).has_value());
            ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return engine->Calls() == 2; })) << honorsToken;
            EXPECT_EQ(reviewer.GetProgress(model).done, 1u);

            reviewer.Abandon();

            EXPECT_EQ(reviewer.GetState(model), ReviewState::Idle) << honorsToken;
            EXPECT_TRUE(reviewer.GetPositions(model).empty());
            EXPECT_TRUE(reviewer.GetMoves(model).empty());
            EXPECT_EQ(reviewer.GetProgress(model).total, 0u);
            engine->Open();  // the old worker answers now, for a review nobody holds
            ASSERT_TRUE(WaitUntil([&] { return engine->Answered() == 2; }));
            UpdateMany(reviewer, model, 100);

            EXPECT_EQ(reviewer.GetState(model), ReviewState::Idle) << "a late answer starts nothing and is never taken: " << honorsToken;
            EXPECT_TRUE(reviewer.GetPositions(model).empty());
            EXPECT_EQ(engine->Calls(), 2);
            EXPECT_FALSE(reviewer.GetError().has_value()) << "a search the reviewer cancelled is not an error";

            ASSERT_TRUE(reviewer.Start(model).has_value()) << "a cancelled review can be started again";
            ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return IsDone(reviewer, model); }));
            EXPECT_EQ(engine->Calls(), 2 + 4) << "from the first position, none of the dropped review's";
        }
    }

    TEST_F(GameReviewerTest, ATakeBackOrAnotherStartPositionDropsTheReviewAtOnceAndForGood) {
        SKIP_WITHOUT_WORKER_THREADS();
        enum class Change { TakeBack, Reset, SetPosition };
        for (const Change change : {Change::TakeBack, Change::Reset, Change::SetPosition}) {
            const auto engine = New<ScriptedEngine>();
            engine->Gate(false, 1);
            CountingAnalysisFactory factory{engine};
            ChessGameModel model;
            ASSERT_TRUE(PlayResigned(model));
            GameReviewer reviewer;
            reviewer.SetEngineFactory(factory.AsFactory());
            ASSERT_TRUE(reviewer.Start(model).has_value());
            ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return engine->Calls() == 2; }));
            const uint64_t revision = reviewer.GetRevision();

            switch (change) {
                case Change::TakeBack:
                    ASSERT_EQ(model.TakeBack(1), 1u);
                    break;
                case Change::Reset:
                    model.Reset();
                    break;
                case Change::SetPosition:
                    model.SetPosition(PositionFromFen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"));
                    break;
            }
            const int which = static_cast<int>(change);

            EXPECT_EQ(reviewer.GetState(model), ReviewState::Idle) << "the reads give nothing before any Update: " << which;
            EXPECT_TRUE(reviewer.GetPositions(model).empty()) << which;
            EXPECT_TRUE(reviewer.GetMoves(model).empty()) << which;
            EXPECT_EQ(reviewer.GetProgress(model).done, 0u) << which;
            reviewer.Update(model);
            EXPECT_GT(reviewer.GetRevision(), revision) << "the drop is a change: " << which;
            engine->Open();  // the old worker answers now, for the old game
            ASSERT_TRUE(WaitUntil([&] { return engine->Answered() == 2; })) << which;
            UpdateMany(reviewer, model, 100);

            EXPECT_EQ(reviewer.GetState(model), ReviewState::Idle) << "for good: " << which;
            EXPECT_EQ(engine->Calls(), 2) << "nothing is started for the new game: " << which;
        }
    }

    TEST_F(GameReviewerTest, AFinishedReviewIsDroppedByATakeBackToo) {
        const auto engine = MakeFoolsMateEngine();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(PlayFoolsMate(model));
        GameReviewer reviewer;
        reviewer.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(reviewer.Start(model).has_value());
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return IsDone(reviewer, model); }));

        ASSERT_EQ(model.TakeBack(1), 1u);
        EXPECT_EQ(reviewer.GetState(model), ReviewState::Idle);
        reviewer.Update(model);
        ASSERT_TRUE(Play(model, "d8h4"));

        EXPECT_EQ(reviewer.GetState(model), ReviewState::Idle) << "the same moves again are a new game: the dropped review does not come back";
        EXPECT_TRUE(reviewer.GetPositions(model).empty());
    }

    TEST_F(GameReviewerTest, AFailingSearchStopsTheReviewAndKeepsThePositionsThatCame) {
        const auto engine = MakeFoolsMateEngine();
        engine->FailWith(kBroken, 2);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(PlayFoolsMate(model));
        GameReviewer reviewer;
        reviewer.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(reviewer.Start(model).has_value());

        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return IsFailed(reviewer, model); }));
        UpdateMany(reviewer, model);

        ASSERT_TRUE(reviewer.GetError().has_value());
        EXPECT_EQ(reviewer.GetError()->kind, EngineErrorKind::ProcessDied);
        EXPECT_EQ(engine->Calls(), 3) << "nothing is retried by itself";
        EXPECT_EQ(reviewer.GetProgress(model).done, 2u);
        const ReviewedPositions positions = reviewer.GetPositions(model);
        EXPECT_TRUE(positions[0].has_value());
        EXPECT_TRUE(positions[1].has_value());
        EXPECT_FALSE(positions[2].has_value());
        EXPECT_TRUE(reviewer.GetMoves(model)[0].has_value()) << "what came stays readable";
    }

    TEST_F(GameReviewerTest, AnEngineThatCannotBeMadeAndAnEmptyAnswerAreFailures) {
        ChessGameModel model;
        ASSERT_TRUE(PlayFoolsMate(model));

        CountingAnalysisFactory failing{EngineError{EngineErrorKind::LaunchFailed, "no engine here"}};
        GameReviewer unmade;
        unmade.SetEngineFactory(failing.AsFactory());
        ASSERT_TRUE(unmade.Start(model).has_value());
        ASSERT_TRUE(UpdateUntil(unmade, model, [&] { return IsFailed(unmade, model); }));
        UpdateMany(unmade, model);
        EXPECT_EQ(unmade.GetError()->kind, EngineErrorKind::LaunchFailed);
        EXPECT_EQ(failing.Calls(), 1) << "one error, not a try per frame";
        EXPECT_EQ(Evaluated(unmade.GetPositions(model)), 1u) << "only the terminal position";

        CountingAnalysisFactory nobody{ChessEnginePtr{}};
        GameReviewer none;
        none.SetEngineFactory(nobody.AsFactory());
        ASSERT_TRUE(none.Start(model).has_value());
        ASSERT_TRUE(UpdateUntil(none, model, [&] { return IsFailed(none, model); }));
        EXPECT_EQ(none.GetError()->kind, EngineErrorKind::LaunchFailed);

        const auto empty = New<ScriptedEngine>();
        empty->AnswerNoLines();
        CountingAnalysisFactory emptyFactory{empty};
        GameReviewer noLines;
        noLines.SetEngineFactory(emptyFactory.AsFactory());
        ASSERT_TRUE(noLines.Start(model).has_value());
        ASSERT_TRUE(UpdateUntil(noLines, model, [&] { return IsFailed(noLines, model); }));
        EXPECT_EQ(noLines.GetError()->kind, EngineErrorKind::NoMove);
        EXPECT_EQ(noLines.GetProgress(model).done, 0u);
    }

    TEST_F(GameReviewerTest, AnUpdateWithNothingToDoStartsNothing) {
        const auto engine = MakeFoolsMateEngine();
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(PlayFoolsMate(model));
        GameReviewer reviewer;
        reviewer.SetEngineFactory(factory.AsFactory());

        UpdateMany(reviewer, model, 200);
        EXPECT_EQ(factory.Calls(), 0) << "never started";
        EXPECT_EQ(reviewer.GetRevision(), 0u);

        ASSERT_TRUE(reviewer.Start(model).has_value());
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return IsDone(reviewer, model); }));
        const uint64_t revision = reviewer.GetRevision();
        UpdateMany(reviewer, model, 200);

        EXPECT_EQ(engine->Calls(), 4) << "done";
        EXPECT_EQ(reviewer.GetRevision(), revision);

        reviewer.Abandon();
        UpdateMany(reviewer, model, 200);
        EXPECT_EQ(engine->Calls(), 4) << "abandoned";
    }

    TEST_F(GameReviewerTest, UpdateDoesNotWaitForTheEngine) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeFoolsMateEngine();
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(PlayFoolsMate(model));
        GameReviewer reviewer;
        reviewer.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(reviewer.Start(model).has_value());

        UpdateMany(reviewer, model, 200);  // the gate is opened only after this: an Update that waited for it would end in the give-up of the gate, not in the state below

        EXPECT_EQ(reviewer.GetState(model), ReviewState::Running);
        EXPECT_EQ(reviewer.GetProgress(model).done, 0u);
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));
        EXPECT_EQ(engine->Calls(), 1) << "one search at a time";
        engine->Open();
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return IsDone(reviewer, model); }));
        EXPECT_EQ(engine->Calls(), 4);
    }

    TEST_F(GameReviewerTest, TheRevisionGrowsWithAPositionAndNotWithASearchThatStarts) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeFoolsMateEngine();
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(PlayFoolsMate(model));
        GameReviewer reviewer;
        reviewer.SetEngineFactory(factory.AsFactory());

        EXPECT_EQ(reviewer.GetRevision(), 0u);
        ASSERT_TRUE(reviewer.Start(model).has_value());
        const uint64_t started = reviewer.GetRevision();
        EXPECT_GT(started, 0u);
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return engine->Calls() == 1; }));
        EXPECT_EQ(reviewer.GetRevision(), started) << "a search that starts is not shown";

        engine->Open();
        uint64_t last = started;
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] {
            EXPECT_GE(reviewer.GetRevision(), last) << "never goes back";
            last = reviewer.GetRevision();
            return IsDone(reviewer, model);
        }));
        EXPECT_EQ(reviewer.GetRevision(), started + 4) << "one step per position that arrived";

        reviewer.Abandon();
        EXPECT_EQ(reviewer.GetRevision(), started + 5);
    }

    TEST_F(GameReviewerTest, CancelKeepsTheReviewAndTheNextUpdateAsksAgain) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeFoolsMateEngine();
        engine->Gate(true);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(PlayFoolsMate(model));
        GameReviewer reviewer;
        reviewer.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(reviewer.Start(model).has_value());
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return engine->Calls() == 1; }));

        reviewer.Cancel();

        EXPECT_EQ(reviewer.GetState(model), ReviewState::Running) << "the review stays";
        ASSERT_TRUE(WaitUntil([&] { return engine->WasCancelled(); }));
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return engine->Calls() == 2; })) << "asked again";
        EXPECT_FALSE(reviewer.GetError().has_value()) << "a search the reviewer cancelled is not an error";
        engine->Open();
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return IsDone(reviewer, model); })) << DescribeError(reviewer.GetError()).c_str();
        EXPECT_EQ(engine->Calls(), 2 + 3);
    }

    TEST_F(GameReviewerTest, ShutdownJoinsNothingDropsTheReviewAndTheEngine) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeFoolsMateEngine();
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(PlayFoolsMate(model));
        GameReviewer reviewer;
        reviewer.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(reviewer.Start(model).has_value());
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return engine->Calls() == 1; }));

        reviewer.Shutdown();

        EXPECT_EQ(engine->Answered(), 0) << "the worker is still at its gate: nothing waited for it";
        EXPECT_EQ(reviewer.GetState(model), ReviewState::Idle);
        engine->Open();
        ASSERT_TRUE(WaitUntil([&] { return engine->Answered() == 1; }));
        UpdateMany(reviewer, model, 100);
        EXPECT_EQ(reviewer.GetState(model), ReviewState::Idle) << "the late answer is never taken";
        EXPECT_EQ(engine->Calls(), 1);

        ASSERT_TRUE(reviewer.Start(model).has_value());
        ASSERT_TRUE(UpdateUntil(reviewer, model, [&] { return IsDone(reviewer, model); }));
        EXPECT_EQ(factory.Calls(), 2) << "the engine was dropped, a new one is made";
    }

    TEST_F(GameReviewerTest, TheDestructorWithASearchInFlightJoinsNothing) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeFoolsMateEngine();
        engine->Gate(false);
        CountingAnalysisFactory factory{engine};
        ChessGameModel model;
        ASSERT_TRUE(PlayFoolsMate(model));
        auto reviewer = MakeUnique<GameReviewer>();
        reviewer->SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(reviewer->Start(model).has_value());
        ASSERT_TRUE(UpdateUntil(*reviewer, model, [&] { return engine->Calls() == 1; }));

        reviewer.reset();

        EXPECT_EQ(engine->Answered(), 0) << "the worker is still at its gate: nothing waited for it";
        engine->Open();
        ASSERT_TRUE(WaitUntil([&] { return engine->Answered() == 1; })) << "the worker frees itself";
    }

    // With the real OwnEngine at kReviewDepth (the default factory): no double, no process
    namespace {

        struct FinishedReview {
            ReviewState state = ReviewState::Idle;
            ReviewedPositions positions;
            ReviewedMoves moves;
        };

        /// The review of @p model by the real engine, waited for
        FinishedReview ReviewWithTheOwnEngine(const ChessGameModel& model) {
            GameReviewer reviewer;
            EXPECT_TRUE(reviewer.Start(model).has_value());
            EXPECT_TRUE(UpdateUntil(reviewer, model, [&] { return reviewer.GetState(model) != ReviewState::Running; })) << "no answer in time";
            EXPECT_FALSE(reviewer.GetError().has_value()) << DescribeError(reviewer.GetError()).c_str();
            return {reviewer.GetState(model), reviewer.GetPositions(model), reviewer.GetMoves(model)};
        }

    }  // namespace

    TEST_F(GameReviewerTest, OwnEngineTheSameGameReviewedTwiceGivesTheSameReviewAndTheMateIsTheBlunder) {
        ChessGameModel model;
        ASSERT_TRUE(PlayFoolsMate(model));

        const FinishedReview first = ReviewWithTheOwnEngine(model);
        const FinishedReview second = ReviewWithTheOwnEngine(model);

        ASSERT_EQ(first.state, ReviewState::Done);
        ASSERT_EQ(first.positions.size(), 5u);
        ASSERT_EQ(first.moves.size(), 4u);
        EXPECT_TRUE(first.positions == second.positions) << "a fixed depth and no clock: the same game, the same review";
        EXPECT_TRUE(first.moves == second.moves);
        for (const Optional<ReviewedMove>& move : first.moves) {
            EXPECT_TRUE(move.has_value());
        }
        EXPECT_EQ(first.moves[2]->review.quality, MoveQuality::Blunder) << "g4 allows the mate";
        EXPECT_TRUE(first.positions[4]->best.IsNull());
        EXPECT_EQ(first.positions[4]->centipawns, -kMateCentipawns);
    }

}  // namespace BECore::Chess::Tests
