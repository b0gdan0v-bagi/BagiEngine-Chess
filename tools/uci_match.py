#!/usr/bin/env python3
"""Measures the strength of the own engine: plays `BagiChessUci` against another UCI engine (Stockfish) at fixed Elo steps, with the
game as referee, and prints an Elo with a 95 % error bar for every step of the own engine. Standard library only.

    python tools/uci_match.py --referee build/ClangmacOS/BagiChess --own build/ClangmacOS/BagiChessUci --opponent /usr/local/bin/stockfish
    python tools/uci_match.py --referee ... --own ... --own-step hard --own-step depth=4,blunder=10 --opponent-elo 1500 --opponent-elo 1800 --games 10 --out match.json
    python tools/uci_match.py --referee build/ClangmacOS/BagiChess --verify-openings

A match is a set of pairs: one step of the own engine (a level `easy|medium|hard|max`, or `depth=N[,blunder=P]`) against one Elo step of
the opponent (`UCI_LimitStrength` and `UCI_Elo`), `--games` games per pair, every opening from both sides. The referee is `BagiChess`
started headless on the manual clock (or a game already running with --control, `--port`): the game knows repetition, the 50-move rule and
insufficient material, which the engines do not report, and its list of legal moves checks every move of both engines. A game that reaches
--max-plies is a draw. The opponent thinks `--opponent-movetime` ms per move; the own engine plays as the game plays it (the depth of its
level or of its `depth=` step, no time limit).

The rating of a step is the Elo that scores as it did against the opponent's Elo steps (`elo_rating.py`: the 95 % bar is 1.96 standard
errors; a step that won, or lost, every game is only a bound, marked `saturated`). It is **an Elo on Stockfish's `UCI_Elo` scale at the
chosen movetime**: not a CCRL rating and not a rating of a person; it moves with the Stockfish version, the machine and the build type
(a Debug build is slow, measure with Release). `--out` is rewritten after every finished pair.

The opponent is looked for in --opponent, the environment variable STOCKFISH, then PATH. With `--port` (no `--referee`) the tool joins a game
that runs with `--control`; that game's opponent must play nobody (the start of every game does; clear it with
`set_state {"opponent": {"plays": null}}` otherwise), or the program would move for a side of the match.
Exit code: 0 done, 1 a failure (a refused move, an engine that died or did not answer, an illegal engine move, a bad value of an argument or
of a step), 2 a command line that argparse cannot read (an unknown option, `--games x`), 77 no opponent engine found. A match that stops
early still writes what was played to `--out`, with `"stopped_early": true` among the settings.
"""

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, TextIO

TOOLS_DIR = Path(__file__).resolve().parent
ENGINE_CONTROL_DIR = TOOLS_DIR.parent / "engine" / "tools" / "control"
for _directory in (ENGINE_CONTROL_DIR, TOOLS_DIR):
    if str(_directory) not in sys.path:
        sys.path.insert(0, str(_directory))

from bagi_control_client import DEFAULT_HOST, DEFAULT_PORT, BagiControlClient, ConnectionClosed, ControlError  # noqa: E402
from game_process import DEFAULT_SHUTDOWN_TIMEOUT, DEFAULT_STARTUP_TIMEOUT, GameLaunchError, GameProcess, build_game_command, free_port  # noqa: E402
from match_steps import (  # noqa: E402
    COLOR_BLACK,
    COLOR_WHITE,
    DEFAULT_OWN_STEPS,
    OPENINGS,
    OPTION_DEPTH,
    OPTION_LEVEL,
    RESULT_DRAW,
    RESULT_LOSS,
    RESULT_WIN,
    TERMINATION_MAX_PLIES,
    GameRecord,
    OwnStep,
    PairRecord,
    format_table,
    parse_elo_steps,
    parse_own_step,
    schedule,
    to_json,
    validate_games,
)
from stockfish_bridge import (  # noqa: E402
    EXIT_FAILED,
    EXIT_PASSED,
    EXIT_SKIPPED,
    MANUAL_CLOCK_STEP_FRAMES,
    STATUS_ONGOING,
    UCI_ANSWER_SLACK_SECONDS,
    BridgeError,
    UciEngine,
    action_moves,
    find_stockfish,
    parse_options,
    position_command,
)

