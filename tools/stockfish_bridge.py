#!/usr/bin/env python3
"""Plays a Chess game of the engine against Stockfish: reads the position over the control API, asks Stockfish over UCI and
sends the move back. Stockfish is a separate process, never linked into the engine. Standard library only.

    python tools/stockfish_bridge.py --exe build/macos-debug/bin/BagiChess --side both          # Stockfish plays both sides
    python tools/stockfish_bridge.py --side white                                          # a game already running with --control
    python tools/stockfish_bridge.py --exe build/macos-debug/bin/BagiChess --side white --opponent random --cross-check

--side picks the sides Stockfish plays. The other side is a person at the window (waited for) or, with --opponent random, a
random legal move. --cross-check asks Stockfish for the legal moves of every position (`go perft 1`) and fails when they differ
from the game's list_actions: a standing monitor of the rules against an independent implementation.

Stockfish is looked for in --stockfish, the environment variable STOCKFISH, then PATH.
Exit code: 0 the game ended or reached --max-plies, 1 a failure (a refused move, a rules mismatch, a lost game), 77 no Stockfish.
"""

import argparse
import os
import queue
import random
import re
import shutil
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, TextIO

from bagi_control_client import DEFAULT_HOST, DEFAULT_PORT, BagiControlClient, ConnectionClosed, ControlError
from game_process import DEFAULT_SHUTDOWN_TIMEOUT, DEFAULT_STARTUP_TIMEOUT, GameLaunchError, GameProcess, build_game_command, free_port

EXIT_PASSED = 0
EXIT_FAILED = 1
EXIT_SKIPPED = 77

DEFAULT_MOVETIME_MS = 100
DEFAULT_MAX_PLIES = 400
DEFAULT_POLL_SECONDS = 0.25
UCI_STARTUP_SECONDS = 15.0
UCI_ANSWER_SLACK_SECONDS = 15.0  # how long past movetime Stockfish gets to answer
UCI_QUIT_SECONDS = 5.0
STATUS_ONGOING = "ongoing"
SIDES = ("white", "black")
MANUAL_CLOCK_STEP_FRAMES = 1  # frames run after a move so that the scene follows it

_PERFT_MOVE = re.compile(r"^([a-h][1-8][a-h][1-8][qrbn]?): \d+$")
_PERFT_TOTAL = re.compile(r"^Nodes searched: (\d+)$")


class BridgeError(Exception):
    """The game cannot go on: a move was refused, a side has no move, Stockfish failed. The message says what happened."""


class UciError(BridgeError):
    """Stockfish did not answer as UCI says, or died."""


class RulesMismatch(BridgeError):
    """--cross-check: Stockfish and the game disagree about the legal moves of a position."""


def find_stockfish(explicit: str | None) -> str | None:
    """The Stockfish executable: --stockfish, then $STOCKFISH, then PATH; None when there is none."""
    for candidate in (explicit, os.environ.get("STOCKFISH")):
        if candidate:
            return candidate if Path(candidate).is_file() else None
    return shutil.which("stockfish")


