"""Everything about a UCI match that needs no process: the steps, the openings, the schedule, the record and the report.

Used by `tools/uci_match.py` (the runner); standard library and `elo_rating` only, no process, no I/O.

    step = parse_own_step("hard")                      # OwnStep(label="hard", options={"Level": "hard"}, go_depth=None)
    step = parse_own_step("depth=2,blunder=15")        # options {"Depth": "2", "BlunderPercent": "15"}, go_depth 2
    opening, own_plays_white = schedule(game_index, OPENINGS)

An own step is what the engine under test (`BagiChessUci`) is set to before a pair of games: a level id, or an explicit depth with an
optional share of blundered moves. `go_depth` is the depth to put in `go depth N`; it is None for a level, where the depth comes from
the option `Level` (so the level is played as the game plays it). A depth step sets both options, because the server keeps its options
from one step to the next.

The record is `PairRecord` (own step x opponent Elo) with its `GameRecord`s. `to_json` makes the file written by `--out`:

    {"settings": {...},
     "pairs":   [{"own_step", "opponent_elo", "wins", "draws", "losses", "score", "rating": {"elo", "error", "saturated", "bound", "games"},
                  "games": [{"opening": [uci, ...], "own_color", "result", "termination", "plies", "moves": [uci, ...]}]}],
     "summary": {"<own step>": {"elo", "error", "saturated", "bound", "games"}}}

`error` is the half width of the 95 % bar and `null` when unknown (`inf` in Python); `bound` is `">="`, `"<="` or `""` and `saturated`
says that `elo` is a bound; `result` is from the own engine's side. The Elo is on the scale of the opponent's numbers (see
`elo_rating`).
"""

import math
import os
import re
from dataclasses import dataclass, field

from elo_rating import Rating, estimate_rating, format_rating

# The ids of the levels of the game, as `GetOpponentLevels()` (src/Chess/Engine/OpponentLevels.cpp) has them and as the option `Level` of
# `BagiChessUci` takes them. The tool needs the ids only; the depth and the blunder share of a level are the server's.
LEVEL_IDS = ("easy", "medium", "hard", "max")
DEFAULT_OWN_STEPS = LEVEL_IDS

DEPTH_MIN = 1  # `Depth` 0 is "no limit from the option"; a step must have a depth
DEPTH_MAX = 64  # the maximum of the option `Depth` (kMaxSearchDepth)
BLUNDER_MIN = 0
BLUNDER_MAX = 100

OPTION_LEVEL = "Level"
OPTION_DEPTH = "Depth"
OPTION_BLUNDER = "BlunderPercent"
KEY_DEPTH = "depth"
KEY_BLUNDER = "blunder"

# Well-known, sound lines of 4 plies in UCI moves. The own engine is deterministic, so without openings every game of a pair would be
# the same. `uci_match.py --verify-openings` plays every line through the game, which refuses an illegal one.
OPENINGS: tuple[tuple[str, ...], ...] = (
    ("e2e4", "e7e5", "g1f3", "b8c6"),  # Italian / Spanish setup
    ("e2e4", "e7e5", "g1f3", "g8f6"),  # Petrov
    ("e2e4", "c7c5", "g1f3", "d7d6"),  # Sicilian, Najdorf setup
    ("e2e4", "e7e6", "d2d4", "d7d5"),  # French
    ("e2e4", "c7c6", "d2d4", "d7d5"),  # Caro-Kann
    ("d2d4", "d7d5", "c2c4", "e7e6"),  # Queen's Gambit Declined
    ("d2d4", "d7d5", "g1f3", "g8f6"),  # Queen's pawn, Nf3
    ("d2d4", "g8f6", "c2c4", "e7e6"),  # Nimzo / Queen's Indian setup
    ("d2d4", "g8f6", "c2c4", "g7g6"),  # King's Indian setup
    ("c2c4", "e7e5", "b1c3", "g8f6"),  # English
    ("g1f3", "d7d5", "g2g3", "g8f6"),  # Reti / King's Indian Attack
    ("g1f3", "g8f6", "c2c4", "g7g6"),  # Reti, fianchetto
)

RESULT_WIN = "win"
RESULT_DRAW = "draw"
RESULT_LOSS = "loss"
RESULT_SCORES = {RESULT_WIN: 1.0, RESULT_DRAW: 0.5, RESULT_LOSS: 0.0}
COLOR_WHITE = "white"
COLOR_BLACK = "black"
TERMINATION_MAX_PLIES = "max_plies"  # the other terminations are the `status` of the game (checkmate, stalemate, draw_*)

