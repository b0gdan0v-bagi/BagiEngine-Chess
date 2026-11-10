#pragma once

#include <Chess/Engine/Search.h>
#include <Chess/Engine/SearchRequest.h>

namespace BECore::Chess {

    /**
     * @brief A search of the engine of our own that has been started and is driven a few nodes at a time: the state of one FindMove() or Analyse().
     *
     * Made by OwnEngine::BeginFindMove() / BeginAnalyse(). A caller that must stay responsive (a coroutine on the single-thread web build) calls Advance() with
     * kSearchSliceNodes until IsDone(), giving the thread back between the calls, and then reads Result(); FindMove() and Analyse() are the same object run to the
     * end in one call, so the sliced and the one-call path cannot give different answers. The object owns everything it needs (a copy of the position, the request's
     * limits, the token, the jobs of the lines with their transposition tables: 4 MB each for a depth of 3 or more, one at a time, freed with the line and with the
     * object), so it may outlive the request it was made from. It is moved, never copied; not thread safe: one thread drives it at a time, though not always the same one.
     * The token may be cancelled from any thread.
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
         * A cancelled token is looked at at the start of every call, so a cancel between two slices ends the search at once, with the error Cancelled and no node
         * searched. A slice goes on from one line of an analysis to the next, and a line that ends the slice's budget exactly may leave the next one for the next call.
         * **A move time is a wall-clock deadline counted from OwnEngine::BeginFindMove() / BeginAnalyse()**, so the time a caller spends elsewhere between two slices
         * counts against it (a search with a depth only is not affected).
         */
        [[nodiscard]] SearchProgress Advance(uint64_t maxNodes = kSearchSliceNodes);

        [[nodiscard]] bool IsDone() const noexcept;

        /// The answer once IsDone(): `min(SearchRequest::lines, legal moves)` lines for BeginAnalyse(), one for BeginFindMove(), as OwnEngine::Analyse() gives them;
        /// or the error that FindMove() / Analyse() would give (Cancelled, IllegalMove). Not meant to be called before IsDone()
        [[nodiscard]] std::expected<Analysis, EngineError> Result() const;

        /// The nodes searched so far by all the lines (the retry's included); for the tests
        [[nodiscard]] uint64_t GetNodes() const noexcept;

    private:
        friend class OwnEngine;
        struct Impl;

        explicit OwnSearch(eastl::unique_ptr<Impl> impl) noexcept;

        /// Validates, finds the position and its moves, and starts the search of the first line; @p lines is the number of lines wanted (before the legal moves limit it)
        [[nodiscard]] static std::expected<OwnSearch, EngineError> Begin(const SearchRequest& request, const CancellationToken& token, int lines);

        eastl::unique_ptr<Impl> _impl;
    };

    /**
     * @brief The engine of our own: IChessEngine over Search(), in the process of the game, on every platform.
     *
     * The same contract as UciEngine::FindMove(), minus the process. The request is checked first (BadRequest for wrong limits, a
     * broken start FEN or a move that is not legal), then NoMove when the side to move has none (the search does not start), then
     * Cancelled when the token is already cancelled. SearchLimits::depth is the last iteration of the search (more than
     * kMaxSearchDepth is brought down to it), SearchLimits::moveTimeMs a deadline for the whole call; both given, whichever comes
     * first; only the time, and the iterations go on until it runs out. A deadline that runs out is not an error: the best move of
     * the last iteration that finished is the answer (the first iteration always finishes). Cancelled when the token is cancelled
     * during the search, also if the search had a move by then: the answer is dropped, as UciEngine drops it. Any other failure of
     * the kinds of UciEngine does not exist here: the returned move is checked against MoveGenerator before it leaves, and
     * IllegalMove would mean a bug in Search(), not a state of the world.
     *
     * **Deterministic for a depth**: a request with a depth and no time gives the same move on every run and platform. A request
     * with a time is not: where the clock stops the search decides the depth reached, hence the move.
     *
     * **Two ways to ask, one implementation.** FindMove() and Analyse() block until the answer. BeginFindMove() and BeginAnalyse() start the same search and hand back
     * an OwnSearch that the caller drives with Advance(), a slice at a time; FindMove() and Analyse() are "begin, run to the end", so they give the answer of a driven
     * search for any size of the slices (the move, the score, the depth and the nodes), with the same errors. The errors of the request (BadRequest, NoMove, a token
     * already cancelled) come back from Begin...() and nothing is started.
     *
     * The object has no state between calls and no process (the state of a call is the OwnSearch), so FindMove() may be called from several threads at once, each with its
     * own request and token, and several OwnSearch of one engine may be driven side by side. It logs nothing (only EngineOpponent, OpponentController, PositionAnalyst and MoveRater log).
     * The search runs on the thread that calls: the plies of a line are frames on the heap (see Search.h), so a worker needs no deep stack.
     */
    class OwnEngine final : public IChessEngine {
    public:
        [[nodiscard]] std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken& token) override;

        /// SearchRequest::lines lines (at most as many as the position has legal moves), best first. Line 1 is the best move of the last iteration that finished, with its
        /// score from the side to move (ScoreFromSearch()) and the depth of that iteration: what a request for one line gives (exactly so for a fixed depth; a move time is not repeatable). Line k (k
        /// = 2..) is a search of its own, at the same depth, with the moves of the lines before it excluded at the root (SearchSettings::excludedRootMoves), so each line has its own table, killers
        /// and history and line 1 and the determinism for a depth are untouched. The scores of the lines are not forced to fall (separate searches). A move time is shared out: line k of N must end by
        /// k / N of it. Cancelled when the token is cancelled during any of the searches (the whole answer is dropped). The contract of FindMove() in everything else (the same private code validates
        /// and searches under both, so they give the same move for the same request, and the errors are the same), also deterministic for a depth, a deadline is not an error and the object may be
        /// asked from several threads at once.
        [[nodiscard]] std::expected<Analysis, EngineError> Analyse(const SearchRequest& request, const CancellationToken& token) override;

        /// The search of FindMove(), started: one line (SearchRequest::lines is validated but otherwise ignored), the answer in OwnSearch::Result() as a one-line Analysis
        [[nodiscard]] std::expected<OwnSearch, EngineError> BeginFindMove(const SearchRequest& request, const CancellationToken& token) const;

        /// The search of Analyse(), started
        [[nodiscard]] std::expected<OwnSearch, EngineError> BeginAnalyse(const SearchRequest& request, const CancellationToken& token) const;

        /// FindMove() as a coroutine: Begin, then `co_await YieldIfNeeded()` and Advance(kSearchSliceNodes) until done (the yield goes BEFORE each slice, so a task started
        /// between two drains of the frame budget gives the frame back at once), then the same answer as FindMove() by the same code. On the single-thread build the search so
        /// spans many frames; on a threaded build YieldIfNeeded() never suspends and the call is FindMove() with one pointer test per slice. The OwnSearch lives in the
        /// coroutine frame and nothing reads the engine once the task runs, so a destroyed engine does not matter. Cancelled is seen at the start of every slice.
        [[nodiscard]] Task<std::expected<Move, EngineError>> FindMoveAsync(SearchRequest request, CancellationToken token) override;

        /// Analyse() as a coroutine, the loop of FindMoveAsync()
        [[nodiscard]] Task<std::expected<Analysis, EngineError>> AnalyseAsync(SearchRequest request, CancellationToken token) override;
    };

}  // namespace BECore::Chess
