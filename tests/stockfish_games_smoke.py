#!/usr/bin/env python3
"""Real games of Stockfish against BagiChess: the last group of tools/tests/test_stockfish.py, as a ctest entry.

    python tests/stockfish_games_smoke.py --exe build/<preset>/bin/BagiChess

ctest runs it as BagiChessStockfishGames (CMakeLists.txt). Stockfish plays both sides, then against random moves, then a mate in one, at a
low skill level and a few milliseconds a move, capped in plies; every position is cross-checked against Stockfish's own move list (perft 1).
The assertions are about events (a move accepted, the same legal moves, the game ended), never about speed.

The games are played by pytest (the file is a pytest module); the other tests of the file need no program and stay a plain `pytest` run.
Exit code: 0 passed, 1 failed, 77 skipped: no Stockfish (`STOCKFISH` or PATH), no executable, or no pytest. The skip is decided here, so
a pytest run that skipped by itself is not shown as a pass.
"""

import argparse
import importlib.util
import os
import subprocess
import sys
from pathlib import Path

SKIP_EXIT_CODE = 77
ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
TEST_FILE = TOOLS / "tests" / "test_stockfish.py"
REAL_GAMES = "stockfish_plays_both_sides or stockfish_beats_random or stockfish_gives_mate"
TIMEOUT_SECONDS = 600.0  # only ends a hung run; the three games take seconds


def skip(reason: str) -> int:
    print(f"SKIP: {reason}")
    return SKIP_EXIT_CODE


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", required=True, help="the BagiChess executable")
    args = parser.parse_args()

    sys.path.insert(0, str(TOOLS))
    sys.path.insert(0, str(ROOT / "engine" / "tools" / "control"))
    import stockfish_bridge

    if importlib.util.find_spec("pytest") is None:
        return skip("pytest is not installed")
    if stockfish_bridge.find_stockfish(None) is None:
        return skip("Stockfish is not installed (put it on PATH or set STOCKFISH)")
    exe = Path(args.exe)
    if not exe.is_file():
        return skip(f"BagiChess is not built: {exe}")

    environment = dict(os.environ, BAGI_CHESS_EXE=str(exe.resolve()))
    command = [sys.executable, "-m", "pytest", str(TEST_FILE), "-k", REAL_GAMES, "-p", "no:cacheprovider", "-q"]
    try:
        done = subprocess.run(command, cwd=ROOT, env=environment, timeout=TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired:
        print(f"FAIL: pytest did not end in {TIMEOUT_SECONDS} s")
        return 1
    return 0 if done.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
