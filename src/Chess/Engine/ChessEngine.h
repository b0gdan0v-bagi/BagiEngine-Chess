#pragma once

#include <Chess/Game/Move.h>
#include <TaskSystem/CancellationToken.h>

namespace BECore::Chess {

    struct SearchRequest;

    /// Why an engine gave no move. BadRequest: the request itself is wrong (limits, FEN, a move that is not legal). LaunchFailed: the program could not be started.
    /// Timeout: no answer in time. ProcessDied: the child ended or its pipe broke. ProtocolError: the engine did not speak UCI. NoMove: the side to move has no move,
    /// or the engine says it has none. IllegalMove: the engine's move is not legal in the position. Cancelled: the caller's token was cancelled.
    /// Unsupported: this platform cannot do it.
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
     * @brief An opponent that picks a move: Stockfish over UCI, or an engine of our own later.
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
    };

}  // namespace BECore::Chess
