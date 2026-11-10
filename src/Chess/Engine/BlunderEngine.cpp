#include "BlunderEngine.h"

#include <Chess/Engine/SearchRequest.h>
#include <Chess/Game/MoveGenerator.h>

namespace BECore::Chess {

    namespace {

        constexpr int kMaxPercent = 100;
        constexpr uint64_t kRollSalt = 0xA24BAED4963EE407ULL;
        constexpr uint64_t kMoveSalt = 0x9FB21C651E98DF25ULL;
        constexpr uint64_t kPlyStep = 0x9E3779B97F4A7C15ULL;

        /// The finalizer of splitmix64: plain uint64_t arithmetic, the same on every platform
        uint64_t Mix(uint64_t value) {
            value ^= value >> 30;
            value *= 0xBF58476D1CE4E5B9ULL;
            value ^= value >> 27;
            value *= 0x94D049BB133111EBULL;
            value ^= value >> 31;
            return value;
        }

        std::unexpected<EngineError> Fail(EngineErrorKind kind, eastl::string detail) {
            return std::unexpected(EngineError{kind, std::move(detail)});
        }

    }  // namespace

    BlunderEngine::BlunderEngine(eastl::shared_ptr<IChessEngine> inner, int percent) : _inner(std::move(inner)), _percent(eastl::clamp(percent, 0, kMaxPercent)) {
        ASSERT(_inner, "BlunderEngine: no engine to wrap");
    }

    std::expected<std::optional<Move>, EngineError> BlunderEngine::BlunderMove(const SearchRequest& request, const CancellationToken& token) const {
        if (_percent == 0) {
            return std::nullopt;
        }

        if (auto valid = request.Validate(); !valid) {
            return std::unexpected(std::move(valid.error()));
        }
        auto found = request.FinalPosition();
        if (!found) {
            return std::unexpected(std::move(found.error()));
        }
        const Position& position = *found;
        const MoveList legal = MoveGenerator::GenerateLegal(position);
        if (legal.empty()) {
            return Fail(EngineErrorKind::NoMove, "the side to move has no legal move: the engine was not asked");
        }

        const uint64_t seed = position.Key() ^ (static_cast<uint64_t>(request.moves.size()) * kPlyStep);
        const bool blunder = Mix(seed ^ kRollSalt) % kMaxPercent < static_cast<uint64_t>(_percent);
        if (!blunder) {
            return std::nullopt;
        }
        if (token.IsCancelled()) {
            return Fail(EngineErrorKind::Cancelled, "cancelled before the move was picked");
        }
        return legal[Mix(seed ^ kMoveSalt) % legal.size()];
    }

    std::expected<Move, EngineError> BlunderEngine::FindMove(const SearchRequest& request, const CancellationToken& token) {
        auto blunder = BlunderMove(request, token);
        if (!blunder) {
            return std::unexpected(std::move(blunder.error()));
        }
        if (*blunder) {
            return **blunder;
        }
        return _inner->FindMove(request, token);
    }

    std::expected<Analysis, EngineError> BlunderEngine::Analyse(const SearchRequest& request, const CancellationToken& token) {
        return _inner->Analyse(request, token);
    }

    // The blunder path answers at once, without a suspension; the engine object must outlive the task, as the wrapped one does
    Task<std::expected<Move, EngineError>> BlunderEngine::FindMoveAsync(SearchRequest request, CancellationToken token) {
        auto blunder = BlunderMove(request, token);
        if (!blunder) {
            co_return std::unexpected(std::move(blunder.error()));
        }
        if (*blunder) {
            co_return** blunder;
        }
        co_return AnswerOfTask(co_await _inner->FindMoveAsync(std::move(request), std::move(token)));
    }

    Task<std::expected<Analysis, EngineError>> BlunderEngine::AnalyseAsync(SearchRequest request, CancellationToken token) {
        co_return AnswerOfTask(co_await _inner->AnalyseAsync(std::move(request), std::move(token)));
    }

}  // namespace BECore::Chess
