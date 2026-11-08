#!/usr/bin/env python3
"""A scriptable stand-in for a UCI engine (Stockfish), written from the UCI protocol alone. The C++ tests of the opponent
(UciProcessTest and the tests after it) start it as a child process, so they need no real engine. Standard library only.

    python fake_uci_engine.py [--log FILE] [--moves m1,m2,...] [--think-ms N] [...]

It answers `uci` with `id name`, `id author`, one option (`Skill Level`) and `uciok`; `isready` with `readyok`; it consumes
`setoption` (and remembers `MultiPV`, see --lines), `ucinewgame` and `position`; on `go ...` it prints one `info depth 1 score cp 10 pv <move>` line and then
`bestmove <move> ponder e7e5`; on `quit` or at the end of its input it exits. The answer to the 1st `go` is the 1st move of
--moves, to the 2nd the 2nd, and so on; when the list is used up it is `bestmove (none)`.

    --log FILE           append every line received to FILE, flushed at once (the file is created when the program starts)
    --moves m1,m2,...    the moves answered to the 1st, 2nd, ... `go` (default: e2e4)
    --scores s1,s2,...   the score in the `info` line of the 1st, 2nd, ... `go`: `cp:35`, `mate:3`, `mate:-2` (default and after the list: cp:10)
    --noisy-info         around the final `info` line (depth 2, the score of --scores) print lines an analysis must not take: an earlier depth
                         (cp -321), the same depth as a bound (cp 654 lowerbound), a second line of the search (multipv 2, mate 7), a text
                         line (`info string score cp 999`) and, after the final line, a bound again (mate -9 upperbound)
    --no-score           the final `info` line carries no `score`; with --lines and `MultiPV` above 1 no line of either depth does (the noise lines of --noisy-info keep theirs)
    --lines m:k:v,...    a search with several lines, the same for every `go`: `e2e4:cp:30,d2d4:cp:20,g1f3:mate:3` (move, then the score as in --scores). The
                         `bestmove` is the first move (--moves is not used) and its score is the first score (--scores is not used). With `MultiPV` 1 (the
                         start, and after `setoption name MultiPV value 1`) the `info` line is the one of today, `info depth 1 score <first score> pv <first move>`.
                         After `setoption name MultiPV value N` with N > 1 it prints, for the depths 1 and 2, `info depth D multipv i score ... pv <move>` for
                         i = 1..min(N, number of lines) in order; the lines of depth 1 carry a score (cp -321) that must not be taken, those of depth 2 the scripted
                         ones (a move is not checked: a test may script an illegal or a repeated one). Not combined with --noisy-info
    --think-ms N         wait N milliseconds before answering `go`; a `stop` line ends the wait at once (an `isready` is answered meanwhile)
    --think-first-only   --think-ms applies to the 1st `go` only (a search that is stopped, then one that answers at once)
    --ignore-stop        a `stop` line does not end the wait
    --hang-on-go         never answer `go`
    --die-on-go          exit with code 9 on `go`
    --die-once MARKER    like --die-on-go, but only while the file MARKER does not exist; the program creates it
    --no-uciok           stay silent after `uci`
    --startup-delay-ms N wait N milliseconds before reading the first line
    --split-lines        write every line in two flushed halves with a short pause between them
    --garbage            print a few unknown lines before the `info` line of an answer
    --flood N            print N `info string` lines in one write before the `info` line of an answer
    --long-line N        print one line of N bytes before the `info` line of an answer
    --ignore-quit        do not leave on `quit` or at the end of the input
    --ignore-term        ignore SIGTERM, and note `<SIGTERM ignored>` in the log (with --ignore-quit only SIGKILL ends the program)
"""

from __future__ import annotations

import argparse
import os
import queue
import signal
import sys
import threading
import time
from types import FrameType
from typing import TextIO

DEFAULT_MOVES = "e2e4"
DEFAULT_SCORE = "cp:10"
PONDER_MOVE = "e7e5"
NO_MOVE = "(none)"
DIE_EXIT_CODE = 9
SPLIT_PAUSE_SECONDS = 0.02
SLEEP_FOREVER_SECONDS = 3600
LOG_FILE_MODE = 0o644
GARBAGE_LINES = ("this line is not UCI", "", "info string noise", "xyzzy")
NOISE_EARLIER_CP = -321
NOISE_BOUND_CP = 654
NOISE_SECOND_LINE_MATE = 7
NOISE_UPPER_BOUND_MATE = -9
OPTION_LINE = "option name Skill Level type spin default 20 min 0 max 20"
TERM_NOTE = "<SIGTERM ignored>"


