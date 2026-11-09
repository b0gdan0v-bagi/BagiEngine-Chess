#include "UciProcess.h"

#if CHESS_HAS_UCI_PROCESS
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>

extern char** environ;

// glibc 2.34 and later can close every other descriptor of the child with a file action (macOS has an attribute for it)
#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 34)
#define CHESS_SPAWN_CLOSEFROM 1
#endif
#endif
#endif

namespace BECore::Chess {

    namespace {

        std::unexpected<EngineError> Fail(EngineErrorKind kind, eastl::string detail) {
            return std::unexpected(EngineError{kind, std::move(detail)});
        }

#if CHESS_HAS_UCI_PROCESS

        constexpr size_t kReadChunkBytes = 4096;
        constexpr size_t kCompactBytes = 64 * 1024;                // the consumed front of the line buffer is dropped once it is this big and half of the buffer
        constexpr std::chrono::milliseconds kTerminateGrace{500};  // how long a child gets after SIGTERM before SIGKILL
        constexpr std::chrono::milliseconds kExitPollInterval{2};  // waitpid has no timeout: the exit is looked for at this pace
        constexpr int kFirstFreeDescriptor = STDERR_FILENO + 1;    // the pipes live above the three the child gets
        constexpr int kPipeReadEnd = 0;
        constexpr int kPipeWriteEnd = 1;
        constexpr const char* kNullDevice = "/dev/null";

        eastl::string ErrnoText(int code) {
            const std::string message = std::system_category().message(code);
            return eastl::string(message.c_str(), message.size());
        }

        /// A file descriptor closed with the object
        class Descriptor {
        public:
            Descriptor() = default;
            explicit Descriptor(int descriptor) : _descriptor(descriptor) {}
            ~Descriptor() {
                Reset();
            }
            Descriptor(const Descriptor&) = delete;
            Descriptor& operator=(const Descriptor&) = delete;
            Descriptor(Descriptor&& other) noexcept : _descriptor(other.Release()) {}
            Descriptor& operator=(Descriptor&& other) noexcept {
                if (this != &other) {
                    Reset();
                    _descriptor = other.Release();
                }
                return *this;
            }

            [[nodiscard]] int Get() const noexcept {
                return _descriptor;
            }

            [[nodiscard]] int Release() noexcept {
                return std::exchange(_descriptor, -1);
            }

            void Reset() noexcept {
                if (_descriptor >= 0) {
                    close(_descriptor);
                    _descriptor = -1;
                }
            }

        private:
            int _descriptor = -1;
        };

        void CloseDescriptor(int& descriptor) noexcept {
            if (descriptor >= 0) {
                close(descriptor);
                descriptor = -1;
            }
        }

        /// A pipe whose ends are close-on-exec and above stderr (an end that came out as 0, 1 or 2 would be taken for the child's own), or the errno
        int MakePipe(Descriptor& readEnd, Descriptor& writeEnd) {
            int ends[2] = {-1, -1};
            if (pipe(ends) != 0) {
                return errno;
            }
            const Descriptor rawRead(ends[kPipeReadEnd]);
            const Descriptor rawWrite(ends[kPipeWriteEnd]);
            readEnd = Descriptor(fcntl(rawRead.Get(), F_DUPFD_CLOEXEC, kFirstFreeDescriptor));
            writeEnd = Descriptor(fcntl(rawWrite.Get(), F_DUPFD_CLOEXEC, kFirstFreeDescriptor));
            return (readEnd.Get() >= 0 && writeEnd.Get() >= 0) ? 0 : errno;
        }

        /// A write to a pipe whose reader is gone must give EPIPE and not SIGPIPE (see the class comment)
        void ProtectFromSigpipe([[maybe_unused]] int writeDescriptor) {
#if defined(__APPLE__)
            if (fcntl(writeDescriptor, F_SETNOSIGPIPE, 1) == 0) {
                return;
            }
#endif
            signal(SIGPIPE, SIG_IGN);
        }

