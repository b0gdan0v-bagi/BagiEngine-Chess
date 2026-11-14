#include "OwnEngine.h"

#include <Chess/Engine/Analysis.h>
#include <Chess/Game/MoveGenerator.h>
#include <TaskSystem/Awaitables.h>

namespace BECore::Chess {

    namespace {

        std::unexpected<EngineError> Fail(EngineErrorKind kind, String detail) {
            return std::unexpected(EngineError{kind, std::move(detail)});
        }

        /// The last iteration to search for @p limits: its depth, or the deepest there is when only the time limits the search
        int MaxDepthOf(const SearchLimits& limits) {
            return limits.depth > 0 ? eastl::min(limits.depth, kMaxSearchDepth) : kMaxSearchDepth;
        }

        /// The time after which line @p line of @p lineCount (counted from 1) must be done: the move time is shared out, line k ends at k / lineCount of it. No move time: no deadline
        Optional<std::chrono::steady_clock::time_point> DeadlineOfLine(std::chrono::steady_clock::time_point start, const SearchLimits& limits, size_t line, size_t lineCount) {
            if (limits.moveTimeMs <= 0) {
                return std::nullopt;
            }
            const int64_t shareMs = static_cast<int64_t>(limits.moveTimeMs) * static_cast<int64_t>(line) / static_cast<int64_t>(lineCount);
            return start + std::chrono::milliseconds(shareMs);
        }

    }  // namespace

    /// The state of one FindMove() / Analyse(): the request's position and limits, the line being searched (a job) and the lines found. The one place for the
    /// line loop, the deadlines, the retry and the checks of the answer: the one-call path is this object run to the end
    struct OwnSearch::Impl {
        /// The position the request leads to, with the history of the game; every line's job searches a copy of it
        Position position;
        MoveList legal;
        SearchLimits limits;
        /// When the call began: the deadlines are counted from it
        std::chrono::steady_clock::time_point start;
        CancellationToken token;
        size_t lineCount;
        /// The best moves of the lines found, which the next line leaves out at the root
        Vector<Move> excluded;
        Analysis analysis;
        /// The search of the line being worked on; none once the answer or the error is ready
        Optional<SearchJob> job;
        /// Whether the job is the retry at depth 1 of a line whose first search was stopped before its first node
        bool retrying = false;
        /// The nodes of the jobs that are over
        uint64_t finishedNodes = 0;
        Optional<std::expected<Analysis, EngineError>> result;

        Impl(Position startPosition, MoveList legalMoves, const SearchLimits& searchLimits, std::chrono::steady_clock::time_point begun, const CancellationToken& cancelToken, size_t lines)
            : position(std::move(startPosition)), legal(std::move(legalMoves)), limits(searchLimits), start(begun), token(cancelToken), lineCount(lines) {}

        /// Starts the search of line `excluded.size() + 1`: with the limits and the deadline of the line, or (@p retry) the first iteration alone, which no limit can stop.
        /// There is no job running (the caller has reset the last one), so the new one never lives beside the old table
        void StartJob(bool retry) {
            SearchSettings settings;
            settings.maxDepth = retry ? 1 : MaxDepthOf(limits);
            settings.excludedRootMoves = excluded;
            SearchControl control;
            if (!retry) {
                control.token = token;
                control.deadline = DeadlineOfLine(start, limits, excluded.size() + 1, lineCount);
            }
            job.emplace(position, std::move(settings), std::move(control));
        }

        void Finish(EngineErrorKind kind, String detail) {
            if (job) {
                finishedNodes += job->GetResult().nodes;
                job.reset();
            }
            result = std::unexpected(EngineError{kind, std::move(detail)});
        }

        /// What the end of the job of a line does, the same for every line: Cancelled when the token is cancelled, the retry when no iteration finished, the check of the
        /// move against the generator, then the line is kept and the next line's job starts, or the analysis is the answer
        void OnJobDone() {
            const SearchResult found = job->GetResult();
            finishedNodes += found.nodes;
            job.reset();

            if (!retrying) {
                if (token.IsCancelled()) {
                    Finish(EngineErrorKind::Cancelled, "the search was cancelled and its answer was dropped");
                    return;
                }
                if (!found.complete) {
                    // The deadline had run out before the first node (a tiny move time on a busy machine): the answer is the first iteration, which has no limit
                    retrying = true;
                    StartJob(true);
                    return;
                }
            }
            retrying = false;

            if (found.best.IsNull() || eastl::find(legal.begin(), legal.end(), found.best) == legal.end()) {
                Finish(EngineErrorKind::IllegalMove, Format("the search answered '{}', which is not legal in {}", found.best.ToUci(), position.ToFen()));
                return;
            }
            analysis.lines.push_back(AnalysisLine{found.best, ScoreFromSearch(found.score), found.depth});
            excluded.push_back(found.best);
            if (excluded.size() >= lineCount) {
                result = std::move(analysis);
                return;
            }
            StartJob(false);
        }
    };

    OwnSearch::OwnSearch(UniquePtr<Impl> impl) noexcept : _impl(std::move(impl)) {}
    OwnSearch::~OwnSearch() = default;
    OwnSearch::OwnSearch(OwnSearch&&) noexcept = default;
    OwnSearch& OwnSearch::operator=(OwnSearch&&) noexcept = default;

