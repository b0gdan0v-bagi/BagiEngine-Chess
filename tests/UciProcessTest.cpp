#include "ChessTestUtil.h"
#include "FakeEngineUtil.h"

#include <Chess/Engine/UciProcess.h>
#include <chrono>

#if CHESS_HAS_UCI_PROCESS && defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#elif CHESS_HAS_UCI_PROCESS
#include <cerrno>
#include <csignal>
#include <poll.h>
#include <unistd.h>
#endif

/// The child process under a UCI engine, against the fake engine (tests/fake_uci_engine.py): no fixed sleeps, a test that must not time out waits long, one that must is short
namespace BECore::Chess::Tests {

#if !CHESS_HAS_UCI_PROCESS

    TEST(UciProcessTest, StartIsUnsupportedWithoutProcessCode) {
        ProcessLaunch launch;
        launch.path = "engine.exe";

        const auto started = UciProcess::Start(launch);

        ASSERT_FALSE(started.has_value());
        EXPECT_EQ(started.error().kind, EngineErrorKind::Unsupported);
        EXPECT_FALSE(started.error().detail.empty()) << "the sentence says why there is no process here";
    }

#else

    namespace {

        using std::chrono::milliseconds;
        using Clock = std::chrono::steady_clock;

        constexpr milliseconds kMustNotTimeOut{30000};  // a limit that only ends a broken test; no test asserts that something took less
        constexpr milliseconds kMustTimeOut{100};
        constexpr milliseconds kShortGrace{100};
        constexpr milliseconds kInheritanceWait{5000};  // the end of the file is there at once when the child holds nothing; this long only when it fails
        constexpr milliseconds kTerminateWait{500};     // UciProcess::Close waits this long after SIGTERM (kTerminateGrace in UciProcess.cpp)
        constexpr size_t kFloodLines = 20000;
        constexpr size_t kLineMargin = 1000;

        UniquePtr<UciProcess> StartFake(std::initializer_list<StringView> flags = {}) {
            auto started = UciProcess::Start(FakeEngineLaunch(flags));
            EXPECT_TRUE(started.has_value()) << (started ? "" : started.error().detail.c_str());
            return started ? std::move(*started) : nullptr;
        }

        /// The next line; a failure of the read is a failure of the test and gives an empty line
        eastl::string Read(UciProcess& process, milliseconds timeout = kMustNotTimeOut) {
            auto line = process.ReadLine(timeout);
            if (!line) {
                ADD_FAILURE() << EnumUtils<EngineErrorKind>::ToString(line.error().kind).data() << ": " << line.error().detail.c_str();
                return {};
            }
            return std::move(*line);
        }

        /// Reads up to and including the line @p last, or up to the first failure
        Vector<eastl::string> ReadUntil(UciProcess& process, const char* last) {
            Vector<eastl::string> lines;
            for (;;) {
                auto line = process.ReadLine(kMustNotTimeOut);
                if (!line) {
                    ADD_FAILURE() << "no '" << last << "': " << line.error().detail.c_str();
                    return lines;
                }
                lines.push_back(std::move(*line));
                if (lines.back() == last) {
                    return lines;
                }
            }
        }

        void Write(UciProcess& process, StringView text) {
            const auto written = process.WriteLine(text);
            EXPECT_TRUE(written.has_value()) << (written ? "" : written.error().detail.c_str());
        }

        /// The child has answered `isready`, so it is up and has read everything sent before
        void Handshake(UciProcess& process) {
            Write(process, "isready");
            EXPECT_EQ(Read(process), "readyok");
        }

#if defined(_WIN32)
        /// The process has ended. Once UciProcess let go of its handle the id names nothing (OpenProcess fails); if something else still holds a handle, it must be signalled
        void ExpectReaped(int processId) {
            const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(processId));
            if (process == nullptr) {
                return;
            }
            const DWORD waited = WaitForSingleObject(process, 0);
            CloseHandle(process);
            EXPECT_EQ(waited, WAIT_OBJECT_0) << "process " << processId << " is still running";
        }

        // ERROR_PATH_NOT_FOUND: the directory of the program does not exist
        constexpr const char* kMissingProgramText = "(error 3)";
#else
        /// The process is gone and collected: a zombie would still answer kill(pid, 0)
        void ExpectReaped(int processId) {
            const int result = kill(static_cast<pid_t>(processId), 0);
            const int error = errno;
            EXPECT_EQ(result, -1) << "process " << processId << " is still there";
            EXPECT_EQ(error, ESRCH) << "process " << processId << " was not collected";
        }