        /// What posix_spawn needs: the child's stdin, stdout and stderr, the descriptors it must not keep, a clean signal state
        class SpawnSetup {
        public:
            SpawnSetup() {
                posix_spawn_file_actions_init(&_actions);
                posix_spawnattr_init(&_attributes);
            }
            ~SpawnSetup() {
                posix_spawn_file_actions_destroy(&_actions);
                posix_spawnattr_destroy(&_attributes);
            }
            SpawnSetup(const SpawnSetup&) = delete;
            SpawnSetup& operator=(const SpawnSetup&) = delete;

            /// 0, or the error number of the first step that failed
            int Configure(int childInput, int childOutput) {
                int error = posix_spawn_file_actions_adddup2(&_actions, childInput, STDIN_FILENO);
                if (error == 0) {
                    error = posix_spawn_file_actions_adddup2(&_actions, childOutput, STDOUT_FILENO);
                }
                if (error == 0) {
                    error = posix_spawn_file_actions_addopen(&_actions, STDERR_FILENO, kNullDevice, O_WRONLY, 0);
                }
                int flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
#if defined(__APPLE__)
                flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;  // everything not set up above is closed in the child
#endif
#if defined(CHESS_SPAWN_CLOSEFROM)
                if (error == 0) {
                    error = posix_spawn_file_actions_addclosefrom_np(&_actions, kFirstFreeDescriptor);
                }
#endif
                sigset_t noBlockedSignals;
                sigemptyset(&noBlockedSignals);
                sigset_t defaultSignals;
                sigemptyset(&defaultSignals);
                sigaddset(&defaultSignals, SIGPIPE);
                if (error == 0) {
                    error = posix_spawnattr_setsigmask(&_attributes, &noBlockedSignals);
                }
                if (error == 0) {
                    error = posix_spawnattr_setsigdefault(&_attributes, &defaultSignals);
                }
                if (error == 0) {
                    error = posix_spawnattr_setflags(&_attributes, static_cast<short>(flags));
                }
                return error;
            }

            [[nodiscard]] const posix_spawn_file_actions_t* Actions() const noexcept {
                return &_actions;
            }
            [[nodiscard]] const posix_spawnattr_t* Attributes() const noexcept {
                return &_attributes;
            }

        private:
            posix_spawn_file_actions_t _actions;
            posix_spawnattr_t _attributes;
        };

#endif  // CHESS_HAS_UCI_PROCESS

    }  // namespace

#if !CHESS_HAS_UCI_PROCESS

    namespace {

        /// Why there is no process here, one sentence per case
        constexpr const char* kNoProcessDetail =
#if defined(_WIN32)
            "a UCI engine process is not implemented on Windows yet";
#elif defined(__EMSCRIPTEN__)
            "a UCI engine cannot run in a browser";
#else
            "a UCI engine process is switched off in this build (CHESS_NO_STOCKFISH)";
#endif

    }  // namespace

    std::expected<eastl::unique_ptr<UciProcess>, EngineError> UciProcess::Start(const ProcessLaunch&) {
        return Fail(EngineErrorKind::Unsupported, kNoProcessDetail);
    }

    UciProcess::UciProcess(int processId, int inputDescriptor, int outputDescriptor) : _pid(processId), _inputDescriptor(inputDescriptor), _outputDescriptor(outputDescriptor) {}

    UciProcess::~UciProcess() = default;

    std::expected<void, EngineError> UciProcess::WriteLine(eastl::string_view) {
        return Fail(EngineErrorKind::Unsupported, kNoProcessDetail);
    }

    std::expected<eastl::string, EngineError> UciProcess::ReadLine(std::chrono::milliseconds) {
        return Fail(EngineErrorKind::Unsupported, kNoProcessDetail);
    }

    void UciProcess::Close(std::chrono::milliseconds) {}

    bool UciProcess::IsRunning() {
        return false;
    }

#else

    UciProcess::UciProcess(int processId, int inputDescriptor, int outputDescriptor) : _pid(processId), _inputDescriptor(inputDescriptor), _outputDescriptor(outputDescriptor) {}

