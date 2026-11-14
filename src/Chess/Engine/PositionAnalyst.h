#pragma once

#include <Chess/Engine/AnalysisEngineFactory.h>
#include <Chess/Engine/MoveRater.h>
#include <Chess/Engine/OpponentController.h>
#include <Chess/Engine/OpponentEngines.h>
#include <Chess/Engine/OpponentSettings.h>
#include <Chess/Engine/SearchRequest.h>
#include <TaskSystem/Task.h>

namespace BECore::Chess {

    /// Why a hint is refused. GameOver: the game has ended, there is nothing to hint. ProgramsTurn: the program plays the side to move, and a hint is for the person
    CORE_ENUM(HintRefusal, uint8_t, GameOver, ProgramsTurn)

    /// The rule of who may ask for a hint, one pure function for every caller (the control API, the HUD button): refused on a finished game, and on the turn of any side the program
    /// plays (@p programPlays; nobody for two people, where either side may ask; both for two programs, where a hint is refused on either turn). Nothing: the hint may be asked
    [[nodiscard]] Optional<HintRefusal> HintRefusalFor(const ChessGameModel& model, ProgramSides programPlays);

    /// The same for a program that plays one color or none
    [[nodiscard]] Optional<HintRefusal> HintRefusalFor(const ChessGameModel& model, Optional<PieceColor> programPlays);

    /// How many lines the analyst asks of the engine: the best one (the evaluation and the hint) and the second best, for whoever compares them (GetLines())
    inline constexpr int kAnalysisLines = 2;

    /**
     * @brief The default analysis engine: a UciEngine over the Stockfish that FindStockfish(settings.stockfishPath) finds for Stockfish, an OwnEngine for Own.
     *
     * Never handicapped (no WithHandicap: an analysis is not spoilt on purpose) and with no `Skill Level` option: full strength, within the limits the analyst gives. LaunchFailed, naming the
     * path that was tried, when there is no Stockfish; nothing falls back to the other engine, nor to `$STOCKFISH` after a path that names no file (FindStockfish's rule). The engine is a new
     * object each time, so it is a second process, never the opponent's. Without Stockfish in the build (CHESS_NO_STOCKFISH) Stockfish is `Unsupported` (UnavailableEngineError) and the picked engine,
     * which a game starts with, is Own.
     */
    [[nodiscard]] std::expected<eastl::shared_ptr<IChessEngine>, EngineError> MakeAnalysisEngine(OpponentEngine engine, const OpponentSettings& settings);

