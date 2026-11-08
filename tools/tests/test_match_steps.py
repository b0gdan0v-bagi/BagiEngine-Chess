"""The steps, the openings and the record of a match (tools/match_steps.py): pure, standard library only.

    python -m unittest discover -s tools/tests -p "test_match_steps.py"
"""

import json
import math
import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from elo_rating import estimate_rating, format_rating
from match_steps import (
    DEFAULT_OWN_STEPS,
    OPENINGS,
    GameRecord,
    PairRecord,
    format_table,
    parse_elo_steps,
    parse_own_step,
    schedule,
    summarize,
    to_json,
    validate_games,
)

UCI_MOVE = re.compile(r"[a-h][1-8][a-h][1-8][qrbn]?")


def game(result: str, color: str = "white", termination: str = "checkmate") -> GameRecord:
    return GameRecord(OPENINGS[0], color, result, termination, 40, ["e2e4", "e7e5"])


def pair(label: str, elo: int, wins: int, draws: int, losses: int) -> PairRecord:
    games = [game("win")] * wins + [game("draw", termination="draw_threefold")] * draws + [game("loss")] * losses
    return PairRecord(label, elo, games)


class ParseOwnStepTest(unittest.TestCase):
    def test_every_level_id_is_a_level_option_with_no_go_depth(self):
        for level in ("easy", "medium", "hard", "max"):
            step = parse_own_step(level)
            self.assertEqual(step.label, level)
            self.assertEqual(step.options, {"Level": level})
            self.assertIsNone(step.go_depth)

    def test_a_level_id_in_any_case(self):
        step = parse_own_step("  HaRd ")
        self.assertEqual(step.label, "hard")
        self.assertEqual(step.options, {"Level": "hard"})

    def test_a_depth_sets_both_options_and_the_go_depth(self):
        step = parse_own_step("depth=3")
        self.assertEqual(step.label, "depth=3")
        self.assertEqual(step.options, {"Depth": "3", "BlunderPercent": "0"})  # the server keeps its options between steps
        self.assertEqual(step.go_depth, 3)

    def test_a_depth_with_a_blunder_share(self):
        step = parse_own_step("depth=2,blunder=15")
        self.assertEqual(step.options, {"Depth": "2", "BlunderPercent": "15"})
        self.assertEqual(step.go_depth, 2)
        self.assertEqual(parse_own_step("Blunder=15, Depth=2").options, step.options)

    def test_the_edges_of_the_ranges(self):
        self.assertEqual(parse_own_step("depth=1").go_depth, 1)
        self.assertEqual(parse_own_step("depth=64,blunder=100").options, {"Depth": "64", "BlunderPercent": "100"})
        self.assertEqual(parse_own_step("depth=5,blunder=0").options["BlunderPercent"], "0")

    def test_bad_texts_are_refused_and_named(self):
        for text in (
            "depth=0",
            "depth=65",
            "depth=3,blunder=101",
            "blunder=101",
            "blunder=10",  # no depth: the step would end with no depth
            "depth=x",
            "depth=",
            "depth=-1",
            "depth=+3",
            "depth=1.5",
            "fast",
            "",
            "   ",
            "depth=3,depth=4",
            "depth=3,blunder=1,blunder=2",
            "depth=3,speed=2",
            "depth=3,",
            "levels",
        ):
            with self.subTest(text=text):
                with self.assertRaises(ValueError) as caught:
                    parse_own_step(text)
                self.assertIn(repr(text), str(caught.exception))

    def test_the_default_steps_are_the_four_levels_in_order(self):
        self.assertEqual(DEFAULT_OWN_STEPS, ("easy", "medium", "hard", "max"))
        for level in DEFAULT_OWN_STEPS:
            parse_own_step(level)


