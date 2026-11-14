#!/usr/bin/env python3
"""Takes the screenshot set of BagiChess for the todo loop: one small PNG per scene, from a game that runs headless on the manual clock.
Standard library only. The set is described in `.claude/rules/qa-project.md` ("## Screenshots"), which `engine/tools/shots/shots.py` reads;
this file is the `command` of that set, and can be run by hand:

    python tools/ui_shots.py --list
    python tools/ui_shots.py --exe build/windows-clang-debug/bin/BagiChess.exe
    python tools/ui_shots.py --exe build/macos-debug/bin/BagiChess --scenes start,checkmate --label before --out /tmp/shots

Every scene is played in a game of its own (`--control --headless --manual-clock`, a free port), so no scene inherits an open panel or a
position from another. A scene is driven only through the control API (`perform_action`, `set_state`, `ui_click`, `step`) and the picture
is the `screenshot` of it, written to `<out>/<label>/<scene>_<W>x<H>.png`: the file name is the pairing key of `shots.py diff`, and the same
build gives the same bytes. A scene reads back what it set up and stops with its name and the call when the game does not agree (a renamed
button must stop the run, not give a picture of the wrong screen). Nothing is written in the repository: `--out` inside a git work tree is
refused.

**One size.** A headless window has the size of the engine's `SDLWindowConfig.xml` (1920x1080); no flag and no control method changes it,
and Chess may not ship a `config/SDLWindowConfig.xml` of its own (`ChessWebConfigTest`). So `--window` takes the one size, and a narrow
window or a promotion panel (it opens from a click on a square, which the control API has no way to make) is not in the set.

**The build is here.** `shots.py` builds with a plain `cmake --build`, which a Ninja preset on Windows cannot run without the Visual
Studio environment (`.claude/rules/build-project.md`), so the set lists no `targets` and this script builds `BagiChess` itself unless
`--no-build`: the preset is the one of the path of `--exe` (`<root>/build/<preset>/bin/<file>`; another path builds nothing), and on
Windows the command goes through `engine/CI/scripts/vs_env.py`. A preset that was never configured is not configured here.

Exit code: 0 done, 1 a failure (a build, a game that did not start, a refused call, a scene that did not reach its state, a picture of
another aspect than the window, an unsupported `--window`, an `--out` inside a repository), 2 arguments argparse cannot read (also an
unknown scene), 77 the executable is missing (`--exe` without `.exe` finds the file with it).
"""

import argparse
import base64
import binascii
import contextlib
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterator

TOOLS_DIR = Path(__file__).resolve().parent
REPOSITORY_ROOT = TOOLS_DIR.parent
DEFAULT_ENGINE_DIR = REPOSITORY_ROOT / "engine"
for _directory in (DEFAULT_ENGINE_DIR / "tools" / "control", DEFAULT_ENGINE_DIR / "tools" / "shots"):
    if str(_directory) not in sys.path:
        sys.path.insert(0, str(_directory))

from bagi_control_client import BagiControlClient, ConnectionClosed, ControlError  # noqa: E402
from game_process import GameLaunchError, GameProcess, build_game_command, free_port  # noqa: E402
from shots import inside_git_work_tree  # noqa: E402

EXIT_DONE = 0
EXIT_FAILED = 1
EXIT_SKIPPED = 77

DEFAULT_WINDOW = "1920x1080"
DEFAULT_MAX_SIZE = 800  # the long side of a picture: the contract says about 640, but the words of the HUD have to stay readable
DEFAULT_LABEL = "manual"
OUT_FOLDER_NAME = "bagi-chess-shots"
ASPECT_TOLERANCE_PIXELS = 1
SHOT_FRAMES = 2  # frames before every picture: the layout of the last draw is what the picture shows
SETTLE_FRAMES = 4  # a played move slides for 0.15 s (ChessPieceComponent): four frames of 0.1 s
SETTLE_FRAME_SECONDS = 0.1
GAME_TARGET = "BagiChess"
VS_ENV_SCRIPT = Path("CI") / "scripts" / "vs_env.py"
BUILD_TAIL_CHARS = 2000