    /**
     * @brief Analyses the position of a ChessGameModel in the background and keeps the answer for exactly that version of the game: the evaluation and the hint.
     *
     * Two things are wanted of the engine: the score of the position (the evaluation, switched on with SetEvaluation() and then kept up to date as the game goes) and the
     * best move (the hint, asked for the position now on the board with RequestHint()). One search serves both: IChessEngine::Analyse() answers a line, its move is the hint and
     * its score is the evaluation, so asking for both costs one search. The search asks for kAnalysisLines = 2 lines (MultiPV): the second line is kept next to the best one for GetLines() and
     * changes neither the evaluation nor the hint (the best line is the first one, as for one line; exact for the own engine at its fixed
     * depth, while Stockfish with MultiPV 2 in the same 2 s may search it a little shallower); a position with one legal move has one line. The owner of the model calls Update() once per frame on the
     * main thread. When something is wanted and no answer is held for the position, Update() copies the game into a SearchRequest (SearchRequest::FromModel) and hands a task that awaits
     * IChessEngine::AnalyseAsync() to TaskManager (the Background pool, where a call that blocks for seconds belongs; in the single-thread build the own engine's search yields to the frame and spans
     * many of them); a later Update() finds the answer. One search runs at a time. TaskManager must be running (CoreManager starts it). Nothing starts while nothing is switched on or asked, and never
     * for a finished game.
     *
     * **The version rule** (the pattern of EngineOpponent). The model's GetVersion() at the time of the request is remembered, and the answer is kept only for that version. A search whose
     * version has moved (a move, a reset, a position set from outside) is cancelled and its answer thrown away, also when the engine ignores the token, and a new search starts for the
     * new version in the same Update() as soon as the old worker has returned. The hint is bound to the version it was asked at: it is gone with the next change of the position and
     * must be asked again. The evaluation follows the game while it is on. An error belongs to a version too: a search that fails (any EngineError, an engine that cannot be made
     * included) is logged once (LOG_ERROR) and kept in GetError(), and no search starts for that version again, so a missing Stockfish is one error and not a try per frame. The error
     * goes with the next version, with a change of the picked engine, or with a new RequestHint() (the retry).
     *
     * **The engine** is the one the person picked (Update() is given it), made lazily at the first search by the factory and made again after the pick changed; a change of the pick
     * cancels the search, drops the result and the error and a new engine answers the next one (the drop is done by Update() and, at once, by SetPicked(), so the reads never show the old
     * engine's answer once the caller has told the analyst of the pick). It is never the opponent's object: a UciEngine's FindMove() and Analyse() share a mutex, so the
     * analysis has a process of its own and the opponent's search never waits for it. The limits are those of the strongest level (LimitsOf(picked, GetOpponentLevels().back())), no handicap:
     * the owner's rule is that nothing is spoilt or replaced behind the person's back. Nothing falls back to the other engine.
     *
     * **The hint rule** lives in HintRefusalFor(): for the person, so refused on a turn of any side the program plays (both, in a game of two programs) and on a finished game. RequestHint() itself
     * only refuses GameOver (the engine is not asked for a finished game); the control adapter and the HUD call HintRefusalFor() first, because only they know which side the program plays.
     *
     * **Threads and shutdown.** Main thread only, as EngineOpponent: the model is read in Update() and in the reads, and the worker sees a copy of the request and a state of its own (the
     * engine, a CancellationToken, the answer behind a mutex, an atomic `done`) which it shares through a shared pointer, so nothing dangles whichever side ends first. The destructor,
     * Cancel() and Shutdown() cancel the token and return at once; they never wait for the worker. A search that ignores its token keeps the engine alive until it returns, and its answer
     * is thrown away. Call Shutdown() (or destroy the analyst) before TaskManager::Shutdown(), for TaskManager::Shutdown() joins the pool and a search that nobody cancelled holds the join
     * (for Stockfish up to the move time plus the slack of the engine); an Update() after the shutdown that has something to do would start a search the scheduler drops, which
     * never raises its `done`. Shutdown() therefore also switches the evaluation off and drops the hint, so a later Update() starts nothing until a switch or a request. The destructor does
     * not touch TaskManager.
     *
     * Reading: GetEvaluation() and GetHint() are for the version of the model passed in and give nothing for another one, so a display never shows an old position's answer. The line is as
     * the engine gave it, from the side to move's view (ScoreForWhite() turns it for a display with one fixed side). GetRevision() grows with a switch, a hint asked or dropped, an answer that
     * arrives and an error that appears or goes, never with a search that starts or ends (IsThinking()), and never goes back; a display that publishes the analysis adds it to the version of
     * the model.
     *
     * **The ratings of the moves.** The analyst also owns a MoveRater (MoveRater.h has the definition and the rules), so the owner of the model has one object to update and to shut down. SetRatings()
     * switches it (off at the start, like the evaluation), Update() ends up in its Update(), Cancel() and Shutdown() forward, GetRatings() reads, and GetRevision() adds its revision. The rater
     * has an engine of its own (the factory is asked for OpponentEngine::Own, whatever the person picked) and a history of its own, not the version rule: the ratings of earlier moves survive the next
     * move, and go with a reset or a position set from outside. Its search never touches the analyst's, and IsThinking() stays the analyst's own.
     *
     * Logging: with EngineOpponent, OpponentController and MoveRater, one of the classes of Engine/ that log (LOG_ERROR once per failed search, LOG_INFO for a dropped one).
     */
    class PositionAnalyst {
    public:
        PositionAnalyst() = default;

