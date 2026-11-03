#!/usr/bin/env python3
"""A scriptable stand-in for a UCI engine (Stockfish), written from the UCI protocol alone. The C++ tests of the opponent
(UciProcessTest and the tests after it) start it as a child process, so they need no real engine. Standard library only.

    python fake_uci_engine.py [--log FILE] [--moves m1,m2,...] [--think-ms N] [...]

It answers `uci` with `id name`, `id author`, one option (`Skill Level`) and `uciok`; `isready` with `readyok`; it consumes
`setoption`, `ucinewgame` and `position`; on `go ...` it prints one `info depth 1 score cp 10 pv <move>` line and then
`bestmove <move> ponder e7e5`; on `quit` or at the end of its input it exits. The answer to the 1st `go` is the 1st move of
--moves, to the 2nd the 2nd, and so on; when the list is used up it is `bestmove (none)`.

    --log FILE           append every line received to FILE, flushed at once (the file is created when the program starts)
    --moves m1,m2,...    the moves answered to the 1st, 2nd, ... `go` (default: e2e4)
    --think-ms N         wait N milliseconds before answering `go`; a `stop` line ends the wait at once (an `isready` is answered meanwhile)
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
PONDER_MOVE = "e7e5"
NO_MOVE = "(none)"
DIE_EXIT_CODE = 9
SPLIT_PAUSE_SECONDS = 0.02
SLEEP_FOREVER_SECONDS = 3600
LOG_FILE_MODE = 0o644
GARBAGE_LINES = ("this line is not UCI", "", "info string noise", "xyzzy")
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
    parser.add_argument("--think-ms", type=int, default=0)
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
        elif command == "go":
            self.on_go()
        elif command == "quit":
            self.on_quit()
        # setoption, ucinewgame, position, stop and anything else are consumed

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
        move = self._moves[index] if index < len(self._moves) else NO_MOVE

        self.think()
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
            self.say(f"info depth 1 score cp 10 pv {move}")
            self.say(f"bestmove {move} ponder {PONDER_MOVE}")

    def should_die_once(self) -> bool:
        marker = self._options.die_once
        if not marker or os.path.exists(marker):
            return False
        with open(marker, "w", encoding="utf-8"):
            pass
        return True

    def think(self) -> None:
        """Waits out --think-ms; a `stop` ends the wait (unless --ignore-stop), `isready` is answered, `quit` is honored."""
        if self._options.think_ms <= 0:
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
