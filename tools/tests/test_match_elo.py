"""The Elo and its error bar (tools/elo_rating.py): pure, standard library only.

    python -m unittest discover -s tools/tests -p "test_match_elo.py"
"""

import math
import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import elo_rating
from elo_rating import Rating, estimate_rating, expected_score, format_rating, score_to_elo_difference


def results(wins: int, draws: int, losses: int, opponent: float = 1500.0) -> list[tuple[float, float]]:
    return [(opponent, 1.0)] * wins + [(opponent, 0.5)] * draws + [(opponent, 0.0)] * losses


class ExpectedScoreTest(unittest.TestCase):
    def test_equal_ratings_expect_half(self):
        self.assertEqual(expected_score(1800, 1800), 0.5)

    def test_the_two_sides_add_up_to_one(self):
        for a, b in ((1500, 1500), (1500, 1900), (2300, 1100), (-300, 700)):
            self.assertAlmostEqual(expected_score(a, b) + expected_score(b, a), 1.0, places=12)

    def test_a_higher_rating_expects_more(self):
        self.assertGreater(expected_score(1600, 1500), expected_score(1500, 1500))


class ScoreToEloDifferenceTest(unittest.TestCase):
    def test_known_points_of_the_logistic(self):
        self.assertEqual(score_to_elo_difference(0.5), 0.0)
        self.assertAlmostEqual(score_to_elo_difference(0.75), 190.85, places=2)  # 400 * log10(3)
        self.assertAlmostEqual(score_to_elo_difference(0.64), 99.95, places=2)
        self.assertAlmostEqual(score_to_elo_difference(0.90), 381.70, places=2)  # 400 * log10(9)
        self.assertAlmostEqual(score_to_elo_difference(0.25), -190.85, places=2)

    def test_it_inverts_expected_score(self):
        for score in (0.01, 0.2, 0.5, 0.77, 0.99):
            self.assertAlmostEqual(expected_score(1500 + score_to_elo_difference(score), 1500), score, places=12)

    def test_the_extremes_have_no_difference(self):
        for score in (0.0, 1.0, -0.1, 1.5):
            with self.assertRaises(ValueError):
                score_to_elo_difference(score)


