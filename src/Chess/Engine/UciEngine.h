#pragma once

#include <Chess/Engine/SearchRequest.h>
#include <Chess/Engine/UciProcess.h>
#include <Chess/Engine/UciProtocol.h>

namespace BECore::Chess {

    /// A UCI engine to start and how long it gets for each step (the tests shorten the times)
    struct UciEngineSettings {
        static constexpr int kDefaultStartupTimeoutMs = 15000;    ///< each of the waits for `uciok` and `readyok`
        static constexpr int kDefaultAnswerSlackMs = 15000;       ///< how long past `movetime` the engine gets to send `bestmove`
        static constexpr int kDefaultDepthOnlyTimeoutMs = 30000;  ///< the deadline of a search that has a depth and no time
        static constexpr int kDefaultStopGraceMs = 1000;          ///< how long a cancelled search waits for the `bestmove` that answers `stop`
        static constexpr int kDefaultQuitGraceMs = 5000;          ///< how long a closed engine gets to leave after `quit` before it is sent a signal

        ProcessLaunch launch;
        eastl::vector<eastl::pair<eastl::string, eastl::string>> options;  ///< `setoption name <first> value <second>`, in this order, after `uciok`
        int startupTimeoutMs = kDefaultStartupTimeoutMs;
        int answerSlackMs = kDefaultAnswerSlackMs;
        int depthOnlyTimeoutMs = kDefaultDepthOnlyTimeoutMs;
        int stopGraceMs = kDefaultStopGraceMs;
        int quitGraceMs = kDefaultQuitGraceMs;
    };

    /**
     * @brief A UCI engine (Stockfish, or any program that speaks UCI) in a child process: the C++ port of `UciEngine` of tools/stockfish_bridge.py.
     *
     * The constructor starts nothing, so the thread that makes the object never waits for a program to start. The process starts on
     * the first FindMove() or Analyse() and again on the first one after an error closed it (a hung or dead engine is replaced, not waited for).
     * FindMove() and Analyse() block the calling thread for the whole search, so a caller that must not wait (the main thread) runs it on a worker.
     * Calls are serialized by a mutex: one search at a time, the others wait for their turn. A call takes the mutex before it looks at its
     * token, so a thread waiting for its turn sees its own cancellation only after the lock is taken, which includes the closing of a process
     * by the search that holds it. The object must not be destroyed while a call is running on another thread.
     *
     * A call goes: the request is checked (BadRequest, and NoMove when the game is over, both before any process is started); the
     * process is started and shaken hands with when there is none (`uci` -> `uciok`, the options, `isready` -> `readyok`); a new game
     * (the first search of a process, another start FEN, or moves that do not extend the previous request's; the same moves again are the same game) sends `ucinewgame` and
     * `isready`; then `position`, `go`, and the lines are read until `bestmove`. `info` lines and lines it does not know are skipped.
     * The waits go in slices of 20 ms, so a cancelled token is seen soon.
     *
     * What closes the process (`quit`, then `quitGraceMs`, then the signals of UciProcess::Close()): Timeout (no `uciok`, `readyok`
     * or `bestmove` in time), ProcessDied, ProtocolError (except Analyse()'s missing score, below), a cancelled search whose engine does not answer `stop` within `stopGraceMs`
     * (the call still answers Cancelled), a cancel during the setup (the waits for `uciok` and `readyok`, `ucinewgame` and `isready`: no
     * search is running yet, so no `stop` is sent, the process is closed and Cancelled comes back once the process has gone: at once for an engine that obeys `quit`, after the
     * `quitGraceMs` and the signals for one that does not), and the
     * destructor. What keeps it: NoMove and IllegalMove (the engine is alive and in step), the ProtocolError of an Analyse() whose `bestmove` came with no usable score (it answered, it is in step),
     * and a cancelled search the engine did answer (its `bestmove` is dropped). The object logs nothing.
     */
    class UciEngine final : public IChessEngine {
    public:
        explicit UciEngine(UciEngineSettings settings);

        /// Closes the process on the thread that destroys the object; it can take up to the quit grace of the settings plus 0.5 s for the signals
        ~UciEngine() override;

        UciEngine(const UciEngine&) = delete;
        UciEngine& operator=(const UciEngine&) = delete;
        UciEngine(UciEngine&&) = delete;
        UciEngine& operator=(UciEngine&&) = delete;

        /**
         * @brief The move the engine answers with, legal in the position @p request leads to.
         *
         * BadRequest: the limits or the request are wrong. NoMove: the position has no legal move (the engine is not asked), or the engine
         * answered `bestmove (none)` / `0000`. IllegalMove: the engine named a move that is not legal in the position (its text is in `detail`).
         * LaunchFailed, Timeout, ProcessDied, ProtocolError: the engine could not be started, did not answer in time, ended, or did not speak UCI.
         * Cancelled: @p token was cancelled. Unsupported: this platform has no process code.
         *
         * The deadline of the answer is `moveTimeMs + answerSlackMs`, or `depthOnlyTimeoutMs` for a search that has a depth and no time.
         */
        [[nodiscard]] std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken& token) override;

