#!/usr/bin/env python3
"""Smoke test of BagiChessUci: starts the real executable and talks UCI to it over pipes, the way a tournament tool does.

    python tests/uci_smoke.py --exe build/ClangmacOS/BagiChessUci

ctest runs it as BagiChessUciSmoke (CMakeLists.txt). Python 3, standard library only. The smoke does not implement chess: it checks that every
bestmove is a move in the form [a-h][1-8][a-h][1-8][qrbn]? and that the NEXT position built with it is accepted, which the server proves by saying
nothing about it (a refusal is an `info string` line; the smoke asks `isready` after each `position` and reads up to `readyok`). A reader thread
feeds the lines of stdout to a queue, so a program that hangs fails the test by a time limit instead of hanging ctest. The scenarios:

  1. one process: the handshake, a game of six plies fed back move by move, a level, a search on the clock, `go infinite` + `stop` (exactly one bestmove);
  2. `go infinite` + `quit`: the program ends with code 0;
  3. `go infinite` + the end of the input: the program ends with code 0.

Every line the program writes to stdout starts with a UCI keyword (a stray log line would break a tournament tool). The child is always killed at
the end. Exit code: 0 passed, 1 failed, 77 skipped (the executable does not exist).
"""

import argparse
import queue
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

SKIP_EXIT_CODE = 77
READ_TIMEOUT_SECONDS = 30.0  # a stall of the machine is not a failure; a program that never answers is
EXIT_TIMEOUT_SECONDS = 15.0
CLOCK_TEST_MILLISECONDS = 2000
STOP_AFTER_SECONDS = 0.3
GAME_PLIES = 6
SEARCH_DEPTH = 2

UCI_KEYWORDS = ("id", "option", "uciok", "readyok", "bestmove", "info")
MOVE_PATTERN = re.compile(r"^[a-h][1-8][a-h][1-8][qrbn]?$")
END_OF_OUTPUT = None  # what the reader thread puts in the queue after the last line


class SmokeFailure(Exception):
    pass


def check(condition: bool, message: str) -> None:
    if not condition:
        raise SmokeFailure(message)


