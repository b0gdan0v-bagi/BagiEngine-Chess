#pragma once

#include <Chess/Engine/ChessEngine.h>
#include <chrono>

// 1 where this class has process code (POSIX in UciProcess.cpp, Windows in UciProcessWindows.cpp), 0 where Start() only answers Unsupported: in a build with CHESS_NO_STOCKFISH
// (no engine program is ever started) and under Emscripten (a browser has no child process, and no <spawn.h>)
#if CHESS_NO_STOCKFISH || defined(__EMSCRIPTEN__)
#define CHESS_HAS_UCI_PROCESS 0
#else
#define CHESS_HAS_UCI_PROCESS 1
#endif

namespace BECore::Chess {

    /// A program to start: the path of its executable (taken as it is, `PATH` is not searched) and the arguments after it
    struct ProcessLaunch {
        eastl::string path;
        eastl::vector<eastl::string> args;
    };

    /**
     * @brief A child process that is talked to in lines of text: one line in on its stdin, one line out of its stdout.
     *
     * The transport under UciEngine; it knows nothing of UCI and logs nothing. Every failure is an EngineError: LaunchFailed (the program did not start), Timeout (no line in time; the process stays
     * usable), ProcessDied (it ended or closed a pipe), ProtocolError (a line longer than kMaxLineBytes), BadRequest (a line with an end of line inside), Unsupported (no process code, below). After a
     * ProtocolError the stream is out of step (the rest of the long line comes later as lines): the caller must Close() the process.
     *
     * One thread at a time: the class has no lock. UciEngine owns its process and touches it only while it holds its mutex (one search at a time,
     * on whatever thread it runs); any other user of a UciProcess keeps to one thread or locks it.
     *
     * The child gets the pipes as stdin and stdout, /dev/null as stderr, and no other descriptor of the game: a control socket or the pipe of
     * another engine would stay open in it otherwise (the pipes here are close-on-exec; on macOS the child closes everything else, with glibc 2.34 or later too).
     *
     * SIGPIPE: a write to a child that is gone must come back as ProcessDied, not kill the game. On macOS the write end carries F_SETNOSIGPIPE,
     * set per descriptor, which touches nothing else in the process. Elsewhere (and on macOS if that fcntl fails) every Start() sets the disposition of
     * SIGPIPE for the whole process to "ignore", overwriting a handler the host installed earlier. A host cannot get its own handler back by installing
     * it after the last Start(), because UciEngine starts the process again after an error: a host that needs its own SIGPIPE handler off Apple
     * cannot share its process with this class. The child gets the default disposition back.
     *
     * Windows (UciProcessWindows.cpp): CreateProcessW with the path as the application (no search) and a command line quoted for the C runtime of the child; the child
     * inherits exactly its three handles (PROC_THREAD_ATTRIBUTE_HANDLE_LIST), its stderr is NUL, and it gets no console window. Its stdout is a named pipe
     * read with overlapped I/O, so ReadLine() waits with a timeout like poll(); a read in flight is kept between calls. There are no signals: a write to a
     * child that is gone is ERROR_NO_DATA / ERROR_BROKEN_PIPE, so ProcessDied, and Close() goes from the grace straight to TerminateProcess. The process id
     * stays valid until Close() lets go of the process handle. The texts of the system errors are the system's, in its language, with the number: "(error 2)".
     *
     * Where CHESS_HAS_UCI_PROCESS is 0 (CHESS_NO_STOCKFISH, Emscripten) Start() answers Unsupported with a sentence for the case, the other calls answer it too,
     * and no system header is included. (The Emscripten case has not been compiled yet: it needs emsdk.)
     */
    class UciProcess {
    public:
        /// The longest line ReadLine() takes; a child that writes more without an end of line gives ProtocolError
        static constexpr size_t kMaxLineBytes = 1024 * 1024;

        /// How long the destructor gives a child to leave after its input is closed, before it sends signals (on Windows: before TerminateProcess)
        static constexpr std::chrono::milliseconds kDefaultCloseGrace{500};

        /// Starts @p launch. The program is started when this returns: a missing file or one that cannot be executed is LaunchFailed with the reason from the system in `detail`
        [[nodiscard]] static std::expected<eastl::unique_ptr<UciProcess>, EngineError> Start(const ProcessLaunch& launch);

        /// Close(kDefaultCloseGrace): the child never outlives the object, and is not left as a zombie
        ~UciProcess();

        UciProcess(const UciProcess&) = delete;
        UciProcess& operator=(const UciProcess&) = delete;
        UciProcess(UciProcess&&) = delete;
        UciProcess& operator=(UciProcess&&) = delete;

        /// Sends @p text and an end of line, unbuffered (nothing waits in the process for a flush). ProcessDied when the child has gone (a broken pipe),
        /// BadRequest when @p text holds an end of line (it would be two commands). A child that does not read blocks the call once the pipe (64 KiB) is full
        [[nodiscard]] std::expected<void, EngineError> WriteLine(StringView text);

        /// The next line the child wrote, without its end of line (`\n` or `\r\n`), waiting at most @p timeout. Timeout when none came whole in that time:
        /// the bytes that did come are kept for the next call, so a line that arrives in pieces, or several lines in one read, are no problem.
        /// ProcessDied once the child closed its output and the lines before that were read (an unfinished last line is dropped)
        [[nodiscard]] std::expected<eastl::string, EngineError> ReadLine(std::chrono::milliseconds timeout);

        /**
         * @brief Ends the child and waits for it, whatever it does; the child is reaped when this returns. A second call does nothing.
         *
         * Closes its stdin (the end of input is the polite request, a caller that speaks a protocol sends its goodbye, for UCI `quit`, first),
         * waits up to @p grace for it to leave, then sends SIGTERM and waits a little, then SIGKILL (on Windows: TerminateProcess right after the grace).
         */
        void Close(std::chrono::milliseconds grace = kDefaultCloseGrace);

        /// True while the child is not known to have ended: false after Close(), after ReadLine() saw the end of its output or WriteLine() a broken pipe, and once it was reaped
        [[nodiscard]] bool IsRunning();

        /// The process id of the child, also after it ended (for a log, and for the tests that look for it)
        [[nodiscard]] int GetProcessId() const noexcept {
            return _pid;
        }

    private:
        Optional<eastl::string> TakeBufferedLine();

#if CHESS_HAS_UCI_PROCESS && defined(_WIN32)
        struct WindowsState;  ///< the handles and the read in flight (UciProcessWindows.cpp: no <windows.h> in this header)

        UciProcess(int processId, eastl::unique_ptr<WindowsState> state);

        eastl::unique_ptr<WindowsState> _windows;
#else
        UciProcess(int processId, int inputDescriptor, int outputDescriptor);

        bool TryReap();
        bool WaitForExit(std::chrono::milliseconds timeout);

        int _inputDescriptor = -1;   ///< our write end, the child's stdin
        int _outputDescriptor = -1;  ///< our read end, the child's stdout
        bool _reaped = false;        ///< waitpid has collected the child; the pid must not be signalled or waited for again
#endif

        int _pid = -1;
        bool _ended = false;        ///< the child is known to be gone or going: no more writes
        bool _outputEnded = false;  ///< end of file on the child's stdout: no more reads once the buffer is empty
        eastl::string _buffer;      ///< bytes read and not yet given out; the next line starts at _head
        size_t _head = 0;
        size_t _scanned = 0;  ///< bytes after _head already searched for an end of line
    };

}  // namespace BECore::Chess