        constexpr const char* kMissingProgramText = "No such file";
#endif

    }  // namespace

    TEST(UciProcessTest, AMissingProgramIsLaunchFailed) {
        ProcessLaunch launch;
        launch.path = "/nonexistent-directory/engine";

        const auto started = UciProcess::Start(launch);

        ASSERT_FALSE(started.has_value());
        EXPECT_EQ(started.error().kind, EngineErrorKind::LaunchFailed);
        EXPECT_NE(started.error().detail.find("/nonexistent-directory/engine"), eastl::string::npos) << started.error().detail.c_str();
        EXPECT_NE(started.error().detail.find(kMissingProgramText), eastl::string::npos) << started.error().detail.c_str();
    }

    TEST(UciProcessTest, ANoProgramAtAllIsLaunchFailed) {
        const auto started = UciProcess::Start(ProcessLaunch{});

        ASSERT_FALSE(started.has_value());
        EXPECT_EQ(started.error().kind, EngineErrorKind::LaunchFailed);
    }

#if defined(_WIN32)

    TEST(UciProcessTest, AFileThatIsNoProgramIsLaunchFailed) {
        const TempDirectory directory;
        {
            std::ofstream text(directory.File("engine.exe"));
            text << "not a program\n";
        }
        ProcessLaunch launch;
        launch.path = directory.FileText("engine.exe");

        const auto refused = UciProcess::Start(launch);

        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().kind, EngineErrorKind::LaunchFailed);
        // ERROR_BAD_EXE_FORMAT (193) or, for a file this short, ERROR_EXE_MACHINE_TYPE_MISMATCH (216): the number is the system's choice, the sentence names it
        EXPECT_NE(refused.error().detail.find("(error "), eastl::string::npos) << refused.error().detail.c_str();
        EXPECT_EQ(refused.error().detail.find("%1"), eastl::string::npos) << "the placeholder of the system's sentence is filled: " << refused.error().detail.c_str();
    }

    TEST(UciProcessTest, AnArgumentWithASpaceArrivesWhole) {
        SKIP_WITHOUT_FAKE_ENGINE();
        // The command line of Windows is one string: an argument with a space must come back whole from the child's C runtime (the quoting of UciProcessWindows.cpp)
        const TempDirectory directory;
        const std::filesystem::path folder = directory.File("with space");
        std::filesystem::create_directories(folder);
        const std::string logPath = (folder / "fake.log").string();
        const eastl::string log(logPath.c_str(), logPath.size());
        const auto process = StartFake({"--log", log, "--moves", "a2a3"});
        ASSERT_NE(process, nullptr);

        Handshake(*process);
        Write(*process, "go");
        EXPECT_EQ(ReadUntil(*process, "bestmove a2a3 ponder e7e5").size(), 2u);
        EXPECT_EQ(ReadFileLines(folder / "fake.log"), Lines({"isready", "go"}));
    }

#else

    TEST(UciProcessTest, AFileWithoutTheExecuteBitIsLaunchFailed) {
        const TempDirectory directory;
        const std::filesystem::path program = directory.File("engine.sh");
        {
            std::ofstream script(program);
            script << "#!/bin/sh\nexit 0\n";
        }
        ProcessLaunch launch;
        launch.path = directory.FileText("engine.sh");

        std::filesystem::permissions(program, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace);
        const auto refused = UciProcess::Start(launch);

        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().kind, EngineErrorKind::LaunchFailed);
        EXPECT_NE(refused.error().detail.find("Permission denied"), eastl::string::npos) << refused.error().detail.c_str();

        // With the bit the same file starts, so the refusal above was about the bit
        std::filesystem::permissions(program, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
        const auto started = UciProcess::Start(launch);
        EXPECT_TRUE(started.has_value()) << (started ? "" : started.error().detail.c_str());
    }

