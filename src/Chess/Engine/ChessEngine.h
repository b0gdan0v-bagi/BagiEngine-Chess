#pragma once

#include <Chess/Engine/Analysis.h>
#include <Chess/Game/Move.h>
#include <TaskSystem/CancellationToken.h>
#include <TaskSystem/Task.h>

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

    /// What `co_await` of an async engine call gives: the engine's own answer inside the task's result. The task itself fails only when it was abandoned or threw,
    /// which is a Cancelled answer to the caller
    template <typename T>
    [[nodiscard]] std::expected<T, EngineError> AnswerOfTask(std::expected<std::expected<T, EngineError>, TaskError> result) {
        if (!result) {
            return std::unexpected(EngineError{EngineErrorKind::Cancelled, "the engine task did not finish"});
        }
        return std::move(*result);
    }

    /**
     * @brief An engine that picks a move and analyses a position: Stockfish over UCI, the engine of our own (OwnEngine), or a handicap over either.
     *
     * FindMove() and Analyse() are synchronous and blocking: they return when the engine has answered, was cancelled or failed, so a caller that must not
     * wait (the main thread) runs them on a worker. FindMoveAsync() and AnalyseAsync() are the same calls as coroutines: an engine with a resumable search
     * (OwnEngine) gives the thread back between slices of its search, the others block for the whole call (the default). The game's tasks await the async form.
     * It takes a SearchRequest (the start FEN and the moves played), not a Position,
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
         * SearchRequest::lines asks for several lines (the best moves, best first, see Analysis): one by default; FindMove() gives one move whatever it says, but still validates it.
         * Not pure: an engine that does not analyse keeps this default, which answers Unsupported, so the doubles of the tests and every other
         * implementation stay as they are. An engine that wraps another one forwards the call.
         */
        [[nodiscard]] virtual std::expected<Analysis, EngineError> Analyse([[maybe_unused]] const SearchRequest& request, [[maybe_unused]] const CancellationToken& token) {
            return std::unexpected(EngineError{EngineErrorKind::Unsupported, "this engine does not analyse"});
        }

        /**
         * @brief FindMove() as a coroutine: the same answer and the same errors, but an engine with a resumable search may suspend between slices.
         *
         * The default body calls FindMove() and so blocks the thread for the whole call; an engine with a resumable search overrides it. The request and the
         * token are taken by value, so the coroutine frame owns what it uses (a reference would dangle once the caller's frame is gone). The call suspends only
         * through the frame budget (`co_await YieldIfNeeded()`): on a worker pool it behaves as the blocking call. The caller keeps the engine alive until the task ends.
         */
        [[nodiscard]] virtual Task<std::expected<Move, EngineError>> FindMoveAsync(SearchRequest request, CancellationToken token);

        /// Analyse() as a coroutine, with the rules of FindMoveAsync(). The default body calls Analyse(), so an engine that does not analyse answers Unsupported
        [[nodiscard]] virtual Task<std::expected<Analysis, EngineError>> AnalyseAsync(SearchRequest request, CancellationToken token);
    };

}  // namespace BECore::Chess