DEFAULT_GAMES = 20
DEFAULT_MAX_PLIES = 300
DEFAULT_OPPONENT_MOVETIME_MS = 100
DEFAULT_OWN_TIMEOUT_SECONDS = 120.0  # how long the own engine gets for one move (a Debug build at a high level is slow)
DEFAULT_OPPONENT_ELO_OPTION = "UCI_Elo"
DEFAULT_OPPONENT_OPTIONS = {"UCI_LimitStrength": "true"}
# The owner picks the steps near the expected strength of the own engine; these three bracket a program that is neither very weak nor strong.
DEFAULT_OPPONENT_ELOS = ("1400", "1700", "2000")

STATUS_CHECKMATE = "checkmate"
STATUS_STALEMATE = "stalemate"
STATUS_DRAW_PREFIX = "draw_"
UCI_REFUSED_MARKER = "refused"  # the server says `info string option refused: ...` and changes nothing

_ELO_RANGE = re.compile(r"^option name (?P<name>.+?) type spin default (?P<default>-?\d+) min (?P<min>-?\d+) max (?P<max>-?\d+)\s*$")
_ID_NAME = re.compile(r"^id name (.+)$")


class MatchError(BridgeError):
    """The match cannot go on: the game refused a move, an engine played an illegal move or had none. The message says what happened."""


@dataclass
class MatchConfig:
    """What the games of a match have in common."""

    games: int = DEFAULT_GAMES  # per pair
    max_plies: int = DEFAULT_MAX_PLIES
    opponent_movetime_ms: int = DEFAULT_OPPONENT_MOVETIME_MS
    own_timeout_seconds: float = DEFAULT_OWN_TIMEOUT_SECONDS
    opponent_elo_option: str = DEFAULT_OPPONENT_ELO_OPTION
    opponent_options: dict[str, str] = field(default_factory=lambda: dict(DEFAULT_OPPONENT_OPTIONS))
    openings: tuple[tuple[str, ...], ...] = OPENINGS
    step_frames: int | None = None  # frames to run after each move; None: 1 on the manual clock, 0 on the real one


def parse_elo_range(uci_lines: list[str], option_name: str) -> tuple[int, int] | None:
    """(min, max) of the spin option `option_name` from the reply to `uci`; None when the engine has no such option."""
    for line in uci_lines:
        found = _ELO_RANGE.match(line.strip())
        if found and found.group("name").lower() == option_name.lower():
            return int(found.group("min")), int(found.group("max"))
    return None


def engine_name(uci_lines: list[str]) -> str:
    """The `id name` of the engine, or an empty string."""
    for line in uci_lines:
        found = _ID_NAME.match(line.strip())
        if found:
            return found.group(1)
    return ""


def check_elo_steps(elos: list[int], elo_range: tuple[int, int] | None, option_name: str) -> None:
    """ValueError when a step is outside the range the opponent allows; an engine with no range (no such option) takes any."""
    if elo_range is None:
        return
    low, high = elo_range
    outside = [elo for elo in elos if not low <= elo <= high]
    if outside:
        raise ValueError(f"opponent Elo {outside} is outside {low}..{high}, the range of the option {option_name} of the opponent")


def check_own_step(step: OwnStep) -> None:
    """ValueError for a step that would end in a `go` with no depth (that runs the deepening driver until `stop`)."""
    options = step.options
    has_depth = step.go_depth is not None or OPTION_LEVEL in options or int(options.get(OPTION_DEPTH, "0")) > 0
    if not has_depth:
        raise ValueError(f"own step {step.label!r} has no depth: a `go` without one would never end")


