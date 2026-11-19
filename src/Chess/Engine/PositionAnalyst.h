#pragma once

#include <Chess/Engine/AnalysisEngineFactory.h>
#include <Chess/Engine/GameReviewer.h>
#include <Chess/Engine/MoveRater.h>
#include <Chess/Engine/OpponentController.h>
#include <Chess/Engine/OpponentEngines.h>
#include <Chess/Engine/OpponentSettings.h>
#include <Chess/Engine/SearchRequest.h>
#include <TaskSystem/Task.h>

namespace BECore::Chess {

    class PuzzleSession;

    /// Why a hint is refused. GameOver: the game has ended. ProgramsTurn: the program plays the side to move, and a hint is for the person
    CORE_ENUM(HintRefusal, uint8_t, GameOver, ProgramsTurn)

    /// The rule of who may ask for a hint, for every caller (the control API, the HUD button): refused on a finished game and on the turn of any side the program plays (@p programPlays;
    /// nobody for two people, both for two programs: refused on either turn). Nothing: the hint may be asked
    [[nodiscard]] Optional<HintRefusal> HintRefusalFor(const ChessGameModel& model, ProgramSides programPlays);

    /// The same for a program that plays one color or none
    [[nodiscard]] Optional<HintRefusal> HintRefusalFor(const ChessGameModel& model, Optional<PieceColor> programPlays);

    /// The seats HintRefusalFor() is asked with: while a puzzle is on in @p model every side the solver does not play (the program's reply is not the person's to hint, whatever the opponent is
    /// picked), else @p opponent unchanged
    [[nodiscard]] ProgramSides HintSeatsFor(const ChessGameModel& model, const PuzzleSession& puzzle, ProgramSides opponent);

    /// How many lines the analyst asks of the engine: the best one (the evaluation and the hint) and the second best, for GetLines()
    inline constexpr int kAnalysisLines = 2;

    /**
     * @brief The default analysis engine: a UciEngine over the Stockfish that FindStockfish(settings.stockfishPath) finds for Stockfish, an OwnEngine for Own.
     *
     * Never handicapped and with no `Skill Level` option. A new object each time, so a second process, never the opponent's. LaunchFailed when there is no Stockfish (the message names the
     * configured path, when one is set); nothing falls back to the other engine, nor to `$STOCKFISH` after a path that names no file (FindStockfish's rule). With CHESS_NO_STOCKFISH,
     * Stockfish is `Unsupported` (UnavailableEngineError).
     */
    [[nodiscard]] std::expected<ChessEnginePtr, EngineError> MakeAnalysisEngine(OpponentEngine engine, const OpponentSettings& settings);

