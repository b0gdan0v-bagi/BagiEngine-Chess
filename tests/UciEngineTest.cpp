#include "ChessTestUtil.h"
#include "FakeEngineUtil.h"
#include "StockfishUtil.h"

#include <Chess/Engine/Analysis.h>
#include <Chess/Engine/SearchRequest.h>
#include <Chess/Engine/UciEngine.h>
#include <chrono>
#include <thread>

#if !defined(_WIN32)
#include <cstdlib>
#endif

/// UciEngine against the fake engine (tests/fake_uci_engine.py): what it sends, how it answers, how it recovers. No fixed sleeps: a test that must
/// not time out waits long, one that must is short. UciEngineStockfishTest runs the same class against a real Stockfish, when there is one
namespace BECore::Chess::Tests {

    namespace {

        using std::chrono::milliseconds;
        using Clock = std::chrono::steady_clock;

        constexpr int kMoveTimeMs = 50;
        constexpr int kMustNotTimeOutMs = 15000;  // a limit that only ends a broken test: the grace of a stop that the fake answers at once
        constexpr int kShortTimeoutMs = 100;      // a time that is meant to pass
        constexpr const char* kLogName = "fake.log";

        eastl::string StartFen() {
            return eastl::string(Position::kStartFen.data(), Position::kStartFen.size());
        }

        eastl::string StartPositionCommand() {
            return Format("position fen {}", Position::kStartFen);
        }

        SearchRequest MakeRequest(std::initializer_list<StringView> moves, SearchLimits limits = SearchLimits{kMoveTimeMs, 0}, const eastl::string& startFen = StartFen()) {
            SearchRequest request;
            request.startFen = startFen;
            for (const StringView move : moves) {
                request.moves.push_back(Mv(move));
            }
            request.limits = limits;
            return request;
        }

        UciEngineSettings FakeSettings(std::initializer_list<StringView> flags) {
            UciEngineSettings settings;
            settings.launch = FakeEngineLaunch(flags);
            return settings;
        }

        void ExpectMove(const std::expected<Move, EngineError>& result, StringView uci) {
            if (!result) {
                ADD_FAILURE() << "expected the move " << eastl::string(uci).c_str() << ", got " << Describe(result.error()).c_str();
                return;
            }
            EXPECT_EQ(result->ToUci(), eastl::string(uci));
        }

        void ExpectError(const std::expected<Move, EngineError>& result, EngineErrorKind kind) {
            if (result) {
                ADD_FAILURE() << "expected " << EnumUtils<EngineErrorKind>::ToString(kind).data() << ", got the move " << result->ToUci().c_str();
                return;
            }
            EXPECT_EQ(result.error().kind, kind) << result.error().detail.c_str();
            EXPECT_FALSE(result.error().detail.empty());
        }

        void ExpectAnalysis(const std::expected<Analysis, EngineError>& result, StringView uci, AnalysisScore score, int depth) {
            if (!result) {
                ADD_FAILURE() << "expected an analysis with the move " << eastl::string(uci).c_str() << ", got " << Describe(result.error()).c_str();
                return;
            }
            ASSERT_EQ(result->lines.size(), 1u);
            EXPECT_EQ(result->Best().move.ToUci(), eastl::string(uci));
            EXPECT_EQ(result->Best().score, score) << ScoreText(result->Best().score).c_str();
            EXPECT_EQ(result->Best().depth, depth);
        }

        void ExpectAnalysisError(const std::expected<Analysis, EngineError>& result, EngineErrorKind kind) {
            if (result) {
                ADD_FAILURE() << "expected " << EnumUtils<EngineErrorKind>::ToString(kind).data() << ", got the move " << result->Best().move.ToUci().c_str();
                return;
            }
            EXPECT_EQ(result.error().kind, kind) << result.error().detail.c_str();
            EXPECT_FALSE(result.error().detail.empty());
        }

        constexpr AnalysisScore Cp(int value) {
            return AnalysisScore{AnalysisScoreKind::Centipawns, value};
        }

        constexpr AnalysisScore MateOf(int moves) {
            return AnalysisScore{AnalysisScoreKind::Mate, moves};
        }

        size_t CountLines(const Vector<eastl::string>& lines, StringView text) {
            return static_cast<size_t>(std::count_if(lines.begin(), lines.end(), [text](const eastl::string& line) { return StringView(line) == text; }));
        }

        /// Cancels a token once the fake has received a line: the moment a search is known to be running on the other side of the pipe
        class CancelOnLogLine {
        public:
            CancelOnLogLine(CancellationToken token, std::filesystem::path logFile, eastl::string line)
                : _thread([this, token, logFile = std::move(logFile), line = std::move(line)]() mutable {
                      WaitForLogLine(logFile, StringView(line));  // cancels even when the line never came, so that the test fails and does not hang
                      _cancelledAt = Clock::now();
                      token.Cancel();
                  }) {}

            ~CancelOnLogLine() {
                Join();
            }
            CancelOnLogLine(const CancelOnLogLine&) = delete;
            CancelOnLogLine& operator=(const CancelOnLogLine&) = delete;

            /// When the token was cancelled
            Clock::time_point CancelledAt() {
                Join();
                return _cancelledAt;
            }

        private:
            void Join() {
                if (_thread.joinable()) {
                    _thread.join();
                }
            }

            Clock::time_point _cancelledAt;
            std::thread _thread;  // last: it starts in the constructor, and uses the members above
        };

    }  // namespace

#if !CHESS_HAS_UCI_PROCESS

    TEST(UciEngineTest, ASearchIsUnsupportedWithoutProcessCode) {
        UciEngineSettings settings;
        settings.launch.path = "engine.exe";
        UciEngine engine(std::move(settings));

        ExpectError(engine.FindMove(MakeRequest({}), CancellationToken()), EngineErrorKind::Unsupported);
    }

    TEST(UciEngineTest, AnAnalysisIsUnsupportedWithoutProcessCode) {
        UciEngineSettings settings;
        settings.launch.path = "engine.exe";
        UciEngine engine(std::move(settings));

        ExpectAnalysisError(engine.Analyse(MakeRequest({}), CancellationToken()), EngineErrorKind::Unsupported);
    }

#else

    TEST(UciEngineTest, TheConstructorStartsNothing) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        {
            const UciEngine engine(FakeSettings({"--log", log}));
        }

