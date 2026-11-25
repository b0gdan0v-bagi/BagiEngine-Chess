#!/usr/bin/env python3
"""Browser smoke test of the Chess web page: serves a dist folder (tools/web/package.py) with serve.py, opens it in a headless
Chromium or Edge through Playwright for Python and plays the first moves of a game with the mouse.

    python tests/web_chess_smoke.py --dist dist --expect-console "Initialized with \\d+ worker threads"
    python tests/web_chess_smoke.py --dist dist-single --single-thread --expect-console "single-thread mode"
    python tests/web_chess_smoke.py --dist dist --save-shots build/web-shots

It is not in ctest (it needs emsdk, a browser and a built page); its pure parts have tests/test_web_chess_smoke.py, which ctest runs.
The engine's tools/web/tests/web_smoke.py is imported for what it shares (the server, the browser launch, the page of a game, the
checks of the page and the console); see its docstring and tools/web/tests/CLAUDE.md for the venv and for what each of its checks proves.

The window is 960x540 by default: the Wide view of the scene (config/SceneConfig.xml, the aspect 1.4 up), whose buttons and board are at
the places of VIEW_* below. The page has no control API, so every step is judged by pictures. The checks (numbered as the report numbers them; 6 is played before 3 and 7):
  1. the page loads: HTTP 200, no uncaught page error;
  2. the first frame is drawn (not one flat color);
  3. the opponent is picked with the menu (Menu, Opponent, Computer, Back, Close): each click changes the picture;
  4. the browser console has no error-level message outside the allow-list (ALLOWED_CONSOLE and --allow-console), and every
     --expect-console REGEX matched a message;
  5. the canvas backing store is the CSS size times the pixel ratio;
  6. two people play e2 e4: the board changes. This is done before the opponent is picked, so the picture of "the move alone" is known;
  7. after Reset and the pick of Computer, the same two clicks are answered: the board settles on a picture that is neither the move
     alone nor the selection, within --reply-timeout;
  8. the page keeps drawing while the program thinks: a frame counter of the page's own (an animation frame loop that a blocked main
     thread starves) saw frames, and the longest gap between two is below FROZEN_FRAME_SECONDS. With --single-thread the gap is
     reported, not failed: the search of that build runs on the main thread in slices.
The board (the 640x640 units of it, not the HUD) is the picture that is compared, so a move text or a hover of the HUD is not a reply.

The program plays Black against the person (the default of "Play as"), so the person's e2 e4 is answered by a Black move. The pointer is
parked in an empty margin of the view after every click, and a picture counts only once STABLE_SAMPLES samples in a row are equal (a
slide of a piece is not a reply).

Exit code: 0 passed, 1 failed (the reasons are printed), 77 skipped (no Playwright, or the named browser is absent), 2 a wrong
command line. --save-shots DIR keeps the picture of every step and, for a failed run, the last one: the click points are computed
from the layout, and a run that misses is read from them.
"""

import argparse
import hashlib
import os
import re
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Collection

EXIT_PASSED = 0
EXIT_FAILED = 1
EXIT_USAGE = 2
EXIT_SKIPPED = 77


def find_engine_web_tests() -> Path:
    """tools/web/tests of the engine checkout this script belongs to."""
    here = Path(__file__).resolve()
    candidates = []
    if os.environ.get("BAGI_ENGINE_DIR"):
        candidates.append(Path(os.environ["BAGI_ENGINE_DIR"]))
    candidates.append(here.parents[1] / "engine")  # the engine as a submodule
    for engine in candidates:
        tests_dir = engine / "tools" / "web" / "tests"
        if (tests_dir / "web_smoke.py").is_file():
            return tests_dir
    raise SystemExit(f"cannot find tools/web/tests/web_smoke.py of the engine (looked in {[str(c) for c in candidates]}); set BAGI_ENGINE_DIR")


sys.path.insert(0, str(find_engine_web_tests()))

import web_smoke as smoke  # noqa: E402  (its own sys.path entries make serve, package and png_reader importable)

