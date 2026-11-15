#!/usr/bin/env python3
"""Check that the licence notices of the game hold to the tree.

    python tests/check_notices.py

ctest runs it as BagiChessThirdPartyNotices (CMakeLists.txt). It fails when:
  - LICENSE is missing or is not the Beerware text of the game's author;
  - THIRD_PARTY_NOTICES.md is missing, has no Stockfish section, has a table row whose path or licence file does not exist,
    or lacks a row for a file of assets/;
  - a Stockfish program or network file is in the repository (the game only starts a Stockfish a person installed).
The engine's own notices (engine/THIRD_PARTY_NOTICES.md) are the engine's ctest BagiThirdPartyNotices, not this one.
Python 3, standard library only. Exit code: 0 passed, 1 failed.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

GAME_ROOT = Path(__file__).resolve().parent.parent
NOTICES = "THIRD_PARTY_NOTICES.md"
LICENSE = "LICENSE"
LICENSE_MARKS = ("THE BEER-WARE LICENSE", "Andrey Bogdanov")
STOCKFISH_HEADING = "## Stockfish"
# A table row: the first cell is a path in backticks, the fourth the licence file (a path in backticks)
ROW = re.compile(r"^\|\s*`([^`]+)`\s*\|")
CELL_PATH = re.compile(r"`([^`]+)`")
LICENCE_FILE_CELL = 3
# What is not the game's own tree: the submodule, build output, tool caches
SKIPPED_FOLDERS = {"engine", "build", ".git", ".venv", "__pycache__", "node_modules"}
# A Stockfish file by name (a program or a network); the scripts and docs that talk about it are not those
STOCKFISH_NAME = re.compile(r"^stockfish.*\.(exe|nnue|bin|dll|so|dylib|wasm)$|^stockfish$|\.nnue$", re.IGNORECASE)


def read_rows(text: str) -> list[tuple[str, str | None]]:
    """(path, licence file or None) of every table row whose first cell is a path."""
    rows = []
    for line in text.splitlines():
        match = ROW.match(line)
        if not match:
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        found = CELL_PATH.search(cells[LICENCE_FILE_CELL]) if len(cells) > LICENCE_FILE_CELL else None
        rows.append((match.group(1), found.group(1) if found else None))
    return rows


def walk(folder: Path):
    for entry in sorted(folder.iterdir()):
        if entry.is_dir():
            if entry.name not in SKIPPED_FOLDERS:
                yield from walk(entry)
        else:
            yield entry


def check() -> list[str]:
    problems = []
    licence = GAME_ROOT / LICENSE
    if not licence.is_file():
        problems.append(f"{LICENSE} is missing")
    else:
        text = licence.read_text(encoding="utf-8")
        problems += [f"{LICENSE} lacks \"{mark}\"" for mark in LICENSE_MARKS if mark not in text]

    notices = GAME_ROOT / NOTICES
    if not notices.is_file():
        problems.append(f"{NOTICES} is missing")
    else:
        text = notices.read_text(encoding="utf-8")
        if STOCKFISH_HEADING not in text:
            problems.append(f"{NOTICES} has no \"{STOCKFISH_HEADING}\" section")
        rows = read_rows(text)
        listed = {path for path, _ in rows}
        for path, licence_file in rows:
            if not (GAME_ROOT / path).exists():
                problems.append(f"{path}: the row's path does not exist")
            if licence_file is not None and not (GAME_ROOT / licence_file).is_file():
                problems.append(f"{path}: the licence file {licence_file} does not exist")
        assets = GAME_ROOT / "assets"
        if assets.is_dir():
            for file in walk(assets):
                relative = file.relative_to(GAME_ROOT).as_posix()
                if relative not in listed:
                    problems.append(f"{relative}: no row in {NOTICES}")

    for file in walk(GAME_ROOT):
        if STOCKFISH_NAME.search(file.name):
            problems.append(f"{file.relative_to(GAME_ROOT).as_posix()}: a Stockfish file does not belong in the repository")
    return problems


def main() -> int:
    problems = check()
    for problem in problems:
        print(problem)
    if not problems:
        print(f"{NOTICES}: ok")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
