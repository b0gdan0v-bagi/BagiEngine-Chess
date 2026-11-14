#include "ChessTestUtil.h"
#include "FakeEngineUtil.h"

#include <Chess/Engine/EngineOpponent.h>
#include <Chess/Engine/OwnEngine.h>
#include <Chess/Engine/UciEngine.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>
#include <Common/EngineEnvironment.h>
#include <chrono>
#include <thread>

/// EngineOpponent with the real TaskManager: against the fake UCI engine (a child process, tests/fake_uci_engine.py) for the whole way from a click to a move, and
/// against engines made in this file (no process) for the rules of the driver itself. Every wait polls a condition with a time limit; none is a fixed sleep
namespace BECore::Chess::Tests {

    namespace {

        using std::chrono::milliseconds;
        using Clock = std::chrono::steady_clock;

        constexpr SearchLimits kLimits{50, 0};
        constexpr milliseconds kMustNotTimeOut{30000};  // the give-up limit of a wait: it only ends a broken test
        constexpr milliseconds kPollInterval{1};
        constexpr int kManyUpdates = 1000;
        constexpr const char* kLogName = "fake.log";
        constexpr const char* kThinksUntilStopped = "60000";  // the fake thinks for a minute unless it is sent `stop`
        constexpr StringView kGoCommand = "go movetime 50";
        constexpr StringView kMatedBlackFen = "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1";
#if !CHESS_HAS_UCI_PROCESS
        constexpr EngineErrorKind kMissingProgramError = EngineErrorKind::Unsupported;
#else
        constexpr EngineErrorKind kMissingProgramError = EngineErrorKind::LaunchFailed;
#endif

        bool Play(ChessGameModel& model, StringView uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        /// Polls @p done until it is true; false when it has not become true within @p timeout
        template <typename Condition>
        bool WaitUntil(Condition done, milliseconds timeout = kMustNotTimeOut) {
            const Clock::time_point deadline = Clock::now() + timeout;
            while (!done()) {
                if (Clock::now() >= deadline) {
                    return false;
                }
                std::this_thread::sleep_for(kPollInterval);
            }
            return true;
        }

        /// What a game loop does: Update every frame until @p done
        template <typename Condition>
        bool UpdateUntil(EngineOpponent& opponent, ChessGameModel& model, Condition done) {
            return WaitUntil([&] {
                opponent.Update(model);
                return done();
            });
        }

        String Describe(const Optional<EngineError>& error) {
            return error ? Format("{}: {}", EnumUtils<EngineErrorKind>::ToString(error->kind), error->detail) : String("no error");
        }

        String StartPositionCommand() {
            return Format("position fen {}", Position::kStartFen);
        }

        /// The fake UCI engine with @p flags, as the engine of an opponent
        SharedPtr<UciEngine> MakeFakeEngine(std::initializer_list<StringView> flags) {
            UciEngineSettings settings;
            settings.launch = FakeEngineLaunch(flags);
            return MakeShared<UciEngine>(std::move(settings));
        }

        /// Counts how often the engine inside it is asked
        class CountingEngine final : public IChessEngine {
        public:
            explicit CountingEngine(SharedPtr<IChessEngine> inner) : _inner(std::move(inner)) {}

            std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken& token) override {
                _calls.fetch_add(1);
                return _inner->FindMove(request, token);
            }

            [[nodiscard]] int Calls() const {
                return _calls.load();
            }

        private:
            SharedPtr<IChessEngine> _inner;
            std::atomic<int> _calls{0};
        };

        /// An engine with no process that answers a script, one move per call and without looking at its token. A gated one waits for Open() before it answers
        class ScriptedEngine final : public IChessEngine {
        public:
            ScriptedEngine(std::initializer_list<StringView> moves, bool gated) : _open(!gated) {
                for (const StringView move : moves) {
                    _moves.push_back(Mv(move));
                }
            }

            std::expected<Move, EngineError> FindMove(const SearchRequest&, const CancellationToken&) override {
                const size_t index = static_cast<size_t>(_calls.fetch_add(1));
                if (!WaitUntil([this] { return _open.load(); })) {  // a test that never opens the gate must not hold a worker for good
                    return std::unexpected(EngineError{EngineErrorKind::Timeout, "the gate was never opened"});
                }
                if (index >= _moves.size()) {
                    return std::unexpected(EngineError{EngineErrorKind::NoMove, "the script is used up"});
                }
                return _moves[index];
            }

