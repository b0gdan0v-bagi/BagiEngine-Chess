#!/usr/bin/env python3
"""Smoke test of the match tool: runs `tools/uci_match.py` for real, with BagiChessUci playing BagiChessUci and BagiChess as the referee.

    python tests/match_smoke.py --game-exe build/macos-debug/bin/BagiChess --uci-exe build/macos-debug/bin/BagiChessUci

ctest runs it as BagiChessMatchSmoke (CMakeLists.txt). Python 3, standard library only, no Stockfish: the opponent is a second BagiChessUci
whose `Depth` option stands in for the Elo option (`--opponent-elo-option Depth --opponent-elo 1 --opponent-elo 2`), so the numbers of the table
mean depths here and only the mechanics are checked. The tool is a child process with a time limit; the scenarios:

  1. `--verify-openings`: the referee takes every line of the opening table, exit code 0;
  2. a match of one step (`depth=1`) against the opponent at two "Elo" steps, 2 games each, capped at 40 plies, with `--out`: exit code 0, the table on
     stdout, and a record with 2 pairs of 2 games (W + D + L is 2 in each, every game has moves and a termination, the summary has an `elo` for the step);
  3. an opponent that does not exist: exit code 77 (the tool's "no opponent engine").

The temporary files live in a directory that is removed at the end. Exit code: 0 passed, 1 failed, 77 skipped (an executable or the tool is missing).
"""

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SKIP_EXIT_CODE = 77
TOOL_EXIT_SKIPPED = 77  # the tool's own code for "no opponent engine"
VERIFY_TIMEOUT_SECONDS = 120.0  # starts the game and plays 12 lines of 4 plies
MATCH_TIMEOUT_SECONDS = 240.0  # a Debug build searches slowly; 4 games of at most 40 plies at depth 1 and 2
GAMES_PER_PAIR = 2
OPPONENT_STEPS = (1, 2)  # depths, in the place of Elo steps
MAX_PLIES = 40
OWN_STEP = "depth=1"
MOVE_PATTERN = re.compile(r"^[a-h][1-8][a-h][1-8][qrbn]?$")
RESULTS = ("win", "draw", "loss")


class SmokeFailure(Exception):
    pass


def check(condition: bool, message: str) -> None:
    if not condition:
        raise SmokeFailure(message)


def run_tool(tool: Path, arguments: list, timeout: float) -> subprocess.CompletedProcess:
    """The tool as a child process, its output kept; a tool that hangs fails the test by the time limit."""
    command = [sys.executable, str(tool)] + [str(argument) for argument in arguments]
    try:
        return subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired as expired:
        raise SmokeFailure(f"the tool did not end in {timeout} s: {' '.join(command)}\nstderr: {expired.stderr}")


def describe(done: subprocess.CompletedProcess) -> str:
    return f"exit code {done.returncode}\nstdout: {done.stdout}\nstderr: {done.stderr}"


def scenario_verify_openings(tool: Path, game_exe: Path, uci_exe: Path, directory: Path) -> None:
    done = run_tool(tool, ["--verify-openings", "--referee", game_exe], VERIFY_TIMEOUT_SECONDS)
    check(done.returncode == 0, f"--verify-openings: {describe(done)}")
    lines = [line for line in done.stdout.splitlines() if line.strip()]
    check(lines and all(line.startswith("ok") for line in lines), f"--verify-openings did not say ok for every line: {done.stdout}")