class UciProgram:
    """The executable with pipes; every stdout line is kept in `lines` for the keyword check."""

    def __init__(self, exe: Path) -> None:
        self.process = subprocess.Popen([str(exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.lines = []
        self._queue = queue.Queue()
        self._reader = threading.Thread(target=self._read_stdout, daemon=True)
        self._reader.start()

    def _read_stdout(self) -> None:
        for line in self.process.stdout:
            self._queue.put(line.rstrip("\r\n"))
        self._queue.put(END_OF_OUTPUT)

    def send(self, line: str) -> None:
        try:
            self.process.stdin.write(line + "\n")
            self.process.stdin.flush()
        except OSError as error:
            raise SmokeFailure(f"cannot write '{line}' to the program: {error}")

    def read_until(self, prefix: str, what: str) -> list:
        """The lines up to and including the first one that starts with prefix."""
        deadline = time.monotonic() + READ_TIMEOUT_SECONDS
        got = []
        while True:
            try:
                line = self._queue.get(timeout=max(0.0, deadline - time.monotonic()))
            except queue.Empty:
                raise SmokeFailure(f"no '{prefix}' ({what}) in {READ_TIMEOUT_SECONDS} s; got {got}")
            if line is END_OF_OUTPUT:
                raise SmokeFailure(f"the program closed its output before '{prefix}' ({what}); got {got}")
            self.lines.append(line)
            got.append(line)
            if line.startswith(prefix):
                return got

    def close_input(self) -> None:
        try:
            self.process.stdin.close()
        except OSError:
            pass

    def wait_exit(self, what: str) -> int:
        try:
            return self.process.wait(timeout=EXIT_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired:
            raise SmokeFailure(f"the program did not end in {EXIT_TIMEOUT_SECONDS} s ({what})")

    def drain(self) -> None:
        """Takes what is left of stdout into `lines` (the program has ended, so the reader reaches the end of the output)."""
        while True:
            try:
                line = self._queue.get(timeout=EXIT_TIMEOUT_SECONDS)
            except queue.Empty:
                raise SmokeFailure("the output of the program did not end")
            if line is END_OF_OUTPUT:
                return
            self.lines.append(line)

    def kill(self) -> None:
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait()


def best_move_of(lines: list) -> str:
    """The move of the one bestmove line in lines."""
    best = [line for line in lines if line.startswith("bestmove")]
    check(len(best) == 1, f"expected one bestmove, got {best} in {lines}")
    words = best[0].split()
    check(len(words) >= 2 and MOVE_PATTERN.match(words[1]) is not None, f"'{best[0]}' is not a bestmove with a move")
    return words[1]


def sync(program: UciProgram, what: str) -> list:
    """isready / readyok: what the program said since the last command, and proof that it has handled everything sent before."""
    program.send("isready")
    return program.read_until("readyok", what)


def check_no_refusal(lines: list, what: str) -> None:
    check(not any(line.startswith("info string") for line in lines), f"{what}: the program answered with {lines}")


def handshake(program: UciProgram) -> None:
    program.send("uci")
    reply = program.read_until("uciok", "the handshake")
    check(reply[0].startswith("id name "), f"the first line is not 'id name': {reply}")
    check(any(line.startswith("id author ") for line in reply), f"no 'id author' in {reply}")
    for option in ("Depth", "BlunderPercent", "Level"):
        check(any(line.startswith(f"option name {option} type ") for line in reply), f"option {option} is not announced in {reply}")
    check(sync(program, "isready") == ["readyok"], "isready was not answered with a single readyok")


def play_a_game(program: UciProgram) -> None:
    """Six plies: each bestmove is fed back in the next `position`, which must be accepted."""
    program.send("ucinewgame")
    moves = []
    for ply in range(GAME_PLIES):
        program.send("position startpos" + (" moves " + " ".join(moves) if moves else ""))
        check_no_refusal(sync(program, f"position after {moves}"), f"the position after {moves} was refused")
        program.send(f"go depth {SEARCH_DEPTH}")
        moves.append(best_move_of(program.read_until("bestmove", f"ply {ply + 1}")))


def play_on_the_clock(program: UciProgram) -> None:
    """Level easy answers a clock at once; a clock alone (no depth) is held to its share of the time."""
    program.send("setoption name Level value easy")
    program.send("position startpos")
    program.send(f"go wtime {CLOCK_TEST_MILLISECONDS} btime {CLOCK_TEST_MILLISECONDS}")
    best_move_of(program.read_until("bestmove", "go on the clock at level easy"))

    program.send("setoption name Depth value 0")
    program.send("setoption name BlunderPercent value 0")
    program.send("position startpos")
    check_no_refusal(sync(program, "options"), "the options or the position were refused")
    started = time.monotonic()
    program.send(f"go wtime {CLOCK_TEST_MILLISECONDS} btime {CLOCK_TEST_MILLISECONDS} movestogo 1")
    best_move_of(program.read_until("bestmove", "go on the clock with movestogo 1"))
    elapsed = time.monotonic() - started
    check(elapsed < CLOCK_TEST_MILLISECONDS / 1000.0, f"a move on a clock of {CLOCK_TEST_MILLISECONDS} ms took {elapsed:.2f} s")


def stop_an_infinite_search(program: UciProgram) -> None:
    program.send("position startpos")
    program.send("go infinite")
    time.sleep(STOP_AFTER_SECONDS)
    program.send("stop")
    stopped = program.read_until("bestmove", "go infinite and stop")
    best_move_of(stopped)
    # Exactly one bestmove: nothing more comes before the answer to the next isready
    check_no_refusal(stopped, "stop")
    after = sync(program, "after stop")
    check(not any(line.startswith("bestmove") for line in after), f"a second bestmove after stop: {after}")


def run_one_program(exe: Path, programs: list):
    program = UciProgram(exe)
    programs.append(program)
    return program


def scenario_session(exe: Path, programs: list) -> None:
    program = run_one_program(exe, programs)
    handshake(program)
    play_a_game(program)
    play_on_the_clock(program)
    stop_an_infinite_search(program)
    program.send("quit")
    check(program.wait_exit("quit after a session") == 0, f"the program ended with code {program.process.returncode} after quit")
    program.drain()


def scenario_quit_during_a_search(exe: Path, programs: list) -> None:
    program = run_one_program(exe, programs)
    program.send("position startpos")
    program.send("go infinite")
    time.sleep(STOP_AFTER_SECONDS)
    program.send("quit")
    check(program.wait_exit("quit during go infinite") == 0, f"the program ended with code {program.process.returncode} after quit during a search")
    program.drain()
    check(sum(line.startswith("bestmove") for line in program.lines) <= 1, f"more than one bestmove: {program.lines}")


def scenario_end_of_input(exe: Path, programs: list) -> None:
    program = run_one_program(exe, programs)
    program.send("position startpos")
    program.send("go infinite")
    time.sleep(STOP_AFTER_SECONDS)
    program.close_input()
    check(program.wait_exit("end of the input during go infinite") == 0, f"the program ended with code {program.process.returncode} at the end of its input")
    program.drain()


SCENARIOS = [
    ("session: handshake, a game, a level, a clock, go infinite and stop", scenario_session),
    ("go infinite and quit", scenario_quit_during_a_search),
    ("go infinite and the end of the input", scenario_end_of_input),
]


def check_keywords(programs: list) -> None:
    for program in programs:
        for line in program.lines:
            check(line.split(" ", 1)[0] in UCI_KEYWORDS, f"a line of stdout that is not UCI: '{line}'")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, help="path of BagiChessUci")
    exe = Path(parser.parse_args().exe)
    if not exe.is_file():
        print(f"SKIP: {exe} does not exist (build the target BagiChessUci)", flush=True)
        return SKIP_EXIT_CODE

    programs = []
    try:
        for name, scenario in SCENARIOS:
            print(f"--- {name}", flush=True)
            scenario(exe, programs)
        check_keywords(programs)
    except SmokeFailure as failure:
        print(f"FAILED: {failure}", flush=True)
        return 1
    finally:
        for program in programs:
            program.kill()
    print("PASSED", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
