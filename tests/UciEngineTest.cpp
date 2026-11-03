#include "ChessTestUtil.h"
#include "FakeEngineUtil.h"

#include <Chess/Engine/SearchRequest.h>
#include <Chess/Engine/UciEngine.h>
#include <chrono>
#include <thread>

#if !defined(_WIN32)
#include <cstdlib>
#endif

// A test of UciEngineStockfishTest begins with this: it declares `path` (the Stockfish executable) or skips the test
#define SKIP_WITHOUT_STOCKFISH(path)                                                                                                                                                                   \
    const std::optional<eastl::string> path = ::BECore::Chess::Tests::StockfishPath();                                                                                                                 \
    if (!path.has_value()) {                                                                                                                                                                           \
        GTEST_SKIP() << "no Stockfish (or no process code on this platform): set STOCKFISH to its executable or put stockfish on PATH";                                                                \
    }

/// UciEngine against the fake engine (tests/fake_uci_engine.py): what it sends, how it answers, how it recovers. No fixed sleeps: a test that must
/// not time out waits long, one that must is short. UciEngineStockfishTest runs the same class against a real Stockfish, when there is one
namespace BECore::Chess::Tests {

    namespace {

        using std::chrono::milliseconds;
        using Clock = std::chrono::steady_clock;

        constexpr int kMoveTimeMs = 50;
        constexpr milliseconds kMustNotTimeOut{15000};
        constexpr int kShortTimeoutMs = 100;  // a time that is meant to pass
        constexpr const char* kLogName = "fake.log";

        eastl::string StartFen() {
            return eastl::string(Position::kStartFen.data(), Position::kStartFen.size());
        }

        eastl::string StartPositionCommand() {
            return Format("position fen {}", Position::kStartFen);
        }

        SearchRequest MakeRequest(std::initializer_list<eastl::string_view> moves, SearchLimits limits = SearchLimits{kMoveTimeMs, 0}, const eastl::string& startFen = StartFen()) {
            SearchRequest request;
            request.startFen = startFen;
            for (const eastl::string_view move : moves) {
                request.moves.push_back(Mv(move));
            }
            request.limits = limits;
            return request;
        }

        UciEngineSettings FakeSettings(std::initializer_list<eastl::string_view> flags) {
            UciEngineSettings settings;
            settings.launch = FakeEngineLaunch(flags);
            return settings;
        }

        eastl::string Describe(const EngineError& error) {
            return Format("{}: {}", EnumUtils<EngineErrorKind>::ToString(error.kind), error.detail);
        }

        void ExpectMove(const std::expected<Move, EngineError>& result, eastl::string_view uci) {
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

        size_t CountLines(const eastl::vector<eastl::string>& lines, eastl::string_view text) {
            return static_cast<size_t>(std::count_if(lines.begin(), lines.end(), [text](const eastl::string& line) { return eastl::string_view(line.data(), line.size()) == text; }));
        }

        /// Cancels a token once the fake has received a line: the moment a search is known to be running on the other side of the pipe
        class CancelOnLogLine {
        public:
            CancelOnLogLine(CancellationToken token, std::filesystem::path logFile, eastl::string line)
                : _thread([this, token, logFile = std::move(logFile), line = std::move(line)]() mutable {
                      WaitForLogLine(logFile, eastl::string_view(line.data(), line.size()));  // cancels even when the line never came, so that the test fails and does not hang
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

#if defined(_WIN32)

    TEST(UciEngineTest, ASearchIsUnsupportedOnWindows) {
        UciEngineSettings settings;
        settings.launch.path = "engine.exe";
        UciEngine engine(std::move(settings));

        ExpectError(engine.FindMove(MakeRequest({}), CancellationToken()), EngineErrorKind::Unsupported);
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

        const eastl::vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
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
        const eastl::vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
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
        const eastl::vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
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
        EXPECT_LT(took, kMustNotTimeOut);
        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), "quit")) << "the hung engine was told to leave";
        EXPECT_EQ(CountLines(ReadFileLines(directory.File(kLogName)), "uci"), 1u);

        ExpectError(engine.FindMove(request, CancellationToken()), EngineErrorKind::Timeout);
        const eastl::vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
        EXPECT_EQ(CountLines(lines, "uci"), 2u) << "the second search started a process of its own";
        EXPECT_EQ(CountLines(lines, "ucinewgame"), 2u) << "and a new process starts a game";
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

    TEST(UciEngineTest, NoUciokIsAnErrorWithinTheStartupTimeAndTheProcessIsClosed) {
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
        EXPECT_LT(took, kMustNotTimeOut);
        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), "quit"));
        EXPECT_EQ(CountLines(ReadFileLines(directory.File(kLogName)), "go movetime 50"), 0u) << "no search on an engine that did not shake hands";
    }

    TEST(UciEngineTest, ALineBeyondTheCapIsProtocolErrorAndTheProcessIsClosed) {
        SKIP_WITHOUT_FAKE_ENGINE();
        constexpr const char* kTooLong = "1100000";  // UciProcess::kMaxLineBytes is 1 MiB
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngineSettings settings = FakeSettings({"--log", log, "--long-line", kTooLong});
        settings.quitGraceMs = kShortTimeoutMs;  // the child is blocked writing the rest of the line and leaves by a signal
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
        // The first go thinks for five seconds unless it is stopped; the second answers at once. The answer to `stop` is e2e4 and must not be taken for the second search's
        UciEngine engine(FakeSettings({"--log", log, "--think-ms", "5000", "--think-first-only", "--moves", "e2e4,d2d4"}));
        const CancellationToken token = CancellationToken::Create();
        CancelOnLogLine canceller(token, directory.File(kLogName), "go movetime 50");

        const Clock::time_point begin = Clock::now();
        const auto cancelled = engine.FindMove(MakeRequest({}), token);
        const Clock::duration took = Clock::now() - begin;

        ExpectError(cancelled, EngineErrorKind::Cancelled);
        EXPECT_LT(took, milliseconds(4000)) << "the engine would have thought for 5000 ms";
        ExpectMove(engine.FindMove(MakeRequest({}), CancellationToken()), "d2d4");
        const eastl::vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
        EXPECT_EQ(CountLines(lines, "stop"), 1u);
        EXPECT_EQ(CountLines(lines, "uci"), 1u) << "the engine answered `stop`: it stays";
        EXPECT_EQ(CountLines(lines, "quit"), 0u);
    }