DEFAULT_VIEWPORT = (960, 540)
DEFAULT_TIMEOUT_SECONDS = 60.0
DEFAULT_REPLY_TIMEOUT_SECONDS = 60.0
STEP_TIMEOUT_SECONDS = 15.0  # the longest a click may take to show on the board or the panels
SAMPLE_INTERVAL_SECONDS = 0.3
STABLE_SAMPLES = 3
FROZEN_FRAME_SECONDS = 5.0  # a page whose frames are further apart than this was frozen, not slow
# Console messages that are errors by level and say nothing about the game; each with its reason. None so far.
ALLOWED_CONSOLE: tuple[str, ...] = ()

# The Wide view of config/SceneConfig.xml (1120 x 800 units, from the aspect 1.4 up) and the places in it, in view units. The buttons are
# the centres of the rectangles that the control API's ui_tree gives for the desktop game at this view (HUD Menu / Reset in
# config/nodes/ChessHud.xml, the plate of the menu and of the opponent panel, both centred, in ChessMenuPanel.xml and ChessOpponentPanel.xml).
VIEW_SIZE = (1120.0, 800.0)
WIDE_MIN_ASPECT = 1.4
BOARD_ORIGIN = (100.0, 80.0)
SQUARE_SIZE = 80.0
BOARD_SQUARES = 8
VIEW_MENU_BUTTON = (931.0, 40.0)
VIEW_RESET_BUTTON = (1053.0, 40.0)
VIEW_MENU_OPPONENT = (560.0, 150.0)
VIEW_OPPONENT_COMPUTER = (752.5, 270.0)
VIEW_OPPONENT_BACK = (560.0, 586.0)
VIEW_MENU_CLOSE = (560.0, 694.0)
VIEW_PARKED_POINTER = (20.0, 780.0)  # the bottom left margin: no button, no square
FIRST_SQUARE = "e2"
SECOND_SQUARE = "e4"

# An animation frame loop of the page itself: its callbacks stop while the main thread is busy, which is what "the page keeps drawing"
# means. Installed before the game's scripts; reset() starts a new window of observation.
FRAME_COUNTER_SCRIPT = """(() => {
    const stats = {count: 0, maxGapMs: 0, last: 0};
    const tick = (now) => {
        if (stats.last) {
            stats.maxGapMs = Math.max(stats.maxGapMs, now - stats.last);
        }
        stats.last = now;
        stats.count += 1;
        window.requestAnimationFrame(tick);
    };
    window.requestAnimationFrame(tick);
    window.__chessFrames = {
        reset: () => { stats.count = 0; stats.maxGapMs = 0; stats.last = 0; },
        read: () => ({count: stats.count, maxGapMs: stats.maxGapMs}),
    };
})();"""


class LayoutError(Exception):
    pass


@dataclass(frozen=True)
class Layout:
    """Where the Wide view of the scene lies in a canvas of `width` x `height` pixels: the view is scaled to fit and centred."""

    width: float
    height: float

    @property
    def scale(self) -> float:
        return min(self.width / VIEW_SIZE[0], self.height / VIEW_SIZE[1])

    def to_pixel(self, point: tuple[float, float]) -> tuple[float, float]:
        scale = self.scale
        left = (self.width - VIEW_SIZE[0] * scale) / 2
        top = (self.height - VIEW_SIZE[1] * scale) / 2
        return left + point[0] * scale, top + point[1] * scale

    def board_clip(self) -> dict[str, float]:
        """The board in canvas pixels, as the `clip` of a screenshot."""
        left, top = self.to_pixel(BOARD_ORIGIN)
        side = BOARD_SQUARES * SQUARE_SIZE * self.scale
        return {"x": left, "y": top, "width": side, "height": side}


def wide_layout(width: float, height: float) -> Layout:
    """The layout of a canvas the scene shows as its Wide view; any other shape shows another view, whose points are not these."""
    if height <= 0 or width / height < WIDE_MIN_ASPECT:
        raise LayoutError(f"a {width:g}x{height:g} window is not in the Wide view (aspect {WIDE_MIN_ASPECT:g} up): the click points are its own")
    return Layout(width, height)


