#pragma once

#include <Chess/Engine/Analysis.h>

namespace BECore::Chess {

    /**
     * @brief Why a played move is or is not a brilliant move. Pure: no engine, no task, no scene.
     *
     * **The definition** (the only one the game uses): a move is brilliant when it is the engine's best move in the position it was played in (it equals the first line of an analysis of the
     * position BEFORE the move, at a fixed depth) and it was a choice; the chess-site meaning (a good sacrifice) is not asked for. A best move is not marked when there was no choice:
     *  - `Forced`: the position had one legal move (an analysis of it has one line, `Analysis::Second()` is null);
     *  - `Obvious`: the second line is far behind (the gap to line 1 is at least kForcedMargin): "the only reasonable move".
     * `NotBest` is any other move than the first line's (decided first, so it never depends on the second line).
     *
     * A tie (equal scores of the first two lines) is a gap of 0: the move of line 1 is the best move and the other move of the tie is `NotBest`. The own engine searches each line on its own, so
     * line 2 may score above line 1: a negative gap counts as 0, it is not a failure.
     */
    CORE_ENUM(MoveRatingReason, uint8_t, Brilliant, NotBest, Forced, Obvious)

    /// The gap between the first two lines, in centipawns, from which the best move is "the only reasonable one" (`Obvious`); a smaller gap is still a choice
    inline constexpr int kForcedMargin = 200;

    /// The scale on which a mate is compared with centipawns: a mate in m moves by the side to move is `kMateCentipawns - kCentipawnsPerMateMove * m`, the side to move mated in m moves is its
    /// negation
    inline constexpr int kMateCentipawns = 100000;

    /// How much a mate one move closer is worth more on that scale
    inline constexpr int kCentipawnsPerMateMove = 100;

    /// The rating of one played move
    struct MoveRating {
        MoveRatingReason reason = MoveRatingReason::NotBest;

        [[nodiscard]] bool IsBrilliant() const noexcept {
            return reason == MoveRatingReason::Brilliant;
        }

        [[nodiscard]] bool operator==(const MoveRating&) const noexcept = default;
    };

    /**
     * @brief @p score (from the side to move's view, as the analysis gives it) on one scale of centipawns, so that two lines can be subtracted.
     *
     * Centipawns as they are; a mate as above. Any mate beats any centipawn score (a search keeps those below kMateScore - kMaxMatePly, Evaluation.h, far below a mate of kMaxMatePly plies on this
     * scale), a closer mate is larger, and a closer mate against the side to move is smaller.
     */
    [[nodiscard]] int ScoreToCentipawns(const AnalysisScore& score) noexcept;

    /// The id of @p reason on the control API: "brilliant", "not_best", "forced", "obvious" (snake_case, unique)
    [[nodiscard]] StringView MoveRatingReasonId(MoveRatingReason reason);

    /**
     * @brief Rates @p played, a move made in the position that @p before analyses (the position BEFORE the move, two lines asked for), by the definition above.
     *
     * The order is `NotBest`, `Forced`, `Obvious`, else `Brilliant`; the gap is `ScoreToCentipawns(line 1) - ScoreToCentipawns(line 2)`. @p before must have a line (an analysis that came back
     * from an engine always has).
     */
    [[nodiscard]] MoveRating RateMove(const Analysis& before, Move played);

}  // namespace BECore::Chess