        EXPECT_FALSE(std::filesystem::exists(directory.File(kLogName)));
    }

    TEST(UciEngineTest, TheFirstSearchShakesHandsSetsTheOptionsStartsAGameAndSearches) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngineSettings settings = FakeSettings({"--log", log, "--moves", "e2e4"});
        settings.options.emplace_back("Skill Level", "5");
        settings.options.emplace_back("Threads", "1");
        UciEngine engine(std::move(settings));

        const auto result = engine.FindMove(MakeRequest({}), CancellationToken());

        ExpectMove(result, "e2e4");
        const eastl::string position = StartPositionCommand();
        EXPECT_EQ(ReadFileLines(directory.File(kLogName)),
                  Lines({"uci", "setoption name Skill Level value 5", "setoption name Threads value 1", "isready", "ucinewgame", "isready", position, "go movetime 50"}));
    }

    TEST(UciEngineTest, ASearchSendsTheMovesInThePositionCommandAndTheLimitsInGo) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log, "--moves", "g1f3,d7d5,e4d5"}));

        // One answer per search, each legal in the position that search asks about
        ExpectMove(engine.FindMove(MakeRequest({"e2e4", "e7e5"}, SearchLimits{100, 0}), CancellationToken()), "g1f3");
        ExpectMove(engine.FindMove(MakeRequest({"e2e4", "e7e5", "g1f3"}, SearchLimits{0, 6}), CancellationToken()), "d7d5");
        ExpectMove(engine.FindMove(MakeRequest({"e2e4", "e7e5", "g1f3", "d7d5"}, SearchLimits{80, 8}), CancellationToken()), "e4d5");

        const Vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
        EXPECT_EQ(CountLines(lines, Format("{} moves e2e4 e7e5", StartPositionCommand())), 1u);
        EXPECT_EQ(CountLines(lines, Format("{} moves e2e4 e7e5 g1f3", StartPositionCommand())), 1u);
        EXPECT_EQ(CountLines(lines, Format("{} moves e2e4 e7e5 g1f3 d7d5", StartPositionCommand())), 1u);
        EXPECT_EQ(CountLines(lines, "go movetime 100"), 1u);
        EXPECT_EQ(CountLines(lines, "go depth 6"), 1u);
        EXPECT_EQ(CountLines(lines, "go depth 8 movetime 80"), 1u);
    }

    TEST(UciEngineTest, OnlyANewGameSendsUcinewgame) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        // One answer per search, each legal in the position that search asks about
        UciEngine engine(FakeSettings({"--log", log, "--moves", "e7e5,b8c6,e2e4,e7e5,d7d5"}));
        const eastl::string pawnEndgame = "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1";
        const auto ucinewgames = [&] { return CountLines(ReadFileLines(directory.File(kLogName)), "ucinewgame"); };

        ExpectMove(engine.FindMove(MakeRequest({"e2e4"}), CancellationToken()), "e7e5");
        EXPECT_EQ(ucinewgames(), 1u) << "the first search of a process starts a game";

        ExpectMove(engine.FindMove(MakeRequest({"e2e4", "e7e5", "g1f3"}), CancellationToken()), "b8c6");
        EXPECT_EQ(ucinewgames(), 1u) << "the moves extend the previous request's: the same game";

        ExpectMove(engine.FindMove(MakeRequest({}, SearchLimits{kMoveTimeMs, 0}, pawnEndgame), CancellationToken()), "e2e4");
        EXPECT_EQ(ucinewgames(), 2u) << "another start FEN";

        ExpectMove(engine.FindMove(MakeRequest({"e2e4"}), CancellationToken()), "e7e5");
        EXPECT_EQ(ucinewgames(), 3u) << "the start FEN is back";

        ExpectMove(engine.FindMove(MakeRequest({"d2d4"}), CancellationToken()), "d7d5");
        EXPECT_EQ(ucinewgames(), 4u) << "the moves do not extend the previous request's";
    }

    TEST(UciEngineTest, InfoLinesAndUnknownLinesAreSkipped) {
        SKIP_WITHOUT_FAKE_ENGINE();
        UciEngine engine(FakeSettings({"--garbage", "--moves", "e2e4,d2d4"}));  // the fake prints an info line before every bestmove

        ExpectMove(engine.FindMove(MakeRequest({}), CancellationToken()), "e2e4");
        ExpectMove(engine.FindMove(MakeRequest({}), CancellationToken()), "d2d4");
    }

    TEST(UciEngineTest, ALineWrittenInTwoPiecesIsOneLine) {
        SKIP_WITHOUT_FAKE_ENGINE();
        UciEngine engine(FakeSettings({"--split-lines", "--moves", "e2e4"}));

        ExpectMove(engine.FindMove(MakeRequest({}), CancellationToken()), "e2e4");
    }

    TEST(UciEngineTest, BestmoveNoneIsNoMoveAndTheEngineIsKept) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log, "--moves", "(none),a7a6"}));
        const SearchRequest request = MakeRequest({"e2e4"});  // black to move, and black has moves

        const auto none = engine.FindMove(request, CancellationToken());
        const auto next = engine.FindMove(request, CancellationToken());

        ExpectError(none, EngineErrorKind::NoMove);
        ExpectMove(next, "a7a6");
        const Vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
        EXPECT_EQ(CountLines(lines, "uci"), 1u) << "the engine was alive and in step: no new process";
        EXPECT_EQ(CountLines(lines, "quit"), 0u);
    }

    TEST(UciEngineTest, BestmoveZeroIsNoMove) {
        SKIP_WITHOUT_FAKE_ENGINE();
        UciEngine engine(FakeSettings({"--moves", "0000"}));

        ExpectError(engine.FindMove(MakeRequest({"e2e4"}), CancellationToken()), EngineErrorKind::NoMove);
    }

    TEST(UciEngineTest, AGameThatIsOverIsNoMoveAndNoProcessIsStarted) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log}));

        const eastl::string mate = "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1";
        const eastl::string stalemate = "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1";
        const auto mated = engine.FindMove(MakeRequest({}, SearchLimits{kMoveTimeMs, 0}, mate), CancellationToken());
        const auto stalemated = engine.FindMove(MakeRequest({}, SearchLimits{kMoveTimeMs, 0}, stalemate), CancellationToken());

        ExpectError(mated, EngineErrorKind::NoMove);
        ExpectError(stalemated, EngineErrorKind::NoMove);
        EXPECT_FALSE(std::filesystem::exists(directory.File(kLogName))) << "the fake was started";
    }

    TEST(UciEngineTest, AnIllegalMoveFromTheEngineIsIllegalMoveAndTheEngineIsKept) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log, "--moves", "e2e5,e2e4"}));

        const auto illegal = engine.FindMove(MakeRequest({}), CancellationToken());
        const auto again = engine.FindMove(MakeRequest({}), CancellationToken());

        ExpectError(illegal, EngineErrorKind::IllegalMove);
        if (!illegal) {
            EXPECT_NE(illegal.error().detail.find("e2e5"), eastl::string::npos) << illegal.error().detail.c_str();
        }
        ExpectMove(again, "e2e4");
        const Vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
        EXPECT_EQ(CountLines(lines, "uci"), 1u) << "the same process answered both";
        EXPECT_EQ(CountLines(lines, "ucinewgame"), 1u) << "the same position again is not a new game";
        EXPECT_EQ(CountLines(lines, "quit"), 0u);
    }

    TEST(UciEngineTest, AnEngineThatNeverAnswersIsTimeoutAndIsReplaced) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngineSettings settings = FakeSettings({"--log", log, "--hang-on-go"});
        settings.answerSlackMs = kShortTimeoutMs;
        UciEngine engine(std::move(settings));
        const SearchRequest request = MakeRequest({}, SearchLimits{20, 0});

        const Clock::time_point begin = Clock::now();
        const auto first = engine.FindMove(request, CancellationToken());
        const Clock::duration took = Clock::now() - begin;

        ExpectError(first, EngineErrorKind::Timeout);
        EXPECT_GE(took, milliseconds(20 + kShortTimeoutMs));  // movetime + slack, not earlier
        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), "quit")) << "the hung engine was told to leave";
        EXPECT_EQ(CountLines(ReadFileLines(directory.File(kLogName)), "uci"), 1u);

        ExpectError(engine.FindMove(request, CancellationToken()), EngineErrorKind::Timeout);
        const Vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
        EXPECT_EQ(CountLines(lines, "uci"), 2u) << "the second search started a process of its own";
        EXPECT_EQ(CountLines(lines, "ucinewgame"), 2u) << "and a new process starts a game";
    }

    TEST(UciEngineTest, AHugeMoveTimeKeepsADeadlineThatIsInTheFuture) {
        SKIP_WITHOUT_FAKE_ENGINE();
        // movetime + answerSlackMs does not fit in an int; wrapping round would give a negative deadline and a Timeout at once
        UciEngine engine(FakeSettings({"--think-ms", "100", "--moves", "e2e4"}));

        ExpectMove(engine.FindMove(MakeRequest({}, SearchLimits{std::numeric_limits<int>::max(), 0}), CancellationToken()), "e2e4");
    }

    TEST(UciEngineTest, AnEngineThatDiesOnGoIsProcessDiedEachTime) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log, "--die-on-go"}));

        ExpectError(engine.FindMove(MakeRequest({}), CancellationToken()), EngineErrorKind::ProcessDied);
        ExpectError(engine.FindMove(MakeRequest({}), CancellationToken()), EngineErrorKind::ProcessDied);

        EXPECT_EQ(CountLines(ReadFileLines(directory.File(kLogName)), "uci"), 2u) << "a new process for the second search";
    }

    TEST(UciEngineTest, ARestartAfterADeathWorks) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        const eastl::string marker = directory.FileText("died-once");
        UciEngine engine(FakeSettings({"--log", log, "--die-once", marker, "--moves", "e2e4"}));

        ExpectError(engine.FindMove(MakeRequest({}), CancellationToken()), EngineErrorKind::ProcessDied);
        ExpectMove(engine.FindMove(MakeRequest({}), CancellationToken()), "e2e4");

        EXPECT_EQ(CountLines(ReadFileLines(directory.File(kLogName)), "uci"), 2u);
    }

    TEST(UciEngineTest, NoUciokIsATimeoutAfterTheStartupTimeAndTheProcessIsClosed) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngineSettings settings = FakeSettings({"--log", log, "--no-uciok"});
        settings.startupTimeoutMs = 300;
        UciEngine engine(std::move(settings));

        const Clock::time_point begin = Clock::now();
        const auto result = engine.FindMove(MakeRequest({}), CancellationToken());
        const Clock::duration took = Clock::now() - begin;

        ExpectError(result, EngineErrorKind::Timeout);
        EXPECT_GE(took, milliseconds(300));
        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), "quit"));
        EXPECT_EQ(CountLines(ReadFileLines(directory.File(kLogName)), "go movetime 50"), 0u) << "no search on an engine that did not shake hands";
    }

    TEST(UciEngineTest, ALineBeyondTheCapIsProtocolErrorAndTheProcessIsClosed) {
        SKIP_WITHOUT_FAKE_ENGINE();
        constexpr const char* kTooLong = "1100000";  // UciProcess::kMaxLineBytes is 1 MiB
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngineSettings settings = FakeSettings({"--log", log, "--long-line", kTooLong});
        // The rest of the line (about 50 KB) stays unread in the pipe: the child finishes writing it and leaves on `quit`. That relies on a pipe that holds this much;
        // the short grace keeps one that does not from costing seconds
        settings.quitGraceMs = kShortTimeoutMs;
        UciEngine engine(std::move(settings));

        ExpectError(engine.FindMove(MakeRequest({}), CancellationToken()), EngineErrorKind::ProtocolError);
        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), "quit")) << "the stream is out of step: the process is closed";

        ExpectError(engine.FindMove(MakeRequest({}), CancellationToken()), EngineErrorKind::ProtocolError);
        EXPECT_EQ(CountLines(ReadFileLines(directory.File(kLogName)), "uci"), 2u) << "and the next search starts another one";
    }

    TEST(UciEngineTest, AProgramThatCannotBeStartedIsLaunchFailedEachTime) {
        UciEngineSettings settings;
        settings.launch.path = "/nonexistent-directory/engine";
        UciEngine engine(std::move(settings));

        ExpectError(engine.FindMove(MakeRequest({}), CancellationToken()), EngineErrorKind::LaunchFailed);
        ExpectError(engine.FindMove(MakeRequest({}), CancellationToken()), EngineErrorKind::LaunchFailed);
    }

    TEST(UciEngineTest, ACancelledSearchStopsTheEngineAndTheEngineIsReusable) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        // The first go thinks for ten minutes unless it is stopped (a stop that hangs is ended by ctest's timeout); the second answers at once. The answer to `stop` is e2e4 and must not be taken
        // for the second search's. The grace is long: a slow machine must not take the answer to `stop` for no answer and close the engine
        UciEngineSettings settings = FakeSettings({"--log", log, "--think-ms", "600000", "--think-first-only", "--moves", "e2e4,d2d4"});
        settings.stopGraceMs = kMustNotTimeOutMs;
        UciEngine engine(std::move(settings));
        const CancellationToken token = CancellationToken::Create();
        CancelOnLogLine canceller(token, directory.File(kLogName), "go movetime 50");

        const auto cancelled = engine.FindMove(MakeRequest({}), token);

        ExpectError(cancelled, EngineErrorKind::Cancelled);
        ExpectMove(engine.FindMove(MakeRequest({}), CancellationToken()), "d2d4");
        const Vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
        EXPECT_EQ(CountLines(lines, "stop"), 1u);
        EXPECT_EQ(CountLines(lines, "uci"), 1u) << "the engine answered `stop`: it stays";
        EXPECT_EQ(CountLines(lines, "quit"), 0u);
    }

    TEST(UciEngineTest, ACancelledSearchOfAnEngineThatIgnoresStopClosesTheProcessAfterTheGrace) {
        SKIP_WITHOUT_FAKE_ENGINE();
        constexpr int kStopGraceMs = 200;
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngineSettings settings = FakeSettings({"--log", log, "--think-ms", "600000", "--ignore-stop"});
        settings.stopGraceMs = kStopGraceMs;
        UciEngine engine(std::move(settings));
        const CancellationToken token = CancellationToken::Create();
        CancelOnLogLine canceller(token, directory.File(kLogName), "go movetime 50");

        const auto result = engine.FindMove(MakeRequest({}), token);
        const Clock::time_point returned = Clock::now();

        ExpectError(result, EngineErrorKind::Cancelled);
        EXPECT_GE(returned - canceller.CancelledAt(), milliseconds(kStopGraceMs)) << "the engine had its grace to answer `stop`";
        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), "quit")) << "and then it was closed";
        const Vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
        EXPECT_EQ(CountLines(lines, "stop"), 1u);
    }

    TEST(UciEngineTest, ATokenCancelledBeforeTheSearchIsCancelledWithoutAProcess) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log}));
        CancellationToken token = CancellationToken::Create();
        token.Cancel();

        ExpectError(engine.FindMove(MakeRequest({}), token), EngineErrorKind::Cancelled);

        EXPECT_FALSE(std::filesystem::exists(directory.File(kLogName)));
    }

    TEST(UciEngineTest, LimitsThatAskForNothingAreBadRequestWithoutAProcess) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log}));

        ExpectError(engine.FindMove(MakeRequest({}, SearchLimits{0, 0}), CancellationToken()), EngineErrorKind::BadRequest);
        ExpectError(engine.FindMove(MakeRequest({}, SearchLimits{-1, 5}), CancellationToken()), EngineErrorKind::BadRequest);

        EXPECT_FALSE(std::filesystem::exists(directory.File(kLogName)));
    }

    TEST(UciEngineTest, ABrokenRequestIsBadRequestWithoutAProcess) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log}));

        ExpectError(engine.FindMove(MakeRequest({}, SearchLimits{kMoveTimeMs, 0}, "not a fen"), CancellationToken()), EngineErrorKind::BadRequest);
        ExpectError(engine.FindMove(MakeRequest({"e2e5"}), CancellationToken()), EngineErrorKind::BadRequest);

        EXPECT_FALSE(std::filesystem::exists(directory.File(kLogName)));
    }

    TEST(UciEngineTest, TheDestructorEndsTheChild) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        {
            UciEngine engine(FakeSettings({"--log", log}));
            ExpectMove(engine.FindMove(MakeRequest({}), CancellationToken()), "e2e4");
            EXPECT_EQ(CountLines(ReadFileLines(directory.File(kLogName)), "quit"), 0u);
        }

        // The destructor closed the process and waited for it; the line `quit` is its polite request
        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), "quit"));
    }

    TEST(UciEngineTest, TheDestructorEndsAChildThatIgnoresQuit) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        {
            UciEngineSettings settings = FakeSettings({"--log", log, "--ignore-quit"});
            settings.quitGraceMs = kShortTimeoutMs;
            UciEngine engine(std::move(settings));
            ExpectMove(engine.FindMove(MakeRequest({}), CancellationToken()), "e2e4");
        }

        // The destructor returned, so the child that ignores `quit` was ended by a signal after the grace
        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), "quit"));
    }

    TEST(UciEngineTest, TwoThreadsAreServedOneAfterTheOther) {
        SKIP_WITHOUT_FAKE_ENGINE();
        constexpr milliseconds kThink{100};
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log, "--think-ms", "100", "--moves", "e2e4,d2d4"}));
        std::expected<Move, EngineError> results[2] = {std::unexpected(EngineError{}), std::unexpected(EngineError{})};

        const Clock::time_point begin = Clock::now();
        {
            std::thread first([&] { results[0] = engine.FindMove(MakeRequest({}), CancellationToken()); });
            std::thread second([&] { results[1] = engine.FindMove(MakeRequest({}), CancellationToken()); });
            first.join();
            second.join();
        }
        const Clock::duration took = Clock::now() - begin;

        ASSERT_TRUE(results[0].has_value()) << Describe(results[0].error()).c_str();
        ASSERT_TRUE(results[1].has_value()) << Describe(results[1].error()).c_str();
        Vector<eastl::string> answers = {results[0]->ToUci(), results[1]->ToUci()};
        std::sort(answers.begin(), answers.end());
        EXPECT_EQ(answers, Lines({"d2d4", "e2e4"})) << "each search got the answer of its own go";
        EXPECT_GE(took, 2 * kThink) << "the second search waited for the first";

        // Commands never interleave: every position is followed by its go, then the next search's position
        Vector<eastl::string> searchLines;
        for (const eastl::string& line : ReadFileLines(directory.File(kLogName))) {
            if (line.find("position") == 0 || line.find("go") == 0) {
                searchLines.push_back(line);
            }
        }
        const eastl::string position = StartPositionCommand();
        EXPECT_EQ(searchLines, Lines({position, "go movetime 50", position, "go movetime 50"}));
    }

    // -- Analyse ---------------------------------------------------------------------------------------------------------------

    TEST(UciEngineTest, AnalyseGivesTheScriptedMoveWithItsCentipawnScoreAndDepth) {
        SKIP_WITHOUT_FAKE_ENGINE();
        UciEngine engine(FakeSettings({"--moves", "e2e4", "--scores", "cp:35"}));

        ExpectAnalysis(engine.Analyse(MakeRequest({}), CancellationToken()), "e2e4", Cp(35), 1);
    }

    TEST(UciEngineTest, AnalyseReadsAMateScoreAndANegativeOne) {
        SKIP_WITHOUT_FAKE_ENGINE();
        UciEngine engine(FakeSettings({"--moves", "e2e4,e7e5,g1f3", "--scores", "mate:3,mate:-2,cp:-120"}));

        ExpectAnalysis(engine.Analyse(MakeRequest({}), CancellationToken()), "e2e4", MateOf(3), 1);
        ExpectAnalysis(engine.Analyse(MakeRequest({"e2e4"}), CancellationToken()), "e7e5", MateOf(-2), 1);
        ExpectAnalysis(engine.Analyse(MakeRequest({"e2e4", "e7e5"}), CancellationToken()), "g1f3", Cp(-120), 1);
    }

    TEST(UciEngineTest, AnalyseTakesTheExactMainLineScoreOfTheLastDepthAmongNoise) {
        SKIP_WITHOUT_FAKE_ENGINE();
        // Around the final line the fake prints an earlier depth, the same depth as a lower bound, a second line of the search, a text line with a score in it and an upper bound after it
        UciEngine engine(FakeSettings({"--noisy-info", "--moves", "e2e4,e7e5", "--scores", "cp:42,mate:2"}));

        ExpectAnalysis(engine.Analyse(MakeRequest({}), CancellationToken()), "e2e4", Cp(42), 2);
        ExpectAnalysis(engine.Analyse(MakeRequest({"e2e4"}), CancellationToken()), "e7e5", MateOf(2), 2);
    }

    TEST(UciEngineTest, AnalyseWithNoScoreIsProtocolErrorAndTheProcessIsKept) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log, "--no-score", "--moves", "e2e4,d2d4,g1f3"}));

        const auto first = engine.Analyse(MakeRequest({}), CancellationToken());
        const auto played = engine.FindMove(MakeRequest({}), CancellationToken());
        const auto second = engine.Analyse(MakeRequest({}), CancellationToken());

        ExpectAnalysisError(first, EngineErrorKind::ProtocolError);
        if (!first) {
            EXPECT_NE(first.error().detail.find("no score"), eastl::string::npos) << first.error().detail.c_str();
        }
        ExpectMove(played, "d2d4");  // the same object, the same process: a move needs no score
        ExpectAnalysisError(second, EngineErrorKind::ProtocolError);
        const Vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
        EXPECT_EQ(CountLines(lines, "uci"), 1u) << "the engine was in step: no new process";
        EXPECT_EQ(CountLines(lines, "ucinewgame"), 1u);
        EXPECT_EQ(CountLines(lines, "quit"), 0u);
    }

    TEST(UciEngineTest, AnalyseSendsTheSameCommandsAsFindMove) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string moveLog = directory.FileText("find.log");
        const eastl::string analysisLog = directory.FileText("analyse.log");
        UciEngineSettings analyserSettings = FakeSettings({"--log", analysisLog, "--moves", "g1f3"});
        analyserSettings.options.emplace_back("Skill Level", "5");
        UciEngineSettings playerSettings = FakeSettings({"--log", moveLog, "--moves", "g1f3"});
        playerSettings.options.emplace_back("Skill Level", "5");
        UciEngine analyser(std::move(analyserSettings));
        UciEngine player(std::move(playerSettings));
        const SearchRequest request = MakeRequest({"e2e4", "e7e5"}, SearchLimits{80, 8});

        ExpectAnalysis(analyser.Analyse(request, CancellationToken()), "g1f3", Cp(10), 1);
        ExpectMove(player.FindMove(request, CancellationToken()), "g1f3");

        const Vector<eastl::string> lines = ReadFileLines(directory.File("analyse.log"));
        EXPECT_EQ(lines, ReadFileLines(directory.File("find.log")));
        EXPECT_EQ(CountLines(lines, "go depth 8 movetime 80"), 1u);
        EXPECT_EQ(CountLines(lines, Format("{} moves e2e4 e7e5", StartPositionCommand())), 1u);
    }

    TEST(UciEngineTest, AnalyseOfAGameThatIsOverIsNoMoveAndNoProcessIsStarted) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log}));

        const eastl::string mate = "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1";
        const eastl::string stalemate = "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1";
        ExpectAnalysisError(engine.Analyse(MakeRequest({}, SearchLimits{kMoveTimeMs, 0}, mate), CancellationToken()), EngineErrorKind::NoMove);
        ExpectAnalysisError(engine.Analyse(MakeRequest({}, SearchLimits{kMoveTimeMs, 0}, stalemate), CancellationToken()), EngineErrorKind::NoMove);

        EXPECT_FALSE(std::filesystem::exists(directory.File(kLogName))) << "the fake was started";
    }

    TEST(UciEngineTest, AnalyseWhenTheEngineSaysBestmoveNoneOrZerosIsNoMoveAndTheEngineIsKept) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log, "--moves", "(none),0000,a7a6"}));
        const SearchRequest request = MakeRequest({"e2e4"});  // black to move, and black has moves

        const auto none = engine.Analyse(request, CancellationToken());
        const auto zeros = engine.Analyse(request, CancellationToken());
        const auto next = engine.Analyse(request, CancellationToken());

        ExpectAnalysisError(none, EngineErrorKind::NoMove);
        ExpectAnalysisError(zeros, EngineErrorKind::NoMove);
        ExpectAnalysis(next, "a7a6", Cp(10), 1);
        const Vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
        EXPECT_EQ(CountLines(lines, "uci"), 1u);
        EXPECT_EQ(CountLines(lines, "quit"), 0u);
    }

    TEST(UciEngineTest, AnalyseWithAnIllegalMoveFromTheEngineIsIllegalMoveAndTheEngineIsKept) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log, "--moves", "e2e5,e2e4"}));

        const auto illegal = engine.Analyse(MakeRequest({}), CancellationToken());
        const auto again = engine.Analyse(MakeRequest({}), CancellationToken());

        ExpectAnalysisError(illegal, EngineErrorKind::IllegalMove);
        if (!illegal) {
            EXPECT_NE(illegal.error().detail.find("e2e5"), eastl::string::npos) << illegal.error().detail.c_str();
        }
        ExpectAnalysis(again, "e2e4", Cp(10), 1);
        const Vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
        EXPECT_EQ(CountLines(lines, "uci"), 1u);
        EXPECT_EQ(CountLines(lines, "quit"), 0u);
    }

    TEST(UciEngineTest, AnalyseWithBadLimitsOrARequestThatIsBrokenIsBadRequestWithoutAProcess) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log}));

        ExpectAnalysisError(engine.Analyse(MakeRequest({}, SearchLimits{0, 0}), CancellationToken()), EngineErrorKind::BadRequest);
        ExpectAnalysisError(engine.Analyse(MakeRequest({}, SearchLimits{-1, 5}), CancellationToken()), EngineErrorKind::BadRequest);
        ExpectAnalysisError(engine.Analyse(MakeRequest({}, SearchLimits{kMoveTimeMs, 0}, "not a fen"), CancellationToken()), EngineErrorKind::BadRequest);
        ExpectAnalysisError(engine.Analyse(MakeRequest({"e2e5"}), CancellationToken()), EngineErrorKind::BadRequest);

        EXPECT_FALSE(std::filesystem::exists(directory.File(kLogName)));
    }

    TEST(UciEngineTest, AnalyseWithATokenCancelledBeforeIsCancelledWithoutAProcess) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log}));
        CancellationToken token = CancellationToken::Create();
        token.Cancel();

        ExpectAnalysisError(engine.Analyse(MakeRequest({}), token), EngineErrorKind::Cancelled);

        EXPECT_FALSE(std::filesystem::exists(directory.File(kLogName)));
    }

    TEST(UciEngineTest, AnalyseOfAnEngineThatNeverAnswersIsTimeoutAndIsReplaced) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngineSettings settings = FakeSettings({"--log", log, "--hang-on-go"});
        settings.answerSlackMs = kShortTimeoutMs;
        UciEngine engine(std::move(settings));
        const SearchRequest request = MakeRequest({}, SearchLimits{20, 0});

        const Clock::time_point begin = Clock::now();
        const auto first = engine.Analyse(request, CancellationToken());
        const Clock::duration took = Clock::now() - begin;

        ExpectAnalysisError(first, EngineErrorKind::Timeout);
        EXPECT_GE(took, milliseconds(20 + kShortTimeoutMs));
        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), "quit")) << "the hung engine was told to leave";

        ExpectAnalysisError(engine.Analyse(request, CancellationToken()), EngineErrorKind::Timeout);
        EXPECT_EQ(CountLines(ReadFileLines(directory.File(kLogName)), "uci"), 2u) << "the second analysis started a process of its own";
    }

    TEST(UciEngineTest, AnalyseOfAnEngineThatDiesIsProcessDiedAndTheNextOneRestarts) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        const eastl::string marker = directory.FileText("died-once");
        UciEngine engine(FakeSettings({"--log", log, "--die-once", marker, "--moves", "e2e4", "--scores", "cp:7"}));

        ExpectAnalysisError(engine.Analyse(MakeRequest({}), CancellationToken()), EngineErrorKind::ProcessDied);
        ExpectAnalysis(engine.Analyse(MakeRequest({}), CancellationToken()), "e2e4", Cp(7), 1);

        EXPECT_EQ(CountLines(ReadFileLines(directory.File(kLogName)), "uci"), 2u);
    }

    TEST(UciEngineTest, ACancelledAnalysisStopsTheEngineAndTheEngineIsReusable) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        // As the search above: ten minutes unless stopped, then an answer at once; the answer to `stop` is e2e4 and must not be taken for the second search's
        UciEngineSettings settings = FakeSettings({"--log", log, "--think-ms", "600000", "--think-first-only", "--moves", "e2e4,d2d4", "--scores", "cp:1,cp:2"});
        settings.stopGraceMs = kMustNotTimeOutMs;
        UciEngine engine(std::move(settings));
        const CancellationToken token = CancellationToken::Create();
        CancelOnLogLine canceller(token, directory.File(kLogName), "go movetime 50");

        const auto cancelled = engine.Analyse(MakeRequest({}), token);

        ExpectAnalysisError(cancelled, EngineErrorKind::Cancelled);
        ExpectAnalysis(engine.Analyse(MakeRequest({}), CancellationToken()), "d2d4", Cp(2), 1);
        const Vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
        EXPECT_EQ(CountLines(lines, "stop"), 1u);
        EXPECT_EQ(CountLines(lines, "uci"), 1u) << "the engine answered `stop`: it stays";
        EXPECT_EQ(CountLines(lines, "quit"), 0u);
    }

    TEST(UciEngineTest, AnalyseAndFindMoveOnOneObjectShareTheNewGameRule) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log, "--moves", "e7e5,b8c6,e2e4,e7e5", "--scores", "cp:1,cp:2,cp:3,cp:4"}));
        const eastl::string pawnEndgame = "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1";
        const auto ucinewgames = [&] { return CountLines(ReadFileLines(directory.File(kLogName)), "ucinewgame"); };

        ExpectMove(engine.FindMove(MakeRequest({"e2e4"}), CancellationToken()), "e7e5");
        EXPECT_EQ(ucinewgames(), 1u);

        ExpectAnalysis(engine.Analyse(MakeRequest({"e2e4", "e7e5", "g1f3"}), CancellationToken()), "b8c6", Cp(2), 1);
        EXPECT_EQ(ucinewgames(), 1u) << "the moves extend what FindMove asked: the same game";

        ExpectAnalysis(engine.Analyse(MakeRequest({}, SearchLimits{kMoveTimeMs, 0}, pawnEndgame), CancellationToken()), "e2e4", Cp(3), 1);
        EXPECT_EQ(ucinewgames(), 2u) << "another start FEN";

        ExpectMove(engine.FindMove(MakeRequest({"e2e4"}), CancellationToken()), "e7e5");
        EXPECT_EQ(ucinewgames(), 3u) << "the start FEN is back";
        EXPECT_EQ(CountLines(ReadFileLines(directory.File(kLogName)), "uci"), 1u);
    }

    TEST(UciEngineTest, TwoThreadsAnalysingAreServedOneAfterTheOther) {
        SKIP_WITHOUT_FAKE_ENGINE();
        constexpr milliseconds kThink{100};
        UciEngine engine(FakeSettings({"--think-ms", "100", "--moves", "e2e4,d2d4", "--scores", "cp:1,cp:2"}));
        std::expected<Analysis, EngineError> results[2] = {std::unexpected(EngineError{}), std::unexpected(EngineError{})};

        const Clock::time_point begin = Clock::now();
        {
            std::thread first([&] { results[0] = engine.Analyse(MakeRequest({}), CancellationToken()); });
            std::thread second([&] { results[1] = engine.Analyse(MakeRequest({}), CancellationToken()); });
            first.join();
            second.join();
        }
        const Clock::duration took = Clock::now() - begin;

        ASSERT_TRUE(results[0].has_value()) << Describe(results[0].error()).c_str();
        ASSERT_TRUE(results[1].has_value()) << Describe(results[1].error()).c_str();
        // Each analysis got the move and the score of its own go, never the move of one and the score of the other
        for (const auto& result : results) {
            const eastl::string move = result->Best().move.ToUci();
            EXPECT_EQ(result->Best().score, move == "e2e4" ? Cp(1) : Cp(2)) << move.c_str();
        }
        EXPECT_NE(results[0]->Best().move, results[1]->Best().move);
        EXPECT_GE(took, 2 * kThink) << "the second analysis waited for the first";
    }

    // -- Analyse with several lines (MultiPV) ----------------------------------------------------------------------------------

    namespace {

        struct ExpectedLine {
            StringView move;
            AnalysisScore score;
            int depth = 2;  // the final depth the fake prints for --lines with MultiPV above 1
        };

        SearchRequest WithLines(SearchRequest request, int lines) {
            request.lines = lines;
            return request;
        }

        void ExpectLines(const std::expected<Analysis, EngineError>& result, std::initializer_list<ExpectedLine> expected) {
            if (!result) {
                ADD_FAILURE() << "expected an analysis of " << expected.size() << " lines, got " << Describe(result.error()).c_str();
                return;
            }
            ASSERT_EQ(result->lines.size(), expected.size());
            size_t at = 0;
            for (const ExpectedLine& line : expected) {
                const AnalysisLine& got = result->lines[at];
                EXPECT_EQ(got.move.ToUci(), eastl::string(line.move)) << "line " << at + 1;
                EXPECT_EQ(got.score, line.score) << "line " << at + 1 << ": " << ScoreText(got.score).c_str();
                EXPECT_EQ(got.depth, line.depth) << "line " << at + 1;
                ++at;
            }
        }

        size_t CountMultiPvOptions(const Vector<eastl::string>& lines) {
            return static_cast<size_t>(std::count_if(lines.begin(), lines.end(), [](const eastl::string& line) { return line.find("setoption name MultiPV") == 0; }));
        }

        constexpr const char* kThreeLines = "e2e4:cp:30,d2d4:cp:20,g1f3:cp:-15";

    }  // namespace

    TEST(UciEngineTest, AnalyseWithTwoLinesAsksForMultiPvBeforePositionAndGivesTheScriptedLines) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log, "--lines", kThreeLines}));

        // The fake prints depth 1 lines with a score that must not be taken, then the depth 2 ones; line 3 is not asked for
        ExpectLines(engine.Analyse(WithLines(MakeRequest({}), 2), CancellationToken()), {{"e2e4", Cp(30)}, {"d2d4", Cp(20)}});

        EXPECT_EQ(ReadFileLines(directory.File(kLogName)), Lines({"uci", "isready", "ucinewgame", "isready", "setoption name MultiPV value 2", StartPositionCommand(), "go movetime 50"}));
    }

    TEST(UciEngineTest, AnalyseWithThreeLinesGivesThemBestFirstWithTheirScoresFromTheSideToMove) {
        SKIP_WITHOUT_FAKE_ENGINE();
        UciEngine engine(FakeSettings({"--lines", kThreeLines}));

        ExpectLines(engine.Analyse(WithLines(MakeRequest({}), 3), CancellationToken()), {{"e2e4", Cp(30)}, {"d2d4", Cp(20)}, {"g1f3", Cp(-15)}});
    }

    TEST(UciEngineTest, AnalyseReadsAMateScoreOnALaterLine) {
        SKIP_WITHOUT_FAKE_ENGINE();
        UciEngine engine(FakeSettings({"--lines", "e2e4:mate:3,d2d4:mate:-2"}));

        ExpectLines(engine.Analyse(WithLines(MakeRequest({}), 2), CancellationToken()), {{"e2e4", MateOf(3)}, {"d2d4", MateOf(-2)}});
    }

    TEST(UciEngineTest, AOneLineSearchPutsMultiPvBackAndAFreshProcessGetsNoOption) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log, "--lines", kThreeLines}));
        const eastl::string position = StartPositionCommand();

        // A fresh process has one line: a search for one line sends no option. Three lines ask for it; a search for one line (Analyse, then FindMove) puts it back once
        ExpectAnalysis(engine.Analyse(MakeRequest({}), CancellationToken()), "e2e4", Cp(30), 1);
        EXPECT_EQ(CountMultiPvOptions(ReadFileLines(directory.File(kLogName))), 0u);
        ExpectLines(engine.Analyse(WithLines(MakeRequest({}), 3), CancellationToken()), {{"e2e4", Cp(30)}, {"d2d4", Cp(20)}, {"g1f3", Cp(-15)}});
        ExpectAnalysis(engine.Analyse(MakeRequest({}), CancellationToken()), "e2e4", Cp(30), 1);
        ExpectMove(engine.FindMove(MakeRequest({}), CancellationToken()), "e2e4");

        EXPECT_EQ(ReadFileLines(directory.File(kLogName)), Lines({"uci", "isready", "ucinewgame", "isready", position, "go movetime 50", "setoption name MultiPV value 3", position, "go movetime 50",
                                                                  "setoption name MultiPV value 1", position, "go movetime 50", position, "go movetime 50"}));
    }

    TEST(UciEngineTest, FindMoveBringsMultiPvBackToOneAfterAnAnalysisOfThreeLines) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log, "--lines", kThreeLines}));

        ExpectLines(engine.Analyse(WithLines(MakeRequest({}), 3), CancellationToken()), {{"e2e4", Cp(30)}, {"d2d4", Cp(20)}, {"g1f3", Cp(-15)}});
        ExpectMove(engine.FindMove(MakeRequest({}), CancellationToken()), "e2e4");
        ExpectMove(engine.FindMove(MakeRequest({}), CancellationToken()), "e2e4");

        const Vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
        EXPECT_EQ(CountLines(lines, "setoption name MultiPV value 3"), 1u);
        EXPECT_EQ(CountLines(lines, "setoption name MultiPV value 1"), 1u) << "once, not before every search";
        EXPECT_EQ(CountMultiPvOptions(lines), 2u);
    }

    TEST(UciEngineTest, MultiPvIsSentAgainToTheProcessThatReplacedAClosedOne) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        const eastl::string marker = directory.FileText("died-once");
        UciEngine engine(FakeSettings({"--log", log, "--die-once", marker, "--lines", kThreeLines}));
        const SearchRequest request = WithLines(MakeRequest({}), 2);

        ExpectAnalysisError(engine.Analyse(request, CancellationToken()), EngineErrorKind::ProcessDied);
        ExpectLines(engine.Analyse(request, CancellationToken()), {{"e2e4", Cp(30)}, {"d2d4", Cp(20)}});

        const Vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
        EXPECT_EQ(CountLines(lines, "uci"), 2u);
        EXPECT_EQ(CountLines(lines, "setoption name MultiPV value 2"), 2u) << "the new process has one line again";
    }

    TEST(UciEngineTest, AnIllegalMoveOnALaterLineIsIllegalMoveAndTheProcessIsKept) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log, "--lines", "e2e4:cp:30,e2e5:cp:20"}));

        const auto illegal = engine.Analyse(WithLines(MakeRequest({}), 2), CancellationToken());
        const auto again = engine.Analyse(MakeRequest({}), CancellationToken());

        ExpectAnalysisError(illegal, EngineErrorKind::IllegalMove);
        if (!illegal) {
            EXPECT_NE(illegal.error().detail.find("e2e5"), eastl::string::npos) << illegal.error().detail.c_str();
        }
        ExpectAnalysis(again, "e2e4", Cp(30), 1);
        const Vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
        EXPECT_EQ(CountLines(lines, "uci"), 1u);
        EXPECT_EQ(CountLines(lines, "quit"), 0u);
    }

    TEST(UciEngineTest, ARepeatedMoveOnALaterLineIsDropped) {
        SKIP_WITHOUT_FAKE_ENGINE();
        UciEngine engine(FakeSettings({"--lines", "e2e4:cp:30,e2e4:cp:25,d2d4:cp:10"}));

        ExpectLines(engine.Analyse(WithLines(MakeRequest({}), 3), CancellationToken()), {{"e2e4", Cp(30)}, {"d2d4", Cp(10)}});
    }

    TEST(UciEngineTest, FewerLinesThanAskedForIsNotAnError) {
        SKIP_WITHOUT_FAKE_ENGINE();
        UciEngine engine(FakeSettings({"--lines", "e2e4:cp:30,d2d4:cp:20"}));

        ExpectLines(engine.Analyse(WithLines(MakeRequest({}), 4), CancellationToken()), {{"e2e4", Cp(30)}, {"d2d4", Cp(20)}});
    }

    TEST(UciEngineTest, ALineWithoutAScoreIsNotAnAnalysisEvenWithManyLinesAsked) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log, "--no-score", "--lines", kThreeLines}));

        ExpectAnalysisError(engine.Analyse(WithLines(MakeRequest({}), 3), CancellationToken()), EngineErrorKind::ProtocolError);

        EXPECT_EQ(CountLines(ReadFileLines(directory.File(kLogName)), "quit"), 0u) << "the engine answered: it is in step";
    }

    TEST(UciEngineTest, AnalyseWithNoLinesOrTooManyIsBadRequestWithoutAProcess) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log}));

        ExpectAnalysisError(engine.Analyse(WithLines(MakeRequest({}), 0), CancellationToken()), EngineErrorKind::BadRequest);
        ExpectAnalysisError(engine.Analyse(WithLines(MakeRequest({}), -1), CancellationToken()), EngineErrorKind::BadRequest);
        ExpectAnalysisError(engine.Analyse(WithLines(MakeRequest({}), kMaxAnalysisLines + 1), CancellationToken()), EngineErrorKind::BadRequest);
        ExpectError(engine.FindMove(WithLines(MakeRequest({}), 0), CancellationToken()), EngineErrorKind::BadRequest);

        EXPECT_FALSE(std::filesystem::exists(directory.File(kLogName)));
    }

    TEST(UciEngineTest, FindMoveIgnoresTheLinesOfTheRequest) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngine engine(FakeSettings({"--log", log, "--lines", kThreeLines}));

        ExpectMove(engine.FindMove(WithLines(MakeRequest({}), 3), CancellationToken()), "e2e4");

        EXPECT_EQ(CountMultiPvOptions(ReadFileLines(directory.File(kLogName))), 0u) << "a move is one line";
    }