#endif  // _WIN32

    TEST(UciProcessTest, ALineGoesInAndTheLinesOfUciComeBack) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const auto process = StartFake();
        ASSERT_NE(process, nullptr);
        EXPECT_TRUE(process->IsRunning());

        Write(*process, "uci");
        const auto lines = ReadUntil(*process, "uciok");

        EXPECT_EQ(lines, Lines({"id name FakeUciEngine", "id author chess-tests", "option name Skill Level type spin default 20 min 0 max 20", "uciok"}));
        Write(*process, "isready");
        EXPECT_EQ(Read(*process), "readyok");
        EXPECT_TRUE(process->IsRunning());
    }

    TEST(UciProcessTest, ArgumentsReachTheChild) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText("fake.log");
        const auto process = StartFake({"--log", log, "--moves", "a2a3"});
        ASSERT_NE(process, nullptr);

        Handshake(*process);
        Write(*process, "go movetime 10");
        const auto lines = ReadUntil(*process, "bestmove a2a3 ponder e7e5");

        // The move comes from --moves and the log file from --log: both arguments arrived. The engine logs a line before it answers it
        EXPECT_EQ(lines, Lines({"info depth 1 score cp 10 pv a2a3", "bestmove a2a3 ponder e7e5"}));
        EXPECT_EQ(ReadFileLines(directory.File("fake.log")), Lines({"isready", "go movetime 10"}));
    }

    TEST(UciProcessTest, AReadTimesOutAndTheProcessAnswersLater) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const auto process = StartFake({"--think-ms", "600000"});
        ASSERT_NE(process, nullptr);
        Write(*process, "go");

        const Clock::time_point begin = Clock::now();
        const auto nothing = process->ReadLine(kMustTimeOut);
        const Clock::duration waited = Clock::now() - begin;

        ASSERT_FALSE(nothing.has_value());
        EXPECT_EQ(nothing.error().kind, EngineErrorKind::Timeout);
        EXPECT_GE(waited, kMustTimeOut);
        EXPECT_TRUE(process->IsRunning());

        // The process is in step: the stop it is sent ends the search, and the answer that comes after the timeout is read whole
        Write(*process, "stop");
        EXPECT_EQ(Read(*process), "info depth 1 score cp 10 pv e2e4");
        EXPECT_EQ(Read(*process), "bestmove e2e4 ponder e7e5");
    }

    TEST(UciProcessTest, AReadWithNoTimeLookedOnlyForWhatIsThere) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const auto process = StartFake();
        ASSERT_NE(process, nullptr);
        Handshake(*process);

        const auto nothing = process->ReadLine(milliseconds{0});

        ASSERT_FALSE(nothing.has_value());
        EXPECT_EQ(nothing.error().kind, EngineErrorKind::Timeout);
        Write(*process, "isready");
        EXPECT_EQ(Read(*process), "readyok");
    }

    TEST(UciProcessTest, ALineWrittenInTwoPiecesIsReadAsOne) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const auto process = StartFake({"--split-lines"});
        ASSERT_NE(process, nullptr);

        Write(*process, "uci");
        const auto lines = ReadUntil(*process, "uciok");

        EXPECT_EQ(lines, Lines({"id name FakeUciEngine", "id author chess-tests", "option name Skill Level type spin default 20 min 0 max 20", "uciok"}));
    }

    TEST(UciProcessTest, AFloodOfOutputIsReadInFull) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const eastl::string flood = Format("{}", kFloodLines);
        const auto process = StartFake({"--flood", flood});
        ASSERT_NE(process, nullptr);
        Write(*process, "go");

        // Far more than a pipe holds: the child waits for the reader, and no line is lost or cut
        for (size_t number = 0; number < kFloodLines; ++number) {
            ASSERT_EQ(Read(*process), Format("info string flood {}", number));
        }
        EXPECT_EQ(Read(*process), "info depth 1 score cp 10 pv e2e4");
        EXPECT_EQ(Read(*process), "bestmove e2e4 ponder e7e5");
    }

    TEST(UciProcessTest, ALongLineBelowTheLimitIsRead) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const eastl::string length = Format("{}", UciProcess::kMaxLineBytes - kLineMargin);
        const auto process = StartFake({"--long-line", length});
        ASSERT_NE(process, nullptr);
        Write(*process, "go");

        const eastl::string line = Read(*process);

        EXPECT_EQ(line.size(), UciProcess::kMaxLineBytes - kLineMargin);
        EXPECT_EQ(Read(*process), "info depth 1 score cp 10 pv e2e4");
    }

    TEST(UciProcessTest, ALineOverTheLimitIsAProtocolError) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const eastl::string length = Format("{}", UciProcess::kMaxLineBytes + kLineMargin);
        const auto process = StartFake({"--long-line", length});
        ASSERT_NE(process, nullptr);
        Write(*process, "go");

        const auto refused = process->ReadLine(kMustNotTimeOut);

        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().kind, EngineErrorKind::ProtocolError);
    }

    TEST(UciProcessTest, AnEndOfLineInsideACommandIsRefused) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText("fake.log");
        const auto process = StartFake({"--log", log});
        ASSERT_NE(process, nullptr);

        const auto refused = process->WriteLine("isready\nquit");
        const auto refusedCarriageReturn = process->WriteLine("isready\rquit");

        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().kind, EngineErrorKind::BadRequest);
        ASSERT_FALSE(refusedCarriageReturn.has_value());
        EXPECT_EQ(refusedCarriageReturn.error().kind, EngineErrorKind::BadRequest);
        Handshake(*process);  // nothing of the refused texts reached the child
        EXPECT_EQ(ReadFileLines(directory.File("fake.log")), Lines({"isready"}));
    }

    TEST(UciProcessTest, ADeadChildIsProcessDiedOnRead) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const auto process = StartFake({"--die-on-go"});
        ASSERT_NE(process, nullptr);
        const int processId = process->GetProcessId();
        Write(*process, "go");

        const auto first = process->ReadLine(kMustNotTimeOut);
        const auto second = process->ReadLine(kMustNotTimeOut);

        ASSERT_FALSE(first.has_value());
        EXPECT_EQ(first.error().kind, EngineErrorKind::ProcessDied);
        ASSERT_FALSE(second.has_value());
        EXPECT_EQ(second.error().kind, EngineErrorKind::ProcessDied);
        EXPECT_FALSE(process->IsRunning());
        process->Close();
        ExpectReaped(processId);
    }

    TEST(UciProcessTest, TheLinesBeforeTheEndAreReadBeforeProcessDied) {
        SKIP_WITHOUT_FAKE_ENGINE();
        // The child answers isready and exits on go: the answer is in the pipe when it is gone, and is read first
        const auto process = StartFake({"--die-on-go"});
        ASSERT_NE(process, nullptr);
        Write(*process, "isready");
        Write(*process, "go");

        EXPECT_EQ(Read(*process), "readyok");
        const auto afterwards = process->ReadLine(kMustNotTimeOut);
        ASSERT_FALSE(afterwards.has_value());
        EXPECT_EQ(afterwards.error().kind, EngineErrorKind::ProcessDied);
    }

    TEST(UciProcessTest, AWriteToADeadChildIsProcessDiedAndTheTestSurvives) {
        SKIP_WITHOUT_FAKE_ENGINE();
#if !defined(_WIN32)
        std::signal(SIGPIPE, SIG_DFL);  // the default would end this process on a write to a broken pipe
#endif
        const auto process = StartFake({"--die-on-go"});
        ASSERT_NE(process, nullptr);
        Write(*process, "go");

        // Nothing here reads the child: it is the write that finds the pipe broken, once the child has exited
        Optional<EngineError> failure;
        const Clock::time_point deadline = Clock::now() + kMustNotTimeOut;
        while (!failure && Clock::now() < deadline) {
            const auto written = process->WriteLine("ucinewgame");
            if (!written) {
                failure = written.error();
            }
        }

        ASSERT_TRUE(failure.has_value());
        EXPECT_EQ(failure->kind, EngineErrorKind::ProcessDied);
        EXPECT_FALSE(process->IsRunning());
        const auto again = process->WriteLine("isready");
        ASSERT_FALSE(again.has_value());
        EXPECT_EQ(again.error().kind, EngineErrorKind::ProcessDied);
    }

    TEST(UciProcessTest, ClosingAPoliteChildEndsIt) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText("fake.log");
        const auto process = StartFake({"--log", log});
        ASSERT_NE(process, nullptr);
        Handshake(*process);
        const int processId = process->GetProcessId();
        Write(*process, "quit");

        // The log shows the child got `quit`; whether it left by itself or a signal ended it, the test cannot tell (that would take a clock)
        process->Close(kMustNotTimeOut);

        ExpectReaped(processId);
        EXPECT_FALSE(process->IsRunning());
        EXPECT_EQ(ReadFileLines(directory.File("fake.log")), Lines({"isready", "quit"}));
    }

    TEST(UciProcessTest, ClosingEndsAChildThatReadsItsInputToTheEnd) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const auto process = StartFake();
        ASSERT_NE(process, nullptr);
        Handshake(*process);
        const int processId = process->GetProcessId();

        // No quit: the end of the input is the request
        process->Close(kMustNotTimeOut);

        ExpectReaped(processId);
    }

    TEST(UciProcessTest, ClosingAChildThatIgnoresQuitEndsItAfterTheGrace) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText("fake.log");
        const auto process = StartFake({"--ignore-quit", "--log", log});
        ASSERT_NE(process, nullptr);
        Handshake(*process);
        const int processId = process->GetProcessId();
        Write(*process, "quit");
        ASSERT_TRUE(WaitForLogLine(directory.File("fake.log"), "quit"));  // read by the child before Close: a loaded machine may be slower than the short grace

        const Clock::time_point begin = Clock::now();
        process->Close(kShortGrace);
        const Clock::duration took = Clock::now() - begin;

        ExpectReaped(processId);
        EXPECT_GE(took, kShortGrace);  // it got its grace first
        EXPECT_EQ(ReadFileLines(directory.File("fake.log")), Lines({"isready", "quit"}));
    }

