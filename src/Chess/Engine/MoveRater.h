#pragma once

#include <Chess/Engine/AnalysisEngineFactory.h>
#include <Chess/Engine/MoveRating.h>
#include <Chess/Engine/SearchRequest.h>
#include <TaskSystem/Task.h>

namespace BECore::Chess {

    /// The depth every move is rated at, the depth of the strongest level of the own engine
    inline constexpr int kRatingDepth = 5;

    /// How many lines the rater asks of the engine for the position before a move: the best one and the second best, which RateMove() compares
    inline constexpr int kRatingLines = 2;

    /// One entry per played move, in the order of ChessGameModel::GetMoves(); an empty one is a move that is not rated (yet)
    using MoveRatings = Vector<Optional<MoveRating>>;

    /**
     * @brief Rates the moves of a ChessGameModel in the background: for every played move, an analysis of the position BEFORE it, and RateMove() over that analysis (MoveRating.h has the definition).
     *
     * **Which engine and when.** Always the own engine at the fixed depth kRatingDepth, with kRatingLines lines and no move time: deterministic, so the same game gives the same ratings twice,
     * with no process to depend on. It is made lazily by the factory (SetEngineFactory(); empty: MakeAnalysisEngine(Own, {})) asked for OpponentEngine::Own, so it is an object of its own, never
     * the opponent's and never the analyst's. It is **opt-in** (SetOn(), off at the start: it costs CPU); nothing starts while it is off or when every move is rated. The owner of the model calls
     * Update() once per frame on the main thread; one search runs at a time on the Background pool (TaskManager must be running), the oldest move that is not rated first. The request of ply `i`
     * is the start FEN of the game with the moves `[0, i)`, so the answer is about the position the move `i` was played in.
     *
     * **The history rule** (the pattern of EngineOpponent, with the history of the game in place of the version). The rater keeps the start FEN and the moves it has seen, and a game is read
     * against them as the longest common prefix L: the same start FEN and the same first L moves. A game that only grew keeps its ratings (unlike a hint, a rating does not go with the next
     * move); a takeback keeps the ratings of the moves that remain, and a different move after it is rated from ply L on. What is past L (ratings, failures) is dropped, and a search for a ply
     * `>= L` is cancelled and its answer thrown away, also when the engine ignores the token; a search for a ply below L is about a request that did not change and stays. Another start FEN (a
     * reset, a position set from outside) is L = 0. GetRating() and GetRatings() give nothing at or past L even before the next Update(), so the display of a game that has diverged never shows
     * the held game's rating.
     *
     * **Errors.** An engine that cannot be made blocks the rater: the error is kept in GetError(), logged once (LOG_ERROR), and nothing is tried again. A search that fails (any EngineError)
     * fails that one move: logged once, kept in GetError(), the move stays empty and is not asked again every frame; **the later moves are still tried**. Both go with a new history, a switch off
     * and on, and Shutdown().
     *
     * **Threads and shutdown** are PositionAnalyst's (main thread only, the worker's shared state, the destructor, Cancel() and Shutdown() return at once, Shutdown() before
     * TaskManager::Shutdown()); Shutdown() also switches the rating off and drops the ratings.
     *
     * GetRevision() grows with a rating that arrives or is dropped, a switch, an error that appears or goes; never with a search that starts or ends (IsThinking()), and never goes back. A display
     * that publishes the ratings adds it to the version of the model.
     */
    class MoveRater {
    public:
        MoveRater() = default;

        /// Cancels the search in flight and returns at once
        ~MoveRater();

        MoveRater(const MoveRater&) = delete;
        MoveRater& operator=(const MoveRater&) = delete;
        MoveRater(MoveRater&&) = delete;
        MoveRater& operator=(MoveRater&&) = delete;

        /// How the engine is made; an empty one puts the default back (MakeAnalysisEngine(Own, {})). Read when the next engine is made
        void SetEngineFactory(AnalysisEngineFactory factory);

        /// Switches the rating on or off. Off cancels a running search at the next Update(); the ratings stay. Either way a standing error goes (switching off and on is the retry)
        void SetOn(bool on);

