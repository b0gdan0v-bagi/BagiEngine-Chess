#include "UciProcess.h"

// The Windows process code of UciProcess (the class comment in UciProcess.h); POSIX and the stub are in UciProcess.cpp
#if CHESS_HAS_UCI_PROCESS && defined(_WIN32)

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>

namespace BECore::Chess {

    namespace {

        constexpr DWORD kReadChunkBytes = 4096;
        constexpr DWORD kPipeBytes = 64 * 1024;    // the size the system is asked for, for both pipes (a hint: it may round it)
        constexpr UINT kTerminatedExitCode = 1;    // the exit code of a child that TerminateProcess ended
        constexpr wchar_t kNullDevice[] = L"NUL";  // the child's stderr
        constexpr wchar_t kPipeNamePrefix[] = L"\\\\.\\pipe\\BagiChess-uci-";

        std::unexpected<EngineError> Fail(EngineErrorKind kind, String detail) {
            return std::unexpected(EngineError{kind, std::move(detail)});
        }

        /// UTF-16 of UTF-8 text, nothing for text that is not UTF-8
        Optional<std::wstring> Widen(StringView text) {
            if (text.empty()) {
                return std::wstring();
            }
            const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (length <= 0) {
                return std::nullopt;
            }
            std::wstring wide(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), length);
            return wide;
        }