#if !defined(_WIN32)  // no signals on Windows: Close() goes from the grace to TerminateProcess, the test above
    TEST(UciProcessTest, ClosingAChildThatIgnoresSigtermSendsSigkill) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText("fake.log");
        const auto process = StartFake({"--ignore-quit", "--ignore-term", "--log", log});
        ASSERT_NE(process, nullptr);
        Handshake(*process);  // the signal handler is installed before the first line is read
        const int processId = process->GetProcessId();
        Write(*process, "quit");
        ASSERT_TRUE(WaitForLogLine(directory.File("fake.log"), "quit"));

        const Clock::time_point begin = Clock::now();
        process->Close(kShortGrace);
        const Clock::duration took = Clock::now() - begin;

        ExpectReaped(processId);
        EXPECT_GE(took, kShortGrace + kTerminateWait);  // the grace, then SIGTERM and its wait, then SIGKILL
        EXPECT_EQ(ReadFileLines(directory.File("fake.log")), Lines({"isready", "quit", "<SIGTERM ignored>"}));
    }
#endif                // !_WIN32

    TEST(UciProcessTest, TheDestructorEndsAChildThatIgnoresEverything) {
        SKIP_WITHOUT_FAKE_ENGINE();
        auto process = StartFake({"--ignore-quit", "--ignore-term"});
        ASSERT_NE(process, nullptr);
        Handshake(*process);
        const int processId = process->GetProcessId();

        process.reset();

        ExpectReaped(processId);
    }

    TEST(UciProcessTest, AClosedProcessAnswersProcessDiedAndClosingTwiceIsHarmless) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const auto process = StartFake();
        ASSERT_NE(process, nullptr);
        const int processId = process->GetProcessId();

        process->Close(kMustNotTimeOut);
        process->Close(kMustNotTimeOut);
        const auto written = process->WriteLine("isready");
        const auto read = process->ReadLine(kMustTimeOut);

        ASSERT_FALSE(written.has_value());
        EXPECT_EQ(written.error().kind, EngineErrorKind::ProcessDied);
        ASSERT_FALSE(read.has_value());
        EXPECT_EQ(read.error().kind, EngineErrorKind::ProcessDied);
        EXPECT_FALSE(process->IsRunning());
        EXPECT_EQ(process->GetProcessId(), processId);
        ExpectReaped(processId);
    }