        /// Cancels the search in flight and returns at once
        ~PositionAnalyst();

        PositionAnalyst(const PositionAnalyst&) = delete;
        PositionAnalyst& operator=(const PositionAnalyst&) = delete;
        PositionAnalyst(PositionAnalyst&&) = delete;
        PositionAnalyst& operator=(PositionAnalyst&&) = delete;

        /// How an engine is made at the first search and after a change of the pick; an empty one puts the default back (MakeAnalysisEngine over empty settings). Read at the next search.
        /// The rater gets the same factory (and asks it for OpponentEngine::Own)
        void SetEngineFactory(AnalysisEngineFactory factory);

        /**
         * @brief Tells the analyst which engine the person has picked, at once. Update() does the same with its argument, so a caller that changes the pick between two frames and then reads
         * (the control adapter: `set_state` of the engine, then `get_state`) calls this to have the reads right without waiting for the next Update().
         *
         * What belongs to another engine than the pick is cancelled or dropped: the search, the engine object, the result and the error (the revision grows when
         * something shown goes). The same pick does nothing.
         */
        void SetPicked(OpponentEngine picked);

        /**
         * @brief One step, for the main thread; it never waits for the engine.
         *
         * In this order: a finished search is taken (kept when it is still wanted: not cancelled, asked at this version and for this engine pick; else dropped); a running search that
         * is stale or no longer wanted is cancelled; a hint request, a result and an error that belong to an older version (or to another engine) are dropped; then, when the evaluation is
         * on or a hint was asked for this version, no answer is held, no search runs, no error stands and the game is not over, a search starts with the limits of the strongest level of
         * @p picked. So an answer dropped as stale can be followed by a new search in the same call. Then the rater takes its step (MoveRater::Update(), which never waits either).
         */
        void Update(const ChessGameModel& model, OpponentEngine picked);

        /// Switches the evaluation on or off. Off stops a search nobody else wants at the next Update(); the answer is kept for the version, so switching on again is at once
        void SetEvaluation(bool on);

        /// Asks for the hint for the position now on the board (the version of @p model). GameOver (nothing changes, the engine is not asked) for a finished game; whether the side to move
        /// may be hinted is HintRefusalFor()'s business. Asking again for the same version does nothing, except that it clears a standing error (the retry)
        std::expected<void, HintRefusal> RequestHint(const ChessGameModel& model);

        /// Drops the hint request (the hint, the search stays when the evaluation wants it)
        void CancelHint();

        /// Switches the rating of the moves on or off (MoveRater::SetOn()): off stops its search at the next Update(), the ratings stay
        void SetRatings(bool on);

        /// Cancels the search in flight (the rater's too) and drops the answer; the switches stay, so the next Update() asks again. Returns at once
        void Cancel();

        /// Cancels, switches the evaluation and the ratings off, drops the hint, the answer, the ratings and the engines. Later Update()s start nothing until a switch or a request. Call it before
        /// TaskManager::Shutdown()
        void Shutdown();

        [[nodiscard]] bool IsEvaluationOn() const noexcept {
            return _evaluationOn;
        }

        [[nodiscard]] bool AreRatingsOn() const noexcept {
            return _rater.IsOn();
        }

        /// The ratings of the moves of @p model, in the order of GetMoves(); an empty entry is a move that is not rated (yet). Never more entries than moves (MoveRater::GetRatings())
        [[nodiscard]] MoveRatings GetRatings(const ChessGameModel& model) const {
            return _rater.GetRatings(model);
        }

        /// Why the rating of a move failed, when it did (MoveRater::GetError()); kept apart from GetError(), which is the analysis's
        [[nodiscard]] const Optional<EngineError>& GetRatingError() const noexcept {
            return _rater.GetError();
        }

