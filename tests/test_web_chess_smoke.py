"""Tests of the parts of web_chess_smoke.py that need no browser: the layout of the Wide view, the watch of a board, the verdicts and the
command line. Standard library only; ctest runs it as BagiChessWebSmokeUnit, and it needs neither Playwright nor a built page.

    python -m unittest discover -s tests -p "test_web_chess_smoke.py"
"""

import contextlib
import io
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import web_chess_smoke as chess  # noqa: E402


class FakeClock:
    """A clock that only the fake sleep moves, so a watch runs without waiting."""

    def __init__(self) -> None:
        self.time = 0.0

    def now(self) -> float:
        return self.time

    def sleep(self, seconds: float) -> None:
        self.time += seconds


def watch(pictures: list, ignore=(), timeout: float = 10.0, interval: float = 1.0, stable_samples: int = 3) -> chess.Watch:
    """watch_until_settled over a scripted list of samples; the last one repeats when the list runs out."""
    clock = FakeClock()
    samples = iter(pictures)
    last = [pictures[-1]]

    def grab() -> bytes:
        last[0] = next(samples, last[0])
        return last[0]

    return chess.watch_until_settled(grab, ignore, timeout, interval, stable_samples, clock.now, clock.sleep)


class LayoutTest(unittest.TestCase):
    def test_the_default_window_is_the_wide_view_scaled_by_its_height(self):
        layout = chess.wide_layout(960, 540)
        self.assertAlmostEqual(layout.scale, 540 / 800)
        # 1120 units of the view are 756 px, centred in 960
        self.assertAlmostEqual(layout.to_pixel((0, 0))[0], (960 - 756) / 2)
        self.assertAlmostEqual(layout.to_pixel((0, 0))[1], 0)

    def test_a_wide_window_is_limited_by_its_height_and_the_view_is_centred(self):
        layout = chess.wide_layout(1400, 800)
        self.assertAlmostEqual(layout.scale, 1.0)
        self.assertAlmostEqual(layout.to_pixel((0, 0))[0], 140)

    def test_the_board_clip_is_the_640_units_of_the_board(self):
        clip = chess.wide_layout(960, 540).board_clip()
        scale = 540 / 800
        self.assertAlmostEqual(clip["width"], 640 * scale)
        self.assertAlmostEqual(clip["height"], 640 * scale)
        self.assertAlmostEqual(clip["x"], 102 + 100 * scale)
        self.assertAlmostEqual(clip["y"], 80 * scale)

    def test_the_squares_have_white_at_the_bottom(self):
        self.assertEqual(chess.square_centre("a1"), (140.0, 680.0))
        self.assertEqual(chess.square_centre("h8"), (700.0, 120.0))
        self.assertEqual(chess.square_centre("e2"), (460.0, 600.0))
        self.assertEqual(chess.square_centre("e4"), (460.0, 440.0))

    def test_a_name_that_is_not_a_square_is_refused(self):
        for name in ("", "e", "i1", "a9", "e22"):
            with self.subTest(name=name):
                with self.assertRaises(ValueError):
                    chess.square_centre(name)

    def test_the_buttons_lie_inside_the_view(self):
        points = (chess.VIEW_MENU_BUTTON, chess.VIEW_RESET_BUTTON, chess.VIEW_MENU_OPPONENT, chess.VIEW_OPPONENT_COMPUTER,
                  chess.VIEW_OPPONENT_BACK, chess.VIEW_MENU_CLOSE, chess.VIEW_PARKED_POINTER)
        for x, y in points:
            self.assertTrue(0 < x < chess.VIEW_SIZE[0] and 0 < y < chess.VIEW_SIZE[1], (x, y))

    def test_the_parked_pointer_is_off_the_board(self):
        x, y = chess.VIEW_PARKED_POINTER
        left, top = chess.BOARD_ORIGIN
        side = chess.BOARD_SQUARES * chess.SQUARE_SIZE
        self.assertFalse(left <= x <= left + side and top <= y <= top + side)

    def test_a_window_the_scene_shows_in_another_view_is_refused(self):
        for size in ((800, 800), (540, 960), (1119, 800)):
            with self.subTest(size=size):
                with self.assertRaises(chess.LayoutError):
                    chess.wide_layout(*size)
        chess.wide_layout(1120, 800)


class WatchTest(unittest.TestCase):
    def test_a_board_at_rest_is_found_after_equal_samples(self):
        result = watch([b"a", b"a", b"a"])
        self.assertTrue(result.settled and result.changed)
        self.assertEqual(result.picture, b"a")

    def test_a_slide_is_not_rest(self):
        result = watch([b"a", b"b", b"c", b"d", b"d", b"d"])
        self.assertEqual(result.picture, b"d")
        self.assertTrue(result.settled)

    def test_ignored_pictures_never_settle(self):
        result = watch([b"moved"], ignore=(b"moved",), timeout=5.0)
        self.assertFalse(result.settled)
        self.assertFalse(result.changed)

    def test_the_reply_after_the_move_alone(self):
        # the move alone is at rest for a while, then the program's move shows up and rests
        result = watch([b"sel", b"moved", b"moved", b"moved", b"slide", b"reply", b"reply", b"reply"], ignore=(b"sel", b"moved"))
        self.assertEqual(result.picture, b"reply")
        self.assertTrue(result.settled)

    def test_a_reply_that_keeps_moving_is_changed_but_not_settled(self):
        pictures = [bytes([n]) for n in range(1, 60)]
        result = watch(pictures, ignore=(b"x",), timeout=5.0)
        self.assertTrue(result.changed)
        self.assertFalse(result.settled)

    def test_a_stale_shot_of_the_selection_between_samples_resets_the_count(self):
        result = watch([b"reply", b"sel", b"reply", b"reply", b"reply"], ignore=(b"sel",))
        self.assertTrue(result.settled)
        self.assertEqual(result.seconds, 4.0)

    def test_the_time_spent_is_reported(self):
        self.assertEqual(watch([b"a", b"a", b"a"]).seconds, 2.0)