_NUMBER = re.compile(r"[0-9]+")  # `int()` alone also takes "+3", "-3", " 3", "3_0" and non-ASCII digits


@dataclass(frozen=True)
class OwnStep:
    """One strength of the engine under test: the options set before a pair and the depth of `go` (None: it comes from the options)."""

    label: str
    options: dict[str, str]
    go_depth: int | None


def _whole_number(text: str, what: str, step: str, low: int, high: int) -> int:
    if not _NUMBER.fullmatch(text):
        raise ValueError(f"bad step {step!r}: {what} must be a whole number, got {text!r}")
    value = int(text)
    if not low <= value <= high:
        raise ValueError(f"bad step {step!r}: {what} must be {low}..{high}, got {value}")
    return value


def parse_own_step(text: str) -> OwnStep:
    """A level id (`easy|medium|hard|max`, any case) or `depth=N[,blunder=P]` (N 1..64, P 0..100); ValueError names the text."""
    label = text.strip().lower()
    if label in LEVEL_IDS:
        return OwnStep(label, {OPTION_LEVEL: label}, None)

    values: dict[str, str] = {}
    for part in label.split(","):
        key, separator, value = part.partition("=")
        key, value = key.strip(), value.strip()
        if not separator or key not in (KEY_DEPTH, KEY_BLUNDER):
            raise ValueError(
                f"bad step {text!r}: expected a level ({', '.join(LEVEL_IDS)}) or {KEY_DEPTH}=N[,{KEY_BLUNDER}=P]"
            )
        if key in values:
            raise ValueError(f"bad step {text!r}: {key} is given twice")
        values[key] = value
    if KEY_DEPTH not in values:
        raise ValueError(f"bad step {text!r}: a step with {KEY_BLUNDER} needs {KEY_DEPTH}=N too")

    depth = _whole_number(values[KEY_DEPTH], KEY_DEPTH, text, DEPTH_MIN, DEPTH_MAX)
    blunder = _whole_number(values.get(KEY_BLUNDER, "0"), KEY_BLUNDER, text, BLUNDER_MIN, BLUNDER_MAX)
    # Both options always: the server keeps its options between steps, so an omitted share would be the one of the step before.
    return OwnStep(label, {OPTION_DEPTH: str(depth), OPTION_BLUNDER: str(blunder)}, depth)


def parse_elo_steps(texts: list[str]) -> list[int]:
    """The opponent Elo steps: whole positive numbers, no duplicates, at least one; returned sorted."""
    if not texts:
        raise ValueError("at least one opponent Elo is needed")
    steps = []
    for text in texts:
        text = str(text).strip()
        if not _NUMBER.fullmatch(text) or int(text) <= 0:
            raise ValueError(f"bad opponent Elo {text!r}: expected a positive whole number")
        steps.append(int(text))
    if len(set(steps)) != len(steps):
        raise ValueError(f"an opponent Elo is given twice: {sorted(steps)}")
    return sorted(steps)


def validate_games(games: int) -> int:
    """The number of games per pair: positive and even (each opening is played from both sides); ValueError otherwise."""
    if games <= 0 or games % 2 != 0:
        raise ValueError(f"--games must be a positive even number (each opening is played from both sides), got {games}")
    return games


