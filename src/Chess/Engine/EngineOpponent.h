#pragma once

#include <Chess/Engine/SearchRequest.h>
#include <TaskSystem/Task.h>

namespace BECore::Chess {

    /**
     * @brief Plays one side of a ChessGameModel against an IChessEngine, without making the caller wait.
     *
     * The owner of the model calls Update() once per frame on the main thread. When it is the opponent's turn, Update() copies the game into a
     * SearchRequest and hands a task that awaits IChessEngine::FindMoveAsync() to TaskManager (the Background pool, which is where a call that blocks
     * for seconds belongs); a later Update() finds the answer and plays it with ChessGameModel::TryMove(). One search runs at a time. TaskManager
     * must be running (CoreManager starts it).
     *
     * Shutdown: destroy the opponent (or Cancel() it) before TaskManager::Shutdown(), and do not call Update() after it. Shutdown() joins the pool, so
     * a running search whose token nobody cancelled holds the join until the engine's own deadline (for UciEngine movetime + answerSlackMs); a search
     * still queued is run before the join too. An Update() after the shutdown would start a search that the scheduler drops: its `done` never rises and
     * IsThinking() stays true. The destructor does not touch TaskManager, so destroying the opponent after the shutdown is safe.
     *
     * Threads: the model is read and written in Update() only, on the thread that calls it; the worker sees a copy of the request and a state
     * of its own (the engine, a CancellationToken, the answer), which it shares with the opponent through a shared pointer, so nothing dangles when
     * either side ends first. The destructor and Cancel() cancel the token and return at once: they never wait for the worker. A search that
     * ignores its token (or, in the single-thread build, one parked between frames) may outlive the opponent; it keeps the engine alive until it returns, and the answer is thrown away.
     *
     * A search is answered only if it is still wanted: when the model's GetVersion() has moved since the request (a reset, a position set from
     * outside, a move made by someone else) the search is cancelled and the answer dropped. An opponent serves one model: the version means nothing across two.
     *
     * A search that fails, or an answer the model refuses, is logged once and kept in GetLastError(). No new search starts until ClearError(), so a
     * missing engine is one error and not a try per frame.
     */
    class EngineOpponent {
    public:
        EngineOpponent(ChessEnginePtr engine, PieceColor side, SearchLimits limits);

        /// Cancels the search in flight and returns at once
        ~EngineOpponent();

        EngineOpponent(const EngineOpponent&) = delete;
        EngineOpponent& operator=(const EngineOpponent&) = delete;
        EngineOpponent(EngineOpponent&&) = delete;
        EngineOpponent& operator=(EngineOpponent&&) = delete;

        /**
         * @brief One step, for the main thread; it never waits for the engine.
         *
         * A finished search is dealt with first: its move is played when the model is still at the version it was asked about, and dropped when it
         * is not or when the search was cancelled. A running search whose version is stale is cancelled. Then, when nothing is running, no error
         * is waiting, the game is not over and it is the opponent's turn, a new search starts (so an answer dropped as stale can be followed by a new
         * request in the same call).
         */
        void Update(ChessGameModel& model);

        /// A search is under way and wanted: from the Update() that started it to the one that takes its answer. False once it is cancelled, though the worker may still be returning
        [[nodiscard]] bool IsThinking() const;

        /// Cancels the search in flight and drops its answer; the next Update() asks again if it is still the opponent's turn. Returns at once
        void Cancel();

        /// Why the last search failed (any EngineError, or a move the model refused); empty when none did, or after ClearError(). A Cancelled of a search the opponent
        /// itself cancelled is dropped, not an error; an engine that answers Cancelled without the opponent having cancelled is recorded as one (and no retry loop follows)
        [[nodiscard]] const Optional<EngineError>& GetLastError() const noexcept {
            return _lastError;
        }

        /// Forgets the error, so that the next Update() may start a search again
        void ClearError() noexcept {
            _lastError.reset();
        }

        /// The side it plays, fixed by the constructor: to play the other one, destroy the opponent and make another
        [[nodiscard]] PieceColor GetSide() const noexcept {
            return _side;
        }

    private:
        /// What the worker and the opponent share. The worker writes the answer and then raises `done`; the opponent reads the answer after it sees `done`
        struct Search : RefCountedAtomic {
            explicit Search(ChessEnginePtr engineToAsk) : engine(std::move(engineToAsk)) {}

            ChessEnginePtr engine;
            CancellationToken token = CancellationToken::Create();
            Mutex mutex{BE_LOCK_SITE("EngineOpponent")};  ///< guards `answer`
            std::expected<Move, EngineError> answer = std::unexpected(EngineError{EngineErrorKind::NoMove, "the search has not finished"});
            std::atomic<bool> done{false};
        };

        /// Runs on the pool: asks the engine and leaves the answer in @p search. Takes its arguments by value, so the frame owns everything it uses
        static Task<void> RunSearch(IntrusivePtrAtomic<Search> search, SearchRequest request);

        void StartSearch(const ChessGameModel& model);
        void CancelWhenStale(const ChessGameModel& model);
        void TakeAnswer(ChessGameModel& model);
        void Fail(EngineError error);

        ChessEnginePtr _engine;
        PieceColor _side;
        SearchLimits _limits;
        IntrusivePtrAtomic<Search> _search;  ///< the search under way, null when none; main thread only
        uint64_t _searchVersion = 0;         ///< the version of the model when _search was asked
        Optional<EngineError> _lastError;
    };

}  // namespace BECore::Chess
