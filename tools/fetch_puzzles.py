#!/usr/bin/env python3
"""Makes assets/puzzles/puzzles.tsv, the puzzles of the Puzzles mode, from the lichess puzzle database
(https://database.lichess.org/#puzzles, CC0). Standard library only; unpacking the .zst needs the `zstandard` package or the `zstd` program.

    python tools/fetch_puzzles.py
    python tools/fetch_puzzles.py --max-bytes 62914560 --out /tmp/puzzles.tsv
    python tools/fetch_puzzles.py --csv /tmp/lichess_db_puzzle.csv

The database is one 310 MB file, lichess_db_puzzle.csv.zst, with the columns
`PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags`. The tool downloads only the first --max-bytes of
the compressed stream (the same prefix gives the same file; the prefix used and the date of the file are in
licenses/lichess-puzzles-CC0.txt), unpacks it and drops the row the cut falls into. --csv reads an already unpacked file instead.

The puzzles kept are mate in 1, 2 and 3 (the own engine can check those), well played and well rated by the people who solved them
(see select()), spread over the ratings MIN_RATING .. MAX_RATING. The output is one header line `id<TAB>fen<TAB>moves<TAB>rating<TAB>themes`
and the rows ordered by (rating, id); fen and moves are the database's, unchanged (UTF-8, LF).
Exit code: 0 written, 1 a download, an unpacking or a file that is not what it should be.
"""

import argparse
import csv
import io
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path

URL = "https://database.lichess.org/lichess_db_puzzle.csv.zst"
DEFAULT_MAX_BYTES = 60 * 1000 * 1000
TIMEOUT_SECONDS = 60
CHUNK_BYTES = 1 << 20
DEFAULT_OUT = Path(__file__).resolve().parent.parent / "assets" / "puzzles" / "puzzles.tsv"

CSV_COLUMNS = ("PuzzleId", "FEN", "Moves", "Rating", "RatingDeviation", "Popularity", "NbPlays", "Themes")
OUT_HEADER = "id\tfen\tmoves\trating\tthemes\n"

# The own engine finds a forced mate of this length at a depth the tests can afford.
MATE_THEMES = ("mateIn1", "mateIn2", "mateIn3")
# How many rows each theme gets (about 130 in all: small for the web package).
ROWS_PER_THEME = {"mateIn1": 40, "mateIn2": 60, "mateIn3": 30}
MIN_RATING = 600
MAX_RATING = 2200
BAND_WIDTH = 200
MIN_POPULARITY = 90
MIN_PLAYS = 1000
MAX_RATING_DEVIATION = 90


@dataclass(frozen=True)
class Row:
    id: str
    fen: str
    moves: str
    rating: int
    deviation: int
    popularity: int
    plays: int
    themes: str


def parse_row(fields: dict[str, str]) -> Row | None:
    """The Row of a database record, or None for a record with a missing or non-numeric column."""
    try:
        row = Row(
            id=fields["PuzzleId"],
            fen=fields["FEN"],
            moves=fields["Moves"],
            rating=int(fields["Rating"]),
            deviation=int(fields["RatingDeviation"]),
            popularity=int(fields["Popularity"]),
            plays=int(fields["NbPlays"]),
            themes=fields["Themes"],
        )
    except (KeyError, TypeError, ValueError):
        return None
    if not row.id or not row.fen or not row.moves:
        return None
    if any(char in value for value in (row.id, row.fen, row.moves, row.themes) for char in "\t\r\n"):
        return None
    return row


def mate_theme(row: Row) -> str | None:
    """The one mateIn<N> theme of the row, or None when it has none or several."""
    found = [theme for theme in row.themes.split() if theme in MATE_THEMES]
    return found[0] if len(found) == 1 else None


def is_candidate(row: Row) -> bool:
    return (
        mate_theme(row) is not None
        and row.popularity >= MIN_POPULARITY
        and row.plays >= MIN_PLAYS
        and row.deviation <= MAX_RATING_DEVIATION
        and MIN_RATING <= row.rating <= MAX_RATING
        and len(row.moves.split()) % 2 == 0
    )


