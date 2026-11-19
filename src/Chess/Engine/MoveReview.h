#pragma once

#include <Chess/Engine/MoveRating.h>
#include <Chess/Game/Position.h>

namespace BECore::Chess {

    /**
     * @brief How much a played move cost, on a scale. Pure: no engine, no task, no scene.
     *
     * **The definition** (the only one the game uses). A move is made in a position that the engine evaluates as `b` for the side to move, with best move `m` (line 1); the position after it is
     * evaluated as `a` for ITS side to move, so the played move is worth `-a` to the mover. The loss is `max(0, clamp(b) - clamp(-a))` in centipawns, both values on the scale of
     * ScoreToCentipawns() and clamped to +-kReviewClamp: a won game stays won (mate in 3 against mate in 5 is no loss), a mate thrown away for +300 is a loss of 700. The best move `m` has loss
     * 0 whatever the numbers (an engine's noise on its own move is no loss), and a move that scores above line 1 has loss 0, never a negative one.
     *
     * **The scale:** `Best` is the best move itself, else by the loss: `Good` below kInaccuracyLoss, `Inaccuracy` below kMistakeLoss, `Mistake` below kBlunderLoss, else `Blunder`.
     */
    CORE_ENUM(MoveQuality, uint8_t, Best, Good, Inaccuracy, Mistake, Blunder)

    /// Both evaluations of a move are held within +-this many centipawns before they are subtracted
    inline constexpr int kReviewClamp = 1000;

    /// The loss, in centipawns, from which a move is an inaccuracy; a smaller one is `Good`
    inline constexpr int kInaccuracyLoss = 50;

    /// The loss from which a move is a mistake
    inline constexpr int kMistakeLoss = 100;

    /// The loss from which a move is a blunder
    inline constexpr int kBlunderLoss = 300;

    /// The id of @p quality on the control API: "best", "good", "inaccuracy", "mistake", "blunder" (snake_case, unique)
    [[nodiscard]] StringView MoveQualityId(MoveQuality quality);

    /// Inaccuracy or worse: what the move list marks
    [[nodiscard]] constexpr bool IsMistake(MoveQuality quality) noexcept {
        return quality >= MoveQuality::Inaccuracy;
    }

    /// The review of one played move
    struct MoveReview {
        int loss = 0;  ///< centipawns, never negative
        MoveQuality quality = MoveQuality::Best;

        [[nodiscard]] bool operator==(const MoveReview&) const noexcept = default;
    };

    /**
     * @brief What a position with no legal move is worth to its side to move: -kMateCentipawns when it is in check (mated), 0 for a stalemate; none when it has a legal move.
     *
     * Only the last position of a game can be terminal; an engine has no line to give there, so the review takes this value in its place.
     */
    [[nodiscard]] Optional<int> TerminalCentipawns(const Position& position);

    /**
     * @brief Reviews @p played, a move made in a position whose line 1 is @p bestMove at @p bestCentipawns (for the side to move there), by the definition above.
     *
     * @p afterCentipawns is the evaluation of the position AFTER the move, for ITS side to move (the other side); both are on the scale of ScoreToCentipawns() and are clamped here.
     */
    [[nodiscard]] MoveReview ReviewMove(int bestCentipawns, Move bestMove, Move played, int afterCentipawns);

}  // namespace BECore::Chess
