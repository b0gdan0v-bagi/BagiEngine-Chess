#include "OwnEngine.h"

#include <Chess/Engine/Analysis.h>
#include <Chess/Game/MoveGenerator.h>

namespace BECore::Chess {

    namespace {

        std::unexpected<EngineError> Fail(EngineErrorKind kind, eastl::string detail) {
            return std::unexpected(EngineError{kind, std::move(detail)});
        }

        /// The last iteration to search for @p limits: its depth, or the deepest there is when only the time limits the search
        int MaxDepthOf(const SearchLimits& limits) {
            return limits.depth > 0 ? eastl::min(limits.depth, kMaxSearchDepth) : kMaxSearchDepth;
        }

        /// What the request leads to, ready to be searched: the position, its legal moves and when the call began (the deadlines are counted from it)
        struct Prepared {
            Position position;
            MoveList legal;
            std::chrono::steady_clock::time_point start;
        };

        /// The part of FindMove() and Analyse() that they share, so the two cannot drift: validate, the final position, NoMove and an early Cancelled
        std::expected<Prepared, EngineError> Prepare(const SearchRequest& request, const CancellationToken& token) {
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

            if (auto valid = request.Validate(); !valid) {
                return std::unexpected(std::move(valid.error()));
            }
            auto found = request.FinalPosition();
            if (!found) {
                return std::unexpected(std::move(found.error()));
            }
            Prepared prepared{std::move(*found), {}, start};
            prepared.legal = MoveGenerator::GenerateLegal(prepared.position);
            if (prepared.legal.empty()) {
                return Fail(EngineErrorKind::NoMove, "the side to move has no legal move: the engine was not asked");
            }
            if (token.IsCancelled()) {
                return Fail(EngineErrorKind::Cancelled, "cancelled before the search started");
            }
            return prepared;
        }

        /// One search of FindMove() and Analyse(), the same for every line: the settings, the search with @p deadline (nothing: no time limit) and without the root
        /// moves @p excluded, Cancelled after it, the retry when no iteration finished, and the check of the move against the generator.
        /// The result has a best move that is legal in the position
        std::expected<SearchResult, EngineError> SearchOnce(Prepared& prepared, const SearchRequest& request, const CancellationToken& token,
                                                            std::optional<std::chrono::steady_clock::time_point> deadline, const eastl::vector<Move>& excluded) {
            SearchSettings settings;
            settings.maxDepth = MaxDepthOf(request.limits);
            settings.excludedRootMoves = excluded;
            SearchControl control;
            control.token = token;
            control.deadline = deadline;

            SearchResult result = Search(prepared.position, settings, control);
            if (token.IsCancelled()) {
                return Fail(EngineErrorKind::Cancelled, "the search was cancelled and its answer was dropped");
            }
            if (!result.complete) {
                // The deadline had run out before the first node (a tiny move time on a busy machine): the answer is the first iteration, which has no limit
                settings.maxDepth = 1;
                result = Search(prepared.position, settings, SearchControl{});
            }

            if (result.best.IsNull() || eastl::find(prepared.legal.begin(), prepared.legal.end(), result.best) == prepared.legal.end()) {
                return Fail(EngineErrorKind::IllegalMove, Format("the search answered '{}', which is not legal in {}", result.best.ToUci(), prepared.position.ToFen()));
            }
            return result;
        }

        /// The time after which line @p line of @p lineCount (counted from 1) must be done: the move time is shared out, line k ends at k / lineCount of it. No move time: no deadline
        std::optional<std::chrono::steady_clock::time_point> DeadlineOfLine(const Prepared& prepared, const SearchLimits& limits, size_t line, size_t lineCount) {
            if (limits.moveTimeMs <= 0) {
                return std::nullopt;
            }
            const int64_t shareMs = static_cast<int64_t>(limits.moveTimeMs) * static_cast<int64_t>(line) / static_cast<int64_t>(lineCount);
            return prepared.start + std::chrono::milliseconds(shareMs);
        }

        /// The search of FindMove(): the whole move time, no root move excluded (Analyse() prepares and searches by itself, line by line)
        std::expected<SearchResult, EngineError> RunSearch(const SearchRequest& request, const CancellationToken& token) {
            auto prepared = Prepare(request, token);
            if (!prepared) {
                return std::unexpected(std::move(prepared.error()));
            }
            return SearchOnce(*prepared, request, token, DeadlineOfLine(*prepared, request.limits, 1, 1), {});
        }

    }  // namespace

    std::expected<Move, EngineError> OwnEngine::FindMove(const SearchRequest& request, const CancellationToken& token) {
        auto searched = RunSearch(request, token);
        if (!searched) {
            return std::unexpected(std::move(searched.error()));
        }
        return searched->best;
    }

    std::expected<Analysis, EngineError> OwnEngine::Analyse(const SearchRequest& request, const CancellationToken& token) {
        auto prepared = Prepare(request, token);
        if (!prepared) {
            return std::unexpected(std::move(prepared.error()));
        }
        // Line 1 is the search of a request for one line (for one line it is all there is, the call FindMove() makes); line k is a search of its own
        // with the moves of the lines before it left out at the root. A position with fewer legal moves than lines asked for gives as many lines as it has moves
        const size_t lineCount = eastl::min(static_cast<size_t>(request.lines), static_cast<size_t>(prepared->legal.size()));
        Analysis analysis;
        eastl::vector<Move> excluded;
        for (size_t line = 1; line <= lineCount; ++line) {
            auto searched = SearchOnce(*prepared, request, token, DeadlineOfLine(*prepared, request.limits, line, lineCount), excluded);
            if (!searched) {
                return std::unexpected(std::move(searched.error()));
            }
            analysis.lines.push_back(AnalysisLine{searched->best, ScoreFromSearch(searched->score), searched->depth});
            excluded.push_back(searched->best);
        }
        return analysis;
    }

}  // namespace BECore::Chess