def schedule(game_index: int, openings: tuple[tuple[str, ...], ...] = OPENINGS) -> tuple[tuple[str, ...], bool]:
    """The opening and whether the own engine has White in game `game_index` of a pair: openings in turn, each twice, colors alternate."""
    if game_index < 0:
        raise ValueError(f"game index must not be negative, got {game_index}")
    if not openings:
        raise ValueError("no openings")
    return openings[(game_index // 2) % len(openings)], game_index % 2 == 0


@dataclass
class GameRecord:
    """One finished game; `result` is from the own engine's side, `moves` are the UCI moves of the whole game, the opening included."""

    opening: tuple[str, ...]
    own_color: str
    result: str
    termination: str
    plies: int
    moves: list[str] = field(default_factory=list)

    def __post_init__(self):
        if self.result not in RESULT_SCORES:
            raise ValueError(f"result must be one of {sorted(RESULT_SCORES)}, got {self.result!r}")
        if self.own_color not in (COLOR_WHITE, COLOR_BLACK):
            raise ValueError(f"own color must be {COLOR_WHITE} or {COLOR_BLACK}, got {self.own_color!r}")

    def score(self) -> float:
        return RESULT_SCORES[self.result]


@dataclass
class PairRecord:
    """The games of one own step against one opponent Elo."""

    own_label: str
    opponent_elo: int
    games: list[GameRecord] = field(default_factory=list)

    @property
    def wins(self) -> int:
        return sum(1 for game in self.games if game.result == RESULT_WIN)

    @property
    def draws(self) -> int:
        return sum(1 for game in self.games if game.result == RESULT_DRAW)

    @property
    def losses(self) -> int:
        return sum(1 for game in self.games if game.result == RESULT_LOSS)

    def scores(self) -> list[float]:
        return [game.score() for game in self.games]

    def score_fraction(self) -> float | None:
        """The points won per game (0..1); None for a pair with no games."""
        return sum(self.scores()) / len(self.games) if self.games else None

    def rating(self) -> Rating | None:
        """The Elo of this pair alone (one opponent Elo); None for a pair with no games."""
        return estimate_rating([(self.opponent_elo, score) for score in self.scores()]) if self.games else None


def summarize(pairs: list[PairRecord]) -> dict[str, Rating]:
    """One rating per own step, over all its pairs (every game against its own opponent Elo); in the order of first appearance."""
    results: dict[str, list[tuple[float, float]]] = {}
    for pair in pairs:
        results.setdefault(pair.own_label, []).extend((pair.opponent_elo, score) for score in pair.scores())
    return {label: estimate_rating(scores) for label, scores in results.items() if scores}


def _plain(value):
    """`value` made of lists, dicts, numbers and strings only: tuples become lists, non-finite floats None, paths strings."""
    if isinstance(value, dict):
        return {str(key): _plain(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [_plain(item) for item in value]
    if isinstance(value, float):
        return value if math.isfinite(value) else None
    if value is None or isinstance(value, (bool, int, str)):
        return value
    if isinstance(value, os.PathLike):
        return os.fspath(value)
    raise TypeError(f"cannot put {type(value).__name__} into the record")


def _rating_json(rating: Rating | None) -> dict | None:
    if rating is None:
        return None
    return _plain(
        {
            "elo": rating.elo,
            "error": rating.error,
            "saturated": rating.saturated,
            "bound": rating.bound,
            "games": rating.games,
        }
    )


def to_json(settings: dict, pairs: list[PairRecord]) -> dict:
    """The record of a match as a dict for `json.dump`: the settings, every pair with its games, and the summary per own step."""
    return {
        "settings": _plain(settings),
        "pairs": [
            {
                "own_step": pair.own_label,
                "opponent_elo": pair.opponent_elo,
                "wins": pair.wins,
                "draws": pair.draws,
                "losses": pair.losses,
                "score": _plain(pair.score_fraction()),
                "rating": _rating_json(pair.rating()),
                "games": [
                    {
                        "opening": list(game.opening),
                        "own_color": game.own_color,
                        "result": game.result,
                        "termination": game.termination,
                        "plies": game.plies,
                        "moves": list(game.moves),
                    }
                    for game in pair.games
                ],
            }
            for pair in pairs
        ],
        "summary": {label: _rating_json(rating) for label, rating in summarize(pairs).items()},
    }


def format_table(pairs: list[PairRecord]) -> str:
    """A plain ASCII table: one row per pair (step, opponent Elo, W/D/L, score, Elo of the pair), then one line per step."""
    header = ("step", "opp Elo", "W/D/L", "score", "Elo")
    rows = []
    for pair in pairs:
        fraction = pair.score_fraction()
        rating = pair.rating()
        rows.append(
            (
                pair.own_label,
                str(pair.opponent_elo),
                f"{pair.wins}/{pair.draws}/{pair.losses}",
                "-" if fraction is None else f"{fraction * 100:.1f} %",
                "-" if rating is None else format_rating(rating),
            )
        )
    widths = [max(len(row[column]) for row in [header, *rows]) for column in range(len(header))]

    def line(cells) -> str:
        return "  ".join(cell.ljust(width) for cell, width in zip(cells, widths)).rstrip()

    lines = [line(header), line(["-" * width for width in widths]), *(line(row) for row in rows)]
    summary = summarize(pairs)
    if summary:
        lines.append("")
        label_width = max(len(label) for label in summary)
        for label, rating in summary.items():
            lines.append(f"{label.ljust(label_width)}  Elo {format_rating(rating)}  ({rating.games} games)")
    return "\n".join(lines)