    UciProcess::~UciProcess() {
        Close(kDefaultCloseGrace);
    }

    std::expected<eastl::unique_ptr<UciProcess>, EngineError> UciProcess::Start(const ProcessLaunch& launch) {
        if (launch.path.empty()) {
            return Fail(EngineErrorKind::LaunchFailed, "no program to start");
        }

        Descriptor childInput;
        Descriptor inputToChild;
        Descriptor outputFromChild;
        Descriptor childOutput;
        int error = MakePipe(childInput, inputToChild);
        if (error == 0) {
            error = MakePipe(outputFromChild, childOutput);
        }
        if (error != 0) {
            return Fail(EngineErrorKind::LaunchFailed, Format("cannot start {}: no pipe: {}", launch.path, ErrnoText(error)));
        }

        SpawnSetup setup;
        error = setup.Configure(childInput.Get(), childOutput.Get());
        if (error != 0) {
            return Fail(EngineErrorKind::LaunchFailed, Format("cannot start {}: {}", launch.path, ErrnoText(error)));
        }

        eastl::vector<char*> argv;  // posix_spawn takes char* const[] but does not change the strings
        argv.push_back(const_cast<char*>(launch.path.c_str()));
        for (const eastl::string& argument : launch.args) {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);

        ProtectFromSigpipe(inputToChild.Get());
        pid_t processId = 0;
        error = posix_spawn(&processId, launch.path.c_str(), setup.Actions(), setup.Attributes(), argv.data(), environ);
        if (error != 0) {
            return Fail(EngineErrorKind::LaunchFailed, Format("cannot start {}: {}", launch.path, ErrnoText(error)));
        }

        // Only the child holds its ends now; with ours open here it would never see the end of the input, and we never the end of its output
        childInput.Reset();
        childOutput.Reset();
        return eastl::unique_ptr<UciProcess>(new UciProcess(static_cast<int>(processId), inputToChild.Release(), outputFromChild.Release()));
    }