def square_centre(square: str) -> tuple[float, float]:
    """The centre of a square in view units, White at the bottom (the board of a new game)."""
    if len(square) != 2 or square[0] not in "abcdefgh" or square[1] not in "12345678":
        raise ValueError(f"'{square}' is not a square")
    file = ord(square[0]) - ord("a")
    rank = int(square[1]) - 1
    return (BOARD_ORIGIN[0] + (file + 0.5) * SQUARE_SIZE, BOARD_ORIGIN[1] + (BOARD_SQUARES - 1 - rank + 0.5) * SQUARE_SIZE)


# ----- judging: no browser needed -----

@dataclass
class Watch:
    """What watch_until_settled saw."""

    picture: bytes | None  # the last sample
    changed: bool  # a sample that is none of the ignored pictures was seen
    settled: bool  # STABLE_SAMPLES equal samples in a row, none of them ignored
    seconds: float


def watch_until_settled(grab: Callable[[], bytes], ignore: Collection[bytes], timeout: float, interval: float = SAMPLE_INTERVAL_SECONDS,
                        stable_samples: int = STABLE_SAMPLES, now: Callable[[], float] = time.monotonic,
                        sleep: Callable[[float], None] = time.sleep) -> Watch:
    """Samples `grab` until `stable_samples` equal samples in a row are none of the `ignore` pictures, or `timeout` seconds are gone.

    A slide of a piece makes pictures that are not stable, and the picture before the click, the one of "the move alone" and a stale
    shot are the ignored ones, so only a board that is new and at rest ends the wait. With nothing to ignore it waits for rest."""
    started = now()
    deadline = started + timeout
    previous = None
    stable = 0
    changed = False
    while True:
        picture = grab()
        if picture in ignore:
            stable = 0
        else:
            changed = True
            stable = stable + 1 if picture == previous else 1
            if stable >= stable_samples:
                return Watch(picture, True, True, now() - started)
        previous = picture
        if now() >= deadline:
            return Watch(picture, changed, False, now() - started)
        sleep(interval)


def judge_game(start: bytes, moved: Watch, replied: Watch | None, reply_timeout: float) -> list[str]:
    """The reasons of checks 6 and 7 on the board pictures: `start` before the first click, `moved` after e2 e4 with two people,
    `replied` after the same clicks against the program (None: not reached)."""
    problems = []
    if not moved.settled:
        problems.append(f"(6) the board did not settle after {FIRST_SQUARE} {SECOND_SQUARE} within {STEP_TIMEOUT_SECONDS:g} s")
    elif moved.picture == start:
        problems.append(f"(6) the board is as it was after {FIRST_SQUARE} {SECOND_SQUARE}: the move was not played")
    if replied is not None and not replied.settled:
        if replied.changed:
            problems.append(f"(7) the board did not come to rest within {reply_timeout:g} s after the person's move: the reply keeps moving it")
        else:
            problems.append(f"(7) the program did not reply within {reply_timeout:g} s: the board shows no move but the person's")
    return problems


def frame_problem(frames: int, longest_gap_seconds: float, report_only: bool) -> str | None:
    """Check 8. `report_only` (the single-thread build) never fails for a long gap, but a page that drew no frame at all is frozen."""
    if frames == 0:
        return "(8) the page drew no frame while the program was thinking"
    if not report_only and longest_gap_seconds > FROZEN_FRAME_SECONDS:
        return f"(8) the page stood still for {longest_gap_seconds:.1f} s while the program was thinking (more than {FROZEN_FRAME_SECONDS:g} s)"
    return None


# ----- command line -----