def scenario_match(tool: Path, game_exe: Path, uci_exe: Path, directory: Path) -> None:
    out = directory / "match.json"
    arguments = ["--referee", game_exe, "--own", uci_exe, "--own-step", OWN_STEP, "--opponent", uci_exe,
                 "--opponent-elo-option", "Depth", "--opponent-option", "BlunderPercent=0", "--opponent-movetime", 50,
                 "--games", GAMES_PER_PAIR, "--max-plies", MAX_PLIES, "--out", out]
    for step in OPPONENT_STEPS:
        arguments += ["--opponent-elo", step]
    done = run_tool(tool, arguments, MATCH_TIMEOUT_SECONDS)
    check(done.returncode == 0, f"the match: {describe(done)}")
    check(OWN_STEP in done.stdout and "W/D/L" in done.stdout, f"no table on stdout: {done.stdout}")
    check(out.is_file(), f"--out wrote no file: {describe(done)}")
    check(not out.with_name(out.name + ".tmp").exists(), "the temporary file of --out is still there")

    record = json.loads(out.read_text(encoding="utf-8"))
    check("stopped_early" not in record["settings"], f"the finished match says it stopped early: {record['settings']}")
    pairs = record["pairs"]
    check(len(pairs) == len(OPPONENT_STEPS), f"expected {len(OPPONENT_STEPS)} pairs, got {len(pairs)}")
    for pair, step in zip(pairs, OPPONENT_STEPS):
        check(pair["own_step"] == OWN_STEP and pair["opponent_elo"] == step, f"unexpected pair {pair['own_step']} vs {pair['opponent_elo']}")
        check(len(pair["games"]) == GAMES_PER_PAIR, f"pair vs {step}: {len(pair['games'])} games, expected {GAMES_PER_PAIR}")
        check(pair["wins"] + pair["draws"] + pair["losses"] == GAMES_PER_PAIR, f"pair vs {step}: W+D+L is not {GAMES_PER_PAIR}: {pair}")
        colors = {game["own_color"] for game in pair["games"]}
        check(colors == {"white", "black"}, f"pair vs {step}: the own engine did not play both colors: {colors}")
        for game in pair["games"]:
            check(game["result"] in RESULTS, f"unknown result {game['result']}")
            check(game["termination"], f"a game without a termination: {game}")
            check(game["moves"] and all(MOVE_PATTERN.match(move) for move in game["moves"]), f"a game without moves, or with a bad one: {game['moves']}")
            check(len(game["moves"]) <= MAX_PLIES, f"a game of {len(game['moves'])} plies passed the cap of {MAX_PLIES}")
    check(list(record["summary"]) == [OWN_STEP], f"the summary should hold only {OWN_STEP}: {list(record['summary'])}")
    check("elo" in record["summary"][OWN_STEP], f"the summary of {OWN_STEP} has no elo: {record['summary']}")


def scenario_no_opponent(tool: Path, game_exe: Path, uci_exe: Path, directory: Path) -> None:
    done = run_tool(tool, ["--own", uci_exe, "--opponent", directory / "no-such-engine"], VERIFY_TIMEOUT_SECONDS)
    check(done.returncode == TOOL_EXIT_SKIPPED, f"a missing opponent should exit {TOOL_EXIT_SKIPPED}: {describe(done)}")
    check("no opponent engine" in done.stderr, f"the exit 77 comes with no message: {done.stderr}")


SCENARIOS = [
    ("--verify-openings plays the opening table through the game", scenario_verify_openings),
    ("a match of BagiChessUci against BagiChessUci under BagiChess", scenario_match),
    ("a missing opponent engine exits 77", scenario_no_opponent),
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game-exe", required=True, help="path of BagiChess (the referee)")
    parser.add_argument("--uci-exe", required=True, help="path of BagiChessUci (both engines)")
    parser.add_argument("--tool", default=str(Path(__file__).resolve().parent.parent / "tools" / "uci_match.py"), help="path of uci_match.py")
    arguments = parser.parse_args()
    game_exe, uci_exe, tool = Path(arguments.game_exe), Path(arguments.uci_exe), Path(arguments.tool)
    for path, what in ((game_exe, "BagiChess"), (uci_exe, "BagiChessUci"), (tool, "the tool uci_match.py")):
        if not path.is_file():
            print(f"SKIP: {path} does not exist ({what}; build the targets BagiChess and BagiChessUci)", flush=True)
            return SKIP_EXIT_CODE

    directory = Path(tempfile.mkdtemp(prefix="bagi-match-smoke-"))
    try:
        for name, scenario in SCENARIOS:
            print(f"--- {name}", flush=True)
            scenario(tool, game_exe, uci_exe, directory)
    except SmokeFailure as failure:
        print(f"FAILED: {failure}", flush=True)
        return 1
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    print("PASSED", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
