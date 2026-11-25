#pragma once

#include <Chess/Engine/AnalysisEngineFactory.h>
#include <Chess/Engine/MoveRater.h>
#include <Chess/Engine/MoveReview.h>
#include <Chess/Engine/SearchRequest.h>
#include <TaskSystem/Task.h>

namespace BECore::Chess {

    /// The depth every position of a review is evaluated at: the depth of the ratings, so the cost of a game is known
    inline constexpr int kReviewDepth = kRatingDepth;

    /// How many lines a position of a review is asked for: the loss of a move needs the best line only
    inline constexpr int kReviewLines = 1;

    /// Why a review is refused. NotFinished: the game is not over. NoMoves: there is nothing to review
    CORE_ENUM(ReviewRefusal, uint8_t, NotFinished, NoMoves)

    /// The rule of who may start a review, for every caller: nothing when it may be started. A puzzle on is the control adapter's business (it knows the puzzle)
    [[nodiscard]] Optional<ReviewRefusal> ReviewRefusalFor(const ChessGameModel& model);

    /// Idle: no review for this game. Running: positions are being evaluated. Done: all of them are. Failed: a search failed, the review stopped (GetError())
    CORE_ENUM(ReviewState, uint8_t, Idle, Running, Done, Failed)

    /// How far a review is: @p done positions of @p total (the positions that are searched; a mated or stalemated last position is not)
    struct ReviewProgress {
        size_t done = 0;
        size_t total = 0;
    };

    /**
     * @brief One position of the game as the review evaluated it: the first line the engine gave, from the view of the side to move there.
     *
     * The position after the last move may have no legal move: it is not searched and has its TerminalCentipawns(), no best move, and a score of `Mate` 0 when its side is mated or
     * `Centipawns` 0 for a stalemate (the engine never gives a mate in 0).
     */
    struct ReviewedPosition {
        AnalysisScore score;
        int centipawns = 0;  ///< the score on the scale of ScoreToCentipawns(), what the loss of a move is made of
        Move best;           ///< null for a position with no legal move
        PieceColor sideToMove = PieceColor::White;

        [[nodiscard]] bool operator==(const ReviewedPosition&) const noexcept = default;
    };

    /// A move of the game reviewed: its loss and quality, and the best move of the position it was played in
    struct ReviewedMove {
        MoveReview review;
        Move best;

        [[nodiscard]] bool operator==(const ReviewedMove&) const noexcept = default;
    };

    /// One entry per position of the game (the moves plus one), the start first; an empty one is a position that is not evaluated yet
    using ReviewedPositions = Vector<Optional<ReviewedPosition>>;

    /// One entry per played move; an empty one is a move whose position or the one after it is not evaluated yet
    using ReviewedMoves = Vector<Optional<ReviewedMove>>;

    /**
     * @brief Evaluates every position of a finished game in the background, and from the evaluations the loss of every played move (MoveReview.h has the definition).
     *
     * **Which engine and what.** Always the own engine at the fixed depth kReviewDepth with kReviewLines line and no move time: deterministic, so the same game gives the same review twice. It
     * is made lazily by the factory (SetEngineFactory(); empty: MakeAnalysisEngine(Own, {})) asked for OpponentEngine::Own, so it is an object of its own, never the opponent's and never the
     * analyst's. The positions `0..n` of a game of `n` moves (position `p` is the start FEN with the first `p` moves) are searched oldest first, one search at a time on the Background pool
     * (TaskManager must be running); a position with no legal move (only the last can be) is not searched and has TerminalCentipawns(). The owner of the model calls Update() once per
     * frame on the main thread; it never waits. Start() begins a review, only for a finished game with moves (ReviewRefusalFor()); the person's Abandon() drops it.
     *
     * **The history rule.** A review is of one exact game: the start FEN and the moves at Start(). When the model's history differs in any way (a takeback, a reset, a position set from
     * outside) the review is gone: every read gives nothing at once, even before the next Update(), which then drops it, cancels its search and forgets that search, so an answer that comes
     * later, also from an engine that ignores its token, has nobody to reach. A finished game has no other way to change, so there is no prefix rule (unlike MoveRater).
     *
     * **States.** `Idle` (nothing held), `Running`, `Done`, `Failed`. Start() on a review that runs or is done for this game does nothing; after `Failed` it starts again from the first
     * position. Abandon() drops everything, partial results too. **Errors:** an engine that cannot be made and a search that fails (any EngineError, an empty answer included) stop the review as
     * `Failed`: logged once (LOG_ERROR), kept in GetError(), the positions that came stay readable, nothing is retried by itself.
     *
     * **Threads and shutdown** are PositionAnalyst's: main thread only; the worker sees a copy of the request and a state of its own (the engine, a CancellationToken, the answer behind a
     * mutex, an atomic `done`) shared through an IntrusivePtr, so nothing dangles whichever side ends first. The destructor, Cancel() and Shutdown() cancel the token and return at once; call
     * Shutdown() (or destroy the reviewer) before TaskManager::Shutdown(), which joins the pool and would wait for a search nobody cancelled. The destructor does not touch TaskManager.
     * Drop() forgets a search whose task may still run on the engine object; that is safe only because the reviewer always uses the own engine (OwnEngine allows concurrent calls).
     *
     * GetRevision() grows with a position that arrives, a start, an abandon or a drop, and a failure; never with a search that starts or ends, and never goes back. A display that publishes the
     * review adds it to the version of the model. One of the classes of Engine/ that log.
     */
    class GameReviewer {
    public:
        GameReviewer() = default;