START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
AFTER_E4_FEN = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"
MATE_IN_ONE_FEN = "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"
MATING_MOVE = "a1a8"
STALEMATE_FEN = "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"
MIDDLEGAME_MOVES = ("e2e4", "d7d5", "e4d5", "d8d5", "b1c3", "d5a5")
OPPONENT_PANEL_CLICKS = ("Menu", "MenuSectionOpponent", "EngineOwn", "ModeComputer", "LevelMedium", "ColorWhite")  # the person is White: the program plays Black and has nothing to answer
OPPONENT_PANEL_EXPECTED = {"plays": "black", "engine": "own", "level": "medium"}
TURNED_BOARD_OPPONENT = {"plays": "white", "engine": "own", "level": "max"}  # the person is Black; White's turn is not on, so nothing is searched


class ShotError(Exception):
    """A scene or the build cannot go on; the message is printed (with the scene's name) and the exit code is 1."""


@dataclass
class Session:
    """One game as a scene sees it: the control calls, the frames and the picture."""

    client: object  # a BagiControlClient, or a stand-in with `call`
    out_dir: Path
    window: str = DEFAULT_WINDOW
    max_size: int = DEFAULT_MAX_SIZE

    def call(self, method: str, params: dict | None = None) -> dict:
        try:
            return self.client.call(method, params)
        except ControlError as error:
            raise ShotError(f"the call {method} {params if params is not None else ''} was refused: {error}") from error
        except (ConnectionClosed, OSError) as error:
            raise ShotError(f"the call {method} lost the game: {error}") from error

    def step(self, frames: int = SHOT_FRAMES, dt: float | None = None) -> None:
        params: dict = {"frames": frames}
        if dt is not None:
            params["dt"] = dt
        self.call("step", params)

    def settle(self) -> None:
        """The pieces of a move just played have finished sliding."""
        self.step(SETTLE_FRAMES, SETTLE_FRAME_SECONDS)

    def state(self) -> dict:
        return self.call("get_state")["state"]

    def check(self, condition: bool, what: str) -> None:
        if not condition:
            raise ShotError(what)

    def shoot(self, scene: str) -> Path:
        """The picture of the game now, as `<out>/<scene>_<W>x<H>.png`; a half-written file is never left behind."""
        self.step()
        answer = self.call("screenshot", {"maxSize": self.max_size})
        width, height = parse_window(self.window)
        picture_width, picture_height = answer["width"], answer["height"]
        if abs(picture_height * width - picture_width * height) > ASPECT_TOLERANCE_PIXELS * width:
            raise ShotError(f"the picture is {picture_width}x{picture_height}, not the aspect of the window {self.window}")
        try:
            data = base64.b64decode(answer["png_base64"], validate=True)
        except (binascii.Error, KeyError, TypeError) as error:
            raise ShotError(f"the screenshot has no readable picture: {error}") from error
        self.out_dir.mkdir(parents=True, exist_ok=True)
        target = self.out_dir / f"{scene}_{self.window}.png"
        partial = target.with_name(target.name + ".part")
        partial.write_bytes(data)
        os.replace(partial, target)
        return target


def play_moves(session: Session, moves: tuple[str, ...]) -> None:
    for uci in moves:
        session.call("perform_action", {"type": "move", "uci": uci})


def scene_start(session: Session) -> None:
    session.step()
    state = session.state()
    session.check(state["fen"] == START_FEN, f"the game did not start from the start position: {state['fen']}")


def scene_middlegame(session: Session) -> None:
    session.step()
    play_moves(session, MIDDLEGAME_MOVES)
    material = session.state()["material"]
    taken = (sum(material["capturedByWhite"].values()), sum(material["capturedByBlack"].values()))
    session.check(all(taken), f"after {' '.join(MIDDLEGAME_MOVES)} both sides should have taken something: {material}")
    session.settle()