@dataclass
class ChessOptions:
    dist: Path
    single_thread: bool
    reply_timeout: float
    save_shots: Path | None
    web: smoke.Options  # what the engine's launch and page helpers read


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Open the Chess web page in a headless Chromium-based browser and play e2 e4 against the computer.")
    parser.add_argument("--dist", type=Path, required=True, help="folder made by tools/web/package.py (holds index.html)")
    parser.add_argument("--single-thread", action="store_true", help="the dist is the single-thread build: a long frame is reported, not failed")
    parser.add_argument("--viewport", type=smoke.parse_size, metavar="WxH", default=DEFAULT_VIEWPORT,
                        help=f"size of the browser window, in the Wide view (aspect {WIDE_MIN_ASPECT:g} up) (default: {DEFAULT_VIEWPORT[0]}x{DEFAULT_VIEWPORT[1]})")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_SECONDS,
                        help=f"seconds to wait for the page and the first frame (default: {DEFAULT_TIMEOUT_SECONDS:g})")
    parser.add_argument("--reply-timeout", type=float, default=DEFAULT_REPLY_TIMEOUT_SECONDS,
                        help=f"seconds the program may take to reply (default: {DEFAULT_REPLY_TIMEOUT_SECONDS:g})")
    parser.add_argument("--allow-console", action="append", default=[], metavar="REGEX",
                        help="an error-level console message matching this is not a failure (repeatable)")
    parser.add_argument("--expect-console", action="append", default=[], metavar="REGEX",
                        help="fail unless a console message (any level) matching this appears (repeatable)")
    parser.add_argument("--chromium-arg", action="append", default=[], metavar="ARG", help="an extra command-line flag for Chromium (repeatable)")
    parser.add_argument("--browser-channel", metavar="NAME", help="an installed browser instead of Playwright's Chromium: msedge, chrome, ...")
    parser.add_argument("--executable-path", type=Path, metavar="FILE", help="a Chromium-based executable to launch")
    parser.add_argument("--save-shots", type=Path, metavar="DIR", help="keep the picture of every step in this folder")
    return parser


def parse_options(argv: list[str] | None = None) -> ChessOptions:
    """The command line as options; a wrong one ends the program with exit code 2 (argparse)."""
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.timeout <= 0 or args.reply_timeout <= 0:
        parser.error("--timeout and --reply-timeout must be positive")
    if args.browser_channel and args.executable_path:
        parser.error("name at most one of --browser-channel and --executable-path")
    try:
        wide_layout(*args.viewport)
    except LayoutError as error:
        parser.error(str(error))
    for option, patterns in (("--allow-console", args.allow_console), ("--expect-console", args.expect_console)):
        for pattern in patterns:
            try:
                re.compile(pattern)
            except re.error as error:
                parser.error(f"{option} '{pattern}' is not a regular expression: {error}")
    web = smoke.Options(args.dist, False, None, args.viewport, args.timeout, 0.0, [*ALLOWED_CONSOLE, *args.allow_console], args.chromium_arg,
                        "none", args.browser_channel, args.executable_path, expect_console=args.expect_console)
    return ChessOptions(args.dist, args.single_thread, args.reply_timeout, args.save_shots, web)


# ----- the browser -----

@dataclass
class ChessRun:
    """What the browser showed, as plain data, and the problems the steps themselves found."""

    page: smoke.BrowserRun
    problems: list[str] = field(default_factory=list)
    moved: Watch | None = None
    replied: Watch | None = None
    start: bytes | None = None
    frames: int = 0
    longest_gap_seconds: float = 0.0
    reached_reply: bool = False


class Shots:
    """The pictures of --save-shots, numbered in the order they are taken; nothing without a folder."""

    def __init__(self, folder: Path | None):
        self._folder = folder
        self._count = 0
        if folder is not None:
            folder.mkdir(parents=True, exist_ok=True)

    def save(self, name: str, png: bytes) -> None:
        if self._folder is None:
            return
        self._count += 1
        (self._folder / f"{self._count:02d}-{name}.png").write_bytes(png)


def digest(png: bytes) -> bytes:
    return hashlib.sha256(png).digest()