    TEST(UciEngineTest, ACancelledSearchOfAnEngineThatIgnoresStopClosesTheProcessAfterTheGrace) {
        SKIP_WITHOUT_FAKE_ENGINE();
        constexpr int kStopGraceMs = 200;
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        UciEngineSettings settings = FakeSettings({"--log", log, "--think-ms", "5000", "--ignore-stop"});
        settings.stopGraceMs = kStopGraceMs;
        UciEngine engine(std::move(settings));
        const CancellationToken token = CancellationToken::Create();
        CancelOnLogLine canceller(token, directory.File(kLogName), "go movetime 50");

        const auto result = engine.FindMove(MakeRequest({}), token);
        const Clock::time_point returned = Clock::now();

        ExpectError(result, EngineErrorKind::Cancelled);
        EXPECT_GE(returned - canceller.CancelledAt(), milliseconds(kStopGraceMs)) << "the engine had its grace to answer `stop`";
        EXPECT_LT(returned - canceller.CancelledAt(), milliseconds(4000));
        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), "quit")) << "and then it was closed";
        const eastl::vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
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
        const Clock::time_point begin = Clock::now();
        {
            UciEngineSettings settings = FakeSettings({"--log", log, "--ignore-quit"});
            settings.quitGraceMs = kShortTimeoutMs;
            UciEngine engine(std::move(settings));
            ExpectMove(engine.FindMove(MakeRequest({}), CancellationToken()), "e2e4");
        }
        const Clock::duration took = Clock::now() - begin;

        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), "quit"));
        EXPECT_LT(took, kMustNotTimeOut) << "the child was ended by a signal after the grace";
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
        eastl::vector<eastl::string> answers = {results[0]->ToUci(), results[1]->ToUci()};
        std::sort(answers.begin(), answers.end());
        EXPECT_EQ(answers, Lines({"d2d4", "e2e4"})) << "each search got the answer of its own go";
        EXPECT_GE(took, 2 * kThink) << "the second search waited for the first";

        // Commands never interleave: every position is followed by its go, then the next search's position
        eastl::vector<eastl::string> searchLines;
        for (const eastl::string& line : ReadFileLines(directory.File(kLogName))) {
            if (line.find("position") == 0 || line.find("go") == 0) {
                searchLines.push_back(line);
            }
        }
        const eastl::string position = StartPositionCommand();
        EXPECT_EQ(searchLines, Lines({position, "go movetime 50", position, "go movetime 50"}));
    }

#endif  // defined(_WIN32)

    TEST(UciEngineTest, FindStockfishTakesAnExplicitExistingFile) {
        const TempDirectory directory;
        const std::filesystem::path file = directory.File("stockfish-copy");
        {
            std::ofstream stream(file);
            stream << "not a program\n";
        }
        const std::string text = file.string();

        const std::optional<eastl::string> found = FindStockfish(eastl::string_view(text.data(), text.size()));

        ASSERT_TRUE(found.has_value());
        EXPECT_EQ(*found, eastl::string(text.c_str(), text.size()));
    }

    TEST(UciEngineTest, FindStockfishAnswersNothingForAnExplicitFileThatIsNotThere) {
        const TempDirectory directory;
        const std::string missing = directory.File("no-such-file").string();
        const std::string folder = directory.File("").string();

        EXPECT_FALSE(FindStockfish(eastl::string_view(missing.data(), missing.size())).has_value());
        EXPECT_FALSE(FindStockfish(eastl::string_view(folder.data(), folder.size())).has_value()) << "a directory is no executable";
    }

#if !defined(_WIN32)

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
            const std::optional<eastl::string> found = FindStockfish({});
            ASSERT_TRUE(found.has_value());
            EXPECT_EQ(*found, eastl::string(file.c_str(), file.size()));
            EXPECT_FALSE(FindStockfish(eastl::string_view(missing.data(), missing.size())).has_value()) << "an explicit path that is not there does not fall back";
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
            const std::optional<eastl::string> found = FindStockfish({});
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

#endif  // !defined(_WIN32)

    // The same class against a real Stockfish, which the tests neither ship nor install: they find it like the bridge does (STOCKFISH, then PATH) and skip without it

    /// Where Stockfish is, or nothing; nothing too where the process code is not there
    inline std::optional<eastl::string> StockfishPath() {
#if defined(_WIN32)
        return std::nullopt;
#else
        return FindStockfish({});
#endif
    }

    namespace {

        UciEngineSettings StockfishSettings(const eastl::string& path) {
            UciEngineSettings settings;
            settings.launch.path = path;
            return settings;
        }

    }  // namespace

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
        const auto second = engine.FindMove(MakeRequest({eastl::string_view(firstMove.data(), firstMove.size())}, SearchLimits{100, 0}), CancellationToken());

        EXPECT_TRUE(second.has_value()) << (second ? "" : Describe(second.error()).c_str());
    }

}  // namespace BECore::Chess::Tests