#endif  // !CHESS_HAS_UCI_PROCESS

#if CHESS_NO_STOCKFISH

    TEST(UciEngineTest, FindStockfishFindsNothingWithoutStockfishWhateverIsThere) {
        const TempDirectory directory;
        const std::filesystem::path file = directory.File("stockfish-copy");
        {
            std::ofstream stream(file);
            stream << "not a program\n";
        }
        const std::string text = file.string();

        EXPECT_FALSE(FindStockfish(FromStd(text)).has_value()) << "an explicit file that is there is not looked at";
        EXPECT_FALSE(FindStockfish({}).has_value());
    }

#else

    TEST(UciEngineTest, FindStockfishTakesAnExplicitExistingFile) {
        const TempDirectory directory;
        const std::filesystem::path file = directory.File("stockfish-copy");
        {
            std::ofstream stream(file);
            stream << "not a program\n";
        }
        const std::string text = file.string();

        const Optional<eastl::string> found = FindStockfish(FromStd(text));

        ASSERT_TRUE(found.has_value());
        EXPECT_EQ(*found, eastl::string(text.c_str(), text.size()));
    }

    TEST(UciEngineTest, FindStockfishAnswersNothingForAnExplicitFileThatIsNotThere) {
        const TempDirectory directory;
        const std::string missing = directory.File("no-such-file").string();
        const std::string folder = directory.File("").string();

        EXPECT_FALSE(FindStockfish(FromStd(missing)).has_value());
        EXPECT_FALSE(FindStockfish(FromStd(folder)).has_value()) << "a directory is no executable";
    }