def run_browser(playwright_api, url: str, options: ChessOptions) -> ChessRun:
    """Plays the steps and collects what was seen; does not judge the page checks (evaluate() of web_smoke does). Raises
    smoke.BrowserMissing when the browser the options name is not there."""
    web = options.web
    run = ChessRun(smoke.BrowserRun(check_canvas_size=True))
    shots = Shots(options.save_shots)
    with playwright_api.sync_playwright() as playwright:
        browser = smoke.launch_browser(playwright, web)
        try:
            run.page.status = smoke.fetch_from(url)(smoke.PAGE_QUERY)[0]  # page.goto raises on a network failure, not on an HTTP error
            page, canvas = smoke.open_game_page(browser, url, web, run.page.console, run.page.page_errors, FRAME_COUNTER_SCRIPT)
            try:
                canvas.wait_for(state="visible", timeout=web.timeout * 1000)
            except Exception as error:
                run.problems.append(f"(2) no visible {smoke.CANVAS_SELECTOR} within {web.timeout:g} s: {str(error).splitlines()[0]}")
                return run
            sleep = lambda seconds: page.wait_for_timeout(seconds * 1000)  # noqa: E731  (the page's timer handles the console events)
            run.page.first_png, drawn = smoke.poll(canvas.screenshot, smoke.png_is_drawn, web.timeout, sleep=sleep)
            if not drawn:
                return run
            run.page.page_info = smoke.read_page_info(page)
            shots.save("first-frame", run.page.first_png)
            try:
                play(page, canvas, run, options, shots, sleep)
            finally:
                if run.problems:
                    shots.save("last", canvas.screenshot())
            smoke.wait_for_expected_console(page, run.page, web.expect_console, web.timeout)
        finally:
            browser.close()
    return run


def play(page, canvas, run: ChessRun, options: ChessOptions, shots: Shots, sleep: Callable[[float], None]) -> None:
    box = canvas.bounding_box()
    layout = wide_layout(box["width"], box["height"])

    def pixel(point: tuple[float, float]) -> tuple[float, float]:
        x, y = layout.to_pixel(point)
        return box["x"] + x, box["y"] + y

    clip = layout.board_clip()
    clip = {"x": box["x"] + clip["x"], "y": box["y"] + clip["y"], "width": clip["width"], "height": clip["height"]}

    def whole() -> bytes:
        return page.screenshot(clip={"x": box["x"], "y": box["y"], "width": box["width"], "height": box["height"]})

    def board() -> bytes:
        return page.screenshot(clip=clip)

    def park() -> None:
        page.mouse.move(*pixel(VIEW_PARKED_POINTER))

    def click(point: tuple[float, float]) -> None:
        x, y = pixel(point)
        page.mouse.move(x, y)
        page.mouse.click(x, y)
        park()

    def click_step(name: str, point: tuple[float, float]) -> None:
        """Check 3: a click on a button of the menu changes the picture of the page."""
        before = watch_until_settled(lambda: digest(whole()), (), STEP_TIMEOUT_SECONDS, sleep=sleep)
        click(point)
        after = watch_until_settled(lambda: digest(whole()), (before.picture,), STEP_TIMEOUT_SECONDS, sleep=sleep)
        shots.save(name, whole())
        if not after.settled:
            run.problems.append(f"(3) the picture did not change after the click on {name} (view point {point[0]:g},{point[1]:g})")

    def play_two_clicks() -> Watch:
        """e2, then e4; returns the board that follows the second click. The selection of the first click is not what a move leaves."""
        click(square_centre(FIRST_SQUARE))
        selected = watch_until_settled(lambda: digest(board()), (), STEP_TIMEOUT_SECONDS, sleep=sleep)
        shots.save("selected", whole())
        click(square_centre(SECOND_SQUARE))
        return selected

    park()
    start = watch_until_settled(lambda: digest(board()), (), STEP_TIMEOUT_SECONDS, sleep=sleep)
    run.start = start.picture

    # Check 6: two people (the state of a new game) play the move, which gives the picture of the move alone
    selected = play_two_clicks()
    run.moved = watch_until_settled(lambda: digest(board()), (selected.picture,), STEP_TIMEOUT_SECONDS, sleep=sleep)
    shots.save("moved-by-two-people", whole())
    if not run.moved.settled:
        return

    # Check 3: Reset, then the program is picked as the opponent
    click_step("Reset", VIEW_RESET_BUTTON)
    for name, point in (("Menu", VIEW_MENU_BUTTON), ("Opponent", VIEW_MENU_OPPONENT), ("Computer", VIEW_OPPONENT_COMPUTER),
                        ("Back", VIEW_OPPONENT_BACK), ("Close", VIEW_MENU_CLOSE)):
        click_step(name, point)

    # Check 7: the same clicks, now answered; frames are counted from the second click to the settled reply
    selected = play_two_clicks()
    page.evaluate("window.__chessFrames.reset()")
    run.replied = watch_until_settled(lambda: digest(board()), (selected.picture, run.moved.picture, run.start), options.reply_timeout, sleep=sleep)
    frames = page.evaluate("window.__chessFrames.read()")
    run.frames = frames["count"]
    run.longest_gap_seconds = frames["maxGapMs"] / 1000
    run.reached_reply = True
    shots.save("replied", whole())