    /**
     * @brief Analyses the position of a ChessGameModel in the background and keeps the answer for exactly that version of the game: the evaluation and the hint.
     *
     * One search serves both: IChessEngine::Analyse() answers a line, its move is the hint and its score is the evaluation. The evaluation is switched on with SetEvaluation() and then follows
     * the game; the hint is asked for the position now on the board with RequestHint(). The search asks for kAnalysisLines lines (MultiPV): the second is kept for GetLines() and changes
     * neither the evaluation nor the hint (exact for the own engine at its fixed depth; Stockfish on a move time may search a little shallower with two lines); a position with one legal move has
     * one line. The owner of the model calls Update() once per frame on the main thread; it never waits. When something is wanted and no answer is held, Update() copies the game into a SearchRequest
     * and hands a task that awaits IChessEngine::AnalyseAsync() to TaskManager (Background pool); a later Update() finds the answer. One search runs at a time. TaskManager must be running.
     * Nothing starts while nothing is switched on or asked, and never for a finished game.
     *
     * **The version rule** (the pattern of EngineOpponent). The model's GetVersion() at the request is remembered and the answer is kept only for it. A search whose version has moved is cancelled and
     * its answer dropped, also when the engine ignores the token; an answer dropped as stale can be followed by a new search in the same Update(). The hint dies with the version and must be asked
     * again; the evaluation follows the game. **The error rule:** an error belongs to a version too: a search that fails (any EngineError, an engine that cannot be made included) is logged once
     * (LOG_ERROR) and kept in GetError(), and no search starts for that version again (a missing Stockfish is one error, not a try per frame). It goes with the next version, a change of the picked
     * engine, or a new RequestHint() (the retry).
     *
     * **The engine** is the one the person picked (Update() is given it), made lazily at the first search and again after the pick changed; the old search, result and error go with the pick (by
     * Update() and, at once, by SetPicked(), so the reads never show the old engine's answer). It is never the opponent's object: a UciEngine serializes FindMove() and Analyse(), so the analysis
     * has a process of its own and the opponent's search never waits for it. The limits are those of the strongest level (LimitsOf(picked, GetOpponentLevels().back())), no handicap: nothing is
     * spoilt behind the person's back, and nothing falls back to the other engine.
     *
     * **The hint rule** lives in HintRefusalFor(). RequestHint() itself only refuses GameOver; the control adapter and the HUD call HintRefusalFor() first, because only they know which side the
     * program plays.
     *
     * **Threads and shutdown.** Main thread only, as EngineOpponent. The worker sees a copy of the request and a state of its own (the engine, a CancellationToken, the answer behind a mutex, an
     * atomic `done`) shared through an IntrusivePtrAtomic, so nothing dangles whichever side ends first. The destructor, Cancel() and Shutdown() cancel the token and return at once; a search that
     * ignores its token keeps the engine alive until it returns, and its answer is thrown away. Call Shutdown() (or destroy the analyst) before TaskManager::Shutdown(), which joins the pool and
     * would wait for a search nobody cancelled; a search started after that is dropped by the scheduler and never raises `done`, so Shutdown() also switches the evaluation off and drops the hint:
     * a later Update() starts nothing until a switch or a request. The destructor does not touch TaskManager.
     *
     * **Reads:** GetEvaluation(), GetHint() and GetLines() are for the version of the model passed in and give nothing for another one. A line is as the engine gave it, from the side to move's
     * view (ScoreForWhite() turns it for a display with one fixed side). GetRevision() grows with a switch, a hint asked or dropped, an answer that arrives, an error that appears or goes and the
     * rater's revision; never with a search that starts or ends (IsThinking()), and never goes back. A display that publishes the analysis adds it to the version of the model.
     *
     * **The ratings of the moves.** The analyst owns a MoveRater (MoveRater.h has the definition and the rules), so the owner of the model has one object to update and to shut down: SetRatings(),
     * Update(), Cancel() and Shutdown() forward to it. The rater has an engine of its own (the factory is asked for OpponentEngine::Own, whatever the person picked) and a history rule of its own,
     * not the version rule. IsThinking() is the analyst's only.
     *
     * **The review of a finished game.** The analyst owns a GameReviewer (GameReviewer.h has the rules), so it is shut down and stepped with everything else: StartReview() and AbandonReview()
     * are the person's, the reads forward to it. Its engine is the own engine (the factory is asked for OpponentEngine::Own) and a search of its own, so the analysis of the position on the
     * board is never delayed by a review. Shutdown() drops the review.
     *
     * Logging: with EngineOpponent, OpponentController, MoveRater, GameReviewer and DrawOfferJudge, one of the classes of Engine/ that log.
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

        /// How an engine is made; an empty one puts the default back (MakeAnalysisEngine over empty settings). Read at the next search. The rater and the reviewer get the same factory
        void SetEngineFactory(AnalysisEngineFactory factory);

        /// Makes an engine through the factory set (or the default). For the owner that hands the same factory to another object, which is move-only and so cannot be given twice
        [[nodiscard]] std::expected<ChessEnginePtr, EngineError> MakeEngine(OpponentEngine engine) const;

        /**
         * @brief Tells the analyst which engine the person has picked, at once. Update() does the same with its argument; a caller that changes the pick between two frames and then reads
         * (the control adapter) calls this to have the reads right.
         *
         * What belongs to another engine than the pick is cancelled or dropped: the search, the engine object, the result and the error. The same pick does nothing.
         */
        void SetPicked(OpponentEngine picked);

