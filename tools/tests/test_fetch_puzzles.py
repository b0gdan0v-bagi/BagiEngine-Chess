"""tools/fetch_puzzles.py: the selection and the written format. No network, no unpacking.

    python -m unittest discover -s tools/tests -p "test_fetch_puzzles.py"
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import fetch_puzzles
from fetch_puzzles import Row, band_of, format_table, parse_row, read_csv, select, whole_lines

MATE_IN_1_MOVES = "a b"
MATE_IN_2_MOVES = "a b c d"
MATE_IN_3_MOVES = "a b c d e f"
MOVES_OF = {"mateIn1": MATE_IN_1_MOVES, "mateIn2": MATE_IN_2_MOVES, "mateIn3": MATE_IN_3_MOVES}


def make_row(id="p", theme="mateIn1", rating=1000, popularity=95, plays=5000, deviation=75, moves=None, themes=None) -> Row:
    return Row(
        id=id,
        fen="8/8/8/8/8/8/8/8 w - - 0 1",
        moves=moves if moves is not None else MOVES_OF[theme],
        rating=rating,
        deviation=deviation,
        popularity=popularity,
        plays=plays,
        themes=themes if themes is not None else f"mate {theme} short",
    )


class CandidateTest(unittest.TestCase):
    def ids(self, *rows: Row) -> list[str]:
        return [r.id for r in select(list(rows))]

    def test_a_good_row_is_kept(self):
        self.assertEqual(self.ids(make_row(id="a")), ["a"])

    def test_a_row_without_a_mate_theme_is_dropped(self):
        self.assertEqual(self.ids(make_row(themes="fork short")), [])

    def test_a_row_with_two_mate_themes_is_dropped(self):
        self.assertEqual(self.ids(make_row(themes="mateIn1 mateIn2")), [])

    def test_a_mate_in_4_is_dropped(self):
        self.assertEqual(self.ids(make_row(themes="mate mateIn4")), [])

    def test_popularity_plays_and_deviation_are_limits_that_include_the_edge(self):
        edge = make_row(id="edge", popularity=fetch_puzzles.MIN_POPULARITY, plays=fetch_puzzles.MIN_PLAYS, deviation=fetch_puzzles.MAX_RATING_DEVIATION)
        self.assertEqual(self.ids(edge), ["edge"])
        self.assertEqual(self.ids(make_row(popularity=fetch_puzzles.MIN_POPULARITY - 1)), [])
        self.assertEqual(self.ids(make_row(plays=fetch_puzzles.MIN_PLAYS - 1)), [])
        self.assertEqual(self.ids(make_row(deviation=fetch_puzzles.MAX_RATING_DEVIATION + 1)), [])

    def test_the_rating_range_includes_both_ends(self):
        self.assertEqual(self.ids(make_row(id="low", rating=fetch_puzzles.MIN_RATING), make_row(id="high", rating=fetch_puzzles.MAX_RATING)), ["low", "high"])
        self.assertEqual(self.ids(make_row(rating=fetch_puzzles.MIN_RATING - 1)), [])
        self.assertEqual(self.ids(make_row(rating=fetch_puzzles.MAX_RATING + 1)), [])

    def test_a_line_of_odd_length_is_dropped(self):
        self.assertEqual(self.ids(make_row(moves="a b c")), [])


class SelectTest(unittest.TestCase):
    def test_the_rows_are_ordered_by_rating_then_id(self):
        rows = [make_row(id="z", rating=900), make_row(id="b", rating=1500), make_row(id="a", rating=1500), make_row(id="y", rating=700)]
        self.assertEqual([r.id for r in select(rows)], ["y", "z", "a", "b"])

    def test_the_answer_does_not_depend_on_the_order_of_the_input(self):
        rows = [make_row(id=f"p{i}", rating=700 + i * 7, popularity=90 + i % 10, theme=("mateIn1", "mateIn2", "mateIn3")[i % 3]) for i in range(300)]
        self.assertEqual(select(rows), select(list(reversed(rows))))

    def test_a_band_keeps_its_most_popular_rows_then_the_most_played_then_the_smaller_id(self):
        rows = [make_row(id=f"p{i}", rating=650, popularity=90 + i) for i in range(6)]
        rows += [make_row(id="tie-b", rating=650, popularity=99, plays=9000), make_row(id="tie-a", rating=650, popularity=99, plays=9000)]
        per_band = fetch_puzzles.ROWS_PER_THEME["mateIn1"] // (band_of(fetch_puzzles.MAX_RATING) + 1)
        kept = {r.id for r in select(rows)}
        self.assertEqual(len(kept), per_band)
        self.assertEqual(kept, {"tie-a", "tie-b", "p5", "p4", "p3"})

    def test_every_theme_gets_its_quota_spread_over_the_bands(self):
        rows = []
        for theme in fetch_puzzles.MATE_THEMES:
            for band in range(band_of(fetch_puzzles.MAX_RATING) + 1):
                rating = fetch_puzzles.MIN_RATING + band * fetch_puzzles.BAND_WIDTH + 10
                rows += [make_row(id=f"{theme}-{band}-{i}", theme=theme, rating=rating) for i in range(40)]
        chosen = select(rows)
        for theme, quota in fetch_puzzles.ROWS_PER_THEME.items():
            self.assertEqual(sum(1 for r in chosen if fetch_puzzles.mate_theme(r) == theme), quota)
        bands = {band_of(r.rating) for r in chosen}
        self.assertEqual(bands, set(range(band_of(fetch_puzzles.MAX_RATING) + 1)))

    def test_a_thin_band_gives_what_it_has(self):
        self.assertEqual(len(select([make_row(id="only", rating=650)])), 1)

    def test_the_top_rating_belongs_to_the_last_band(self):
        self.assertEqual(band_of(fetch_puzzles.MAX_RATING), band_of(fetch_puzzles.MAX_RATING - 1))

    def test_a_repeated_id_is_kept_once(self):
        self.assertEqual([r.id for r in select([make_row(id="a"), make_row(id="a")])], ["a"])


class ParseTest(unittest.TestCase):
    HEADER = "PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,GameUrl,OpeningTags\n"

    def test_a_record_of_the_database_parses(self):
        row = read_csv(self.HEADER + "00008,fen w - - 0 1,e2e4 e7e5,1914,75,95,5000,mate mateIn1,https://lichess.org/x#1,\n")[0]
        self.assertEqual((row.id, row.fen, row.moves, row.rating, row.deviation, row.popularity, row.plays, row.themes),
                         ("00008", "fen w - - 0 1", "e2e4 e7e5", 1914, 75, 95, 5000, "mate mateIn1"))

    def test_a_record_with_a_missing_or_bad_number_is_skipped(self):
        text = (self.HEADER + "a,f,m,1000,75,95,5000,mateIn1,u,\n" + "b,f,m,x,75,95,5000,mateIn1,u,\n" + "c,f,m\n" + "d,f,m,1000,75,95,,mateIn1,u,\n")
        self.assertEqual([r.id for r in read_csv(text)], ["a"])

    def test_a_header_without_a_column_is_refused(self):
        with self.assertRaises(ValueError):
            read_csv("PuzzleId,FEN,Moves\nx,y,z\n")

    def test_a_field_with_a_tab_is_refused_because_the_table_is_tab_separated(self):
        self.assertIsNone(parse_row({"PuzzleId": "a", "FEN": "f", "Moves": "m\tn", "Rating": "1", "RatingDeviation": "1", "Popularity": "1", "NbPlays": "1", "Themes": "t"}))

    def test_the_row_a_cut_stream_ends_in_is_dropped(self):
        self.assertEqual(whole_lines("a\nb\nc,cut"), "a\nb\n")
        self.assertEqual(whole_lines("a\nb\n"), "a\nb\n")


class FormatTest(unittest.TestCase):
    def test_header_then_tab_separated_rows_with_lf_only(self):
        text = format_table([make_row(id="a", rating=900), make_row(id="b", rating=1100, theme="mateIn2")])
        lines = text.split("\n")
        self.assertEqual(lines[0], "id\tfen\tmoves\trating\tthemes")
        self.assertEqual(lines[1], "a\t8/8/8/8/8/8/8/8 w - - 0 1\ta b\t900\tmate mateIn1 short")
        self.assertEqual(lines[2].split("\t")[3], "1100")
        self.assertEqual(lines[3], "")
        self.assertNotIn("\r", text)

    def test_the_shipped_file_is_what_the_format_writes_and_ordered(self):
        shipped = fetch_puzzles.DEFAULT_OUT.read_text(encoding="utf-8")
        self.assertTrue(shipped.startswith(fetch_puzzles.OUT_HEADER))
        keys = []
        for line in shipped.splitlines()[1:]:
            fields = line.split("\t")
            self.assertEqual(len(fields), 5)
            keys.append((int(fields[3]), fields[0]))
        self.assertEqual(keys, sorted(keys))


if __name__ == "__main__":
    unittest.main()
