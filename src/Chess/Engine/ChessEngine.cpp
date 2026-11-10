#include "ChessEngine.h"

#include <Chess/Engine/SearchRequest.h>

namespace BECore::Chess {

    // Out of line: SearchRequest.h includes ChessEngine.h, and a coroutine takes the request by value, so its type must be complete here
    Task<std::expected<Move, EngineError>> IChessEngine::FindMoveAsync(SearchRequest request, CancellationToken token) {
        co_return FindMove(request, token);
    }

    Task<std::expected<Analysis, EngineError>> IChessEngine::AnalyseAsync(SearchRequest request, CancellationToken token) {
        co_return Analyse(request, token);
    }

}  // namespace BECore::Chess