        String Narrow(const wchar_t* text, size_t length) {
            if (length == 0) {
                return {};
            }
            const int size = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
            if (size <= 0) {
                return {};
            }
            String narrow(static_cast<size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), narrow.data(), size, nullptr, nullptr);
            return narrow;
        }

        /// The system's sentence for @p code, in its language, without the end of line and the full stop, and the number: "Access is denied (error 5)".
        /// A few sentences have a placeholder for the file ("This version of %1 is not compatible ..."): it becomes "the program"
        String ErrorText(DWORD code) {
            wchar_t* message = nullptr;
            const DWORD length =
                FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
            size_t end = length;
            while (end > 0 && (message[end - 1] == L'\r' || message[end - 1] == L'\n' || message[end - 1] == L' ' || message[end - 1] == L'.')) {
                --end;
            }
            String text = message != nullptr ? Narrow(message, end) : String();
            LocalFree(message);
            constexpr StringView kPlaceholder = "%1";
            for (size_t at = text.find(kPlaceholder.data(), 0, kPlaceholder.size()); at != String::npos; at = text.find(kPlaceholder.data(), at, kPlaceholder.size())) {
                text.replace(at, kPlaceholder.size(), "the program");
            }
            if (text.empty()) {
                return Format("error {}", code);
            }
            return Format("{} (error {})", text, code);
        }

        /// A handle closed with the object; null and INVALID_HANDLE_VALUE both mean none
        class UniqueHandle {
        public:
            UniqueHandle() = default;
            explicit UniqueHandle(HANDLE handle) : _handle(handle == INVALID_HANDLE_VALUE ? nullptr : handle) {}
            ~UniqueHandle() {
                Reset();
            }
            UniqueHandle(const UniqueHandle&) = delete;
            UniqueHandle& operator=(const UniqueHandle&) = delete;
            UniqueHandle(UniqueHandle&& other) noexcept : _handle(std::exchange(other._handle, nullptr)) {}
            UniqueHandle& operator=(UniqueHandle&& other) noexcept {
                if (this != &other) {
                    Reset();
                    _handle = std::exchange(other._handle, nullptr);
                }
                return *this;
            }

            [[nodiscard]] HANDLE Get() const noexcept {
                return _handle;
            }

            [[nodiscard]] explicit operator bool() const noexcept {
                return _handle != nullptr;
            }

            void Reset() noexcept {
                if (_handle != nullptr) {
                    CloseHandle(_handle);
                    _handle = nullptr;
                }
            }

        private:
            HANDLE _handle = nullptr;
        };

        /// The PROC_THREAD_ATTRIBUTE_LIST that names the only handles the child inherits
        class InheritedHandles {
        public:
            InheritedHandles() = default;
            ~InheritedHandles() {
                if (_initialized) {
                    DeleteProcThreadAttributeList(List());
                }
            }
            InheritedHandles(const InheritedHandles&) = delete;
            InheritedHandles& operator=(const InheritedHandles&) = delete;

            /// 0, or the error of the step that failed. @p handles must outlive the object (the list points at them)
            DWORD Configure(HANDLE* handles, size_t count) {
                SIZE_T size = 0;
                InitializeProcThreadAttributeList(nullptr, 1, 0, &size);  // fails with ERROR_INSUFFICIENT_BUFFER and gives the size
                _storage.resize(size);
                if (!InitializeProcThreadAttributeList(List(), 1, 0, &size)) {
                    return GetLastError();
                }
                _initialized = true;
                if (!UpdateProcThreadAttribute(List(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, count * sizeof(HANDLE), nullptr, nullptr)) {
                    return GetLastError();
                }
                return 0;
            }

            [[nodiscard]] LPPROC_THREAD_ATTRIBUTE_LIST List() noexcept {
                return reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(_storage.data());
            }

        private:
            eastl::vector<char> _storage;
            bool _initialized = false;
        };

        /// @p argument quoted so that the C runtime of the child (CommandLineToArgvW, the MSVC CRT) gives it back as it is: backslashes are literal except before a quote
        void AppendQuoted(std::wstring& line, const std::wstring& argument) {
            if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
                line += argument;
                return;
            }
            line += L'"';
            for (auto it = argument.begin();; ++it) {
                size_t backslashes = 0;
                while (it != argument.end() && *it == L'\\') {
                    ++it;
                    ++backslashes;
                }
                if (it == argument.end()) {
                    line.append(backslashes * 2, L'\\');  // the closing quote must stay a quote
                    break;
                }
                if (*it == L'"') {
                    line.append(backslashes * 2 + 1, L'\\');
                } else {
                    line.append(backslashes, L'\\');
                }
                line += *it;
            }
            line += L'"';
        }

        /// A name no other pipe of this machine has: the id of this process and a counter
        std::wstring UniquePipeName() {
            static std::atomic<uint32_t> counter{0};
            return std::wstring(kPipeNamePrefix) + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(counter.fetch_add(1));
        }

        /// A job whose processes are killed when its last handle closes; none when the system refuses one
        UniqueHandle MakeKillOnCloseJob() {
            UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
            if (!job) {
                return job;
            }
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
                job.Reset();
            }
            return job;
        }

        /// A wait for the Win32 calls, which take a DWORD of milliseconds where INFINITE is reserved
        DWORD WaitMilliseconds(std::chrono::milliseconds time) {
            return static_cast<DWORD>(std::clamp<long long>(time.count(), 0, static_cast<long long>(INFINITE) - 1));
        }

    }  // namespace

    struct UciProcess::WindowsState {
        UniqueHandle job;  ///< the child and whatever it starts; killed when closed (JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE), also when the game itself dies. None if the system refused it
        UniqueHandle process;
        UniqueHandle input;     ///< our write end, the child's stdin (an anonymous pipe)
        UniqueHandle output;    ///< our read end, the child's stdout (a named pipe opened for overlapped I/O)
        UniqueHandle readDone;  ///< the event of `overlapped`, manual reset
        OVERLAPPED overlapped{};
        bool readPending = false;  ///< a ReadFile into `chunk` is in flight: `overlapped` and `chunk` belong to the system until it completes
        char chunk[kReadChunkBytes];

        /// Ends a read in flight and waits until the system lets go of `overlapped` and `chunk`
        void CancelRead() {
            if (!readPending) {
                return;
            }
            CancelIoEx(output.Get(), &overlapped);
            DWORD ignored = 0;
            GetOverlappedResult(output.Get(), &overlapped, &ignored, TRUE);
            readPending = false;
        }

        ~WindowsState() {
            CancelRead();
        }
    };

    UciProcess::UciProcess(int processId, eastl::unique_ptr<WindowsState> state) : _windows(std::move(state)), _pid(processId) {}

    UciProcess::~UciProcess() {
        Close(kDefaultCloseGrace);
    }

    std::expected<eastl::unique_ptr<UciProcess>, EngineError> UciProcess::Start(const ProcessLaunch& launch) {
        if (launch.path.empty()) {
            return Fail(EngineErrorKind::LaunchFailed, "no program to start");
        }
        const auto cannotStart = [&launch](StringView reason) { return Fail(EngineErrorKind::LaunchFailed, Format("cannot start {}: {}", launch.path, reason)); };

        Optional<std::wstring> application = Widen(launch.path);
        if (!application) {
            return cannotStart("the path is not UTF-8");
        }
        std::replace(application->begin(), application->end(), L'/', L'\\');
        // argv[0] is read by other rules (no escapes, quotes only), and a path holds no quote: it is always quoted
        std::wstring commandLine = L"\"" + *application + L"\"";
        for (const String& argument : launch.args) {
            const Optional<std::wstring> wide = Widen(argument);
            if (!wide) {
                return cannotStart("an argument is not UTF-8");
            }
            commandLine += L' ';
            AppendQuoted(commandLine, *wide);
        }

        auto state = eastl::make_unique<WindowsState>();
        SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};

        // stdin: an anonymous pipe, our end kept from the child
        HANDLE childInputRaw = nullptr;
        HANDLE inputRaw = nullptr;
        if (!CreatePipe(&childInputRaw, &inputRaw, &inheritable, kPipeBytes)) {
            return cannotStart(Format("no pipe: {}", ErrorText(GetLastError())));
        }
        const UniqueHandle childInput(childInputRaw);
        state->input = UniqueHandle(inputRaw);
        if (!SetHandleInformation(state->input.Get(), HANDLE_FLAG_INHERIT, 0)) {
            return cannotStart(Format("no pipe: {}", ErrorText(GetLastError())));
        }

        // stdout: a named pipe, because an anonymous one cannot be read with a timeout; the child's end is an ordinary synchronous handle
        const std::wstring pipeName = UniquePipeName();
        state->output = UniqueHandle(CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                                      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 0, kPipeBytes, 0, nullptr));
        if (!state->output) {
            return cannotStart(Format("no pipe: {}", ErrorText(GetLastError())));
        }
        const UniqueHandle childOutput(CreateFileW(pipeName.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES, 0, &inheritable, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!childOutput) {
            return cannotStart(Format("no pipe: {}", ErrorText(GetLastError())));
        }
        const UniqueHandle childError(CreateFileW(kNullDevice, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING, 0, nullptr));
        if (!childError) {
            return cannotStart(Format("no {}: {}", "NUL", ErrorText(GetLastError())));
        }
        state->readDone = UniqueHandle(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!state->readDone) {
            return cannotStart(ErrorText(GetLastError()));
        }
        state->overlapped.hEvent = state->readDone.Get();

        HANDLE inherited[] = {childInput.Get(), childOutput.Get(), childError.Get()};
        InheritedHandles handleList;
        if (const DWORD error = handleList.Configure(inherited, std::size(inherited)); error != 0) {
            return cannotStart(ErrorText(error));
        }

        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = childInput.Get();
        startup.StartupInfo.hStdOutput = childOutput.Get();
        startup.StartupInfo.hStdError = childError.Get();
        startup.lpAttributeList = handleList.List();

        state->job = MakeKillOnCloseJob();

        PROCESS_INFORMATION created{};
        // CREATE_NO_WINDOW: a console program started from the game (a window, no console) would open a console window of its own. Suspended until it is in the job,
        // so that nothing it starts is outside
        if (!CreateProcessW(application->c_str(), commandLine.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr,
                            &startup.StartupInfo, &created)) {
            return cannotStart(ErrorText(GetLastError()));
        }
        state->process = UniqueHandle(created.hProcess);
        if (state->job && !AssignProcessToJobObject(state->job.Get(), created.hProcess)) {
            state->job.Reset();  // it runs without one: Close() still ends the child itself, only what it started could stay
        }
        ResumeThread(created.hThread);
        CloseHandle(created.hThread);

        // The child's ends close here as the handles above go out of scope: only the child holds them now, so we see the end of its output, and it the end of its input
        return eastl::unique_ptr<UciProcess>(new UciProcess(static_cast<int>(created.dwProcessId), std::move(state)));
    }

    std::expected<void, EngineError> UciProcess::WriteLine(StringView text) {
        if (text.find_first_of("\r\n") != StringView::npos) {
            return Fail(EngineErrorKind::BadRequest, "a command line must not hold an end of line");
        }
        if (_ended || !_windows->input) {
            return Fail(EngineErrorKind::ProcessDied, Format("process {} is not running", _pid));
        }

        String line(text.data(), text.size());
        line.push_back('\n');
        size_t written = 0;
        while (written < line.size()) {
            DWORD count = 0;
            if (!WriteFile(_windows->input.Get(), line.data() + written, static_cast<DWORD>(line.size() - written), &count, nullptr)) {
                const DWORD error = GetLastError();  // ERROR_NO_DATA or ERROR_BROKEN_PIPE: the child closed its input or is gone
                _ended = true;
                return Fail(EngineErrorKind::ProcessDied, Format("cannot write to process {}: {}", _pid, ErrorText(error)));
            }
            written += count;
        }
        return {};
    }

    std::expected<String, EngineError> UciProcess::ReadLine(std::chrono::milliseconds timeout) {
        using Clock = std::chrono::steady_clock;
        const Clock::time_point deadline = Clock::now() + timeout;
        WindowsState& state = *_windows;

        const auto tooLong = [this] { return Fail(EngineErrorKind::ProtocolError, Format("process {} wrote a line longer than {} bytes", _pid, kMaxLineBytes)); };
        const auto outputEnded = [this] {
            _outputEnded = true;  // the child is gone, or about to be: the loop answers once the buffered lines are out
            _ended = true;
        };

        for (;;) {
            if (Optional<String> line = TakeBufferedLine()) {
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
            if (_outputEnded || !state.output) {
                return Fail(EngineErrorKind::ProcessDied, Format("process {} closed its output", _pid));
            }

            if (!state.readPending) {
                // A read that completes at once still signals the event, so both cases go to the wait below
                if (!ReadFile(state.output.Get(), state.chunk, kReadChunkBytes, nullptr, &state.overlapped) && GetLastError() != ERROR_IO_PENDING) {
                    const DWORD error = GetLastError();
                    outputEnded();
                    if (error == ERROR_BROKEN_PIPE) {
                        continue;
                    }
                    return Fail(EngineErrorKind::ProcessDied, Format("cannot read from process {}: {}", _pid, ErrorText(error)));
                }
                state.readPending = true;
            }

            // Rounded up, so that a timeout never ends early
            const auto remaining = std::max(Clock::duration::zero(), deadline - Clock::now());
            const auto remainingMs = std::chrono::ceil<std::chrono::milliseconds>(remaining);
            const DWORD waited = WaitForSingleObject(state.readDone.Get(), WaitMilliseconds(remainingMs));
            if (waited == WAIT_TIMEOUT) {
                if (Clock::now() < deadline) {
                    continue;
                }
                return Fail(EngineErrorKind::Timeout, Format("no line from process {} in {} ms", _pid, timeout.count()));  // the read stays in flight for the next call
            }
            if (waited != WAIT_OBJECT_0) {
                const DWORD error = GetLastError();
                return Fail(EngineErrorKind::ProcessDied, Format("cannot wait for process {}: {}", _pid, ErrorText(error)));
            }

            DWORD count = 0;
            const BOOL done = GetOverlappedResult(state.output.Get(), &state.overlapped, &count, FALSE);
            const DWORD error = done ? ERROR_SUCCESS : GetLastError();
            state.readPending = false;
            if (done) {
                _buffer.append(state.chunk, count);
            } else if (error == ERROR_BROKEN_PIPE) {
                outputEnded();
            } else {
                outputEnded();
                return Fail(EngineErrorKind::ProcessDied, Format("cannot read from process {}: {}", _pid, ErrorText(error)));
            }
        }
    }

    void UciProcess::Close(std::chrono::milliseconds grace) {
        _ended = true;
        WindowsState& state = *_windows;
        state.input.Reset();  // the end of the input: a child that reads it to the end leaves by itself

        if (state.process) {
            if (WaitForSingleObject(state.process.Get(), WaitMilliseconds(grace)) != WAIT_OBJECT_0) {
                if (!state.job || !TerminateJobObject(state.job.Get(), kTerminatedExitCode)) {
                    TerminateProcess(state.process.Get(), kTerminatedExitCode);
                }
                WaitForSingleObject(state.process.Get(), INFINITE);
            }
            state.process.Reset();  // the last handle of ours: the system lets go of the process and its id
        }
        state.job.Reset();  // kills what the child started and left behind (a wrapper's program)

        state.CancelRead();
        state.output.Reset();  // after the exit, like on POSIX
        _outputEnded = true;
        _buffer.clear();
        _head = 0;
        _scanned = 0;
    }

    bool UciProcess::IsRunning() {
        if (_ended) {
            return false;
        }
        if (!_windows->process || WaitForSingleObject(_windows->process.Get(), 0) == WAIT_OBJECT_0) {
            _ended = true;
            return false;
        }
        return true;
    }

}  // namespace BECore::Chess

#endif  // CHESS_HAS_UCI_PROCESS && defined(_WIN32)