        /// One step, for the main thread; it never waits for the engine. Compares the history of @p model with the held one, takes a finished search, and starts the search for the oldest move
        /// that is not rated and has not failed
        void Update(const ChessGameModel& model);

        /// Cancels the search in flight; the ratings and the switch stay, so the next Update() asks again. Returns at once
        void Cancel();

        /// Cancels, switches the rating off, drops the ratings, the error and the engine. Call it before TaskManager::Shutdown()
        void Shutdown();

        [[nodiscard]] bool IsOn() const noexcept {
            return _on;
        }

        /// A search is under way and wanted. False once it is cancelled, though the worker may still be returning
        [[nodiscard]] bool IsThinking() const;

        /// The ratings of the moves of @p model, in the order of GetMoves() and never more than there are moves; an empty entry is a move that is not rated yet, and so is every move at or past
        /// the common prefix of the game and the held one
        [[nodiscard]] MoveRatings GetRatings(const ChessGameModel& model) const;

        /// The rating of move @p ply of @p model, as GetRatings()[ply], and no allocation: empty when @p ply is out of range, the move is not rated, or the game does not hold that move as the
        /// rater has it
        [[nodiscard]] Optional<MoveRating> GetRating(const ChessGameModel& model, size_t ply) const;

        /// Why the last search failed (an engine that could not be made included); empty when none did or after it went with a new history, a switch or Shutdown()
        [[nodiscard]] const Optional<EngineError>& GetError() const noexcept {
            return _error;
        }

        /// See the class comment
        [[nodiscard]] uint64_t GetRevision() const noexcept {
            return _revision;
        }

    private:
        /// What the worker and the rater share. The worker writes the answer and then raises `done`; the rater reads the answer after it sees `done`
        struct Search : RefCountedAtomic {
            explicit Search(ChessEnginePtr engineToAsk) : engine(std::move(engineToAsk)) {}

            ChessEnginePtr engine;
            CancellationToken token = CancellationToken::Create();
            Mutex mutex{BE_LOCK_SITE("MoveRater")};  ///< guards `answer`
            std::expected<Analysis, EngineError> answer = std::unexpected(EngineError{EngineErrorKind::NoMove, "the search has not finished"});
            std::atomic<bool> done{false};
        };

        /// Runs on the pool: asks the engine and leaves the answer in @p search. Its arguments are by value, so the frame owns everything it uses
        static Task<void> RunSearch(IntrusivePtr<Search> search, SearchRequest request);

        /// How many first moves @p model has in common with the held game; 0 for another start FEN
        [[nodiscard]] size_t CommonPrefix(const ChessGameModel& model) const;
        void SyncHistory(const ChessGameModel& model);
        /// Drops the held moves, ratings and failures from ply @p keep on, the search for such a ply and the error
        void DropHistoryFrom(size_t keep);
        void TakeAnswer();
        void StartSearch(size_t ply);
        [[nodiscard]] Optional<size_t> OldestUnrated() const;
        void Fail(Optional<size_t> ply, EngineError error);
        void ClearError();

        AnalysisEngineFactory _factory;  ///< empty: MakeAnalysisEngine over empty settings
        ChessEnginePtr _engine;
        bool _on = false;

        String _startFen;  ///< the game the ratings belong to: its start position and the moves seen so far
        Vector<Move> _moves;
        MoveRatings _ratings;      ///< aligned with _moves
        Vector<bool> _failed;      ///< aligned with _moves: the search for this move failed and is not retried
        uint64_t _generation = 0;  ///< grows when the search in flight is for a dropped ply; its answer is thrown away

        IntrusivePtr<Search> _search;  ///< the search under way, null when none; main thread only
        size_t _searchPly = 0;         ///< the move _search rates
        uint64_t _searchGeneration = 0;

        Optional<EngineError> _error;
        bool _blocked = false;  ///< the engine could not be made: nothing is tried until the error goes

        uint64_t _revision = 0;
    };

}  // namespace BECore::Chess