class ParseEloStepsTest(unittest.TestCase):
    def test_sorted_integers(self):
        self.assertEqual(parse_elo_steps(["2000", "1400", "1700"]), [1400, 1700, 2000])
        self.assertEqual(parse_elo_steps([" 1500 "]), [1500])
        self.assertEqual(parse_elo_steps([1800, 1400]), [1400, 1800])

    def test_at_least_one(self):
        with self.assertRaises(ValueError):
            parse_elo_steps([])

    def test_a_duplicate_is_refused(self):
        with self.assertRaises(ValueError):
            parse_elo_steps(["1400", "1700", "1400"])

    def test_non_numbers_and_non_positive_numbers_are_refused(self):
        for text in ("abc", "", "14.5", "-1400", "0", "1e3", "+1400"):
            with self.subTest(text=text):
                with self.assertRaises(ValueError):
                    parse_elo_steps(["1400", text])


class OpeningsTest(unittest.TestCase):
    def test_no_duplicates_four_plies_and_uci_moves(self):
        self.assertGreaterEqual(len(OPENINGS), 10)
        self.assertEqual(len(set(OPENINGS)), len(OPENINGS))
        for line in OPENINGS:
            self.assertEqual(len(line), 4, line)
            for move in line:
                self.assertRegex(move, UCI_MOVE)

    def test_a_ply_never_moves_back_to_the_square_the_previous_ply_left(self):
        # A cheap sanity check on typos: a ply never moves a piece to the square it just came from.
        for line in OPENINGS:
            for first, second in zip(line, line[1:]):
                self.assertNotEqual(first[:2], second[2:4], line)

    def test_every_line_starts_with_a_mainline_first_move(self):
        for line in OPENINGS:
            self.assertRegex(line[0], r"^(e2e4|d2d4|c2c4|g1f3)$")  # White's first moves with a clear mainline