class EstimateRatingTest(unittest.TestCase):
    def test_half_against_one_opponent_is_that_opponent_with_a_symmetric_bar(self):
        rating = estimate_rating(results(50, 0, 50, opponent=1800))
        self.assertAlmostEqual(rating.elo, 1800.0, places=3)
        self.assertFalse(rating.saturated)
        self.assertEqual(rating.games, 100)
        # The bar is the same at the same score above or below: 1.96 * sqrt(25) / (ln(10)/400 * 25)
        self.assertAlmostEqual(rating.error, 1.96 * 5.0 / (math.log(10) / 400 * 25.0), places=6)
        mirrored = estimate_rating([(1800, 1.0 - score) for _, score in results(30, 0, 70, opponent=1800)])
        direct = estimate_rating(results(30, 0, 70, opponent=1800))
        self.assertAlmostEqual(mirrored.error, direct.error, places=5)

    def test_the_known_points_of_the_logistic(self):
        for wins, difference in ((75, 190.85), (64, 99.95), (90, 381.70)):
            rating = estimate_rating(results(wins, 0, 100 - wins, opponent=1000))
            self.assertAlmostEqual(rating.elo - 1000, difference, places=1)

    def test_the_worked_example_60_wins_40_losses_against_3000(self):
        # Score 0.6: R = 3000 + 400 * log10(1.5) = 3070.4. Residuals 0.4 (x60) and -0.6 (x40): sqrt(60 * 0.16 + 40 * 0.36) = sqrt(24).
        # Information: ln(10)/400 * 100 * 0.24. SE = sqrt(24) / that = 35.46, the bar 1.96 * SE = 69.5.
        rating = estimate_rating(results(60, 0, 40, opponent=3000))
        self.assertAlmostEqual(rating.elo, 3070.4, places=1)
        self.assertAlmostEqual(rating.error, 69.5, places=1)
        self.assertFalse(rating.saturated)

    def test_draws_narrow_the_bar_at_the_same_score(self):
        with_draws = estimate_rating(results(40, 20, 40))
        without = estimate_rating(results(50, 0, 50))
        self.assertAlmostEqual(with_draws.elo, without.elo, places=3)
        self.assertLess(with_draws.error, without.error)

    def test_doubling_the_games_shrinks_the_bar_by_the_root_of_two(self):
        small = estimate_rating(results(30, 10, 20))
        large = estimate_rating(results(60, 20, 40))
        self.assertAlmostEqual(small.elo, large.elo, places=3)
        self.assertAlmostEqual(small.error / large.error, math.sqrt(2.0), places=6)

    def test_several_opponents(self):
        rating = estimate_rating([(1400, 1.0)] * 5 + [(1400, 0.0)] * 5 + [(1600, 1.0)] * 5 + [(1600, 0.0)] * 5)
        self.assertAlmostEqual(rating.elo, 1500.0, places=3)
        self.assertEqual(rating.games, 20)

    def test_a_better_result_in_one_game_never_lowers_the_rating(self):
        base = [(1400, 1.0), (1400, 0.5), (1700, 0.0), (1700, 0.5), (2000, 0.0), (2000, 0.0)]
        for index in range(len(base)):
            for better in (0.5, 1.0):
                if better <= base[index][1]:
                    continue
                changed = list(base)
                changed[index] = (base[index][0], better)
                self.assertGreater(estimate_rating(changed).elo, estimate_rating(base).elo)

    def test_the_order_of_the_results_does_not_matter(self):
        base = [(1400, 1.0), (1400, 0.5), (1700, 0.0), (1700, 0.5), (2000, 0.0), (2000, 1.0), (2000, 0.5)]
        expected = estimate_rating(base)
        shuffler = random.Random(7)
        for _ in range(5):
            shuffled = list(base)
            shuffler.shuffle(shuffled)
            rating = estimate_rating(shuffled)
            self.assertAlmostEqual(rating.elo, expected.elo, places=6)
            self.assertAlmostEqual(rating.error, expected.error, places=6)

    def test_all_wins_are_saturated_and_finite(self):
        rating = estimate_rating(results(20, 0, 0))
        self.assertTrue(rating.saturated)
        self.assertEqual(rating.bound, elo_rating.BOUND_LOWER)
        self.assertTrue(math.isfinite(rating.elo))
        self.assertTrue(math.isfinite(rating.error))
        self.assertGreater(rating.error, 0.0)
        # The score of one half game short of the extreme: 19.5 / 20
        self.assertAlmostEqual(rating.elo, 1500.0 + score_to_elo_difference(19.5 / 20), places=3)

    def test_all_losses_are_saturated_and_finite(self):
        rating = estimate_rating(results(0, 0, 20))
        self.assertTrue(rating.saturated)
        self.assertEqual(rating.bound, elo_rating.BOUND_UPPER)
        self.assertTrue(math.isfinite(rating.elo))
        self.assertTrue(math.isfinite(rating.error))
        self.assertAlmostEqual(rating.elo, 1500.0 + score_to_elo_difference(0.5 / 20), places=3)

    def test_the_saturated_bound_rises_with_the_number_of_games(self):
        self.assertGreater(estimate_rating(results(40, 0, 0)).elo, estimate_rating(results(10, 0, 0)).elo)
        self.assertLess(estimate_rating(results(0, 0, 40)).elo, estimate_rating(results(0, 0, 10)).elo)

    def test_saturated_against_several_opponents_is_finite(self):
        rating = estimate_rating([(1400, 1.0), (1700, 1.0), (2000, 1.0)])
        self.assertTrue(rating.saturated)
        self.assertTrue(math.isfinite(rating.elo) and math.isfinite(rating.error))

    def test_one_game(self):
        win = estimate_rating([(1500, 1.0)])
        self.assertTrue(win.saturated)
        self.assertAlmostEqual(win.elo, 1500.0, places=3)  # half a game short of one: a score of 0.5
        self.assertEqual(win.games, 1)
        draw = estimate_rating([(1500, 0.5)])
        self.assertFalse(draw.saturated)
        self.assertAlmostEqual(draw.elo, 1500.0, places=3)
        loss = estimate_rating([(1500, 0.0)])
        self.assertTrue(loss.saturated)

    def test_no_results_raise(self):
        with self.assertRaises(ValueError):
            estimate_rating([])

    def test_a_score_outside_0_to_1_raises(self):
        with self.assertRaises(ValueError):
            estimate_rating([(1500, 2.0)])


class FormatRatingTest(unittest.TestCase):
    def test_a_plain_rating(self):
        self.assertEqual(format_rating(Rating(1523.4, 87.2, False, 40)), "1523 +/- 87")

    def test_an_unknown_error(self):
        self.assertEqual(format_rating(Rating(1523.0, math.inf, False, 1)), "1523 +/- ?")

    def test_a_lower_bound(self):
        self.assertEqual(format_rating(Rating(1810.2, 50.0, True, 20, elo_rating.BOUND_LOWER)), ">= 1810 (saturated)")

    def test_an_upper_bound(self):
        self.assertEqual(format_rating(Rating(1249.6, 50.0, True, 20, elo_rating.BOUND_UPPER)), "<= 1250 (saturated)")

    def test_it_formats_what_estimate_rating_gives_in_ascii(self):
        for found in (estimate_rating(results(30, 10, 20)), estimate_rating(results(10, 0, 0)), estimate_rating(results(0, 0, 10))):
            self.assertTrue(format_rating(found).isascii())


if __name__ == "__main__":
    unittest.main()
