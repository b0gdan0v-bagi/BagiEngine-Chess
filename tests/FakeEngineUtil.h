#pragma once

#include <Chess/Engine/UciProcess.h>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

// The interpreter that runs tests/fake_uci_engine.py, given by the root CMakeLists.txt (an empty string when CMake found no Python)
#if !defined(CHESS_TEST_PYTHON)
#define CHESS_TEST_PYTHON ""
#endif

#define SKIP_WITHOUT_FAKE_ENGINE()                                                                                                                                                                     \
    if (!::BECore::Chess::Tests::FakeEngineAvailable()) {                                                                                                                                              \
        GTEST_SKIP() << "no Python interpreter, or no process code in this build (POSIX only, and not CHESS_NO_STOCKFISH)";                                                                            \
    }

/// The fake UCI engine (tests/fake_uci_engine.py) and the files its tests leave behind. A test that starts it begins with SKIP_WITHOUT_FAKE_ENGINE()
namespace BECore::Chess::Tests {

    /// True when the fake engine can run here: there is a Python interpreter and the build has the process code (POSIX, and not CHESS_NO_STOCKFISH: CHESS_HAS_UCI_PROCESS)
    inline bool FakeEngineAvailable() {
#if !CHESS_HAS_UCI_PROCESS
        return false;
#else
        return !std::string_view(CHESS_TEST_PYTHON).empty();
#endif
    }

    /// The script, found from this file like the shipped SceneConfig.xml is found from ChessUiTest.cpp (the source tree is not mounted in the test environment)
    inline std::filesystem::path FakeEngineScript() {
        return std::filesystem::path(__FILE__).parent_path() / "fake_uci_engine.py";
    }

    /// The fake engine with @p flags ("--moves", "e2e4,e7e5", ...), ready for UciProcess::Start()
    inline ProcessLaunch FakeEngineLaunch(std::initializer_list<eastl::string_view> flags = {}) {
        ProcessLaunch launch;
        launch.path = CHESS_TEST_PYTHON;
        const std::string script = FakeEngineScript().string();
        launch.args.emplace_back(script.c_str(), script.size());
        for (const eastl::string_view flag : flags) {
            launch.args.emplace_back(flag.data(), flag.size());
        }
        return launch;
    }

    /// A directory of the running test under the system's temporary one, removed with the object
    class TempDirectory {
    public:
        // One directory per test, named after the suite too: ctest runs the tests in parallel processes, tests of two suites may share a name, and the constructor clears the directory
        TempDirectory() : _path(std::filesystem::temp_directory_path() / DirectoryName().c_str()) {
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
        [[nodiscard]] eastl::string FileText(const char* name) const {
            const std::string text = File(name).string();
            return eastl::string(text.c_str(), text.size());
        }

    private:
        static eastl::string DirectoryName() {
            const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
            return Format("BagiChessTests_{}_{}", info->test_suite_name(), info->name());
        }

        std::filesystem::path _path;
    };

    /// The lines of a text file, none when it does not exist (the fake engine's --log file: what the engine received)
    inline eastl::vector<eastl::string> ReadFileLines(const std::filesystem::path& file) {
        eastl::vector<eastl::string> lines;
        std::ifstream stream(file);
        std::string line;
        while (std::getline(stream, line)) {
            lines.emplace_back(line.c_str(), line.size());
        }
        return lines;
    }

    /// A list of lines to compare with ReadFileLines()
    inline eastl::vector<eastl::string> Lines(std::initializer_list<eastl::string_view> texts) {
        eastl::vector<eastl::string> lines;
        for (const eastl::string_view text : texts) {
            lines.emplace_back(text.data(), text.size());
        }
        return lines;
    }

    /// Waits until the log file has the line @p text (a whole line), polling; false when it has not within @p timeout. The fake engine writes its log as it receives
    /// the lines, so a line the test sent is there a moment later, not when the call that sent it returns: a test that looks for one under load polls
    inline bool WaitForLogLine(const std::filesystem::path& file, eastl::string_view text, std::chrono::milliseconds timeout = std::chrono::seconds(15)) {
        constexpr std::chrono::milliseconds kPollInterval{5};
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            for (const eastl::string& line : ReadFileLines(file)) {
                if (eastl::string_view(line.data(), line.size()) == text) {
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