        /// One step, for the main thread; it never waits for the engine. Takes a finished search, cancels a stale or unwanted one, drops what belongs to an older version or engine, starts a
        /// search when one is wanted, then steps the rater (MoveRater::Update())
        void Update(const ChessGameModel& model, OpponentEngine picked);

        /// Switches the evaluation on or off. Off stops a search nobody else wants at the next Update(); the answer is kept for the version
        void SetEvaluation(bool on);

        /// Asks for the hint for the position now on the board (the version of @p model). GameOver (nothing changes) for a finished game; whether the side to move may be hinted is
        /// HintRefusalFor()'s business. Asking again for the same version does nothing, except that it clears a standing error (the retry)
        std::expected<void, HintRefusal> RequestHint(const ChessGameModel& model);

        /// Drops the hint request; the search stays when the evaluation wants it
        void CancelHint();

        /// Switches the rating of the moves on or off (MoveRater::SetOn())
        void SetRatings(bool on);

        /// Begins the review of the finished game of @p model (GameReviewer::Start())
        std::expected<void, ReviewRefusal> StartReview(const ChessGameModel& model) {
            return _reviewer.Start(model);
        }

        /// The person's cancel of the review: drops it, partial results included (GameReviewer::Abandon())
        void AbandonReview() {
            _reviewer.Abandon();
        }

        /// Cancels the search in flight (the rater's and the reviewer's too) and drops the answer; the switches stay, so the next Update() asks again. Returns at once
        void Cancel();

        /// Cancels, switches the evaluation and the ratings off, drops the hint, the answer, the ratings, the review and the engines. Call it before TaskManager::Shutdown()
        void Shutdown();

        [[nodiscard]] bool IsEvaluationOn() const noexcept {
            return _evaluationOn;
        }

        [[nodiscard]] bool AreRatingsOn() const noexcept {
            return _rater.IsOn();
        }

        /// The ratings of the moves of @p model, in the order of GetMoves(); an empty entry is a move that is not rated (yet) (MoveRater::GetRatings())
        [[nodiscard]] MoveRatings GetRatings(const ChessGameModel& model) const {
            return _rater.GetRatings(model);
        }

        /// The rating of move @p ply of @p model, without building the list (MoveRater::GetRating()); for a display that asks once per frame
        [[nodiscard]] Optional<MoveRating> GetRating(const ChessGameModel& model, size_t ply) const {
            return _rater.GetRating(model, ply);
        }

        /// Why the rating of a move failed (MoveRater::GetError()); apart from GetError(), which is the analysis's
        [[nodiscard]] const Optional<EngineError>& GetRatingError() const noexcept {
            return _rater.GetError();
        }

        /// The state of the review of @p model; Idle for a game that is not the reviewed one (GameReviewer::GetState())
        [[nodiscard]] ReviewState GetReviewState(const ChessGameModel& model) const {
            return _reviewer.GetState(model);
        }

        [[nodiscard]] ReviewProgress GetReviewProgress(const ChessGameModel& model) const {
            return _reviewer.GetProgress(model);
        }

        /// The evaluated positions of the review of @p model, empty for another game (GameReviewer::GetPositions())
        [[nodiscard]] ReviewedPositions GetReviewedPositions(const ChessGameModel& model) const {
            return _reviewer.GetPositions(model);
        }

        /// The reviewed moves of @p model, empty for another game (GameReviewer::GetMoves())
        [[nodiscard]] ReviewedMoves GetReviewedMoves(const ChessGameModel& model) const {
            return _reviewer.GetMoves(model);
        }

        /// Why the review failed (GameReviewer::GetError())
        [[nodiscard]] const Optional<EngineError>& GetReviewError() const noexcept {
            return _reviewer.GetError();
        }

        /// A hint was asked at the version of @p model and not dropped; it may not have come yet
        [[nodiscard]] bool IsHintRequested(const ChessGameModel& model) const noexcept {
            return _hintVersion && *_hintVersion == model.GetVersion();
        }

        /// A search is under way and wanted. False once it is cancelled, though the worker may still be returning
        [[nodiscard]] bool IsThinking() const;