def parse_own_steps(texts: list[str]) -> list[OwnStep]:
    """The own steps in the order given; ValueError for a bad text and for two texts that mean the same options."""
    steps: list[OwnStep] = []
    for text in texts:
        step = parse_own_step(text)
        check_own_step(step)
        for other in steps:
            if other.options == step.options:
                raise ValueError(f"own steps {other.label!r} and {step.label!r} are the same")
        steps.append(step)
    if not steps:
        raise ValueError("at least one own step is needed")
    return steps


def set_options(engine: UciEngine, options: dict[str, str], who: str) -> None:
    """Sets the options one by one; MatchError when the engine says `info string ... refused`. An option it does not know is not checked."""
    for name, value in options.items():
        for line in engine.set_option(name, value):
            if line.startswith("info") and UCI_REFUSED_MARKER in line:
                raise MatchError(f"the {who} engine refused 'setoption name {name} value {value}': {line}")


def result_of(status: str, winner: str | None, own_color: str) -> tuple[str, str]:
    """(result from the own engine's side, termination) of a finished game."""
    if status == STATUS_CHECKMATE:
        if winner not in (COLOR_WHITE, COLOR_BLACK):
            raise MatchError(f"the game is {status} but names no winner ({winner!r})")
        return (RESULT_WIN if winner == own_color else RESULT_LOSS), status
    if status == STATUS_STALEMATE or status.startswith(STATUS_DRAW_PREFIX):
        return RESULT_DRAW, status
    raise MatchError(f"the game ended with the unknown status {status!r}")


def play_opening(client: BagiControlClient, opening: tuple[str, ...], step_frames: int = 0) -> None:
    """Resets the game and plays `opening` through the referee; MatchError names the move the game refused."""
    client.call("reset")
    for index, move in enumerate(opening):
        try:
            client.call("perform_action", {"type": "move", "uci": move})
        except ControlError as error:
            raise MatchError(f"the game refused the opening move {move} (ply {index + 1} of {' '.join(opening)}): {error}") from error
        if step_frames > 0:
            client.call("step", {"frames": step_frames})


def play_game(
    client: BagiControlClient,
    own: UciEngine,
    opponent: UciEngine,
    own_go: str,
    own_plays_white: bool,
    opening: tuple[str, ...],
    config: MatchConfig,
    log: TextIO | None = None,
) -> GameRecord:
    """One game on the referee: the opening, then each engine on its turn until the game ends or `max_plies` plies stand.

    `own_go` is the arguments of the own engine's `go` ("depth 3", or "" when its options give the depth). The game's own
    `status` ends the game; a move that is not in its list of actions, or no move while it lists some, is a MatchError.
    """
    step_frames = config.step_frames
    if step_frames is None:
        step_frames = MANUAL_CLOCK_STEP_FRAMES if client.call("status").get("manualClock") else 0
    own_color = COLOR_WHITE if own_plays_white else COLOR_BLACK
    own.new_game()
    opponent.new_game()
    play_opening(client, opening, step_frames)

    opponent_go = f"movetime {config.opponent_movetime_ms}"
    opponent_timeout = config.opponent_movetime_ms / 1000 + UCI_ANSWER_SLACK_SECONDS
    while True:
        state = client.call("get_state")["state"]
        moves = list(state["moves"])
        if state["status"] != STATUS_ONGOING:
            result, termination = result_of(state["status"], state["winner"], own_color)
            break
        if len(moves) >= config.max_plies:
            result, termination = RESULT_DRAW, TERMINATION_MAX_PLIES
            break

        turn = state["turn"]
        owns_move = (turn == COLOR_WHITE) == own_plays_white
        actions = action_moves(client)
        position = position_command(state)
        if owns_move:
            move = own.search(position, own_go, config.own_timeout_seconds)
        else:
            move = opponent.search(position, opponent_go, opponent_timeout)
        who = "the own engine" if owns_move else "the opponent"
        if move is None:
            raise MatchError(f"{who} has no move for {turn} in {state['fen']}, but the game lists {len(actions)}")
        if move not in actions:
            raise MatchError(f"{who} played {move} for {turn} in {state['fen']}, which is not among the {len(actions)} actions of the game: {sorted(actions)}")
        try:
            client.call("perform_action", {"type": "move", "uci": move})
        except ControlError as error:
            raise MatchError(f"the game refused {move} for {turn} in {state['fen']}: {error}") from error
        if log is not None:
            print(f"{len(moves) + 1:3d}. {turn:5s} {move}", file=log, flush=True)
        if step_frames > 0:
            client.call("step", {"frames": step_frames})

    return GameRecord(opening, own_color, result, termination, len(moves), moves)


