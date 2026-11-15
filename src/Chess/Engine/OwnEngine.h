#pragma once

#include <Chess/Engine/Search.h>
#include <Chess/Engine/SearchRequest.h>

namespace BECore::Chess {

    /**
     * @brief A search of the own engine that has been started and is driven a few nodes at a time: the state of one FindMove() or Analyse().
     *
     * Made by OwnEngine::BeginFindMove() / BeginAnalyse(). A caller that must stay responsive (a coroutine on the single-thread web build) calls Advance() with
     * kSearchSliceNodes until IsDone(), giving the thread back between the calls, and then reads Result(); FindMove() and Analyse() are the same object run to the
     * end in one call. It owns everything it needs (a copy of the position, the limits, the token, the jobs of the lines: one at a time, each with its own table,
     * freed with its line), so it may outlive the request it was made from. Moved, never copied; not thread safe: one thread drives it at a time, though not always
     * the same one. The token may be cancelled from any thread.
     */
    class OwnSearch {
    public:
        ~OwnSearch();
        OwnSearch(OwnSearch&&) noexcept;
        OwnSearch& operator=(OwnSearch&&) noexcept;
        OwnSearch(const OwnSearch&) = delete;
        OwnSearch& operator=(const OwnSearch&) = delete;

        /**
         * @brief Runs the search for about @p maxNodes more nodes (SearchJob::Advance() says how exactly) and answers whether it is over.
         *
         * `Paused`: call again. `Done`: the answer or the error is ready in Result(); a call after that does nothing. UINT64_MAX runs to the end, 0 counts as 1.
         * A cancelled token is looked at at the start of every call, so a cancel between two slices ends the search at once (error Cancelled, no further node
         * searched). A slice goes on from one line of an analysis to the next; a line that ends exactly on the budget may leave the next one for the next call.
         * **A move time is a wall-clock deadline counted from OwnEngine::BeginFindMove() / BeginAnalyse()**, so the time a caller spends elsewhere between two slices
         * counts against it (a search with a depth only is not affected).
         */
        [[nodiscard]] SearchProgress Advance(uint64_t maxNodes = kSearchSliceNodes);

        [[nodiscard]] bool IsDone() const noexcept;

        /// The answer once IsDone(): `min(SearchRequest::lines, legal moves)` lines for BeginAnalyse(), one for BeginFindMove(), or the error that FindMove() /
        /// Analyse() would give (Cancelled, IllegalMove). Not to be called before IsDone()
        [[nodiscard]] std::expected<Analysis, EngineError> Result() const;

        /// The nodes searched so far by all the lines (a retry's included); for the tests
        [[nodiscard]] uint64_t GetNodes() const noexcept;

    private:
        friend class OwnEngine;
        struct Impl;

        explicit OwnSearch(UniquePtr<Impl> impl) noexcept;

        /// Validates, finds the position and its moves, and starts the first line; @p lines is the number wanted (the legal moves may limit it). A @p varietyMargin
        /// above 0 with a seed in the request makes it the search of OwnEngine::FindMove() that chooses among near-equal moves (@p lines is then 1)
        [[nodiscard]] static std::expected<OwnSearch, EngineError> Begin(const SearchRequest& request, const CancellationToken& token, int lines, int varietyMargin);

        UniquePtr<Impl> _impl;
    };

    /// The most lines a FindMove() with a variety margin looks at (the first and the alternatives within the margin): the cost of a varied move is up to this many searches
    inline constexpr int kVarietyMaxLines = 4;

