#pragma once

#include <Chess/Engine/AnalysisEngineFactory.h>
#include <Chess/Engine/MoveRating.h>
#include <Chess/Engine/SearchRequest.h>
#include <TaskSystem/Task.h>

namespace BECore::Chess {

    /// The depth every move is rated at, the depth of the strongest level of the own engine: a fixed depth is deterministic, so the same game gets the same ratings twice
    inline constexpr int kRatingDepth = 5;

    /// How many lines the rater asks of the engine for the position before a move: the best one and the second best, which RateMove() compares
    inline constexpr int kRatingLines = 2;

    /// One entry per played move, in the order of ChessGameModel::GetMoves(); an empty one is a move that is not rated (yet)
    using MoveRatings = eastl::vector<std::optional<MoveRating>>;

    /**
     * @brief Rates the moves of a ChessGameModel in the background: for every played move, an analysis of the position BEFORE it, and RateMove() over that analysis (MoveRating.h has the definition).
     *
     * **Which engine and when.** Always the own engine at the fixed depth kRatingDepth, with kRatingLines lines and no move time (deterministic: the same game gives the same ratings twice, and there
     * is no process and no Stockfish to depend on). It is made lazily by the factory (SetEngineFactory(); empty: MakeAnalysisEngine(Own, {})) asked for OpponentEngine::Own, so it is an object of its
     * own, never the opponent's and never the analyst's: its searches never delay the opponent's move. It is **opt-in** (SetOn(), off at the start: it costs CPU) and nothing starts while it is off,
     * nor when every move is rated. The owner of the model calls Update() once per frame on the main thread; one search runs at a time, on the Background pool (TaskManager must be running), and the
     * oldest move that is not rated goes first. The request of ply `i` is the start FEN of the game with the moves `[0, i)`, so the answer is about the position the move `i` was played in.
     *
     * **The history rule** (the pattern of EngineOpponent, with the history of the game in place of the version). The rater keeps the start FEN and the moves it has seen. A game that only grew keeps
     * its ratings (they are not a hint: a rating does not go with the next move). A game that is not an extension of what the rater holds (another start FEN, a shorter history, a move that differs: a
     * reset, a position set from outside) cancels the search, throws its answer away, also when the engine ignores the token, drops every rating and the error, and rating starts again from the new
     * history. The reads follow the same rule: GetRatings() gives nothing for a model whose game is not an extension of the held one, even before the next Update().
     *
     * **Errors.** An engine that cannot be made blocks the rater (the error is kept in GetError(), logged once with LOG_ERROR, and nothing is tried again). A search that fails (any EngineError) fails
     * that one move: logged once, kept in GetError(), and that move is not asked again every frame; **the later moves are still tried** and the failed one stays empty. Both go with a new history,
     * with a switch off and on, and with Shutdown().
     *
     * **Threads and shutdown.** Main thread only, as PositionAnalyst: the worker sees a copy of the request and a state of its own (the engine, a CancellationToken, the answer behind a mutex, an
     * atomic `done`) that it shares through a shared pointer, so nothing dangles whichever side ends first. The destructor, Cancel() and Shutdown() cancel the token and return at once; a search that
     * ignores its token keeps the engine alive until it returns and its answer is thrown away. Call Shutdown() (or destroy the rater) before TaskManager::Shutdown(); Shutdown() also switches the
     * rating off and drops the ratings, so a later Update() starts nothing until SetOn(true). The destructor does not touch TaskManager.
     *
     * GetRevision() grows with a rating that arrives or is dropped, a switch, an error that appears or goes, never with a search that starts or ends (IsThinking()), and never goes back; a display
     * that publishes the ratings adds it to the version of the model. Logging: LOG_ERROR once per failed move, LOG_INFO for a dropped answer.
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

        /// How the engine is made at the first search; an empty one puts the default back (MakeAnalysisEngine(Own, {})). The factory is asked for OpponentEngine::Own. Read at the next engine made
        void SetEngineFactory(AnalysisEngineFactory factory);

        /// Switches the rating on or off. Off cancels a running search at the next Update(); the ratings stay. Either way a standing error goes (switching off and on is the retry)
        void SetOn(bool on);

        /// One step, for the main thread; it never waits for the engine. In this order: the history of @p model is compared with the one held (see above); a finished search is taken (kept when it
        /// is still wanted, else dropped); a running search is cancelled when the rater went off; then, when it is on, no search runs and a move is not rated and has not failed, a search for the
        /// oldest such move starts
        void Update(const ChessGameModel& model);

        /// Cancels the search in flight; the ratings and the switch stay, so the next Update() asks again. Returns at once
        void Cancel();

        /// Cancels, switches the rating off, drops the ratings, the error and the engine. Later Update()s start nothing until SetOn(true). Call it before TaskManager::Shutdown()
        void Shutdown();

        [[nodiscard]] bool IsOn() const noexcept {
            return _on;
        }

        /// A search is under way and wanted: from the Update() that started it to the one that takes its answer. False once it is cancelled, though the worker may still be returning
        [[nodiscard]] bool IsThinking() const;

        /// The ratings of the moves of @p model, in the order of GetMoves() and never more than there are moves; an empty entry is a move that is not rated yet, and so is every move of a game that is
        /// not an extension of the one held
        [[nodiscard]] MoveRatings GetRatings(const ChessGameModel& model) const;

        /// Why the last search failed (any EngineError, an engine that could not be made included); empty when none did or after it went with a new history, a switch or Shutdown()
        [[nodiscard]] const std::optional<EngineError>& GetError() const noexcept {
            return _error;
        }

        /// Grows with every change a display shows (see above); never goes back
        [[nodiscard]] uint64_t GetRevision() const noexcept {
            return _revision;
        }

    private:
        /// What the worker and the rater share. The worker writes the answer and then raises `done`; the rater reads the answer after it sees `done`
        struct Search {
            explicit Search(eastl::shared_ptr<IChessEngine> engineToAsk) : engine(std::move(engineToAsk)) {}

            eastl::shared_ptr<IChessEngine> engine;
            CancellationToken token = CancellationToken::Create();
            std::mutex mutex;  ///< guards `answer`
            std::expected<Analysis, EngineError> answer = std::unexpected(EngineError{EngineErrorKind::NoMove, "the search has not finished"});
            std::atomic<bool> done{false};
        };

        /// Runs on the pool: asks the engine and leaves the answer in @p search. Takes its arguments by value, so the frame owns everything it uses
        static Task<void> RunSearch(eastl::shared_ptr<Search> search, SearchRequest request);

        /// The game of @p model is the held one, perhaps grown: the same start FEN and the held moves as its first moves
        [[nodiscard]] bool Extends(const ChessGameModel& model) const;
        void SyncHistory(const ChessGameModel& model);
        void DropHistory();
        void TakeAnswer();
        void StartSearch(size_t ply);
        [[nodiscard]] std::optional<size_t> OldestUnrated() const;
        void Fail(std::optional<size_t> ply, EngineError error);
        void ClearError();

        AnalysisEngineFactory _factory;  ///< empty: MakeAnalysisEngine over empty settings
        eastl::shared_ptr<IChessEngine> _engine;
        bool _on = false;

        eastl::string _startFen;  ///< the game the ratings belong to: its start position and the moves seen so far
        eastl::vector<Move> _moves;
        MoveRatings _ratings;         ///< aligned with _moves
        eastl::vector<bool> _failed;  ///< aligned with _moves: the search for this move failed and is not retried
        uint64_t _generation = 0;     ///< grows when the held history is dropped; a search of an older one is thrown away

        eastl::shared_ptr<Search> _search;  ///< the search under way, null when none; main thread only
        size_t _searchPly = 0;              ///< the move _search rates
        uint64_t _searchGeneration = 0;

        std::optional<EngineError> _error;
        bool _blocked = false;  ///< the engine could not be made: nothing is tried until the error goes

        uint64_t _revision = 0;
    };

}  // namespace BECore::Chess