def own_go_arguments(step: OwnStep) -> str:
    return "" if step.go_depth is None else f"depth {step.go_depth}"


def play_pair(
    client: BagiControlClient,
    own: UciEngine,
    opponent: UciEngine,
    step: OwnStep,
    opponent_elo: int,
    config: MatchConfig,
    pair: PairRecord,
    log: TextIO | None = None,
) -> None:
    """Plays `config.games` games of `step` against `opponent_elo`, adding each finished game to `pair` (so a failure keeps the earlier ones)."""
    set_options(own, step.options, "own")
    set_options(opponent, {**config.opponent_options, config.opponent_elo_option: str(opponent_elo)}, "opponent")
    for game_index in range(config.games):
        opening, own_plays_white = schedule(game_index, config.openings)
        pair.games.append(play_game(client, own, opponent, own_go_arguments(step), own_plays_white, opening, config, log))


def play_match(
    client: BagiControlClient,
    own: UciEngine,
    opponent: UciEngine,
    steps: list[OwnStep],
    elos: list[int],
    config: MatchConfig,
    pairs: list[PairRecord],
    on_pair: Callable[[PairRecord], None] | None = None,
    log: TextIO | None = None,
) -> None:
    """Every own step against every opponent Elo, step by step. `pairs` is filled in place (a pair joins it before its first game);
    `on_pair` is called after each finished pair."""
    for step in steps:
        for elo in elos:
            pair = PairRecord(step.label, elo)
            pairs.append(pair)
            play_pair(client, own, opponent, step, elo, config, pair, log)
            if on_pair is not None:
                on_pair(pair)


def play_and_record(
    client: BagiControlClient,
    own: UciEngine,
    opponent: UciEngine,
    steps: list[OwnStep],
    elos: list[int],
    config: MatchConfig,
    settings: dict,
    out: Path | None,
    pairs: list[PairRecord],
    log: TextIO | None = None,
) -> None:
    """The match with its record, `pairs` filled in place: `out` is rewritten after every finished pair and a progress line goes to
    stderr. When the match does not finish (an error, an interrupt) the games played so far are written with `"stopped_early": true`
    and the error goes on; with no game played there is nothing to write."""
    total = len(steps) * len(elos)

    def finished(pair: PairRecord) -> None:
        if out is not None:
            write_record(out, settings, pairs)
        fraction = pair.score_fraction() or 0.0
        print(f"[{len(pairs)}/{total}] {pair.own_label} vs {pair.opponent_elo}: +{pair.wins} ={pair.draws} -{pair.losses} ({fraction * 100:.0f} %)", file=sys.stderr, flush=True)

    completed = False
    try:
        play_match(client, own, opponent, steps, elos, config, pairs, finished, log)
        completed = True
    finally:
        played = [pair for pair in pairs if pair.games]  # the pair the failure interrupted may have no game yet
        if not completed and out is not None and played:
            write_record(out, {**settings, "stopped_early": True}, played)