#endif  // CHESS_NO_STOCKFISH

#if !defined(_WIN32) && !CHESS_NO_STOCKFISH

    namespace {

        /// An environment variable set (or removed, for nullptr) for the length of a scope and put back after it
        class ScopedEnvironment {
        public:
            ScopedEnvironment(const char* name, const char* value) : _name(name) {
                if (const char* old = std::getenv(name)) {
                    _old = old;
                    _hadValue = true;
                }
                Set(value);
            }
            ~ScopedEnvironment() {
                Set(_hadValue ? _old.c_str() : nullptr);
            }
            ScopedEnvironment(const ScopedEnvironment&) = delete;
            ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

        private:
            void Set(const char* value) const {
                if (value != nullptr) {
                    setenv(_name, value, 1);
                } else {
                    unsetenv(_name);
                }
            }

            const char* _name;
            std::string _old;
            bool _hadValue = false;
        };

        void MakeFile(const std::filesystem::path& file, std::filesystem::perms permissions) {
            {
                std::ofstream stream(file);
                stream << "#!/bin/sh\nexit 0\n";
            }
            std::filesystem::permissions(file, permissions, std::filesystem::perm_options::replace);
        }

        constexpr std::filesystem::perms kExecutable = std::filesystem::perms::owner_all;
        constexpr std::filesystem::perms kNotExecutable = std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;

    }  // namespace

    TEST(UciEngineTest, FindStockfishLooksAtTheEnvironmentVariableWhenThereIsNoExplicitPath) {
        const TempDirectory directory;
        MakeFile(directory.File("from-environment"), kExecutable);
        const std::string file = directory.File("from-environment").string();
        const std::string missing = directory.File("no-such-file").string();

        {
            const ScopedEnvironment variable("STOCKFISH", file.c_str());
            const Optional<eastl::string> found = FindStockfish({});
            ASSERT_TRUE(found.has_value());
            EXPECT_EQ(*found, eastl::string(file.c_str(), file.size()));
            EXPECT_FALSE(FindStockfish(FromStd(missing)).has_value()) << "an explicit path that is not there does not fall back";
        }
        {
            const ScopedEnvironment variable("STOCKFISH", missing.c_str());
            const ScopedEnvironment path("PATH", directory.File("").string().c_str());
            EXPECT_FALSE(FindStockfish({}).has_value()) << "a variable that names no file does not fall back to PATH";
        }
    }

    TEST(UciEngineTest, FindStockfishSearchesPathForAnExecutableFileCalledStockfish) {
        const TempDirectory directory;
        const std::filesystem::path withExecutable = directory.File("with-executable");
        const std::filesystem::path withPlainFile = directory.File("with-plain-file");
        std::filesystem::create_directories(withExecutable);
        std::filesystem::create_directories(withPlainFile);
        MakeFile(withExecutable / "stockfish", kExecutable);
        MakeFile(withPlainFile / "stockfish", kNotExecutable);
        const ScopedEnvironment variable("STOCKFISH", nullptr);

        {
            // An empty entry and a directory that is not there come first
            const std::string search = Format("::{}:{}", directory.File("no-such-directory").string(), withExecutable.string()).c_str();
            const ScopedEnvironment path("PATH", search.c_str());
            const Optional<eastl::string> found = FindStockfish({});
            ASSERT_TRUE(found.has_value());
            const std::string expected = (withExecutable / "stockfish").string();
            EXPECT_EQ(*found, eastl::string(expected.c_str(), expected.size()));
        }
        {
            const ScopedEnvironment path("PATH", withPlainFile.string().c_str());
            EXPECT_FALSE(FindStockfish({}).has_value()) << "a file nobody may execute is not Stockfish";
        }
        {
            const ScopedEnvironment path("PATH", nullptr);
            EXPECT_FALSE(FindStockfish({}).has_value());
        }
    }