        /// A hint was asked for the position of @p model (its version) and has not been dropped; it may not have come yet
        [[nodiscard]] bool IsHintRequested(const ChessGameModel& model) const noexcept {
            return _hintVersion && *_hintVersion == model.GetVersion();
        }

        /// A search is under way and wanted: from the Update() that started it to the one that takes its answer. False once it is cancelled, though the worker may still be returning
        [[nodiscard]] bool IsThinking() const;

        /// The line for the position of @p model as the engine gave it (the view of the side to move), when the evaluation is on and the answer for this very version has come
        [[nodiscard]] Optional<AnalysisLine> GetEvaluation(const ChessGameModel& model) const;

        /// The line for the position of @p model when a hint was asked at this version and the answer for it has come; its move is the hint
        [[nodiscard]] Optional<AnalysisLine> GetHint(const ChessGameModel& model) const;

        /// The lines of the answer for the position of @p model, best first (the best one is what GetEvaluation() and GetHint() give, then the second best when the position has a second legal move),
        /// as the engine gave them (the side to move's view). Empty unless the evaluation is on or a hint was asked at this version, and the answer for this very version has come: no line is
        /// shown for another position, another engine pick or a search nobody wants, with the same drop rules as the hint
        [[nodiscard]] eastl::vector<AnalysisLine> GetLines(const ChessGameModel& model) const;

        /// Why the last search failed (any EngineError, an engine that could not be made included); empty when none did or after it went with a new version, pick or request
        [[nodiscard]] const Optional<EngineError>& GetError() const noexcept {
            return _error;
        }

        /// Grows with every change a display shows (see above); never goes back
        [[nodiscard]] uint64_t GetRevision() const noexcept {
            return _revision + _rater.GetRevision();
        }

    private:
        /// What the worker and the analyst share. The worker writes the answer and then raises `done`; the analyst reads the answer after it sees `done`
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

        /// Update() without the rater: the analysis search of the evaluation and the hint
        void UpdateSearch(const ChessGameModel& model, OpponentEngine picked);

        [[nodiscard]] bool IsWantedAt(uint64_t version) const noexcept;
        void TakeAnswer(const ChessGameModel& model, OpponentEngine picked);
        void CancelWhenUnwanted(const ChessGameModel& model, OpponentEngine picked);
        void DropWhatIsOld(const ChessGameModel& model, OpponentEngine picked);
        void StartSearch(const ChessGameModel& model, OpponentEngine picked);
        void Fail(uint64_t version, OpponentEngine engine, EngineError error);
        void ClearError();

        AnalysisEngineFactory _factory;  ///< empty: MakeAnalysisEngine over empty settings
        eastl::shared_ptr<IChessEngine> _engine;
        OpponentEngine _engineKind = kDefaultOpponentEngine;  ///< what _engine was made for; meaningless while it is null

        bool _evaluationOn = false;
        Optional<uint64_t> _hintVersion;  ///< the version of the model a hint was asked at

        eastl::shared_ptr<Search> _search;  ///< the search under way, null when none; main thread only
        uint64_t _searchVersion = 0;        ///< the version of the model when _search was asked
        OpponentEngine _searchEngine = kDefaultOpponentEngine;

        Optional<AnalysisLine> _result;  ///< the answer, for _resultVersion and _resultEngine
        Optional<AnalysisLine> _second;  ///< its second line, when the engine gave one; goes with _result
        uint64_t _resultVersion = 0;
        OpponentEngine _resultEngine = kDefaultOpponentEngine;

        Optional<EngineError> _error;
        uint64_t _errorVersion = 0;
        OpponentEngine _errorEngine = kDefaultOpponentEngine;

        uint64_t _revision = 0;

        MoveRater _rater;  ///< the ratings of the moves, with its own engine and history; its revision is added in GetRevision()
    };

}  // namespace BECore::Chess