def verify_openings(client: BagiControlClient, openings: tuple[tuple[str, ...], ...], out: TextIO) -> bool:
    """Plays each opening once on the referee; prints `ok` per line or the line the game refused. True when all were taken."""
    allowed = True
    for opening in openings:
        try:
            play_opening(client, opening)
        except MatchError as error:
            print(f"refused: {error}", file=out)
            allowed = False
            continue
        print(f"ok  {' '.join(opening)}", file=out)
    client.call("reset")
    return allowed


def write_record(path: Path, settings: dict, pairs: list[PairRecord]) -> None:
    """Writes the record next to `path` and moves it into place, so that a reader never sees half of it."""
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(to_json(settings, pairs), indent=1) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--referee", type=Path, help="the BagiChess executable, started headless on the manual clock (default: join a game running with --control, see --port)")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"control port of a running game (default {DEFAULT_PORT}); with --referee a free one is used")
    parser.add_argument("--own", type=Path, help="the BagiChessUci executable (the engine under test)")
    parser.add_argument("--own-step", action="append", default=[], metavar="STEP", help="a strength of the own engine: easy, medium, hard, max, or depth=N[,blunder=P] (repeatable; default: the four levels)")
    parser.add_argument("--opponent", help="the opponent engine, Stockfish (default: $STOCKFISH, then PATH)")
    parser.add_argument("--opponent-elo", action="append", default=[], metavar="N", help=f"an Elo step of the opponent (repeatable; default {' '.join(DEFAULT_OPPONENT_ELOS)}); choose steps near the expected strength")
    parser.add_argument("--opponent-elo-option", default=DEFAULT_OPPONENT_ELO_OPTION, metavar="NAME", help=f"the UCI option that holds the Elo step (default {DEFAULT_OPPONENT_ELO_OPTION})")
    parser.add_argument("--opponent-option", action="append", default=None, metavar="NAME=VALUE", help="a UCI option of the opponent, set before each pair (repeatable; default UCI_LimitStrength=true; giving any replaces the default)")
    parser.add_argument("--opponent-movetime", type=int, default=DEFAULT_OPPONENT_MOVETIME_MS, metavar="MS", help=f"milliseconds the opponent thinks per move (default {DEFAULT_OPPONENT_MOVETIME_MS})")
    parser.add_argument("--own-timeout", type=float, default=DEFAULT_OWN_TIMEOUT_SECONDS, metavar="SECONDS", help=f"seconds the own engine gets for one move (default {DEFAULT_OWN_TIMEOUT_SECONDS:g})")
    parser.add_argument("--games", type=int, default=DEFAULT_GAMES, help=f"games per pair, even (default {DEFAULT_GAMES})")
    parser.add_argument("--max-plies", type=int, default=DEFAULT_MAX_PLIES, help=f"a game that reaches this many plies is a draw (default {DEFAULT_MAX_PLIES})")
    parser.add_argument("--out", type=Path, help="write the record (settings, every game, the summary) to this JSON file after every finished pair")
    parser.add_argument("--trace", action="store_true", help="print the UCI conversation to stderr")
    parser.add_argument("--verify-openings", action="store_true", help="play every opening once on the referee and exit (needs only --referee)")
    return parser


def fail(message: str) -> int:
    print(message, file=sys.stderr)
    return EXIT_FAILED