#endif  // !defined(_WIN32) && !CHESS_NO_STOCKFISH

    // The same class against a real Stockfish, which the tests neither ship nor install: they find it like the bridge does (STOCKFISH, then PATH) and skip without it

    TEST(UciEngineStockfishTest, FindsTheMateInOne) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        UciEngine engine(StockfishSettings(*stockfish));

        const auto result = engine.FindMove(MakeRequest({}, SearchLimits{0, 6}, "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"), CancellationToken());

        ExpectMove(result, "a1a8");
    }

    TEST(UciEngineStockfishTest, TakesTheHangingQueen) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        UciEngine engine(StockfishSettings(*stockfish));

        // The black queen on d5 stands in front of the white rook on d1 and nothing guards it
        const auto result = engine.FindMove(MakeRequest({}, SearchLimits{0, 8}, "4k3/8/8/3q4/8/8/8/3RK3 w - - 0 1"), CancellationToken());

        ExpectMove(result, "d1d5");
    }

    TEST(UciEngineStockfishTest, AnalysesTheMateInOne) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        UciEngine engine(StockfishSettings(*stockfish));

        const auto result = engine.Analyse(MakeRequest({}, SearchLimits{0, 6}, "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"), CancellationToken());

        ASSERT_TRUE(result.has_value()) << Describe(result.error()).c_str();
        EXPECT_EQ(result->Best().move.ToUci(), "a1a8");
        EXPECT_EQ(result->Best().score, MateOf(1));
        EXPECT_GE(result->Best().depth, 1);
    }

    TEST(UciEngineStockfishTest, ASkillLevelOptionDoesNotBreakTheHandshake) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        UciEngineSettings settings = StockfishSettings(*stockfish);
        settings.options.emplace_back("Skill Level", "5");
        UciEngine engine(std::move(settings));

        const auto result = engine.FindMove(MakeRequest({}, SearchLimits{200, 0}), CancellationToken());

        EXPECT_TRUE(result.has_value()) << (result ? "" : Describe(result.error()).c_str());
    }

    TEST(UciEngineStockfishTest, TwoSearchesShareOneProcess) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        UciEngine engine(StockfishSettings(*stockfish));

        const auto first = engine.FindMove(MakeRequest({}, SearchLimits{100, 0}), CancellationToken());
        ASSERT_TRUE(first.has_value()) << Describe(first.error()).c_str();
        const eastl::string firstMove = first->ToUci();
        const auto second = engine.FindMove(MakeRequest({StringView(firstMove)}, SearchLimits{100, 0}), CancellationToken());

        EXPECT_TRUE(second.has_value()) << (second ? "" : Describe(second.error()).c_str());
    }

}  // namespace BECore::Chess::Tests