        /// The line for the position of @p model (the side to move's view), when the evaluation is on and the answer for this version has come
        [[nodiscard]] Optional<AnalysisLine> GetEvaluation(const ChessGameModel& model) const;

        /// The line when a hint was asked at this version and the answer has come; its move is the hint
        [[nodiscard]] Optional<AnalysisLine> GetHint(const ChessGameModel& model) const;

        /// The lines of the answer, best first (the best one is what GetEvaluation() and GetHint() give, then the second best when there is one), the side to move's view. Empty unless the
        /// evaluation is on or a hint was asked at this version and the answer for this version has come
        [[nodiscard]] Vector<AnalysisLine> GetLines(const ChessGameModel& model) const;

        /// Why the last search failed (an engine that could not be made included); empty when none did or after it went with a new version, pick or request
        [[nodiscard]] const Optional<EngineError>& GetError() const noexcept {
            return _error;
        }

        /// See the class comment
        [[nodiscard]] uint64_t GetRevision() const noexcept {
            return _revision + _rater.GetRevision() + _reviewer.GetRevision();
        }

    private:
        /// What the worker and the analyst share. The worker writes the answer and then raises `done`; the analyst reads the answer after it sees `done`
        struct Search : RefCountedAtomic {
            explicit Search(ChessEnginePtr engineToAsk) : engine(std::move(engineToAsk)) {}

            ChessEnginePtr engine;
            CancellationToken token = CancellationToken::Create();
            Mutex mutex{BE_LOCK_SITE("PositionAnalyst")};  ///< guards `answer`
            std::expected<Analysis, EngineError> answer = std::unexpected(EngineError{EngineErrorKind::NoMove, "the search has not finished"});
            std::atomic<bool> done{false};
        };

        /// Runs on the pool: asks the engine and leaves the answer in @p search. Its arguments are by value, so the frame owns everything it uses
        static Task<void> RunSearch(IntrusivePtrAtomic<Search> search, SearchRequest request);

        /// Update() without the rater and the reviewer
        void UpdateSearch(const ChessGameModel& model, OpponentEngine picked);

        [[nodiscard]] bool IsWantedAt(uint64_t version) const noexcept;
        void TakeAnswer(const ChessGameModel& model, OpponentEngine picked);
        void CancelWhenUnwanted(const ChessGameModel& model, OpponentEngine picked);
        void DropWhatIsOld(const ChessGameModel& model, OpponentEngine picked);
        void StartSearch(const ChessGameModel& model, OpponentEngine picked);
        void Fail(uint64_t version, OpponentEngine engine, EngineError error);
        void ClearError();

        AnalysisEngineFactory _factory;  ///< empty: MakeAnalysisEngine over empty settings
        ChessEnginePtr _engine;
        OpponentEngine _engineKind = kDefaultOpponentEngine;  ///< what _engine was made for; meaningless while it is null

        bool _evaluationOn = false;
        Optional<uint64_t> _hintVersion;  ///< the version of the model a hint was asked at

        IntrusivePtrAtomic<Search> _search;  ///< the search under way, null when none; main thread only
        uint64_t _searchVersion = 0;         ///< the version of the model when _search was asked
        OpponentEngine _searchEngine = kDefaultOpponentEngine;

        Optional<AnalysisLine> _result;  ///< the answer, for _resultVersion and _resultEngine
        Optional<AnalysisLine> _second;  ///< its second line, when the engine gave one; goes with _result
        uint64_t _resultVersion = 0;
        OpponentEngine _resultEngine = kDefaultOpponentEngine;

        Optional<EngineError> _error;
        uint64_t _errorVersion = 0;
        OpponentEngine _errorEngine = kDefaultOpponentEngine;

        uint64_t _revision = 0;

        MoveRater _rater;        ///< the ratings of the moves, with its own engine and history; its revision is added in GetRevision()
        GameReviewer _reviewer;  ///< the review of a finished game, with its own engine and history; its revision is added in GetRevision()
    };

}  // namespace BECore::Chess
