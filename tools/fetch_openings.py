#!/usr/bin/env python3
"""Makes assets/openings/openings.tsv, the table of opening names the HUD shows, from the lichess `chess-openings` data set
(https://github.com/lichess-org/chess-openings, CC0). Standard library only.

    python tools/fetch_openings.py
    python tools/fetch_openings.py --commit <full sha> --out /tmp/openings.tsv

The data set is five files, a.tsv .. e.tsv (one per ECO letter), each with the header line `eco<TAB>name<TAB>pgn`. The output is one header
line and then the rows of the five files in that order, the bytes of the rows unchanged (UTF-8, LF). The default commit is the one the file
in the repository was made from; a newer one refreshes the data (then change DEFAULT_COMMIT, the notices row and
licenses/lichess-chess-openings-CC0.txt with it). The files carry no position column: the game replays each `pgn` itself.
Exit code: 0 written, 1 a download or a file that is not what it should be.
"""

import argparse
import sys
import urllib.error
import urllib.request
from pathlib import Path

DEFAULT_COMMIT = "3ba6b20d31b2e6125cb0e6a2e792f400717d9d39"
BASE_URL = "https://raw.githubusercontent.com/lichess-org/chess-openings"
FILES = ("a.tsv", "b.tsv", "c.tsv", "d.tsv", "e.tsv")
HEADER = b"eco\tname\tpgn\n"
TIMEOUT_SECONDS = 60
DEFAULT_OUT = Path(__file__).resolve().parent.parent / "assets" / "openings" / "openings.tsv"


def download(commit: str, name: str) -> bytes:
    url = f"{BASE_URL}/{commit}/{name}"
    with urllib.request.urlopen(url, timeout=TIMEOUT_SECONDS) as response:
        return response.read()


def join_files(contents: list[bytes]) -> bytes:
    """The shared header line, then the rows of every file; a file that does not start with the header is refused."""
    rows = [HEADER]
    for name, data in zip(FILES, contents):
        if not data.startswith(HEADER):
            raise ValueError(f"{name} does not start with the header line {HEADER!r}")
        if b"\r" in data:
            raise ValueError(f"{name} has a carriage return: the table is LF only")
        body = data[len(HEADER):]
        if not body.endswith(b"\n"):
            raise ValueError(f"{name} does not end with a newline")
        data.decode("utf-8")
        rows.append(body)
    return b"".join(rows)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--commit", default=DEFAULT_COMMIT, help="the commit of lichess-org/chess-openings to take (default: %(default)s)")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="the file to write (default: %(default)s)")
    args = parser.parse_args(argv)

    try:
        table = join_files([download(args.commit, name) for name in FILES])
    except (urllib.error.URLError, ValueError) as error:
        print(f"fetch_openings: {error}", file=sys.stderr)
        return 1

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(table)
    rows = table.count(b"\n") - 1
    print(f"{args.out}: {rows} rows from commit {args.commit}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