    std::expected<void, EngineError> UciProcess::WriteLine(eastl::string_view text) {
        if (text.find_first_of("\r\n") != eastl::string_view::npos) {
            return Fail(EngineErrorKind::BadRequest, "a command line must not hold an end of line");
        }
        if (_ended || _inputDescriptor < 0) {
            return Fail(EngineErrorKind::ProcessDied, Format("process {} is not running", _pid));
        }

        eastl::string line(text.data(), text.size());
        line.push_back('\n');
        size_t written = 0;
        while (written < line.size()) {
            const ssize_t count = write(_inputDescriptor, line.data() + written, line.size() - written);
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                const int error = errno;
                _ended = true;
                return Fail(EngineErrorKind::ProcessDied, Format("cannot write to process {}: {}", _pid, ErrnoText(error)));
            }
            written += static_cast<size_t>(count);
        }
        return {};
    }

    std::optional<eastl::string> UciProcess::TakeBufferedLine() {
        constexpr char kLineEnd = '\n';
        constexpr char kCarriageReturn = '\r';

        const size_t lineEnd = _buffer.find(kLineEnd, _head + _scanned);
        if (lineEnd == eastl::string::npos) {
            _scanned = _buffer.size() - _head;
            return std::nullopt;
        }

        size_t textEnd = lineEnd;
        if (textEnd > _head && _buffer[textEnd - 1] == kCarriageReturn) {
            --textEnd;
        }
        eastl::string line(_buffer.data() + _head, textEnd - _head);
        _head = lineEnd + 1;
        _scanned = 0;
        if (_head == _buffer.size()) {
            _buffer.clear();
            _head = 0;
        } else if (_head >= kCompactBytes && _head * 2 >= _buffer.size()) {
            _buffer.erase(0, _head);
            _head = 0;
        }
        return line;
    }

    std::expected<eastl::string, EngineError> UciProcess::ReadLine(std::chrono::milliseconds timeout) {
        using Clock = std::chrono::steady_clock;
        const Clock::time_point deadline = Clock::now() + timeout;

        const auto tooLong = [this] { return Fail(EngineErrorKind::ProtocolError, Format("process {} wrote a line longer than {} bytes", _pid, kMaxLineBytes)); };

        for (;;) {
            if (std::optional<eastl::string> line = TakeBufferedLine()) {
                if (line->size() > kMaxLineBytes) {
                    return tooLong();  // it came whole within one read, but it is no line of ours
                }
                return std::move(*line);
            }
            if (_buffer.size() - _head > kMaxLineBytes) {
                _buffer.clear();
                _head = 0;
                _scanned = 0;
                return tooLong();
            }
            if (_outputEnded || _outputDescriptor < 0) {
                return Fail(EngineErrorKind::ProcessDied, Format("process {} closed its output", _pid));
            }

            // Rounded up, so that a timeout never ends early
            const auto remaining = std::max(Clock::duration::zero(), deadline - Clock::now());
            const auto remainingMs = std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
            pollfd request{};
            request.fd = _outputDescriptor;
            request.events = POLLIN;
            const int ready = poll(&request, 1, static_cast<int>(std::min<long long>(remainingMs, std::numeric_limits<int>::max())));
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                const int error = errno;
                _outputEnded = true;
                _ended = true;
                return Fail(EngineErrorKind::ProcessDied, Format("cannot wait for process {}: {}", _pid, ErrnoText(error)));
            }
            if (ready == 0) {
                if (Clock::now() < deadline) {
                    continue;  // poll came back early: a timeout is never shorter than asked
                }
                return Fail(EngineErrorKind::Timeout, Format("no line from process {} in {} ms", _pid, timeout.count()));
            }

            char chunk[kReadChunkBytes];
            const ssize_t count = read(_outputDescriptor, chunk, sizeof(chunk));
            if (count > 0) {
                _buffer.append(chunk, static_cast<size_t>(count));
            } else if (count == 0) {
                _outputEnded = true;  // the child is gone, or about to be: the loop answers once the buffered lines are out
                _ended = true;
                TryReap();
            } else if (errno != EINTR && errno != EAGAIN) {
                const int error = errno;
                _outputEnded = true;
                _ended = true;
                return Fail(EngineErrorKind::ProcessDied, Format("cannot read from process {}: {}", _pid, ErrnoText(error)));
            }
        }
    }

    bool UciProcess::TryReap() {
        if (_reaped) {
            return true;
        }
        int status = 0;
        const pid_t result = waitpid(static_cast<pid_t>(_pid), &status, WNOHANG);
        if (result == static_cast<pid_t>(_pid) || (result < 0 && errno != EINTR)) {  // an error here is ECHILD: somebody else collected it
            _reaped = true;
            return true;
        }
        return false;
    }

    bool UciProcess::WaitForExit(std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!TryReap()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(kExitPollInterval);
        }
        return true;
    }

    void UciProcess::Close(std::chrono::milliseconds grace) {
        _ended = true;
        CloseDescriptor(_inputDescriptor);  // the end of the input: a child that reads it to the end leaves by itself

        if (!_reaped && !WaitForExit(grace)) {
            kill(static_cast<pid_t>(_pid), SIGTERM);
            if (!WaitForExit(kTerminateGrace)) {
                kill(static_cast<pid_t>(_pid), SIGKILL);
                int status = 0;
                while (waitpid(static_cast<pid_t>(_pid), &status, 0) < 0 && errno == EINTR) {
                }
                _reaped = true;
            }
        }

        CloseDescriptor(_outputDescriptor);  // after the exit: a child still writing would have got EPIPE while it left
        _outputEnded = true;
        _buffer.clear();
        _head = 0;
        _scanned = 0;
    }

    bool UciProcess::IsRunning() {
        if (_ended) {
            return false;
        }
        if (TryReap()) {
            _ended = true;
            return false;
        }
        return true;
    }

#endif  // !CHESS_HAS_UCI_PROCESS

}  // namespace BECore::Chess