class UciEngine:
    """A UCI engine in a subprocess. Lines are read by a thread, so that every wait has a timeout."""

    def __init__(self, path: str | list[str], options: dict[str, str] | None = None, trace: TextIO | None = None):
        """`path` is the executable, or the whole command as a list (a script run by an interpreter). `uci_lines` keeps the reply to `uci`."""
        self._trace = trace
        self._lines: queue.Queue[str | None] = queue.Queue()
        self.uci_lines: list[str] = []
        command = [path] if isinstance(path, str) else list(path)
        try:
            self._process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
        except OSError as error:
            raise UciError(f"cannot start {command[0]}: {error}") from error
        threading.Thread(target=self._read_lines, daemon=True).start()

        try:
            self._send("uci")
            self._wait_for(self._collect_uci_reply, UCI_STARTUP_SECONDS, "uciok")
            for name, value in (options or {}).items():
                self._send(f"setoption name {name} value {value}")
            self._ready()
        except UciError:
            self.close()
            raise

    def __enter__(self) -> "UciEngine":
        return self

    def __exit__(self, *exc_info: object) -> None:
        self.close()

    def new_game(self) -> None:
        self._send("ucinewgame")
        self._ready()

    def best_move(self, position: str, movetime_ms: int) -> str | None:
        """The UCI move Stockfish picks for `position` (a `position ...` command), or None when it has none (mate, stalemate)."""
        return self.search(position, f"movetime {movetime_ms}", movetime_ms / 1000 + UCI_ANSWER_SLACK_SECONDS)

    def search(self, position: str, go_arguments: str, timeout_seconds: float) -> str | None:
        """Sends `position` and `go <go_arguments>`; the UCI move of the `bestmove` line, or None for `(none)` / `0000`."""
        self._send(position)
        self._send(f"go {go_arguments}".strip())
        line = self._wait_for(lambda text: text.startswith("bestmove"), timeout_seconds, "bestmove")
        fields = line.split()
        move = fields[1] if len(fields) > 1 else "(none)"
        return None if move in ("(none)", "0000") else move

    def set_option(self, name: str, value: str) -> list[str]:
        """Sends one `setoption` and waits for the engine to be ready; gives the lines it said on the way (`info string` refusals)."""
        self._send(f"setoption name {name} value {value}")
        self._send("isready")
        said: list[str] = []

        def collect(line: str) -> bool:
            said.append(line)
            return line == "readyok"

        self._wait_for(collect, UCI_STARTUP_SECONDS, "readyok")
        return said[:-1]

    def legal_moves(self, position: str) -> set[str]:
        """The legal moves of `position` as Stockfish counts them (`go perft 1`)."""
        self._send(position)
        self._send("go perft 1")
        moves: set[str] = set()

        def collect(line: str) -> bool:
            found = _PERFT_MOVE.match(line)
            if found:
                moves.add(found.group(1))
            return _PERFT_TOTAL.match(line) is not None

        self._wait_for(collect, UCI_ANSWER_SLACK_SECONDS, "Nodes searched")
        return moves

    def close(self) -> None:
        process = self._process
        if process.poll() is None:
            try:
                self._send("quit")
                process.wait(timeout=UCI_QUIT_SECONDS)
            except (UciError, OSError, subprocess.TimeoutExpired):
                process.kill()
                process.wait()
        for stream in (process.stdin, process.stdout):
            if stream is not None:
                try:
                    stream.close()
                except OSError:
                    pass

    def _read_lines(self) -> None:
        assert self._process.stdout is not None
        for line in self._process.stdout:
            self._lines.put(line.strip())
        self._lines.put(None)  # the stream ended: the engine is gone

    def _send(self, command: str) -> None:
        if self._trace is not None:
            print(f"> {command}", file=self._trace)
        try:
            assert self._process.stdin is not None
            self._process.stdin.write(command + "\n")
            self._process.stdin.flush()
        except (OSError, ValueError) as error:  # the engine died (broken pipe) or was closed
            raise UciError(f"the engine does not take '{command}': {error}") from error

    def _collect_uci_reply(self, line: str) -> bool:
        self.uci_lines.append(line)
        return line == "uciok"

    def _ready(self) -> None:
        self._send("isready")
        self._wait_for(lambda line: line == "readyok", UCI_STARTUP_SECONDS, "readyok")

    def _wait_for(self, done: Callable[[str], bool], timeout: float, what: str) -> str:
        """Reads engine lines until `done(line)`; gives that line."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise UciError(f"the engine did not send '{what}' in {timeout:.1f} s")
            try:
                line = self._lines.get(timeout=remaining)
            except queue.Empty:
                raise UciError(f"the engine did not send '{what}' in {timeout:.1f} s") from None
            if line is None:
                raise UciError(f"the engine exited before it sent '{what}'")
            if self._trace is not None:
                print(f"< {line}", file=self._trace)
            if done(line):
                return line


@dataclass
class GameReport:
    plies: int = 0  # moves this run sent to the game
    status: str = STATUS_ONGOING
    winner: str | None = None
    moves: list[str] = field(default_factory=list)  # every move of the game, as the game has them
    reached_max_plies: bool = False


def position_command(state: dict) -> str:
    """The UCI `position` command for the game so far: its start position and the moves played, so Stockfish sees the repetitions."""
    command = f"position fen {state['startFen']}"
    if state["moves"]:
        command += " moves " + " ".join(state["moves"])
    return command


def action_moves(client: BagiControlClient) -> list[str]:
    return [action["uci"] for action in client.call("list_actions")["actions"]]


def check_rules(engine: UciEngine, position: str, actions: list[str]) -> None:
    """Fails when Stockfish and the game list different legal moves for `position`."""
    expected = engine.legal_moves(position)
    actual = set(actions)
    if expected != actual:
        raise RulesMismatch(
            f"the legal moves differ in {position}\n"
            f"  only the game has:      {sorted(actual - expected)}\n"
            f"  only Stockfish has:     {sorted(expected - actual)}"
        )


def wait_for_change(client: BagiControlClient, version: int, poll_seconds: float, wait_timeout: float | None) -> None:
    """Blocks until the state version moves (the opponent played, or the game was reset)."""
    deadline = None if wait_timeout is None else time.monotonic() + wait_timeout
    while client.call("get_state")["version"] == version:
        if deadline is not None and time.monotonic() > deadline:
            raise BridgeError(f"the opponent did not move in {wait_timeout} s")
        time.sleep(poll_seconds)


def play_game(
    client: BagiControlClient,
    engine: UciEngine,
    engine_sides: set[str],
    movetime_ms: int = DEFAULT_MOVETIME_MS,
    max_plies: int = DEFAULT_MAX_PLIES,
    cross_check: bool = False,
    opponent: Callable[[list[str]], str] | None = None,
    step_frames: int | None = None,
    poll_seconds: float = DEFAULT_POLL_SECONDS,
    wait_timeout: float | None = None,
    log: TextIO | None = None,
) -> GameReport:
    """Plays the game of the control API until it ends or `max_plies` moves were sent.

    A side in `engine_sides` is played by Stockfish. For the other side, `opponent(actions)` picks the move; without one the
    bridge waits for somebody else (a person at the window, another client) to move. `step_frames` is how many frames to run
    after each move (the scene follows only through frames on the manual clock); None means 1 on the manual clock, 0 otherwise.
    """
    if step_frames is None:
        step_frames = MANUAL_CLOCK_STEP_FRAMES if client.call("status").get("manualClock") else 0
    engine.new_game()

    report = GameReport()
    while True:
        reply = client.call("get_state")
        state, version = reply["state"], reply["version"]
        report.status, report.winner, report.moves = state["status"], state["winner"], list(state["moves"])
        if state["status"] != STATUS_ONGOING:
            return report
        if report.plies >= max_plies:
            report.reached_max_plies = True
            return report

        turn = state["turn"]
        actions = action_moves(client)
        position = position_command(state)
        if cross_check:
            check_rules(engine, position, actions)

        if turn in engine_sides:
            move = engine.best_move(position, movetime_ms)
            if move is None:
                raise BridgeError(f"Stockfish has no move for {turn} in {state['fen']}, but the game lists {len(actions)}")
            if move not in actions:
                raise RulesMismatch(f"Stockfish played {move} for {turn} in {state['fen']}, which is not among the {len(actions)} actions of the game: {sorted(actions)}")
        elif opponent is not None:
            move = opponent(actions)
        else:
            wait_for_change(client, version, poll_seconds, wait_timeout)
            continue

        try:
            client.call("perform_action", {"type": "move", "uci": move})
        except ControlError as error:
            raise BridgeError(f"the game refused {move} for {turn} in {state['fen']}: {error}") from error
        report.plies += 1
        if log is not None:
            print(f"{len(state['moves']) + 1:3d}. {turn:5s} {move}", file=log, flush=True)
        if step_frames > 0:
            client.call("step", {"frames": step_frames})


def parse_options(pairs: list[str]) -> dict[str, str]:
    options: dict[str, str] = {}
    for pair in pairs:
        name, separator, value = pair.partition("=")
        if not separator or not name:
            raise argparse.ArgumentTypeError(f"'{pair}' is not NAME=VALUE")
        options[name.strip()] = value.strip()
    return options


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--side", choices=["white", "black", "both"], default="both", help="the sides Stockfish plays (default: both)")
    parser.add_argument("--opponent", choices=["human", "random"], default="human", help="who plays the other side: a person at the window (waited for; run the game on the real clock so that its scene reacts to clicks) or random legal moves")
    parser.add_argument("--movetime", type=int, default=DEFAULT_MOVETIME_MS, help=f"milliseconds Stockfish thinks per move (default {DEFAULT_MOVETIME_MS})")
    parser.add_argument("--max-plies", type=int, default=DEFAULT_MAX_PLIES, help=f"stop after this many moves sent (default {DEFAULT_MAX_PLIES})")
    parser.add_argument("--cross-check", action="store_true", help="compare the legal moves of every position with Stockfish (go perft 1)")
    parser.add_argument("--option", action="append", default=[], metavar="NAME=VALUE", help='a UCI option, e.g. --option "Skill Level=0" (repeatable)')
    parser.add_argument("--stockfish", help="the Stockfish executable (default: $STOCKFISH, then PATH)")
    parser.add_argument("--seed", type=int, help="seed of the random opponent")
    parser.add_argument("--step-frames", type=int, help="frames to run after each move (default: 1 on the manual clock, 0 on the real one)")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"control port of a running game (default {DEFAULT_PORT}); with --exe a free one is used")
    parser.add_argument("--exe", type=Path, help="start this game executable (headless, manual clock) instead of connecting to a running one")
    parser.add_argument("--window", action="store_true", help="with --exe: open a real window instead of --headless")
    parser.add_argument("--real-clock", action="store_true", help="with --exe: do not use --manual-clock")
    parser.add_argument("--wait-timeout", type=float, help="seconds to wait for a person's move before giving up (default: forever)")
    parser.add_argument("--trace", action="store_true", help="print the UCI conversation to stderr")
    return parser


def run(args: argparse.Namespace) -> int:
    stockfish = find_stockfish(args.stockfish)
    if stockfish is None:
        print("Stockfish is not installed (or --stockfish / $STOCKFISH points nowhere): skipped", file=sys.stderr)
        return EXIT_SKIPPED
    try:
        options = parse_options(args.option)
    except argparse.ArgumentTypeError as error:
        print(error, file=sys.stderr)
        return EXIT_FAILED

    engine_sides = set(SIDES) if args.side == "both" else {args.side}
    rng = random.Random(args.seed)
    opponent = (lambda actions: rng.choice(actions)) if args.opponent == "random" else None

    game: GameProcess | None = None
    client: BagiControlClient | None = None
    try:
        if args.exe is not None:
            exe = args.exe.resolve()
            if not exe.is_file():
                print(f"there is no game executable at {exe}", file=sys.stderr)
                return EXIT_FAILED
            port = free_port()
            game = GameProcess(build_game_command(exe, port, headless=not args.window, manual_clock=not args.real_clock), cwd=exe.parent)
            game.start()
            client = game.connect(port, DEFAULT_STARTUP_TIMEOUT)
        else:
            client = BagiControlClient(host=args.host, port=args.port)
            client.connect()

        with UciEngine(stockfish, options, sys.stderr if args.trace else None) as engine:
            report = play_game(client, engine, engine_sides, args.movetime, args.max_plies, args.cross_check, opponent, args.step_frames, wait_timeout=args.wait_timeout, log=sys.stdout)

        winner = report.winner or "none"
        end = "stopped at --max-plies" if report.reached_max_plies else "game over"
        print(f"result: {report.status}, winner {winner}, {len(report.moves)} moves in the game, {report.plies} sent by this run ({end})")
        return EXIT_PASSED
    except (BridgeError, ControlError, ConnectionClosed, GameLaunchError, OSError) as error:
        print(f"failed: {error}", file=sys.stderr)
        if game is not None:
            print(f"--- game log (tail) ---\n{game.log_tail()}", file=sys.stderr)
        return EXIT_FAILED
    finally:
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