class JudgeTest(unittest.TestCase):
    START = b"start"

    @staticmethod
    def settled(picture: bytes) -> chess.Watch:
        return chess.Watch(picture, True, True, 1.0)

    def test_a_move_and_a_reply_pass(self):
        self.assertEqual(chess.judge_game(self.START, self.settled(b"moved"), self.settled(b"reply"), 60.0), [])

    def test_a_move_that_left_the_board_as_it_was_fails(self):
        problems = chess.judge_game(self.START, self.settled(self.START), None, 60.0)
        self.assertEqual(len(problems), 1)
        self.assertIn("(6)", problems[0])

    def test_a_board_that_never_settled_after_the_move_fails(self):
        problems = chess.judge_game(self.START, chess.Watch(b"slide", True, False, 15.0), None, 60.0)
        self.assertIn("(6)", problems[0])

    def test_no_reply_is_told_from_a_reply_that_never_rests(self):
        silent = chess.judge_game(self.START, self.settled(b"moved"), chess.Watch(b"sel", False, False, 60.0), 60.0)
        restless = chess.judge_game(self.START, self.settled(b"moved"), chess.Watch(b"x", True, False, 60.0), 60.0)
        self.assertIn("did not reply", silent[0])
        self.assertIn("rest", restless[0])

    def test_a_run_that_stopped_before_the_reply_judges_only_the_move(self):
        self.assertEqual(chess.judge_game(self.START, self.settled(b"moved"), None, 60.0), [])


class FrameProblemTest(unittest.TestCase):
    def test_a_page_that_drew_nothing_is_frozen(self):
        self.assertIn("(8)", chess.frame_problem(0, 0.0, report_only=False))
        self.assertIn("(8)", chess.frame_problem(0, 0.0, report_only=True))

    def test_a_long_gap_fails_unless_it_is_only_reported(self):
        gap = chess.FROZEN_FRAME_SECONDS + 1
        self.assertIn("(8)", chess.frame_problem(10, gap, report_only=False))
        self.assertIsNone(chess.frame_problem(10, gap, report_only=True))

    def test_a_smooth_page_passes(self):
        self.assertIsNone(chess.frame_problem(100, 0.05, report_only=False))


class CommandLineTest(unittest.TestCase):
    @staticmethod
    def parse(*argv: str):
        return chess.parse_options(list(argv))

    @staticmethod
    def refused(*argv: str) -> int:
        with contextlib.redirect_stderr(io.StringIO()):
            try:
                chess.parse_options(list(argv))
            except SystemExit as exit_:
                return int(exit_.code)
        return 0

    def test_the_defaults(self):
        options = self.parse("--dist", "dist")
        self.assertEqual(options.dist, Path("dist"))
        self.assertFalse(options.single_thread)
        self.assertEqual(options.web.viewport, chess.DEFAULT_VIEWPORT)
        self.assertIsNone(options.save_shots)

    def test_the_options_reach_the_engines_helpers(self):
        options = self.parse("--dist", "dist-single", "--single-thread", "--expect-console", "single-thread mode", "--allow-console", "x",
                             "--browser-channel", "msedge", "--viewport", "800x500")
        self.assertTrue(options.single_thread)
        self.assertEqual(options.web.expect_console, ["single-thread mode"])
        self.assertEqual(options.web.browser_channel, "msedge")
        self.assertEqual(options.web.viewport, (800, 500))
        self.assertIn("x", options.web.allow_console)

    def test_wrong_command_lines_exit_with_2(self):
        wrong = (
            (),
            ("--dist", "dist", "--viewport", "540x960"),
            ("--dist", "dist", "--viewport", "wide"),
            ("--dist", "dist", "--timeout", "0"),
            ("--dist", "dist", "--reply-timeout", "-1"),
            ("--dist", "dist", "--expect-console", "("),
            ("--dist", "dist", "--browser-channel", "msedge", "--executable-path", "x"),
            ("--dist", "dist", "--no-such-flag"),
        )
        for argv in wrong:
            with self.subTest(argv=argv):
                self.assertEqual(self.refused(*argv), chess.EXIT_USAGE)

    def test_without_playwright_the_run_is_skipped(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(chess.main(["--dist", "dist"], load=lambda: None), chess.EXIT_SKIPPED)

    def test_a_folder_with_no_page_fails(self):
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(chess.main(["--dist", "no-such-folder"], load=lambda: object()), chess.EXIT_FAILED)


if __name__ == "__main__":
    unittest.main()