class ScheduleTest(unittest.TestCase):
    def test_twenty_games_play_each_opening_twice_once_from_each_side(self):
        seen: dict[tuple[str, ...], list[bool]] = {}
        for index in range(20):
            opening, own_white = schedule(index, OPENINGS)
            seen.setdefault(opening, []).append(own_white)
        covered = min(20 // 2, len(OPENINGS))
        self.assertEqual(len(seen), covered)
        for opening, colors in seen.items():
            self.assertEqual(sorted(colors), [False, True], opening)

    def test_colors_alternate(self):
        colors = [schedule(index, OPENINGS)[1] for index in range(8)]
        self.assertEqual(colors, [True, False] * 4)

    def test_a_pair_of_games_shares_one_opening(self):
        for index in range(0, 24, 2):
            self.assertEqual(schedule(index, OPENINGS)[0], schedule(index + 1, OPENINGS)[0])

    def test_it_wraps_around_the_table(self):
        count = len(OPENINGS)
        self.assertEqual(schedule(2 * count, OPENINGS), schedule(0, OPENINGS))
        self.assertEqual(schedule(2 * count + 3, OPENINGS), schedule(3, OPENINGS))

    def test_a_short_table_wraps_early(self):
        table = (("e2e4",), ("d2d4",))
        self.assertEqual([schedule(i, table)[0] for i in range(6)], [("e2e4",)] * 2 + [("d2d4",)] * 2 + [("e2e4",)] * 2)

    def test_the_default_table_is_the_openings(self):
        self.assertEqual(schedule(5), schedule(5, OPENINGS))

    def test_a_negative_index_or_no_openings_raise(self):
        with self.assertRaises(ValueError):
            schedule(-1, OPENINGS)
        with self.assertRaises(ValueError):
            schedule(0, ())

    def test_games_must_be_even_and_positive(self):
        self.assertEqual(validate_games(20), 20)
        self.assertEqual(validate_games(2), 2)
        for games in (0, -2, 1, 3, 21):
            with self.subTest(games=games):
                with self.assertRaises(ValueError):
                    validate_games(games)


class RecordTest(unittest.TestCase):
    def test_game_scores(self):
        self.assertEqual(game("win").score(), 1.0)
        self.assertEqual(game("draw").score(), 0.5)
        self.assertEqual(game("loss").score(), 0.0)

    def test_a_bad_result_or_color_is_refused(self):
        with self.assertRaises(ValueError):
            GameRecord(OPENINGS[0], "white", "won", "checkmate", 10)
        with self.assertRaises(ValueError):
            GameRecord(OPENINGS[0], "red", "win", "checkmate", 10)

    def test_a_hand_made_pair(self):
        record = PairRecord("hard", 1700, [game("win"), game("win"), game("draw"), game("loss"), game("win", "black")])
        self.assertEqual((record.wins, record.draws, record.losses), (3, 1, 1))
        self.assertEqual(record.scores(), [1.0, 1.0, 0.5, 0.0, 1.0])
        self.assertAlmostEqual(record.score_fraction(), 0.7)
        self.assertEqual(record.rating().games, 5)

    def test_an_empty_pair(self):
        record = PairRecord("easy", 1400)
        self.assertEqual((record.wins, record.draws, record.losses), (0, 0, 0))
        self.assertEqual(record.scores(), [])
        self.assertIsNone(record.score_fraction())
        self.assertIsNone(record.rating())

    def test_two_pairs_do_not_share_their_games(self):
        first, second = PairRecord("easy", 1400), PairRecord("easy", 1700)
        first.games.append(game("win"))
        self.assertEqual(second.games, [])


class SummarizeTest(unittest.TestCase):
    def test_two_pairs_of_one_label_equal_the_rating_of_the_joined_scores(self):
        pairs = [pair("hard", 1400, 7, 2, 1), pair("hard", 1700, 4, 3, 3)]
        joined = [(1400, 1.0)] * 7 + [(1400, 0.5)] * 2 + [(1400, 0.0)] + [(1700, 1.0)] * 4 + [(1700, 0.5)] * 3 + [(1700, 0.0)] * 3
        summary = summarize(pairs)
        self.assertEqual(list(summary), ["hard"])
        self.assertEqual(summary["hard"], estimate_rating(joined))
        self.assertEqual(summary["hard"].games, 20)

    def test_two_labels_are_kept_apart_in_the_order_of_appearance(self):
        pairs = [pair("max", 1400, 9, 1, 0), pair("easy", 1400, 1, 1, 8), pair("max", 1700, 6, 2, 2)]
        summary = summarize(pairs)
        self.assertEqual(list(summary), ["max", "easy"])
        self.assertEqual(summary["max"].games, 20)
        self.assertEqual(summary["easy"].games, 10)
        self.assertGreater(summary["max"].elo, summary["easy"].elo)

    def test_pairs_without_games_are_left_out(self):
        self.assertEqual(summarize([]), {})
        self.assertEqual(list(summarize([PairRecord("easy", 1400), pair("max", 1400, 1, 1, 0)])), ["max"])

    def test_all_wins_are_a_saturated_bound(self):
        rating = summarize([pair("max", 1400, 10, 0, 0)])["max"]
        self.assertTrue(rating.saturated)
        self.assertEqual(rating.bound, ">=")


class ToJsonTest(unittest.TestCase):
    def test_it_round_trips_with_an_inf_error_written_as_null(self):
        # All draws against one opponent: the error is unknown (inf), which JSON cannot hold.
        pairs = [pair("hard", 1700, 0, 4, 0), pair("max", 1700, 3, 1, 0)]
        self.assertTrue(math.isinf(summarize(pairs)["hard"].error))
        document = to_json({"movetime": 100, "executables": ("a", "b"), "games": 4}, pairs)
        text = json.dumps(document, allow_nan=False)  # inf / nan would raise here
        loaded = json.loads(text)
        self.assertEqual(loaded, document)
        self.assertIsNone(loaded["summary"]["hard"]["error"])
        self.assertIsNone(loaded["pairs"][0]["rating"]["error"])
        self.assertEqual(loaded["settings"]["executables"], ["a", "b"])

    def test_the_content_of_a_pair_and_the_summary(self):
        pairs = [pair("max", 1700, 3, 1, 0)]
        document = to_json({}, pairs)
        entry = document["pairs"][0]
        self.assertEqual((entry["own_step"], entry["opponent_elo"]), ("max", 1700))
        self.assertEqual((entry["wins"], entry["draws"], entry["losses"]), (3, 1, 0))
        self.assertAlmostEqual(entry["score"], 0.875)
        self.assertEqual(len(entry["games"]), 4)
        first = entry["games"][0]
        self.assertEqual(first["opening"], list(OPENINGS[0]))
        self.assertEqual(
            (first["own_color"], first["result"], first["termination"], first["plies"], first["moves"]),
            ("white", "win", "checkmate", 40, ["e2e4", "e7e5"]),
        )
        self.assertEqual(entry["games"][3]["termination"], "draw_threefold")
        summary = document["summary"]["max"]
        self.assertEqual(set(summary), {"elo", "error", "saturated", "bound", "games"})
        self.assertEqual(summary["games"], 4)
        self.assertFalse(summary["saturated"])
        self.assertEqual(summary["bound"], "")

    def test_a_saturated_summary_names_its_bound(self):
        document = to_json({}, [pair("max", 1400, 4, 0, 0), pair("easy", 1400, 0, 0, 4)])
        self.assertEqual((document["summary"]["max"]["saturated"], document["summary"]["max"]["bound"]), (True, ">="))
        self.assertEqual((document["summary"]["easy"]["saturated"], document["summary"]["easy"]["bound"]), (True, "<="))
        json.dumps(document, allow_nan=False)

    def test_an_empty_match_and_an_empty_pair(self):
        self.assertEqual(to_json({}, []), {"settings": {}, "pairs": [], "summary": {}})
        document = to_json({}, [PairRecord("easy", 1400)])
        self.assertIsNone(document["pairs"][0]["score"])
        self.assertIsNone(document["pairs"][0]["rating"])
        self.assertEqual(document["summary"], {})

    def test_settings_of_another_type_are_refused_or_made_plain(self):
        self.assertEqual(to_json({"path": Path("x")}, [])["settings"], {"path": "x"})
        self.assertEqual(to_json({"nan": math.nan, "none": None, "flag": True}, [])["settings"], {"nan": None, "none": None, "flag": True})
        with self.assertRaises(TypeError):
            to_json({"bad": object()}, [])


class FormatTableTest(unittest.TestCase):
    def test_it_holds_each_label_the_wdl_and_the_rating_text(self):
        pairs = [pair("easy", 1400, 2, 1, 7), pair("max", 1400, 6, 2, 2), pair("max", 1700, 4, 3, 3)]
        table = format_table(pairs)
        summary = summarize(pairs)
        for label in ("easy", "max"):
            self.assertIn(label, table)
        for wdl in ("2/1/7", "6/2/2", "4/3/3"):
            self.assertIn(wdl, table)
        self.assertIn(format_rating(summary["max"]), table)
        self.assertIn(format_rating(summary["easy"]), table)
        self.assertIn(format_rating(pairs[0].rating()), table)
        self.assertIn("1700", table)
        self.assertIn("25.0 %", table)  # easy: (2 + 0.5) / 10
        self.assertIn("70.0 %", table)  # max / 1400: (6 + 1) / 10

    def test_it_is_ascii(self):
        table = format_table([pair("max", 1400, 5, 0, 0), pair("easy", 1400, 0, 4, 0)])
        table.encode("ascii")

    def test_a_saturated_and_an_unknown_error_are_printed_as_such(self):
        table = format_table([pair("max", 1400, 5, 0, 0), pair("easy", 1400, 0, 4, 0)])
        self.assertIn(">=", table)
        self.assertIn("(saturated)", table)
        self.assertIn("+/- ?", table)

    def test_the_summary_follows_the_rows(self):
        lines = format_table([pair("easy", 1400, 2, 1, 7)]).splitlines()
        self.assertEqual(lines[0].split(), ["step", "opp", "Elo", "W/D/L", "score", "Elo"])
        self.assertTrue(lines[1].startswith("-"))
        self.assertEqual(lines[3], "")
        self.assertTrue(lines[4].startswith("easy"))
        self.assertIn("(10 games)", lines[4])

    def test_an_empty_pair_and_no_pairs(self):
        self.assertIn("-", format_table([PairRecord("easy", 1400)]))
        self.assertIn("step", format_table([]))

    def test_a_long_label_keeps_the_columns_aligned(self):
        lines = format_table([pair("depth=3,blunder=15", 1400, 1, 0, 0), pair("hard", 1400, 1, 0, 0)]).splitlines()
        self.assertEqual(lines[2].index("1400"), lines[3].index("1400"))


if __name__ == "__main__":
    unittest.main()