def run(args: argparse.Namespace) -> int:
    try:
        validate_games(args.games)
        steps = parse_own_steps(args.own_step or list(DEFAULT_OWN_STEPS))
        elos = parse_elo_steps(args.opponent_elo or list(DEFAULT_OPPONENT_ELOS))
        opponent_options = parse_options(args.opponent_option) if args.opponent_option is not None else dict(DEFAULT_OPPONENT_OPTIONS)
    except (ValueError, argparse.ArgumentTypeError) as error:
        return fail(str(error))
    if args.max_plies <= 0 or args.opponent_movetime <= 0 or args.own_timeout <= 0:
        return fail("--max-plies, --opponent-movetime and --own-timeout must be positive")

    opponent_path = None
    if not args.verify_openings:
        opponent_path = find_stockfish(args.opponent)
        if opponent_path is None:
            print("no opponent engine: Stockfish is not installed (or --opponent / $STOCKFISH points nowhere): skipped", file=sys.stderr)
            return EXIT_SKIPPED
        opponent_path = str(Path(opponent_path))  # native separators: Windows does not start 'dir/engine.exe' from a command line
        if args.own is None:
            return fail("--own (the BagiChessUci executable) is required")
        if not args.own.is_file():
            return fail(f"there is no own engine at {args.own}")
    if args.referee is not None and not args.referee.is_file():
        return fail(f"there is no game executable at {args.referee}")
    if args.out is not None and not args.out.resolve().parent.is_dir():
        return fail(f"the folder of --out {args.out} does not exist")

    config = MatchConfig(
        games=args.games,
        max_plies=args.max_plies,
        opponent_movetime_ms=args.opponent_movetime,
        own_timeout_seconds=args.own_timeout,
        opponent_elo_option=args.opponent_elo_option,
        opponent_options=opponent_options,
    )
    trace = sys.stderr if args.trace else None
    pairs: list[PairRecord] = []
    settings: dict = {}
    game: GameProcess | None = None
    client: BagiControlClient | None = None
    own: UciEngine | None = None
    opponent: UciEngine | None = None
    try:
        if args.referee is not None:
            exe = args.referee.resolve()
            port = free_port()
            game = GameProcess(build_game_command(exe, port, headless=True, manual_clock=True), cwd=exe.parent)
            game.start()
            client = game.connect(port, DEFAULT_STARTUP_TIMEOUT)
        else:
            client = BagiControlClient(host=args.host, port=args.port)
            client.connect()

        if args.verify_openings:
            return EXIT_PASSED if verify_openings(client, config.openings, sys.stdout) else EXIT_FAILED

        own = UciEngine([str(args.own)], trace=trace)
        opponent = UciEngine(opponent_path, trace=trace)
        elo_range = parse_elo_range(opponent.uci_lines, config.opponent_elo_option)
        if elo_range is None:
            print(f"the opponent has no option {config.opponent_elo_option}; it is set anyway", file=sys.stderr)
        try:
            check_elo_steps(elos, elo_range, config.opponent_elo_option)
        except ValueError as error:
            return fail(str(error))

        settings = {
            "referee": str(args.referee) if args.referee is not None else f"{args.host}:{args.port}",
            "own": str(args.own),
            "own_name": engine_name(own.uci_lines),
            "opponent": str(opponent_path),
            "opponent_name": engine_name(opponent.uci_lines),
            "opponent_elo_option": config.opponent_elo_option,
            "opponent_options": config.opponent_options,
            "opponent_movetime_ms": config.opponent_movetime_ms,
            "own_steps": [step.label for step in steps],
            "opponent_elos": elos,
            "games_per_pair": config.games,
            "max_plies": config.max_plies,
        }
        play_and_record(client, own, opponent, steps, elos, config, settings, args.out, pairs, trace)
        print(format_table(pairs))
        return EXIT_PASSED
    except (BridgeError, ControlError, ConnectionClosed, GameLaunchError, OSError) as error:
        print(f"failed: {error}", file=sys.stderr)
        if game is not None:
            print(f"--- game log (tail) ---\n{game.log_tail()}", file=sys.stderr)
        if pairs:
            print(format_table(pairs), file=sys.stderr)
        return EXIT_FAILED
    finally:
        # the cleanup order of the bridge: the engines first, then the game
        for engine in (own, opponent):
            if engine is not None:
                engine.close()
        if client is not None:
            if game is not None:
                try:
                    client.call("quit")
                except (ControlError, ConnectionClosed, OSError):
                    pass
            client.close()
        if game is not None:
            game.stop(DEFAULT_SHUTDOWN_TIMEOUT)


def main(argv: list[str] | None = None) -> int:
    return run(build_parser().parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())