#if defined(_WIN32)
    TEST(UciProcessTest, TheChildDoesNotInheritOtherHandles) {
        SKIP_WITHOUT_FAKE_ENGINE();
        // An inheritable pipe, like one another part of the game made for a child of its own. Once our write end is closed the read end must report the broken pipe:
        // a child that had inherited the write end would keep it open
        SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE readEnd = nullptr;
        HANDLE writeEnd = nullptr;
        ASSERT_TRUE(CreatePipe(&readEnd, &writeEnd, &inheritable, 0));
        const auto process = StartFake();
        CloseHandle(writeEnd);
        ASSERT_NE(process, nullptr);

        bool broken = false;
        const Clock::time_point deadline = Clock::now() + kInheritanceWait;
        while (!broken && Clock::now() < deadline) {
            DWORD available = 0;
            broken = !PeekNamedPipe(readEnd, nullptr, 0, nullptr, &available, nullptr) && GetLastError() == ERROR_BROKEN_PIPE;
            if (!broken) {
                std::this_thread::sleep_for(milliseconds{5});
            }
        }
        CloseHandle(readEnd);

        EXPECT_TRUE(broken) << "the child still holds the write end";
    }

    TEST(UciProcessTest, ClosingAWrapperEndsTheProgramItStarted) {
        SKIP_WITHOUT_FAKE_ENGINE();
        // A .cmd is the child and the fake its child in turn (no exec on Windows). The fake keeps its log open, and Windows deletes no file a live process has open:
        // once Close() has returned, the file must go, because the job of UciProcess ended the fake too
        const TempDirectory directory;
        const std::filesystem::path wrapper = directory.File("wrapper.cmd");
        const std::filesystem::path log = directory.File("fake.log");
        {
            std::ofstream out(wrapper);
            out << "@\"" << CHESS_TEST_PYTHON << "\" \"" << FakeEngineScript().string() << "\" --ignore-quit --ignore-term --log \"" << log.string() << "\"\r\n";
        }
        ProcessLaunch launch;
        launch.path = directory.FileText("wrapper.cmd");
        auto started = UciProcess::Start(launch);
        ASSERT_TRUE(started.has_value()) << (started ? "" : started.error().detail.c_str());
        const UniquePtr<UciProcess> process = std::move(*started);
        Handshake(*process);  // the fake runs and has its log open

        process->Close(kShortGrace);

        bool removed = false;
        const Clock::time_point deadline = Clock::now() + kInheritanceWait;  // the end of a killed process is a moment after the kill
        while (!removed && Clock::now() < deadline) {
            std::error_code error;
            removed = std::filesystem::remove(log, error);
            if (!removed) {
                std::this_thread::sleep_for(milliseconds{5});
            }
        }
        EXPECT_TRUE(removed) << "the fake behind the wrapper still holds its log";
    }