def judge(run: ChessRun, options: ChessOptions) -> list[str]:
    """The reasons the run failed, one line each; empty when it passed."""
    problems = smoke.evaluate(run.page, options.web.allow_console, options.web.expect_console)
    problems.extend(run.problems)
    if run.moved is not None and run.start is not None:
        problems.extend(judge_game(run.start, run.moved, run.replied, options.reply_timeout))
    if run.reached_reply:
        problem = frame_problem(run.frames, run.longest_gap_seconds, options.single_thread)
        if problem:
            problems.append(problem)
    return problems


def run_smoke(options: ChessOptions, playwright_api) -> int:
    root = options.dist
    if not (root / smoke.PAGE_NAME).is_file():
        print(f"FAIL: {root / smoke.PAGE_NAME} not found: package a web build first (tools/web/package.py)")
        return EXIT_FAILED
    with smoke.served(root) as url:
        print(f"web_chess_smoke: {root} on {url}", flush=True)
        try:
            run = run_browser(playwright_api, url, options)
        except smoke.BrowserMissing as error:
            named = options.web.browser_channel or options.web.executable_path
            print(f"SKIP: the browser is not installed ({'the one you named' if named else 'playwright install chromium'}): {error}")
            return EXIT_SKIPPED
        except Exception as error:  # a timeout of goto, a closed browser: the page did not come up
            print(f"FAIL: (1) the browser run failed: {str(error).strip().splitlines()[0] if str(error).strip() else type(error).__name__}")
            return EXIT_FAILED
    for line in smoke.describe_page(run.page):
        print(line)
    if run.reached_reply:
        print(f"reply: {run.replied.seconds:.1f} s from the second click to a board at rest; {run.frames} frames of the page in it, the longest gap "
              f"{run.longest_gap_seconds * 1000:.0f} ms{' (reported only: single-thread build)' if options.single_thread else ''}")
    problems = judge(run, options)
    if problems:
        for problem in problems:
            print(f"FAIL: {problem}")
        return EXIT_FAILED
    print("ok: (1) the page loaded with HTTP 200 and no page error")
    print("ok: (2) the first frame is drawn (not one flat color)")
    print("ok: (3) Reset, Menu, Opponent, Computer, Back and Close each changed the picture")
    errors = sum(1 for entry in run.page.console if entry.kind == "error")
    print(f"ok: (4) no console error outside the allow-list ({errors} allowed, {len(run.page.console)} messages)")
    info = run.page.page_info
    print(f"ok: (5) the backing store {info.canvas_width}x{info.canvas_height} is the CSS size {info.css_width:g}x{info.css_height:g} "
          f"x devicePixelRatio {info.device_pixel_ratio:g}")
    print(f"ok: (6) {FIRST_SQUARE} {SECOND_SQUARE} with two people changed the board")
    print("ok: (7) the program replied, and the board came to rest")
    print("ok: (8) the page kept drawing while the program thought")
    print("PASS")
    return EXIT_PASSED


def main(argv: list[str] | None = None, load: Callable = smoke.load_playwright) -> int:
    options = parse_options(argv)
    playwright_api = load()
    if playwright_api is None:
        print("SKIP: playwright is not installed")
        return EXIT_SKIPPED
    return run_smoke(options, playwright_api)


if __name__ == "__main__":
    sys.exit(main())