def scene_opponent_panel(session: Session) -> None:
    session.step()
    for name in OPPONENT_PANEL_CLICKS:
        session.call("ui_click", {"name": name})
        session.step()
    opponent = session.state()["opponent"]
    for key, value in OPPONENT_PANEL_EXPECTED.items():
        session.check(opponent[key] == value, f"after the clicks {' '.join(OPPONENT_PANEL_CLICKS)} the opponent is {opponent}, but {key} should be {value!r}")


def scene_turned_board(session: Session) -> None:
    session.step()
    session.call("set_state", {"opponent": TURNED_BOARD_OPPONENT, "fen": AFTER_E4_FEN})
    session.step()
    state = session.state()
    session.check(state["turn"] == "black", f"the position should have Black to move: {state['fen']}")
    session.check(state["opponent"]["plays"] == "white" and not state["opponent"]["thinking"], f"the program should play White and not search: {state['opponent']}")


def scene_checkmate(session: Session) -> None:
    session.step()
    session.call("set_state", {"fen": MATE_IN_ONE_FEN})
    session.call("perform_action", {"type": "move", "uci": MATING_MOVE})
    session.settle()
    state = session.state()
    session.check(state["status"] == "checkmate" and state["winner"] == "white", f"{MATING_MOVE} should mate: {state['status']}, {state['winner']}")


def scene_stalemate(session: Session) -> None:
    session.step()
    session.call("set_state", {"fen": STALEMATE_FEN})
    session.step()
    session.check(session.state()["status"] == "stalemate", f"{STALEMATE_FEN} should be a stalemate")


# name -> (what it shows, how to reach it); the names are the pairing key of `shots.py diff` and the sentences are those of
# `.claude/rules/qa-project.md`, which a test keeps equal
SCENES: dict[str, tuple[str, Callable[[Session], None]]] = {
    "start": ("the start position of two people: the HUD, the coordinates and the empty margins", scene_start),
    "middlegame": ("a scripted game with a capture by each side: the captured material, the marks of the last move, the HUD", scene_middlegame),
    "opponent-panel": ("the opponent panel open, set to Computer, the own engine, medium level, the person as White", scene_opponent_panel),
    "turned-board": ("the board turned for a person who plays Black against the program as White", scene_turned_board),
    "checkmate": ("a game won by a checkmate: the red king, the dimmed board, the bar and the HUD saying who won", scene_checkmate),
    "stalemate": ("a game drawn by a stalemate: the gray bar of a draw", scene_stalemate),
}


def parse_window(text: str) -> tuple[int, int]:
    width, _, height = text.partition("x")
    return int(width), int(height)


def locate_build(exe: Path) -> tuple[Path, str] | None:
    """(repository root, preset) when `exe` is `<root>/build/<preset>/bin/<file>`, else None: a path of another shape builds nothing."""
    path = exe.absolute()
    bin_dir = path.parent
    preset_dir = bin_dir.parent
    if bin_dir.name != "bin" or preset_dir.parent.name != "build":
        return None
    return preset_dir.parent.parent, preset_dir.name


def build_command(preset: str, platform: str, engine: Path) -> list[str]:
    """The build of the game; a Ninja preset on Windows needs the Visual Studio environment, which `vs_env.py` of the engine sets."""
    command = ["cmake", "--build", "--preset", preset, "--target", GAME_TARGET]
    if platform == "win32":
        return [sys.executable, str(engine / VS_ENV_SCRIPT), *command]
    return command


def resolve_executable(exe: Path) -> Path | None:
    """`exe` itself, or `exe` with `.exe` when only that is a file (one `exe` value of the set serves every host)."""
    if exe.is_file():
        return exe.absolute()
    with_suffix = exe.with_name(exe.name + ".exe")
    return with_suffix.absolute() if with_suffix.is_file() else None


def run_build(root: Path, command: list[str]) -> None:
    try:
        done = subprocess.run(command, cwd=root, capture_output=True, text=True)
    except OSError as error:
        raise ShotError(f"cannot run {' '.join(command)}: {error}") from error
    if done.returncode != 0:
        raise ShotError(f"the build failed ({' '.join(command)}):\n{(done.stdout + done.stderr)[-BUILD_TAIL_CHARS:]}")


