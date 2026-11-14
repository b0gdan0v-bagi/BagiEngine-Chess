#pragma once

#include <Chess/Engine/ChessEngine.h>

namespace BECore::Chess {

    /**
     * @brief A handicap: an IChessEngine that plays a random legal move now and then in place of the move of the engine it wraps.
     *
     * It exists because the wrapped engines have a floor: Stockfish at `Skill Level` 0 plays like a 1300-1400 player and the own engine
     * at depth 1 is about as good, which is more than a casual person is. A random legal move costs about five pawns, so mixing it in
     * with a share of moves is the one knob that goes as low as wanted, and it is the same for every engine.
     *
     * For a request it decides whether this move is a blunder and, if so, answers a random legal move of the position; otherwise it
     * delegates to the wrapped engine with the request and the token as they came. **Both the decision and the move are a pure
     * function of the position (its key and the number of moves in the request, counted from its start FEN; a FromPosition request
     * counts 0) and the percent**: no clock, no random device, no state, so the answer is the same on every run and platform, the
     * manual-clock smoke test stays reproducible and a test can pin it. The same position (reached by the same number of moves in the
     * request) therefore blunders the same way every time, and a position that blunders at 15 percent also blunders at 40.
     * A blunder does not start the wrapped engine (no process for a blunder, no search).
     *
     * The contract of IChessEngine stays: BadRequest (limits, start FEN or a move wrong), NoMove (no legal move) and Cancelled (the
     * token is cancelled before the call) are answered by this class on the blunder path, in the order of OwnEngine, and by the wrapped
     * engine otherwise. At 0 percent nothing is rolled and the wrapped engine answers everything, an error too, unchanged; at 100 it is
     * never asked. The percent is clamped to 0..100. No state between calls, so threads may share one object. It logs nothing.
     */
    class BlunderEngine final : public IChessEngine {
    public:
        /// @p percent is the share of moves replaced by a random legal move, 0..100 (brought into that range)
        BlunderEngine(eastl::shared_ptr<IChessEngine> inner, int percent);

        [[nodiscard]] std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken& token) override;

        /// Forwarded to the wrapped engine with the request and the token as they came, at any percent: a handicap is for the moves the program plays, and
        /// a hint or an evaluation of the position must never be spoilt by it (nor must a random move be shown as the best one). An engine that does not
        /// analyse answers Unsupported through this class as well.
        [[nodiscard]] std::expected<Analysis, EngineError> Analyse(const SearchRequest& request, const CancellationToken& token) override;

        /// FindMove() as a coroutine: the same decision (BlunderMove(), one place), a blunder is answered at once with no suspension, otherwise the wrapped engine's
        /// FindMoveAsync() is awaited, so a wrapped OwnEngine still yields between slices. The object must outlive the task
        [[nodiscard]] Task<std::expected<Move, EngineError>> FindMoveAsync(SearchRequest request, CancellationToken token) override;

        /// Forwarded to the wrapped engine's AnalyseAsync() like Analyse()
        [[nodiscard]] Task<std::expected<Analysis, EngineError>> AnalyseAsync(SearchRequest request, CancellationToken token) override;

        /// The share after clamping
        [[nodiscard]] int Percent() const {
            return _percent;
        }

    private:
        /// The part of FindMove() that is the handicap's own: an error of the request (in the order of OwnEngine), the random legal move when this move is a blunder,
        /// nothing when the wrapped engine is to answer (always at 0 percent, with no check of the request)
        [[nodiscard]] std::expected<Optional<Move>, EngineError> BlunderMove(const SearchRequest& request, const CancellationToken& token) const;

        eastl::shared_ptr<IChessEngine> _inner;
        int _percent;
    };

}  // namespace BECore::Chess