    std::expected<OwnSearch, EngineError> OwnSearch::Begin(const SearchRequest& request, const CancellationToken& token, int lines) {
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

        if (auto valid = request.Validate(); !valid) {
            return std::unexpected(std::move(valid.error()));
        }
        auto found = request.FinalPosition();
        if (!found) {
            return std::unexpected(std::move(found.error()));
        }
        MoveList legal = MoveGenerator::GenerateLegal(*found);
        if (legal.empty()) {
            return Fail(EngineErrorKind::NoMove, "the side to move has no legal move: the engine was not asked");
        }
        if (token.IsCancelled()) {
            return Fail(EngineErrorKind::Cancelled, "cancelled before the search started");
        }
        // A position with fewer legal moves than lines asked for gives as many lines as it has moves
        const size_t lineCount = eastl::min(static_cast<size_t>(lines), static_cast<size_t>(legal.size()));
        auto impl = MakeUnique<Impl>(std::move(*found), std::move(legal), request.limits, start, token, lineCount);
        impl->StartJob(false);
        return OwnSearch(std::move(impl));
    }

    SearchProgress OwnSearch::Advance(uint64_t maxNodes) {
        Impl& search = *_impl;
        if (search.result) {
            return SearchProgress::Done;
        }
        if (search.token.IsCancelled()) {
            search.Finish(EngineErrorKind::Cancelled, "the search was cancelled and its answer was dropped");
            return SearchProgress::Done;
        }
        // The nodes left of this call: a line that ends inside the slice hands the rest to the next line's job
        uint64_t remaining = maxNodes;
        for (;;) {
            const uint64_t before = search.job->GetResult().nodes;
            if (search.job->Advance(remaining) == SearchProgress::Paused) {
                return SearchProgress::Paused;
            }
            const uint64_t ran = search.job->GetResult().nodes - before;
            search.OnJobDone();
            if (search.result) {
                return SearchProgress::Done;
            }
            if (ran >= remaining) {
                return SearchProgress::Paused;
            }
            if (remaining != UINT64_MAX) {
                remaining -= ran;
            }
        }
    }

    bool OwnSearch::IsDone() const noexcept {
        return _impl->result.has_value();
    }

    std::expected<Analysis, EngineError> OwnSearch::Result() const {
        ASSERT(_impl->result.has_value(), "OwnSearch::Result: the search is not over");
        if (!_impl->result) {
            return Fail(EngineErrorKind::Cancelled, "the search is not over");
        }
        return *_impl->result;
    }

    uint64_t OwnSearch::GetNodes() const noexcept {
        return _impl->finishedNodes + (_impl->job ? _impl->job->GetResult().nodes : 0);
    }

    std::expected<OwnSearch, EngineError> OwnEngine::BeginFindMove(const SearchRequest& request, const CancellationToken& token) const {
        // FindMove() gives one move whatever request.lines says (the request is validated with it all the same)
        return OwnSearch::Begin(request, token, 1);
    }

    std::expected<OwnSearch, EngineError> OwnEngine::BeginAnalyse(const SearchRequest& request, const CancellationToken& token) const {
        return OwnSearch::Begin(request, token, request.lines);
    }

    namespace {

        /// The answer of FindMove() from a finished search: the move of the first line, or the error of the search. The one place for the blocking and the async path
        std::expected<Move, EngineError> MoveOf(const OwnSearch& search) {
            auto answer = search.Result();
            if (!answer) {
                return std::unexpected(std::move(answer.error()));
            }
            return answer->Best().move;
        }

    }  // namespace

    std::expected<Move, EngineError> OwnEngine::FindMove(const SearchRequest& request, const CancellationToken& token) {
        auto search = BeginFindMove(request, token);
        if (!search) {
            return std::unexpected(std::move(search.error()));
        }
        (void)search->Advance(UINT64_MAX);
        return MoveOf(*search);
    }

    std::expected<Analysis, EngineError> OwnEngine::Analyse(const SearchRequest& request, const CancellationToken& token) {
        auto search = BeginAnalyse(request, token);
        if (!search) {
            return std::unexpected(std::move(search.error()));
        }
        (void)search->Advance(UINT64_MAX);
        return search->Result();
    }

    // The coroutines start the search with OwnSearch::Begin() and not through the members, so nothing reads `this` once the task runs: the engine may be gone by then.
    // The yield goes before the work: a task started between two drains of the frame budget is "spent" and must give the frame back at once.
    Task<std::expected<Move, EngineError>> OwnEngine::FindMoveAsync(SearchRequest request, CancellationToken token) {
        auto search = OwnSearch::Begin(request, token, 1);
        if (!search) {
            co_return std::unexpected(std::move(search.error()));
        }
        while (!search->IsDone()) {
            co_await YieldIfNeeded();
            (void)search->Advance(kSearchSliceNodes);
        }
        co_return MoveOf(*search);
    }

    Task<std::expected<Analysis, EngineError>> OwnEngine::AnalyseAsync(SearchRequest request, CancellationToken token) {
        auto search = OwnSearch::Begin(request, token, request.lines);
        if (!search) {
            co_return std::unexpected(std::move(search.error()));
        }
        while (!search->IsDone()) {
            co_await YieldIfNeeded();
            (void)search->Advance(kSearchSliceNodes);
        }
        co_return search->Result();
    }

}  // namespace BECore::Chess