class Leave(Exception):
    """The program ends with this exit code."""

    def __init__(self, code: int = 0):
        super().__init__(code)
        self.code = code


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--log", metavar="FILE")
    parser.add_argument("--moves", default=DEFAULT_MOVES)
    parser.add_argument("--scores", default=DEFAULT_SCORE)
    parser.add_argument("--noisy-info", action="store_true")
    parser.add_argument("--no-score", action="store_true")
    parser.add_argument("--lines", default="")
    parser.add_argument("--think-ms", type=int, default=0)
    parser.add_argument("--think-first-only", action="store_true")
    parser.add_argument("--ignore-stop", action="store_true")
    parser.add_argument("--hang-on-go", action="store_true")
    parser.add_argument("--die-on-go", action="store_true")
    parser.add_argument("--die-once", metavar="MARKER")
    parser.add_argument("--no-uciok", action="store_true")
    parser.add_argument("--startup-delay-ms", type=int, default=0)
    parser.add_argument("--split-lines", action="store_true")
    parser.add_argument("--garbage", action="store_true")
    parser.add_argument("--flood", type=int, default=0, metavar="N")
    parser.add_argument("--long-line", type=int, default=0, metavar="N")
    parser.add_argument("--ignore-quit", action="store_true")
    parser.add_argument("--ignore-term", action="store_true")
    return parser


def parse_line_spec(spec: str) -> tuple[str, str]:
    """`e2e4:cp:30` is the move `e2e4` and the score text `cp:30`."""
    move, _, score = spec.partition(":")
    return move, score


def read_input(lines: "queue.Queue[str | None]") -> None:
    """Runs in a thread, so that the engine can look at its input while it waits: None marks the end of the input."""
    for raw in sys.stdin:
        lines.put(raw)
    lines.put(None)