        /// Cancels the search in flight and returns at once
        ~GameReviewer();

        GameReviewer(const GameReviewer&) = delete;
        GameReviewer& operator=(const GameReviewer&) = delete;
        GameReviewer(GameReviewer&&) = delete;
        GameReviewer& operator=(GameReviewer&&) = delete;

        /// How the engine is made; an empty one puts the default back (MakeAnalysisEngine(Own, {})). Read when the next engine is made
        void SetEngineFactory(AnalysisEngineFactory factory);

        /// Begins the review of the game of @p model. Refused as ReviewRefusalFor() says, and nothing changes then. Does nothing for a game whose review runs or is done; replaces the review of
        /// another game; restarts a failed one
        std::expected<void, ReviewRefusal> Start(const ChessGameModel& model);

        /// One step, for the main thread; it never waits for the engine. Drops the review when the game of @p model is not the held one, takes a finished search, and starts the search for the
        /// next position
        void Update(const ChessGameModel& model);

        /// Cancels the search in flight; the review stays and the next Update() asks again. Returns at once
        void Cancel();

        /// The person's cancel: drops the review, partial results included, and the search. Returns at once
        void Abandon();

        /// Cancels, drops the review and the engine. Call it before TaskManager::Shutdown()
        void Shutdown();

        /// The state for the game of @p model; Idle for any other game, even before the next Update()
        [[nodiscard]] ReviewState GetState(const ChessGameModel& model) const;

        /// Zero for a game that is not the held one
        [[nodiscard]] ReviewProgress GetProgress(const ChessGameModel& model) const;

        /// The positions of the game of @p model, as ReviewedPositions says; empty for a game that is not the held one
        [[nodiscard]] ReviewedPositions GetPositions(const ChessGameModel& model) const;

        /// Position @p index of the game of @p model, as GetPositions()[index], and no allocation (what a display asks once per frame): empty when @p index is out of range, the position is not
        /// evaluated yet, or the game is not the held one
        [[nodiscard]] Optional<ReviewedPosition> GetPosition(const ChessGameModel& model, size_t index) const;

        /// The moves of the game of @p model, as ReviewedMoves says; empty for a game that is not the held one
        [[nodiscard]] ReviewedMoves GetMoves(const ChessGameModel& model) const;

        /// Why the review failed; empty unless it is `Failed`
        [[nodiscard]] const Optional<EngineError>& GetError() const noexcept {
            return _error;
        }

        /// See the class comment
        [[nodiscard]] uint64_t GetRevision() const noexcept {
            return _revision;
        }

    private:
        /// What the worker and the reviewer share. The worker writes the answer and then raises `done`; the reviewer reads the answer after it sees `done`
        struct Search : RefCountedAtomic {
            explicit Search(ChessEnginePtr engineToAsk) : engine(std::move(engineToAsk)) {}

            ChessEnginePtr engine;
            CancellationToken token = CancellationToken::Create();
            Mutex mutex{BE_LOCK_SITE("GameReviewer")};  ///< guards `answer`
            std::expected<Analysis, EngineError> answer = std::unexpected(EngineError{EngineErrorKind::NoMove, "the search has not finished"});
            std::atomic<bool> done{false};
        };

        /// Runs on the pool: asks the engine and leaves the answer in @p search. Its arguments are by value, so the frame owns everything it uses
        static Task<void> RunSearch(IntrusivePtr<Search> search, SearchRequest request);

        /// The review is held and is of the game of @p model
        [[nodiscard]] bool IsHeld(const ChessGameModel& model) const;
        /// Forgets the review and its search (cancelled); the revision grows when there was one
        void Drop();
        void TakeAnswer();
        void StartSearch();
        void Fail(EngineError error);

        AnalysisEngineFactory _factory;  ///< empty: MakeAnalysisEngine over empty settings
        ChessEnginePtr _engine;

        ReviewState _state = ReviewState::Idle;
        String _startFen;  ///< the game the review belongs to: its start position and its moves
        Vector<Move> _moves;
        PieceColor _startSide = PieceColor::White;  ///< who is to move in position 0; the side alternates with every move
        ReviewedPositions _positions;               ///< one per position of the game
        size_t _done = 0;                           ///< the positions searched so far, which are the first ones: the next to search is position _done
        size_t _total = 0;                          ///< the positions to search
        IntrusivePtr<Search> _search;               ///< the search under way for position _done, null when none; main thread only

        Optional<EngineError> _error;
        uint64_t _revision = 0;
    };

}  // namespace BECore::Chess
