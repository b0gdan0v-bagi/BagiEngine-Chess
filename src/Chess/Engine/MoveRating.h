#pragma once

#include <Chess/Engine/Analysis.h>

namespace BECore::Chess {

    /**
     * @brief Why a played move is or is not a brilliant move. Pure: no engine, no task, no scene.
     *
     * **The definition** (the only one the game uses): a move is brilliant when it is the engine's best move in the position it was played in
     * (it equals the first line of an analysis of the position BEFORE the move, at a fixed depth) and it was a choice. The chess-site meaning (a good
     * sacrifice) is not asked for. A best move is not marked when there was no choice:
     *  - `Forced`: the position had one legal move (an analysis of such a position has one line, `Analysis::Second()` is null);
     *  - `Obvious`: the second best line is far worse (the gap to line 1 is at least kForcedMargin), so it is "the only reasonable move": a forced recapture,
     *    a mate in one against moves that do nothing.
     * `NotBest` is any other move than the first line's (checked first, so it never depends on the second line).
     *
     * A tie (equal scores of the first two lines) is a gap of 0, so the move of line 1 is the best move and the other move of the tie is `NotBest` (the engine's
     * own pick decides between equals). The own engine searches each line on its own, so line 2 may score above line 1: a negative gap counts as 0, it is not a failure.
     */
    CORE_ENUM(MoveRatingReason, uint8_t, Brilliant, NotBest, Forced, Obvious)

    /// The gap between the first two lines, in centipawns (two pawns), from which the best move is "the only reasonable one" (`Obvious`). A gap of kForcedMargin - 1 still is a choice
    inline constexpr int kForcedMargin = 200;

    /// The scale on which a mate is compared with centipawns: a mate in m moves by the side to move is `kMateCentipawns - 100 * m`, the side to move mated in m moves is its negation
    inline constexpr int kMateCentipawns = 100000;

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
     * Centipawns as they are. A mate in m moves is `kMateCentipawns - 100 * m` for the side that mates and its negation for the side that is mated:
     * any mate beats any centipawn score (the search keeps those far below kMateCentipawns - 100 * 128), a closer mate is larger, and a closer mate
     * against the side to move is smaller.
     */
    [[nodiscard]] int ScoreToCentipawns(const AnalysisScore& score) noexcept;

    /// The id of @p reason on the control API: "brilliant", "not_best", "forced", "obvious" (snake_case, unique)
    [[nodiscard]] eastl::string_view MoveRatingReasonId(MoveRatingReason reason);

    /**
     * @brief Rates @p played, a move made in the position that @p before analyses (an analysis of the position BEFORE the move, two lines asked for).
     *
     * `NotBest` when @p played is not `before.Best().move`; else `Forced` when `before.Second()` is null; else `Obvious` when
     * `ScoreToCentipawns(line 1) - ScoreToCentipawns(line 2)` is at least kForcedMargin (a negative gap counts as 0); else `Brilliant`.
     * @p before must have a line (an analysis that came back from an engine always has).
     */
    [[nodiscard]] MoveRating RateMove(const Analysis& before, Move played);

}  // namespace BECore::Chess
