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

        /// The part of FindMove() and Analyse() that they share, so the two cannot drift: validate, the final position, NoMove, an early Cancelled, the
        /// settings, the search, Cancelled after it, the retry when no iteration finished, and the check of the move against the generator
        /// The result has a best move that is legal in the position
        std::expected<SearchResult, EngineError> RunSearch(const SearchRequest& request, const CancellationToken& token) {
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

            if (auto valid = request.Validate(); !valid) {
                return std::unexpected(std::move(valid.error()));
            }
            auto found = request.FinalPosition();
            if (!found) {
                return std::unexpected(std::move(found.error()));
            }
            Position position = std::move(*found);
            const MoveList legal = MoveGenerator::GenerateLegal(position);
            if (legal.empty()) {
                return Fail(EngineErrorKind::NoMove, "the side to move has no legal move: the engine was not asked");
            }
            if (token.IsCancelled()) {
                return Fail(EngineErrorKind::Cancelled, "cancelled before the search started");
            }

            SearchSettings settings;
            settings.maxDepth = MaxDepthOf(request.limits);
            SearchControl control;
            control.token = token;
            if (request.limits.moveTimeMs > 0) {
                control.deadline = start + std::chrono::milliseconds(request.limits.moveTimeMs);
            }

            SearchResult result = Search(position, settings, control);
            if (token.IsCancelled()) {
                return Fail(EngineErrorKind::Cancelled, "the search was cancelled and its answer was dropped");
            }
            if (!result.complete) {
                // The deadline had run out before the first node (a tiny move time on a busy machine): the answer is the first iteration, which has no limit
                settings.maxDepth = 1;
                result = Search(position, settings, SearchControl{});
            }

            if (result.best.IsNull() || eastl::find(legal.begin(), legal.end(), result.best) == legal.end()) {
                return Fail(EngineErrorKind::IllegalMove, Format("the search answered '{}', which is not legal in {}", result.best.ToUci(), position.ToFen()));
            }
            return result;
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
        auto searched = RunSearch(request, token);
        if (!searched) {
            return std::unexpected(std::move(searched.error()));
        }
        const SearchResult& result = *searched;
        return Analysis{{AnalysisLine{result.best, ScoreFromSearch(result.score), result.depth}}};
    }

}  // namespace BECore::Chess