#else
    TEST(UciProcessTest, TheChildDoesNotInheritOtherDescriptors) {
        SKIP_WITHOUT_FAKE_ENGINE();
        // A plain pipe, not close-on-exec, like a socket the game opened. Once our write end is closed the read end must see the end of the file at once:
        // a child that had inherited the write end would keep it open
        int ends[2] = {-1, -1};
        ASSERT_EQ(pipe(ends), 0);
        const auto process = StartFake();
        close(ends[1]);
        ASSERT_NE(process, nullptr);

        pollfd request{};
        request.fd = ends[0];
        request.events = POLLIN;
        const int ready = poll(&request, 1, static_cast<int>(kInheritanceWait.count()));
        char byte = 0;
        const ssize_t count = ready == 1 ? read(ends[0], &byte, 1) : -1;
        close(ends[0]);

        EXPECT_EQ(ready, 1) << "the child still holds the write end";
        EXPECT_EQ(count, 0);
    }
#endif  // _WIN32

    TEST(UciProcessTest, TheFakeEngineAnswersTheMovesInOrderAndThenHasNone) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const auto process = StartFake({"--moves", "e2e4,e7e5"});
        ASSERT_NE(process, nullptr);

        Write(*process, "go");
        EXPECT_EQ(ReadUntil(*process, "bestmove e2e4 ponder e7e5").size(), 2u);
        Write(*process, "go");
        EXPECT_EQ(ReadUntil(*process, "bestmove e7e5 ponder e7e5").size(), 2u);
        Write(*process, "go");
        EXPECT_EQ(ReadUntil(*process, "bestmove (none)").size(), 2u);
    }

    TEST(UciProcessTest, TheFakeEngineDiesOnlyOnceWithAMarker) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string marker = directory.FileText("died");

        const auto first = StartFake({"--die-once", marker});
        ASSERT_NE(first, nullptr);
        Write(*first, "go");
        const auto died = first->ReadLine(kMustNotTimeOut);
        ASSERT_FALSE(died.has_value());
        EXPECT_EQ(died.error().kind, EngineErrorKind::ProcessDied);
        EXPECT_TRUE(std::filesystem::exists(directory.File("died")));

        const auto second = StartFake({"--die-once", marker});
        ASSERT_NE(second, nullptr);
        Write(*second, "go");
        EXPECT_EQ(ReadUntil(*second, "bestmove e2e4 ponder e7e5").size(), 2u);
    }

#endif  // !CHESS_HAS_UCI_PROCESS

}  // namespace BECore::Chess::Tests