@contextlib.contextmanager
def open_game(exe: Path) -> Iterator[BagiControlClient]:
    """A game of its own: headless, on the manual clock, on a free port; asked to quit at the end (killed when it does not)."""
    port = free_port()
    with GameProcess(build_game_command(exe, port), cwd=exe.parent) as game:
        try:
            client = game.connect(port)
        except GameLaunchError as error:
            raise ShotError(str(error)) from error
        try:
            yield client
        finally:
            with contextlib.suppress(ControlError, ConnectionClosed, OSError):
                client.call("quit")
            client.close()


def play_scene(name: str, client: object, out_dir: Path, window: str = DEFAULT_WINDOW, max_size: int = DEFAULT_MAX_SIZE) -> Path:
    """Plays one scene on `client` and gives its picture; every failure is a ShotError that names the scene."""
    session = Session(client, out_dir, window, max_size)
    try:
        SCENES[name][1](session)
        return session.shoot(name)
    except ShotError as error:
        raise ShotError(f"scene {name}: {error}") from error


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Takes the screenshot set of BagiChess (docstring of this file).")
    parser.add_argument("--exe", help="the BagiChess executable (without .exe it is found with it)")
    parser.add_argument("--engine", type=Path, default=DEFAULT_ENGINE_DIR, help="the engine folder (vs_env.py for the build on Windows)")
    parser.add_argument("--label", default=DEFAULT_LABEL, help="the subfolder of --out the pictures go to")
    parser.add_argument("--out", type=Path, default=Path(tempfile.gettempdir()) / OUT_FOLDER_NAME, help="outside every git work tree")
    parser.add_argument("--scenes", default=",".join(SCENES), help="comma separated; default all")
    parser.add_argument("--window", default=DEFAULT_WINDOW, help=f"WxH; only {DEFAULT_WINDOW} (the size of a headless window)")
    parser.add_argument("--max-size", type=int, default=DEFAULT_MAX_SIZE, help="the long side of a picture")
    parser.add_argument("--no-build", action="store_true", help="do not build BagiChess first")
    parser.add_argument("--list", action="store_true", help="print the scene names and exit")
    return parser


def main(argv: list[str] | None = None, game_opener: Callable = open_game) -> int:
    parser = make_parser()
    args = parser.parse_args(argv)
    if args.list:
        for name, (about, _) in SCENES.items():
            print(f"{name}: {about}")
        return EXIT_DONE
    if not args.exe:
        parser.error("--exe is required (or --list)")
    scenes = [name for name in (part.strip() for part in args.scenes.split(",")) if name]
    unknown = [name for name in scenes if name not in SCENES]
    if unknown or not scenes:
        parser.error(f"unknown scene '{', '.join(unknown)}'; known scenes: {', '.join(SCENES)}" if unknown else "no scenes requested")
    if args.window != DEFAULT_WINDOW:
        print(f"ui_shots: the window {args.window} is not available: a headless window has the size of the engine's config, {DEFAULT_WINDOW}, and the engine has no way to change it")
        return EXIT_FAILED
    if inside_git_work_tree(args.out):
        print(f"ui_shots: --out {args.out} is inside a git work tree: pictures must stay outside every repository")
        return EXIT_FAILED

    requested = Path(args.exe)
    location = locate_build(requested)
    try:
        if location and not args.no_build and (location[0] / "build" / location[1] / "CMakeCache.txt").is_file():
            run_build(location[0], build_command(location[1], sys.platform, args.engine))
        exe = resolve_executable(requested)
        if exe is None:
            print(f"ui_shots: {requested} does not exist (is its preset configured and built?)")
            return EXIT_SKIPPED
        folder = args.out / args.label
        for name in scenes:
            with game_opener(exe) as client:
                path = play_scene(name, client, folder, args.window, args.max_size)
            print(f"{name}: {path}")
    except ShotError as error:
        print(f"ui_shots: {error}")
        return EXIT_FAILED
    print(f"pictures in {folder}")
    return EXIT_DONE


if __name__ == "__main__":
    sys.exit(main())