            void Open() {
                _open.store(true);
            }

            /// How often the engine has been asked, answered or not
            [[nodiscard]] int Calls() const {
                return _calls.load();
            }

        private:
            Vector<Move> _moves;
            std::atomic<int> _calls{0};
            std::atomic<bool> _open;
        };

        SharedPtr<ScriptedEngine> MakeScripted(std::initializer_list<StringView> moves, bool gated = false) {
            return MakeShared<ScriptedEngine>(moves, gated);
        }

        /// The plainest engine there is: the first legal move of the final position, at once. It stands for any IChessEngine
        class FirstLegalMoveEngine final : public IChessEngine {
        public:
            std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken&) override {
                const auto position = request.FinalPosition();
                if (!position) {
                    return std::unexpected(position.error());
                }
                const MoveList moves = MoveGenerator::GenerateLegal(*position);
                if (moves.empty()) {
                    return std::unexpected(EngineError{EngineErrorKind::NoMove, "the game is over"});
                }
                return moves.front();
            }
        };

    }  // namespace

    class EngineOpponentTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ::BECore::Tests::EngineEnvironment::EnsureCoreServices();  // TaskManager
        }
    };

    TEST_F(EngineOpponentTest, PlaysItsSideAfterTheOtherSideHasMoved) {
        SKIP_WITHOUT_FAKE_ENGINE();
        SKIP_WITHOUT_WORKER_THREADS();
        const TempDirectory directory;
        const String log = directory.FileText(kLogName);
        ChessGameModel model;
        EngineOpponent opponent(MakeFakeEngine({"--log", log, "--moves", "e7e5,g8f6"}), PieceColor::Black, kLimits);
        const uint64_t versionBefore = model.GetVersion();

        ASSERT_TRUE(Play(model, "e2e4"));
        opponent.Update(model);
        EXPECT_TRUE(opponent.IsThinking());
        ASSERT_TRUE(UpdateUntil(opponent, model, [&] { return model.GetMoves().size() == 2; })) << Describe(opponent.GetLastError()).c_str();

        EXPECT_EQ(model.GetMoves()[1].ToUci(), "e7e5");
        EXPECT_EQ(model.GetVersion(), versionBefore + 2) << "the move of the person and the move of the engine";
        EXPECT_EQ(model.GetTurn(), PieceColor::White);
        EXPECT_FALSE(opponent.IsThinking());
        EXPECT_FALSE(opponent.GetLastError().has_value());
        EXPECT_EQ(opponent.GetSide(), PieceColor::Black);
        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), Format("{} moves e2e4", StartPositionCommand())));

        // White to move: the opponent has nothing to do
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            opponent.Update(model);
            ASSERT_FALSE(opponent.IsThinking());
        }
        EXPECT_EQ(model.GetMoves().size(), 2u);
    }

    TEST_F(EngineOpponentTest, UpdateDoesNotWaitForTheEngine) {
        SKIP_WITHOUT_FAKE_ENGINE();
        SKIP_WITHOUT_WORKER_THREADS();
        const TempDirectory directory;
        const String log = directory.FileText(kLogName);
        ChessGameModel model;
        EngineOpponent opponent(MakeFakeEngine({"--log", log, "--think-ms", kThinksUntilStopped, "--moves", "e7e5"}), PieceColor::Black, kLimits);
        ASSERT_TRUE(Play(model, "e2e4"));

        opponent.Update(model);  // starts the process and the search on the pool
        ASSERT_TRUE(WaitForLogLine(directory.File(kLogName), kGoCommand)) << "the fake has the search and is thinking";
        opponent.Update(model);

        // No clock: the fake thinks a minute and the answer deadline is the move time plus UciEngineSettings::kDefaultAnswerSlackMs, so the search is still running when
        // Update returns and IsThinking() is true; an Update that had waited for the engine would have come back after the search ended, with IsThinking() false
        EXPECT_TRUE(opponent.IsThinking());
        EXPECT_EQ(model.GetMoves().size(), 1u) << "nothing was played: the engine is still thinking";
        // The destructor of the opponent cancels the search; the fake is stopped and ends with the engine
    }

    TEST_F(EngineOpponentTest, StartsNothingOnTheOtherSidesTurn) {
        ChessGameModel model;
        const auto engine = MakeScripted({"e7e5"}, false);
        EngineOpponent opponent(engine, PieceColor::Black, kLimits);

        for (int frame = 0; frame < kManyUpdates; ++frame) {
            opponent.Update(model);
            ASSERT_FALSE(opponent.IsThinking()) << "white is to move";
        }
        EXPECT_EQ(engine->Calls(), 0);

        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(opponent, model, [&] { return model.GetMoves().size() == 2; }));
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            opponent.Update(model);
            ASSERT_FALSE(opponent.IsThinking()) << "white is to move again";
        }
        EXPECT_EQ(engine->Calls(), 1);
    }

    TEST_F(EngineOpponentTest, StartsNothingAfterTheGameIsOver) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen(kMatedBlackFen));
        ASSERT_TRUE(model.IsGameOver());
        const auto engine = MakeScripted({"e7e5"}, false);
        EngineOpponent opponent(engine, PieceColor::Black, kLimits);  // it is black's turn, and black is mated

        for (int frame = 0; frame < kManyUpdates; ++frame) {
            opponent.Update(model);
            ASSERT_FALSE(opponent.IsThinking());
        }

        EXPECT_EQ(engine->Calls(), 0);
        EXPECT_FALSE(opponent.GetLastError().has_value());
    }

    TEST_F(EngineOpponentTest, AResetWhileTheEngineThinksDropsItsAnswer) {
        SKIP_WITHOUT_FAKE_ENGINE();
        SKIP_WITHOUT_WORKER_THREADS();
        const TempDirectory directory;
        const String log = directory.FileText(kLogName);
        // The first search thinks until it is stopped and then answers e7e5; the second answers d7d5 at once
        ChessGameModel model;
        EngineOpponent opponent(MakeFakeEngine({"--log", log, "--think-ms", kThinksUntilStopped, "--think-first-only", "--moves", "e7e5,d7d5"}), PieceColor::Black, kLimits);
        ASSERT_TRUE(Play(model, "e2e4"));
        opponent.Update(model);
        ASSERT_TRUE(WaitForLogLine(directory.File(kLogName), kGoCommand));

        model.Reset();
        opponent.Update(model);  // the search is out of date: cancelled, the fake is sent `stop` and answers a game that is gone
        EXPECT_FALSE(opponent.IsThinking());
        EXPECT_TRUE(model.GetMoves().empty());

        // The same position again. The answer that is dropped would have been legal here (e7e5), so only the version tells it apart
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(opponent, model, [&] { return model.GetMoves().size() == 2; })) << Describe(opponent.GetLastError()).c_str();

        EXPECT_EQ(model.GetMoves()[1].ToUci(), "d7d5") << "e7e5 answered the game before the reset";
        EXPECT_FALSE(opponent.GetLastError().has_value());
    }

    TEST_F(EngineOpponentTest, AnEngineThatIgnoresItsTokenHasItsStaleAnswerDropped) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto engine = MakeScripted({"e7e5", "d7d5"}, true);
        EngineOpponent opponent(engine, PieceColor::Black, kLimits);
        ASSERT_TRUE(Play(model, "e2e4"));
        opponent.Update(model);
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; })) << "the engine has the search and waits at its gate";

        model.Reset();
        opponent.Update(model);
        EXPECT_FALSE(opponent.IsThinking()) << "cancelled, though the worker is still inside the engine";
        engine->Open();  // and now it answers e7e5, after the game it was asked about has gone
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(opponent, model, [&] { return model.GetMoves().size() == 2; })) << Describe(opponent.GetLastError()).c_str();

        EXPECT_EQ(model.GetMoves()[1].ToUci(), "d7d5") << "the answer of the old search was dropped, the new search had its own";
        EXPECT_EQ(engine->Calls(), 2) << "one search at a time: the second began after the first had ended";
        EXPECT_FALSE(opponent.GetLastError().has_value());
    }

    TEST_F(EngineOpponentTest, AMoveOfSomeoneElseMakesTheSearchStale) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto engine = MakeScripted({"e7e5"}, true);
        EngineOpponent opponent(engine, PieceColor::Black, kLimits);
        ASSERT_TRUE(Play(model, "e2e4"));
        opponent.Update(model);
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));

        ASSERT_TRUE(Play(model, "c7c5"));  // a person moves for black while the engine thinks
        opponent.Update(model);
        EXPECT_FALSE(opponent.IsThinking());
        engine->Open();  // its e7e5 is no move in this game
        ASSERT_TRUE(Play(model, "g1f3"));
        ASSERT_TRUE(UpdateUntil(opponent, model, [&] { return !opponent.IsThinking() && opponent.GetLastError().has_value(); })) << "the opponent asks again for the move after g1f3";

        // The new search finds the script used up: that is the only error there is. A stale e7e5 played here would have been refused (IllegalMove)
        EXPECT_EQ(opponent.GetLastError()->kind, EngineErrorKind::NoMove) << Describe(opponent.GetLastError()).c_str();
        EXPECT_EQ(model.GetMoves().size(), 3u);
        EXPECT_EQ(engine->Calls(), 2);
    }

    TEST_F(EngineOpponentTest, CancelDropsTheAnswerAndTheNextUpdateAsksAgain) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto engine = MakeScripted({"e7e5", "d7d5"}, true);
        EngineOpponent opponent(engine, PieceColor::Black, kLimits);
        ASSERT_TRUE(Play(model, "e2e4"));
        opponent.Update(model);
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));

        opponent.Cancel();
        EXPECT_FALSE(opponent.IsThinking());
        engine->Open();
        ASSERT_TRUE(UpdateUntil(opponent, model, [&] { return model.GetMoves().size() == 2; })) << Describe(opponent.GetLastError()).c_str();

        EXPECT_EQ(model.GetMoves()[1].ToUci(), "d7d5") << "the first answer was thrown away, the second search was asked";
        EXPECT_EQ(engine->Calls(), 2);
    }

    TEST_F(EngineOpponentTest, AMissingProgramIsAnErrorThatIsNotRetriedUntilItIsCleared) {
        UciEngineSettings settings;
        settings.launch.path = "/nonexistent-directory/engine";
        const auto engine = MakeShared<CountingEngine>(MakeShared<UciEngine>(std::move(settings)));
        ChessGameModel model;
        EngineOpponent opponent(engine, PieceColor::Black, kLimits);
        ASSERT_TRUE(Play(model, "e2e4"));

        ASSERT_TRUE(UpdateUntil(opponent, model, [&] { return opponent.GetLastError().has_value(); }));
        EXPECT_EQ(opponent.GetLastError()->kind, kMissingProgramError) << Describe(opponent.GetLastError()).c_str();
        EXPECT_FALSE(opponent.GetLastError()->detail.empty());
        EXPECT_FALSE(opponent.IsThinking());
        EXPECT_EQ(model.GetMoves().size(), 1u);

        for (int frame = 0; frame < kManyUpdates; ++frame) {
            opponent.Update(model);
            ASSERT_FALSE(opponent.IsThinking()) << "no search while the error stands";
        }
        EXPECT_EQ(engine->Calls(), 1);

        opponent.ClearError();
        EXPECT_FALSE(opponent.GetLastError().has_value());
        opponent.Update(model);
        EXPECT_TRUE(opponent.IsThinking()) << "after ClearError() it tries again";
        ASSERT_TRUE(UpdateUntil(opponent, model, [&] { return opponent.GetLastError().has_value(); }));
        EXPECT_EQ(opponent.GetLastError()->kind, kMissingProgramError);
        EXPECT_EQ(engine->Calls(), 2);
    }

    TEST_F(EngineOpponentTest, AnIllegalMoveOfTheEngineIsAnErrorAndTheModelIsUnchanged) {
        SKIP_WITHOUT_FAKE_ENGINE();
        ChessGameModel model;
        EngineOpponent opponent(MakeFakeEngine({"--moves", "e7e4"}), PieceColor::Black, kLimits);  // a pawn cannot go three squares
        ASSERT_TRUE(Play(model, "e2e4"));
        const uint64_t version = model.GetVersion();

        ASSERT_TRUE(UpdateUntil(opponent, model, [&] { return opponent.GetLastError().has_value(); }));

        EXPECT_EQ(opponent.GetLastError()->kind, EngineErrorKind::IllegalMove) << Describe(opponent.GetLastError()).c_str();
        EXPECT_EQ(model.GetVersion(), version);
        EXPECT_EQ(model.GetMoves().size(), 1u);
        EXPECT_FALSE(opponent.IsThinking());
    }

    TEST_F(EngineOpponentTest, AMoveTheGameRefusesIsAnErrorAndTheModelIsUnchanged) {
        ChessGameModel model;
        // An engine that does not check its move (UciEngine does): e2e4 is a white move, and black is to play
        const auto engine = MakeScripted({"e2e4"}, false);
        EngineOpponent opponent(engine, PieceColor::Black, kLimits);
        ASSERT_TRUE(Play(model, "d2d4"));
        const uint64_t version = model.GetVersion();

        ASSERT_TRUE(UpdateUntil(opponent, model, [&] { return opponent.GetLastError().has_value(); }));

        EXPECT_EQ(opponent.GetLastError()->kind, EngineErrorKind::IllegalMove);
        EXPECT_NE(opponent.GetLastError()->detail.find("e2e4"), String::npos) << Describe(opponent.GetLastError()).c_str();
        EXPECT_EQ(model.GetVersion(), version);
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            opponent.Update(model);
            ASSERT_FALSE(opponent.IsThinking()) << "no search while the error stands";
        }
        EXPECT_EQ(engine->Calls(), 1);
    }

    TEST_F(EngineOpponentTest, ADyingEngineIsProcessDiedAndTheOpponentRecoversAfterClearError) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const String marker = directory.FileText("died-once");
        ChessGameModel model;
        EngineOpponent opponent(MakeFakeEngine({"--die-once", marker, "--moves", "e7e5"}), PieceColor::Black, kLimits);
        ASSERT_TRUE(Play(model, "e2e4"));

        ASSERT_TRUE(UpdateUntil(opponent, model, [&] { return opponent.GetLastError().has_value(); }));
        EXPECT_EQ(opponent.GetLastError()->kind, EngineErrorKind::ProcessDied) << Describe(opponent.GetLastError()).c_str();
        EXPECT_EQ(model.GetMoves().size(), 1u);
        EXPECT_FALSE(opponent.IsThinking());

        opponent.ClearError();
        ASSERT_TRUE(UpdateUntil(opponent, model, [&] { return model.GetMoves().size() == 2; })) << Describe(opponent.GetLastError()).c_str();

        EXPECT_EQ(model.GetMoves()[1].ToUci(), "e7e5") << "a new process answered";
        EXPECT_FALSE(opponent.GetLastError().has_value());
    }

    TEST_F(EngineOpponentTest, DestroyingTheOpponentWhileTheEngineThinksCancelsTheSearchAndEndsTheChild) {
        SKIP_WITHOUT_FAKE_ENGINE();
        SKIP_WITHOUT_WORKER_THREADS();
        const TempDirectory directory;
        const String log = directory.FileText(kLogName);
        ChessGameModel model;
        {
            EngineOpponent opponent(MakeFakeEngine({"--log", log, "--think-ms", kThinksUntilStopped}), PieceColor::Black, kLimits);
            ASSERT_TRUE(Play(model, "e2e4"));
            opponent.Update(model);
            ASSERT_TRUE(WaitForLogLine(directory.File(kLogName), kGoCommand)) << "the fake has the search and is thinking";
        }  // nobody holds the engine now but the search; the destructor did not wait

        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), "stop")) << "the search was cancelled, a minute early";
        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), "quit")) << "and the engine, last held by the worker, ended its child";
        EXPECT_EQ(model.GetMoves().size(), 1u);
    }

    TEST_F(EngineOpponentTest, AGameInARowEndsLikeTheSameMovesPlayedByHand) {
        SKIP_WITHOUT_FAKE_ENGINE();
        constexpr StringView kWhiteMoves[] = {"e2e4", "g1f3", "f1b5"};
        constexpr StringView kBlackMoves[] = {"e7e5", "b8c6", "a7a6"};
        ChessGameModel model;
        EngineOpponent opponent(MakeFakeEngine({"--moves", "e7e5,b8c6,a7a6"}), PieceColor::Black, kLimits);
        ChessGameModel byHand;

        for (size_t round = 0; round < std::size(kWhiteMoves); ++round) {
            ASSERT_TRUE(Play(model, kWhiteMoves[round]));
            ASSERT_TRUE(UpdateUntil(opponent, model, [&] { return model.GetMoves().size() == 2 * (round + 1); })) << "round " << round << ": " << Describe(opponent.GetLastError()).c_str();
            ASSERT_TRUE(Play(byHand, kWhiteMoves[round]));
            ASSERT_TRUE(Play(byHand, kBlackMoves[round]));
        }

        EXPECT_EQ(model.GetMoves(), byHand.GetMoves());
        EXPECT_EQ(model.GetPosition().ToFen(), byHand.GetPosition().ToFen());
        EXPECT_EQ(model.GetPosition().Key(), byHand.GetPosition().Key());
        EXPECT_EQ(model.GetVersion(), byHand.GetVersion());
        EXPECT_EQ(model.GetStatus(), byHand.GetStatus());
        EXPECT_FALSE(opponent.GetLastError().has_value());
    }

    /// The real own engine over many slices (depth 4 is far more than one slice of nodes): the move is the blocking one, and a version that moves meanwhile drops the answer
    TEST_F(EngineOpponentTest, TheOwnEngineOverManySlicesPlaysTheBlockingMove) {
        constexpr SearchLimits kOwnLimits{0, 4};
        ChessGameModel model;
        const auto expected = OwnEngine().FindMove(SearchRequest::FromModel(model, kOwnLimits), CancellationToken::Create());
        ASSERT_TRUE(expected.has_value());
        EngineOpponent opponent(MakeShared<OwnEngine>(), PieceColor::White, kOwnLimits);

        opponent.Update(model);
        EXPECT_TRUE(opponent.IsThinking());
        ASSERT_TRUE(UpdateUntil(opponent, model, [&] { return model.GetMoves().size() == 1; })) << Describe(opponent.GetLastError()).c_str();

        EXPECT_EQ(model.GetMoves()[0], *expected);
        EXPECT_FALSE(opponent.IsThinking());
        EXPECT_FALSE(opponent.GetLastError().has_value());
    }

    TEST_F(EngineOpponentTest, TheOwnEnginesAnswerIsDroppedWhenTheVersionMovesMeanwhile) {
        constexpr SearchLimits kOwnLimits{0, 4};
        ChessGameModel model;
        EngineOpponent opponent(MakeShared<OwnEngine>(), PieceColor::White, kOwnLimits);

        opponent.Update(model);
        EXPECT_TRUE(opponent.IsThinking());
        ASSERT_TRUE(Play(model, "e2e4"));  // someone else moves for White

        ASSERT_TRUE(UpdateUntil(opponent, model, [&] { return !opponent.IsThinking(); })) << Describe(opponent.GetLastError()).c_str();
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            opponent.Update(model);
        }
        EXPECT_EQ(model.GetMoves().size(), 1u) << "the stale White move was not played, and Black's turn is not the opponent's";
        EXPECT_FALSE(opponent.GetLastError().has_value());
    }

    TEST_F(EngineOpponentTest, AnyEngineWillDoAndTwoOpponentsCanPlayEachOther) {
        constexpr size_t kPlies = 24;
        const auto engine = MakeShared<FirstLegalMoveEngine>();
        ChessGameModel model;
        EngineOpponent white(engine, PieceColor::White, kLimits);
        EngineOpponent black(engine, PieceColor::Black, kLimits);

        const bool finished = WaitUntil([&] {
            white.Update(model);
            black.Update(model);
            return model.GetMoves().size() >= kPlies || model.IsGameOver();
        });

        ASSERT_TRUE(finished) << "moves so far: " << model.GetMoves().size() << ", " << Describe(white.GetLastError()).c_str() << ", " << Describe(black.GetLastError()).c_str();
        EXPECT_FALSE(white.GetLastError().has_value());
        EXPECT_FALSE(black.GetLastError().has_value());
        // Every ply is the first legal move of the position before it, whoever played it
        ChessGameModel replay;
        for (const Move& move : model.GetMoves()) {
            const MoveList legal = MoveGenerator::GenerateLegal(replay.GetPosition());
            ASSERT_FALSE(legal.empty());
            ASSERT_EQ(move, legal.front());
            ASSERT_TRUE(replay.TryMove(move).has_value());
        }
        EXPECT_EQ(replay.GetPosition().Key(), model.GetPosition().Key());
        EXPECT_EQ(replay.GetVersion(), model.GetVersion());
    }

}  // namespace BECore::Chess::Tests
