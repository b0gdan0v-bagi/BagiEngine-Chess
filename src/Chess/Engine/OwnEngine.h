#pragma once

#include <Chess/Engine/Search.h>
#include <Chess/Engine/SearchRequest.h>

namespace BECore::Chess {

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
     * The object has no state between calls and no process, so FindMove() may be called from several threads at once, each with its
     * own request and token. It logs nothing (only EngineOpponent, OpponentController and PositionAnalyst log). The search runs on the thread that
     * calls, which is a worker of the Background pool in the game: Search() keeps a few KB of move lists per ply (see Search.cpp,
     * kMaxPly), which a worker's stack of 512 KB holds.
     */
    class OwnEngine final : public IChessEngine {
    public:
        [[nodiscard]] std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken& token) override;

        /// One line: the best move of the last iteration that finished, with its score from the side to move (ScoreFromSearch()) and the depth of that iteration.
        /// The contract of FindMove() in everything else (the same private search runs under both, so they give the same move for the same request, and the
        /// errors are the same), also deterministic for a depth, a deadline is not an error and the object may be asked from several threads at once.
        [[nodiscard]] std::expected<Analysis, EngineError> Analyse(const SearchRequest& request, const CancellationToken& token) override;
    };

}  // namespace BECore::Chess