    /**
     * @brief The own engine: IChessEngine over Search(), in the process of the game, on every platform.
     *
     * The same contract as UciEngine::FindMove(), minus the process. The request is checked first (BadRequest for wrong limits, a broken start FEN or a move that
     * is not legal), then NoMove when the side to move has none, then Cancelled when the token is already cancelled; nothing starts in those cases.
     * SearchLimits::depth is the last iteration of the search (more than kMaxSearchDepth is brought down to it), SearchLimits::moveTimeMs a deadline for the whole
     * call; both given, whichever comes first; only the time, and the iterations go on until it runs out. A deadline that runs out is not an error: the best move
     * of the last iteration that finished is the answer (the first iteration always finishes). Cancelled when the token is cancelled during the search, also if
     * the search had a move by then: the answer is dropped, as UciEngine drops it. IllegalMove would mean a bug in Search() (the move is checked against
     * MoveGenerator before it leaves), not a state of the world.
     *
     * **Deterministic for a depth**: a request with a depth and no time gives the same move on every run and platform, for the same SearchRequest::seed. A request
     * with a time is not: where the clock stops the search decides the depth reached, hence the move.
     *
     * **Variety.** With a margin (the constructor) above 0 and a request with a seed other than 0, FindMove() does not always play the first line: it looks at up to
     * kVarietyMaxLines lines the way Analyse() does, takes line 1 and every later line whose centipawn score is at most the margin below line 1's (it stops at the
     * first line outside, and at once at a mate, so a forced win or loss is never varied), and plays the candidate that RollKey() picks. The same seed, position and
     * depth give the same move. Margin 0 or seed 0 is the plain search, move, score and node count unchanged; Analyse() is never varied. A move time is a deadline of the
     * whole call for every line: a later line the clock stops is no candidate, line 1 is never shortened by the others.
     *
     * **One implementation, three ways to ask.** FindMove() and Analyse() block until the answer; BeginFindMove() / BeginAnalyse() hand back an OwnSearch to drive
     * with Advance(); FindMoveAsync() and AnalyseAsync() are coroutines that drive it between frames. The blocking calls are "begin, run to the end", so they give
     * the answer of a driven search for any size of the slices.
     *
     * No state between calls and no process (the state of a call is the OwnSearch), so FindMove() may be called from several threads at once, each with its own
     * request and token, and several OwnSearch of one engine may be driven side by side. It logs nothing. The search runs on the thread that calls; the plies of a
     * line are frames on the heap (see Search.h), so a worker needs no deep stack.
     */
    class OwnEngine final : public IChessEngine {
    public:
        /// @p varietyMargin is the centipawns below the best move that FindMove() still counts as equal (0 = none, never varied; below 0 is brought to 0)
        explicit OwnEngine(int varietyMargin = 0);

        [[nodiscard]] std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken& token) override;

        /// SearchRequest::lines lines (at most as many as the position has legal moves), best first. Line 1 is the best move of the last iteration that finished, with
        /// its score from the side to move (ScoreFromSearch()) and the depth of that iteration: what a request for one line gives. Line k (k = 2..) is a search of its
        /// own, at the same depth, with the moves of the lines before it excluded at the root (SearchSettings::excludedRootMoves) and its own table, so line 1 and the
        /// determinism for a depth are untouched. The scores of the lines are not forced to fall (separate searches). A move time is shared out: line k of N must end by
        /// k / N of it. Cancelled when the token is cancelled during any of the searches (the whole answer is dropped). Everything else is the contract of FindMove().
        [[nodiscard]] std::expected<Analysis, EngineError> Analyse(const SearchRequest& request, const CancellationToken& token) override;

        /// The search of FindMove(), started: one line (SearchRequest::lines is validated but otherwise ignored); Result() is a one-line Analysis
        [[nodiscard]] std::expected<OwnSearch, EngineError> BeginFindMove(const SearchRequest& request, const CancellationToken& token) const;

        /// The search of Analyse(), started
        [[nodiscard]] std::expected<OwnSearch, EngineError> BeginAnalyse(const SearchRequest& request, const CancellationToken& token) const;

        /// FindMove() as a coroutine: `co_await YieldIfNeeded()`, then Advance(kSearchSliceNodes), until done; the yield goes BEFORE each slice, so a task started
        /// between two drains of the frame budget gives the frame back at once. On a threaded build YieldIfNeeded() never suspends. The OwnSearch lives in the coroutine
        /// frame and no member of the engine is read once it runs, but the caller keeps the engine alive until the task ends, as IChessEngine::FindMoveAsync() says.
        /// Cancelled is seen at the start of every slice.
        [[nodiscard]] Task<std::expected<Move, EngineError>> FindMoveAsync(SearchRequest request, CancellationToken token) override;

        /// Analyse() as a coroutine, the same loop
        [[nodiscard]] Task<std::expected<Analysis, EngineError>> AnalyseAsync(SearchRequest request, CancellationToken token) override;

    private:
        /// The coroutine of FindMoveAsync(): the margin comes by value, so the frame reads no member of an engine that may be gone
        [[nodiscard]] static Task<std::expected<Move, EngineError>> FindMoveTask(SearchRequest request, CancellationToken token, int varietyMargin);

        int _varietyMargin;
    };

}  // namespace BECore::Chess