class FakeEngine:
    def __init__(self, options: argparse.Namespace, lines: "queue.Queue[str | None]", out: TextIO):
        self._options = options
        self._lines = lines
        self._out = out
        self._moves = [move for move in options.moves.split(",") if move]
        self._scores = [score for score in options.scores.split(",") if score]
        self._script = [parse_line_spec(spec) for spec in options.lines.split(",") if spec]
        self._multipv = 1
        self._go_count = 0
        # A raw descriptor in append mode: every line is in the file when `log` returns, and a signal handler may write too
        self._log_fd = os.open(options.log, os.O_WRONLY | os.O_APPEND | os.O_CREAT, LOG_FILE_MODE) if options.log else None

    def run(self) -> int:
        if self._options.startup_delay_ms > 0:
            time.sleep(self._options.startup_delay_ms / 1000)
        try:
            while True:
                self.handle(self.receive(None))
        except Leave as leave:
            return leave.code

    def log(self, text: str) -> None:
        if self._log_fd is not None:
            os.write(self._log_fd, (text + "\n").encode("utf-8"))

    def receive(self, timeout: float | None) -> str | None:
        """The next line of the input, stripped and logged (empty lines are not logged); None when none came within `timeout` seconds."""
        try:
            raw = self._lines.get(timeout=timeout)
        except queue.Empty:
            return None
        if raw is None:
            self.on_end_of_input()
        text = raw.strip()
        if text:
            self.log(text)
        return text

    def say(self, line: str) -> None:
        if self._options.split_lines and len(line) > 1:
            half = len(line) // 2
            self.write(line[:half])
            time.sleep(SPLIT_PAUSE_SECONDS)
            self.write(line[half:] + "\n")
        else:
            self.write(line + "\n")

    def write(self, text: str) -> None:
        self._out.write(text)
        self._out.flush()

    def handle(self, line: str | None) -> None:
        if not line:
            return
        command = line.split(maxsplit=1)[0]
        if command == "uci":
            self.on_uci()
        elif command == "isready":
            self.say("readyok")
        elif command == "setoption":
            self.on_setoption(line)
        elif command == "go":
            self.on_go()
        elif command == "quit":
            self.on_quit()
        # ucinewgame, position, stop and anything else are consumed

    def on_setoption(self, line: str) -> None:
        """Remembers `setoption name MultiPV value N` (any other option is consumed); a value that is not a number is ignored."""
        words = line.split()
        if len(words) == 5 and words[1] == "name" and words[2].lower() == "multipv" and words[3] == "value" and words[4].isdigit():
            self._multipv = max(int(words[4]), 1)

    def on_uci(self) -> None:
        if self._options.no_uciok:
            return
        self.say("id name FakeUciEngine")
        self.say("id author chess-tests")
        self.say(OPTION_LINE)
        self.say("uciok")

    def on_go(self) -> None:
        index = self._go_count
        self._go_count += 1
        if self._options.die_on_go or self.should_die_once():
            raise Leave(DIE_EXIT_CODE)
        if self._options.hang_on_go:
            return
        if self._script:
            move = self._script[0][0]
        else:
            move = self._moves[index] if index < len(self._moves) else NO_MOVE

        self.think(index)
        if self._options.garbage:
            for line in GARBAGE_LINES:
                self.say(line)
        if self._options.flood > 0:
            self.write("".join(f"info string flood {number}\n" for number in range(self._options.flood)))
        if self._options.long_line > 0:
            self.write("x" * self._options.long_line + "\n")
        if move == NO_MOVE:
            self.say("info depth 0 score mate 0")
            self.say(f"bestmove {NO_MOVE}")
        else:
            self.say_info(index, move)
            self.say(f"bestmove {move} ponder {PONDER_MOVE}")

    def score_words(self, index: int) -> str:
        """The `score ...` words of the `go` number `index` (from --scores, or the first of --lines); empty with --no-score."""
        if self._script:
            return self.words_of(self._script[0][1])
        return self.words_of(self._scores[index] if index < len(self._scores) else DEFAULT_SCORE)

    def words_of(self, text: str) -> str:
        """` score cp 35` from `cp:35`; empty with --no-score."""
        if self._options.no_score:
            return ""
        kind, _, value = text.partition(":")
        return f" score {kind} {value}"

    def say_multipv_lines(self) -> None:
        """--lines with `MultiPV` above 1: an earlier depth whose scores must not be taken, then the scripted lines, each depth in the order of the index."""
        count = min(self._multipv, len(self._script))
        earlier = "" if self._options.no_score else f" score cp {NOISE_EARLIER_CP}"
        for depth, score_of in ((1, lambda _: earlier), (2, lambda text: self.words_of(text))):
            for number, (move, score) in enumerate(self._script[:count], start=1):
                self.say(f"info depth {depth} multipv {number}{score_of(score)} pv {move}")

    def say_info(self, index: int, move: str) -> None:
        """The `info` line(s) before `bestmove`: one line, or with --noisy-info the final line (depth 2) among lines that must not be taken, or the lines of --lines."""
        if self._script and self._multipv > 1:
            self.say_multipv_lines()
            return
        score = self.score_words(index)
        if not self._options.noisy_info:
            self.say(f"info depth 1{score} pv {move}")
            return
        self.say(f"info depth 1 score cp {NOISE_EARLIER_CP} pv {move}")
        self.say(f"info depth 2 score cp {NOISE_BOUND_CP} lowerbound pv {move}")
        self.say(f"info depth 2 multipv 2 score mate {NOISE_SECOND_LINE_MATE} pv {move}")
        self.say("info string score cp 999")
        self.say(f"info depth 2{score} pv {move}")
        self.say(f"info depth 2 score mate {NOISE_UPPER_BOUND_MATE} upperbound pv {move}")

    def should_die_once(self) -> bool:
        marker = self._options.die_once
        if not marker or os.path.exists(marker):
            return False
        with open(marker, "w", encoding="utf-8"):
            pass
        return True

    def think(self, index: int) -> None:
        """Waits out --think-ms before the answer to the `go` number `index`; a `stop` ends the wait (unless --ignore-stop), `isready` is answered, `quit` is honored."""
        if self._options.think_ms <= 0 or (self._options.think_first_only and index > 0):
            return
        deadline = time.monotonic() + self._options.think_ms / 1000
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return
            line = self.receive(remaining)
            if not line:
                continue
            command = line.split(maxsplit=1)[0]
            if command == "stop" and not self._options.ignore_stop:
                return
            if command == "isready":
                self.say("readyok")
            elif command == "quit":
                self.on_quit()

    def on_quit(self) -> None:
        if not self._options.ignore_quit:
            raise Leave(0)

    def on_end_of_input(self) -> None:
        if not self._options.ignore_quit:
            raise Leave(0)
        while True:  # a signal wakes the sleep, runs its handler and the sleep goes on
            time.sleep(SLEEP_FOREVER_SECONDS)


def main(argv: list[str] | None = None) -> int:
    options = build_parser().parse_args(argv)
    lines: "queue.Queue[str | None]" = queue.Queue()
    engine = FakeEngine(options, lines, sys.stdout)

    if options.ignore_term:
        def on_term(signal_number: int, frame: FrameType | None) -> None:
            engine.log(TERM_NOTE)

        signal.signal(signal.SIGTERM, on_term)

    threading.Thread(target=read_input, args=(lines,), daemon=True).start()
    try:
        return engine.run()
    except BrokenPipeError:  # the reader closed its end: nobody listens any more
        os.dup2(os.open(os.devnull, os.O_WRONLY), sys.stdout.fileno())
        return 0


if __name__ == "__main__":
    sys.exit(main())