def band_of(rating: int) -> int:
    """The band of a rating; MAX_RATING itself belongs to the last band."""
    last = (MAX_RATING - MIN_RATING) // BAND_WIDTH - 1
    return min((rating - MIN_RATING) // BAND_WIDTH, last)


def select(rows: list[Row]) -> list[Row]:
    """The rows to ship: per mate theme the ROWS_PER_THEME quota split over the rating bands (the remainder to the lowest bands), in each
    band the most popular ones (then the most played, then the smaller id); a band with fewer candidates gives what it has.
    Ordered by (rating, id); a repeated id is kept once. Pure: the same rows give the same answer in any order."""
    band_count = band_of(MAX_RATING) + 1
    candidates: dict[tuple[str, int], list[Row]] = {}
    seen: set[str] = set()
    for row in sorted(rows, key=lambda r: r.id):
        if not is_candidate(row) or row.id in seen:
            continue
        seen.add(row.id)
        candidates.setdefault((mate_theme(row), band_of(row.rating)), []).append(row)

    chosen: list[Row] = []
    for theme, quota in ROWS_PER_THEME.items():
        for band in range(band_count):
            take = quota // band_count + (1 if band < quota % band_count else 0)
            ranked = sorted(candidates.get((theme, band), []), key=lambda r: (-r.popularity, -r.plays, r.id))
            chosen.extend(ranked[:take])
    return sorted(chosen, key=lambda r: (r.rating, r.id))


def format_table(rows: list[Row]) -> str:
    return OUT_HEADER + "".join(f"{r.id}\t{r.fen}\t{r.moves}\t{r.rating}\t{r.themes}\n" for r in rows)


def read_csv(text: str) -> list[Row]:
    """The rows of the database's CSV; the header must name every column this tool reads. Records that do not parse are skipped."""
    reader = csv.DictReader(io.StringIO(text, newline=""))
    missing = [name for name in CSV_COLUMNS if name not in (reader.fieldnames or [])]
    if missing:
        raise ValueError(f"the CSV has no column {', '.join(missing)}")
    return [row for row in (parse_row(record) for record in reader) if row is not None]


def download_prefix(url: str, max_bytes: int) -> tuple[bytes, str]:
    """The first max_bytes of the file at url (less when it is shorter) and its Last-Modified date."""
    with urllib.request.urlopen(url, timeout=TIMEOUT_SECONDS) as response:
        modified = response.headers.get("Last-Modified", "unknown")
        data = bytearray()
        while len(data) < max_bytes:
            chunk = response.read(min(CHUNK_BYTES, max_bytes - len(data)))
            if not chunk:
                break
            data += chunk
    return bytes(data), modified


def decompress(data: bytes) -> bytes:
    """Unpacks a zstd stream that may be cut short: what the cut leaves is returned. Needs `zstandard` or the `zstd` program."""
    try:
        import zstandard
    except ImportError:
        program = shutil.which("zstd")
        if program is None:
            raise ValueError("no way to unpack .zst: install the `zstandard` package or the `zstd` program (or pass --csv)")
        done = subprocess.run([program, "-dc"], input=data, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=False)
        return done.stdout
    # The database is a series of zstd frames; a reader that goes across them returns what a cut stream still holds.
    with zstandard.ZstdDecompressor().stream_reader(io.BytesIO(data), read_across_frames=True) as reader:
        out = bytearray()
        while True:
            chunk = reader.read(CHUNK_BYTES)
            if not chunk:
                return bytes(out)
            out += chunk


def whole_lines(text: str) -> str:
    """The text up to and including its last newline: the row a cut stream ends in is dropped."""
    return text[: text.rfind("\n") + 1]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--max-bytes", type=int, default=DEFAULT_MAX_BYTES, help="how much of the compressed stream to take (default: %(default)s)")
    parser.add_argument("--csv", type=Path, help="read this unpacked database file instead of downloading")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="the file to write (default: %(default)s)")
    args = parser.parse_args(argv)

    try:
        if args.csv is not None:
            text = args.csv.read_text(encoding="utf-8")
            source = str(args.csv)
        else:
            packed, modified = download_prefix(URL, args.max_bytes)
            text = whole_lines(decompress(packed).decode("utf-8", errors="ignore"))
            source = f"{URL}, first {len(packed)} bytes, Last-Modified {modified}"
        rows = read_csv(text)
        chosen = select(rows)
    except (urllib.error.URLError, OSError, ValueError) as error:
        print(f"fetch_puzzles: {error}", file=sys.stderr)
        return 1
    if not chosen:
        print("fetch_puzzles: no puzzle was selected", file=sys.stderr)
        return 1

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(format_table(chosen).encode("utf-8"))
    counts = {theme: sum(1 for r in chosen if mate_theme(r) == theme) for theme in MATE_THEMES}
    print(f"{args.out}: {len(chosen)} of {len(rows)} rows ({counts}) from {source}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