        /**
         * @brief The best move and how the position stands: `request.lines` lines (Analysis.h), best first, one by default.
         *
         * The same search as FindMove(): the same `position` and `go` (so the limits are the same: a `movetime` has the deadline `moveTimeMs + answerSlackMs`, a
         * depth without time `depthOnlyTimeoutMs`), the same errors and the same effect on the process (what closes it, what keeps it, `ucinewgame`). Line 1 is the
         * `bestmove` (checked against the position like FindMove's) with the score and the depth of the last `info` line before it that carries a score of the main line
         * (`multipv` 1; `info string` text is not read); a `lowerbound` / `upperbound` score is used only when no exact one came. The score is from the side to move
         * already, as UCI sends it, and is not turned. An engine that answers `bestmove` with no scored `info` line of index 1 before it is
         * ProtocolError ("sent no score"); the process is kept, it is in step.
         *
         * More than one line: `setoption name MultiPV value N` is sent before `position` when the process has another value (a new process has 1; a search for one line,
         * `FindMove()` included, sends `MultiPV 1` again, so the opponent's search is never slowed by an analysis on the same object), and each index 2..N gives its line
         * from the last scored `info` line of that index: the first move of its `pv` with its score and depth. A move that is not legal is IllegalMove (the process is kept);
         * a move that is already a line, and an index with no usable line, are dropped, so the answer may have fewer lines than asked (an engine sends fewer when the
         * position has fewer legal moves; that is not an error).
         *
         * One search at a time, FindMove() and Analyse() share the mutex: a game that must analyse while the program plays gives the analysis an engine object of its own
         * (a second process), or the opponent's search would wait for it.
         */
        [[nodiscard]] std::expected<Analysis, EngineError> Analyse(const SearchRequest& request, const CancellationToken& token) override;

    private:
        static constexpr int kSingleLine = 1;  ///< the lines of FindMove(), and the value of `MultiPV` a new process has

        /// What a finished search leaves: the move (legal in @p position, the position of the request) and the `info` lines to analyse from, one per `multipv` index
        /// that had a usable score, in the order of the index (UciProtocol::LineCollector)
        struct Searched {
            Move move;
            Position position;
            eastl::vector<UciProtocol::Info> infos;
        };

        /// The whole of FindMove() and Analyse(), with the mutex already held: validation, NoMove, Cancelled, Begin(), the wait for `bestmove` (StopSearch(), CloseAndFail()), the move checked.
        /// @p lineCount is the `MultiPV` of the search: 1 for FindMove(), `request.lines` for Analyse()
        [[nodiscard]] std::expected<Searched, EngineError> RunSearch(const SearchRequest& request, int lineCount, const CancellationToken& token);
        /// Everything up to and including `go`: the process (started and shaken hands with when there is none), a new game when it is one, `MultiPV` when @p lineCount is not
        /// what the process has, `position`, `go`. On an error the process is in an unknown state; the caller closes it
        [[nodiscard]] std::expected<void, EngineError> Begin(const SearchRequest& request, int lineCount, const CancellationToken& token);
        [[nodiscard]] std::expected<void, EngineError> Handshake(const CancellationToken& token);
        [[nodiscard]] std::expected<void, EngineError> WaitUntilReady(const CancellationToken& token);
        [[nodiscard]] std::expected<void, EngineError> Send(StringView command);

        /// The token was cancelled during the search: `stop`, and the `bestmove` that answers it is read and dropped. The process is closed when it does not answer
        [[nodiscard]] std::unexpected<EngineError> StopSearch();
        [[nodiscard]] bool IsNewGame(const SearchRequest& request) const;
        [[nodiscard]] std::unexpected<EngineError> CloseAndFail(EngineError error);
        void CloseProcess();

        UciEngineSettings _settings;
        std::mutex _mutex;                       ///< held for the whole of FindMove() and Analyse(): everything below is touched under it only
        eastl::unique_ptr<UciProcess> _process;  ///< null until the first search and after an error closed it
        Optional<SearchRequest> _previous;       ///< the request the running process was last given; none for a process that has had no search
        int _multiPv = kSingleLine;              ///< the `MultiPV` the running process has been told (1 for a new one, and again once it is closed)
    };

    /**
     * @brief Where a Stockfish executable is, or nothing: @p explicitPath, then the environment variable STOCKFISH, then `stockfish` on PATH
     * (`stockfish.exe` and `;` between the directories on Windows).
     *
     * The port of `find_stockfish` of tools/stockfish_bridge.py. The first of the explicit path and STOCKFISH that is set decides: it
     * must name an existing file, and when it does not the answer is nothing, with no fallback to the next place (a typo is not hidden by
     * a Stockfish somewhere else). A PATH entry counts when `stockfish` there is a file someone may execute; an empty entry is skipped.
     * Only a lookup: nothing here knows where Stockfish is installed, and nothing of it is shipped with the game.
     * With CHESS_NO_STOCKFISH (the web build) the answer is always nothing and nothing is looked at.
     */
    [[nodiscard]] Optional<eastl::string> FindStockfish(StringView explicitPath);

}  // namespace BECore::Chess
