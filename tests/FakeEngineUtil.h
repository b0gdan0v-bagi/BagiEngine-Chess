#pragma once

#include <Chess/Engine/UciProcess.h>
#include <Common/TempPath.h>
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

// The interpreter that runs tests/fake_uci_engine.py, given by the root CMakeLists.txt (an empty string when CMake found no Python)
#if !defined(CHESS_TEST_PYTHON)
#define CHESS_TEST_PYTHON ""
#endif

#define SKIP_WITHOUT_FAKE_ENGINE()                                                                                                                                                                     \
    if (!::BECore::Chess::Tests::FakeEngineAvailable()) {                                                                                                                                              \
        GTEST_SKIP() << "no Python interpreter, or no process code in this build (CHESS_NO_STOCKFISH, the web)";                                                                                       \
    }

/// The fake UCI engine (tests/fake_uci_engine.py) and the files its tests leave behind. A test that starts it begins with SKIP_WITHOUT_FAKE_ENGINE()
namespace BECore::Chess::Tests {

    /// True when the fake engine can run here: there is a Python interpreter and the build has the process code (CHESS_HAS_UCI_PROCESS)
    inline bool FakeEngineAvailable() {
#if !CHESS_HAS_UCI_PROCESS
        return false;
#else
        return !StringView(CHESS_TEST_PYTHON).empty();
#endif
    }

    /// The script, found from this file like the shipped scene is found from tests/ShippedScene.h (the source tree is not mounted in the test environment)
    inline std::filesystem::path FakeEngineScript() {
        return std::filesystem::path(__FILE__).parent_path() / "fake_uci_engine.py";
    }

    /// The fake engine with @p flags ("--moves", "e2e4,e7e5", ...), ready for UciProcess::Start()
    inline ProcessLaunch FakeEngineLaunch(std::initializer_list<StringView> flags = {}) {
        ProcessLaunch launch;
        launch.path = CHESS_TEST_PYTHON;
        const String script = FromStdString(FakeEngineScript().string());
        launch.args.emplace_back(script.c_str(), script.size());
        for (const StringView flag : flags) {
            launch.args.emplace_back(flag.data(), flag.size());
        }
        return launch;
    }

    /// A directory of the running test under the system's temporary one, removed with the object
    class TempDirectory {
    public:
        // One directory per object: the name has the suite, the test, the process id (beside TestTempPath's tag) and a counter. ctest runs 8 or more processes at a time, beside
        // other checkouts' ctests, and the constructor clears the directory, so a name made of the test alone lets two processes delete each other's files
        TempDirectory() : _path(::BECore::Tests::TestTempPath(DirectoryName())) {
            std::filesystem::remove_all(_path);
            std::filesystem::create_directories(_path);
        }
        ~TempDirectory() {
            std::error_code ignored;
            std::filesystem::remove_all(_path, ignored);
        }
        TempDirectory(const TempDirectory&) = delete;
        TempDirectory& operator=(const TempDirectory&) = delete;

        /// The path of a file in the directory (the file is not made)
        [[nodiscard]] std::filesystem::path File(const char* name) const {
            return _path / name;
        }

        /// The same as text, for a flag of the fake engine such as --log
        [[nodiscard]] String FileText(const char* name) const {
            return FromStdString(File(name).string());
        }

    private:
        static String DirectoryName() {
            const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
            static std::atomic<int> counter{0};
            return Format("BagiChessTests_{}_{}_p{}_{}", info->test_suite_name(), info->name(), ProcessId(), counter.fetch_add(1));
        }

        static int ProcessId() {
#if defined(_WIN32)
            return _getpid();
#else
            return static_cast<int>(getpid());
#endif
        }

        std::filesystem::path _path;
    };

    /// The lines of a text file, none when it does not exist (the fake engine's --log file: what the engine received)
    inline Vector<String> ReadFileLines(const std::filesystem::path& file) {
        Vector<String> lines;
        //-NoStd: file layer (engine-file-layer)
        std::ifstream stream(file);
        std::string line;  //-NoStd: file layer (engine-file-layer)
        //-NoStd: file layer (engine-file-layer)
        while (std::getline(stream, line)) {
            lines.emplace_back(line.c_str(), line.size());
        }
        return lines;
    }

    /// A list of lines to compare with ReadFileLines()
    inline Vector<String> Lines(std::initializer_list<StringView> texts) {
        Vector<String> lines;
        for (const StringView text : texts) {
            lines.emplace_back(text.data(), text.size());
        }
        return lines;
    }

    /// Waits until the log file has the line @p text (a whole line), polling; false when it has not within @p timeout. The fake engine writes its log as it receives
    /// the lines, so a line the test sent is there a moment later, not when the call that sent it returns: a test that looks for one under load polls
    inline bool WaitForLogLine(const std::filesystem::path& file, StringView text, std::chrono::milliseconds timeout = std::chrono::seconds(15)) {
        constexpr std::chrono::milliseconds kPollInterval{5};
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            for (const String& line : ReadFileLines(file)) {
                if (StringView(line) == text) {
                    return true;
                }
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(kPollInterval);
        }
    }

}  // namespace BECore::Chess::Tests
