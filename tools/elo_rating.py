"""Turns game scores against opponents of known Elo into an Elo with a 95 % error bar. Standard library only, no process, no I/O.

    from elo_rating import estimate_rating, format_rating
    print(format_rating(estimate_rating([(1700, 1.0), (1700, 0.5), (1700, 0.0)])))      # 1700 +/- 321

A result is a pair `(opponent_elo, score)` with the score of one game from our side: 1.0 a win, 0.5 a draw, 0.0 a loss. The expected
score against an opponent is the logistic curve `E(R) = 1 / (1 + 10^((opponent - R) / 400))`; the rating `R` is the one for which the
expected scores add up to the scores seen (`sum(x_i - E_i(R)) = 0`, solved by bisection). The error is the sandwich estimate

    SE = sqrt(sum (x_i - E_i)^2) / (ln(10) / 400 * sum E_i (1 - E_i))

which needs no assumption about draws; the bar is 1.96 * SE (95 %). It is an Elo on the scale of the opponents' numbers (for
Stockfish: its `UCI_Elo` at the movetime used), not a rating of a person.

Two cases the formula cannot handle on its own:
- Every game won (or every game lost): the maximum-likelihood rating is infinite. The result is flagged `saturated` and is the rating
  of the score one half game short of the extreme, `(n - 0.5) / n` resp. `0.5 / n`, to be read as a bound (`>=` / `<=`). All the
  residuals of that score are zero against one opponent, so the bar of such a result is the model one, `1 / sqrt(Fisher information)`
  with Bernoulli games: `1.96 / (ln(10) / 400 * sqrt(sum E_i (1 - E_i)))`.
- Draws only against one opponent: the residuals are zero too, so the sandwich bar would be 0 (a claim of perfect precision). The
  error is `inf` then (`format_rating` prints `+/- ?`). Draws against two different opponents have residuals, hence a finite bar.
"""

import math
from dataclasses import dataclass

ELO_SCALE = 400.0
LOG_SLOPE = math.log(10.0) / ELO_SCALE  # d E / d R = LOG_SLOPE * E * (1 - E)
Z_95 = 1.96
RATING_LOWEST = -2000.0  # the bracket of the bisection; a result is clamped into it
RATING_HIGHEST = 6000.0
RATING_TOLERANCE = 1e-6
MAX_BISECTIONS = 200
RESIDUAL_ZERO = 1e-12  # a sum of squared residuals below this is zero (the bisection leaves ~1e-18 of rounding)
SATURATION_SHORTFALL = 0.5  # games short of the extreme score that stand in for "all won" / "all lost"

BOUND_NONE = ""
BOUND_LOWER = ">="  # all won: the rating is at least this
BOUND_UPPER = "<="  # all lost: the rating is at most this

Result = tuple[float, float]  # (opponent_elo, score of one game: 1.0, 0.5 or 0.0)


@dataclass(frozen=True)
class Rating:
    """An Elo with the half width of its 95 % error bar (`inf` when unknown). `saturated` marks a bound (`bound` says which one)."""

    elo: float
    error: float
    saturated: bool
    games: int
    bound: str = BOUND_NONE

    def __post_init__(self):
        if self.saturated != (self.bound != BOUND_NONE):
            raise ValueError(f"saturated={self.saturated} does not match bound={self.bound!r}")


def expected_score(rating: float, opponent_rating: float) -> float:
    """The score (0..1) that a player of `rating` is expected to make against `opponent_rating`."""
    return 1.0 / (1.0 + 10.0 ** ((opponent_rating - rating) / ELO_SCALE))


def score_to_elo_difference(score: float) -> float:
    """The Elo difference to the opponent at which `score` is expected (the inverse of `expected_score`); 0 and 1 have none."""
    if not 0.0 < score < 1.0:
        raise ValueError(f"a score of {score} has no Elo difference (it must be between 0 and 1, exclusive)")
    return -ELO_SCALE * math.log10(1.0 / score - 1.0)


def _fit(opponents: list[float], scores: list[float]) -> tuple[float, list[float]]:
    """The rating at which `sum(scores - expected)` is zero (bisection) and the expected scores there."""
    low, high = RATING_LOWEST, RATING_HIGHEST

    def surplus(rating: float) -> float:
        return sum(x - expected_score(rating, opponent) for opponent, x in zip(opponents, scores))

    if surplus(low) <= 0.0:
        high = low
    elif surplus(high) >= 0.0:
        low = high
    for _ in range(MAX_BISECTIONS):
        if high - low <= RATING_TOLERANCE:
            break
        middle = (low + high) / 2.0
        if surplus(middle) > 0.0:  # more points than expected: the rating is higher
            low = middle
        else:
            high = middle
    rating = (low + high) / 2.0
    return rating, [expected_score(rating, opponent) for opponent in opponents]


def _error(scores: list[float], expected: list[float], saturated: bool) -> float:
    """The half width of the 95 % bar: the sandwich estimate, or the model one when the games were all won or all lost."""
    information = LOG_SLOPE * sum(e * (1.0 - e) for e in expected)
    if information <= 0.0:
        return math.inf
    if saturated:
        return Z_95 / math.sqrt(LOG_SLOPE * information)
    residuals = sum((x - e) ** 2 for x, e in zip(scores, expected))
    if residuals < RESIDUAL_ZERO:  # all draws against one opponent: no spread to measure, not "no error"
        return math.inf
    return Z_95 * math.sqrt(residuals) / information


def estimate_rating(results: list[Result]) -> Rating:
    """The Elo that explains `results` (pairs of opponent Elo and score), with its 95 % bar; ValueError for no results."""
    if not results:
        raise ValueError("no results to rate")
    opponents = [float(opponent) for opponent, _ in results]
    scores = [float(score) for _, score in results]
    if any(not 0.0 <= score <= 1.0 for score in scores):
        raise ValueError("a score must be between 0 and 1")

    games = len(scores)
    total = sum(scores)
    bound = BOUND_NONE
    if total == games:
        bound = BOUND_LOWER
    elif total == 0.0:
        bound = BOUND_UPPER
    saturated = bound != BOUND_NONE
    if saturated:
        adjusted = (games - SATURATION_SHORTFALL) / games if bound == BOUND_LOWER else SATURATION_SHORTFALL / games
        scores = [adjusted] * games

    rating, expected = _fit(opponents, scores)
    return Rating(rating, _error(scores, expected, saturated), saturated, games, bound)


def format_rating(rating: Rating) -> str:
    """`1523 +/- 87` (whole Elo), or `>= 1810 (saturated)` / `<= 1250 (saturated)` for a bound. ASCII only."""
    elo = round(rating.elo)
    if rating.saturated:
        return f"{rating.bound} {elo} (saturated)"
    error = "?" if math.isinf(rating.error) else str(round(rating.error))
    return f"{elo} +/- {error}"
