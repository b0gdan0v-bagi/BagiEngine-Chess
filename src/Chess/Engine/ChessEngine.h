#pragma once

#include <Chess/Engine/Analysis.h>
#include <Chess/Game/Move.h>
#include <TaskSystem/CancellationToken.h>

namespace BECore::Chess {

    struct SearchRequest;

    /// Why an engine gave no move. BadRequest: the request itself is wrong (limits, FEN, a move that is not legal). LaunchFailed: the program could not be started.
    /// Timeout: no answer in time. ProcessDied: the child ended or its pipe broke. ProtocolError: the engine did not speak UCI. NoMove: the side to move has no move,
    /// or the engine says it has none. IllegalMove: the engine's move is not legal in the position. Cancelled: the caller's token was cancelled.
    /// Unsupported: this platform cannot do it, or this engine does not do it (the default IChessEngine::Analyse()).
    CORE_ENUM(EngineErrorKind, uint8_t, BadRequest, LaunchFailed, Timeout, ProcessDied, ProtocolError, NoMove, IllegalMove, Cancelled, Unsupported)

    /// A failure of an engine call: the kind to branch on and a sentence for the log. Only the code that owns the player logs it
    struct EngineError {
        EngineErrorKind kind = EngineErrorKind::BadRequest;
        eastl::string detail;
    };

    /// How long or how deep an engine may search. 0 means no limit of that kind; the limits are valid when neither is negative and at least one is positive
    struct SearchLimits {
        int moveTimeMs = 0;
        int depth = 0;
    };

    /**
     * @brief An engine that picks a move and analyses a position: Stockfish over UCI, the engine of our own (OwnEngine), or a handicap over either.
     *
     * The call is synchronous and blocking: it returns when the engine has answered, was cancelled or failed, so a caller that must not
     * wait (the main thread) runs it on a worker. It takes a SearchRequest (the start FEN and the moves played), not a Position,
     * because UCI engines see repetitions only through the moves. The request type is in Chess/Engine/SearchRequest.h, which includes this file.
     */
    class IChessEngine {
    public:
        virtual ~IChessEngine() = default;

        /**
         * @brief The move the engine plays in the position @p request leads to, within its limits.
         *
         * A returned move is legal in that position (an implementation checks it with MoveGenerator::ParseUci()). An implementation
         * watches @p token and answers Cancelled soon after it is cancelled.
         */
        [[nodiscard]] virtual std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken& token) = 0;

        /**
         * @brief How the position @p request leads to stands: the best move with its score (Analysis.h), within the limits of the request.
         *
         * The contract of FindMove(): the request is validated (BadRequest), a finished game is NoMove before any search or process, a cancelled
         * @p token is Cancelled, and a returned move is legal in the position (IllegalMove otherwise). The score is from the side to move.
         * SearchRequest::lines asks for several lines (the best moves, best first, see Analysis): one by default, and FindMove() never reads it.
         * Not pure: an engine that does not analyse keeps this default, which answers Unsupported, so the doubles of the tests and every other
         * implementation stay as they are. An engine that wraps another one forwards the call.
         */
        [[nodiscard]] virtual std::expected<Analysis, EngineError> Analyse([[maybe_unused]] const SearchRequest& request, [[maybe_unused]] const CancellationToken& token) {
            return std::unexpected(EngineError{EngineErrorKind::Unsupported, "this engine does not analyse"});
        }
    };

}  // namespace BECore::Chess
