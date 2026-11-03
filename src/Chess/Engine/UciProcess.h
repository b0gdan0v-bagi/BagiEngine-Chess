#pragma once

#include <Chess/Engine/ChessEngine.h>
#include <chrono>

namespace BECore::Chess {

    /// A program to start: the path of its executable (taken as it is, `PATH` is not searched) and the arguments after it
    struct ProcessLaunch {
        eastl::string path;
        eastl::vector<eastl::string> args;
    };

    /**
     * @brief A child process that is talked to in lines of text: one line in on its stdin, one line out of its stdout.
     *
     * The transport under a UCI engine (UciEngine of a later step); it knows nothing of UCI and logs nothing. Every failure is an
     * EngineError: LaunchFailed (the program did not start), Timeout (no line in time; the process stays usable), ProcessDied (it ended
     * or closed a pipe), ProtocolError (a line longer than kMaxLineBytes), BadRequest (a line with an end of line inside),
     * Unsupported (Windows, below).
     *
     * One thread at a time: the class has no lock (UciEngine serializes its searches). The child gets the pipes as stdin and stdout, /dev/null as
     * stderr, and no other descriptor of the game: a control socket or the pipe of another engine would stay open in it otherwise
     * (the pipes here are close-on-exec; on macOS the child closes everything else, with glibc 2.34 or later too).
     *
     * SIGPIPE: a write to a child that is gone must come back as ProcessDied, not kill the game. On macOS the write end carries F_SETNOSIGPIPE,
     * which touches nothing else. Elsewhere every Start() sets the disposition of SIGPIPE for the whole process to "ignore", a side effect
     * of the process class (a host that wants its own handler installs it after the last Start()); the child gets the default back.
     *
     * POSIX only (macOS, Linux). On Windows Start() answers Unsupported and no Windows header is included; the process code for it is a later step.
     */
    class UciProcess {
    public:
        /// The longest line ReadLine() takes; a child that writes more without an end of line gives ProtocolError
        static constexpr size_t kMaxLineBytes = 1024 * 1024;

        /// How long the destructor gives a child to leave after its input is closed, before it sends signals
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
        [[nodiscard]] std::expected<void, EngineError> WriteLine(eastl::string_view text);

        /// The next line the child wrote, without its end of line (`\n` or `\r\n`), waiting at most @p timeout. Timeout when none came whole in that time:
        /// the bytes that did come are kept for the next call, so a line that arrives in pieces, or several lines in one read, are no problem.
        /// ProcessDied once the child closed its output and the lines before that were read (an unfinished last line is dropped)
        [[nodiscard]] std::expected<eastl::string, EngineError> ReadLine(std::chrono::milliseconds timeout);

        /**
         * @brief Ends the child and waits for it, whatever it does; the child is reaped when this returns. A second call does nothing.
         *
         * Closes its stdin (the end of input is the polite request, a caller that speaks a protocol sends its goodbye, for UCI `quit`, first),
         * waits up to @p grace for it to leave, then sends SIGTERM and waits a little, then SIGKILL.
         */
        void Close(std::chrono::milliseconds grace = kDefaultCloseGrace);

        /// True while the child is not known to have ended: false after Close(), after ReadLine() saw the end of its output or WriteLine() a broken pipe, and once it was reaped
        [[nodiscard]] bool IsRunning();

        /// The process id of the child, also after it ended (for a log, and for the tests that look for it)
        [[nodiscard]] int GetProcessId() const noexcept {
            return _pid;
        }

    private:
        UciProcess(int processId, int inputDescriptor, int outputDescriptor);

        std::optional<eastl::string> TakeBufferedLine();
        bool TryReap();
        bool WaitForExit(std::chrono::milliseconds timeout);

        int _pid = -1;
        int _inputDescriptor = -1;   ///< our write end, the child's stdin
        int _outputDescriptor = -1;  ///< our read end, the child's stdout
        bool _ended = false;         ///< the child is known to be gone or going: no more writes
        bool _outputEnded = false;   ///< end of file on the child's stdout: no more reads once the buffer is empty
        bool _reaped = false;        ///< waitpid has collected the child; the pid must not be signalled or waited for again
        eastl::string _buffer;       ///< bytes read and not yet given out; the next line starts at _head
        size_t _head = 0;
        size_t _scanned = 0;  ///< bytes after _head already searched for an end of line
    };

}  // namespace BECore::Chess
