#!/usr/bin/env python3
"""A stand-in for Stockfish: speaks the part of UCI that the bridge uses, with answers from a JSON file.

The file named by the environment variable FAKE_UCI_CONFIG:

    {"log": "path",                         every received line is appended there
     "bestmoves": {"<position command>": "e2e4" | null},   null answers "bestmove (none)"
     "perft": {"<position command>": ["e2e4", ...]},       the lines of `go perft 1`
     "hang_on_go": false, "crash_on_go": false}

A position that is not in the table gets "bestmove e2e4", so that a test fails on the position command it logged, not on a hang.
"""

import json
import os
import sys


def main() -> int:
    config = {}
    config_path = os.environ.get("FAKE_UCI_CONFIG")
    if config_path:
        with open(config_path, encoding="utf-8") as stream:
            config = json.load(stream)
    log_path = config.get("log")
    position = ""

    def log(line: str) -> None:
        if log_path:
            with open(log_path, "a", encoding="utf-8") as stream:
                stream.write(line + "\n")

    def say(line: str = "") -> None:
        print(line, flush=True)

    for raw in sys.stdin:
        line = raw.strip()
        if not line:
            continue
        log(line)
        if line == "uci":
            say("id name FakeFish")
            say("id author tests")
            say("uciok")
        elif line == "isready":
            say("readyok")
        elif line.startswith("position"):
            position = line
        elif line.startswith("go perft"):
            for move in config.get("perft", {}).get(position, []):
                say(f"{move}: 1")
            say()
            say(f"Nodes searched: {len(config.get('perft', {}).get(position, []))}")
            say()
        elif line.startswith("go"):
            if config.get("crash_on_go"):
                return 9
            if config.get("hang_on_go"):
                continue
            say("info depth 1 score cp 10 pv e2e4")
            move = config.get("bestmoves", {}).get(position, "e2e4")
            say("bestmove (none)" if move is None else f"bestmove {move} ponder e7e5")
        elif line == "quit":
            return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
