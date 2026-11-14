"""The screenshot command of the game (tools/ui_shots.py): the scenes against a scripted game, the arguments, the exit codes and the build
command. No executable and no game needed.

    engine/.venv/Scripts/python -m pytest tools/tests/test_ui_shots.py
"""

import base64
import contextlib
import sys
from pathlib import Path

import pytest

import ui_shots
from bagi_control_client import ControlError
from ui_shots import (
    AFTER_E4_FEN,
    DEFAULT_MAX_SIZE,
    DEFAULT_WINDOW,
    MATE_IN_ONE_FEN,
    MIDDLEGAME_MOVES,
    OPPONENT_PANEL_CLICKS,
    SCENES,
    STALEMATE_FEN,
    START_FEN,
    ShotError,
)

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "engine" / "tools" / "shots"))
import shots  # noqa: E402  (the engine's tool that reads the set)

FAKE_PNG = b"\x89PNG\r\n\x1a\nnot a real picture, the bytes only have to arrive"
PICTURE_WIDTH = 192
PICTURE_HEIGHT = 108


class FakeChess:
    """What the control API of the game answers, as far as the scenes read it: a handful of members of `get_state` that follow the calls."""

    def __init__(self, width: int = PICTURE_WIDTH, height: int = PICTURE_HEIGHT, misread_color: bool = False, refuse: str | None = None):
        self.width = width
        self.height = height
        self.misread_color = misread_color  # ColorWhite makes the program play White, as if the segment had been renamed
        self.refuse = refuse  # a method that answers an error
        self.calls: list[tuple[str, dict | None]] = []
        self.fen = START_FEN
        self.moves: list[str] = []
        self.status = "ongoing"
        self.winner = None
        self.opponent = {"plays": None, "level": "medium", "engine": "stockfish", "thinking": False}

    def call(self, method: str, params: dict | None = None) -> dict:
        self.calls.append((method, params))
        if method == self.refuse:
            raise ControlError("not_found", f"scripted refusal of {method}")
        return getattr(self, f"on_{method}")(params or {})

    def methods(self) -> list[str]:
        return [method for method, _ in self.calls]

    def params_of(self, method: str) -> list[dict | None]:
        return [params for name, params in self.calls if name == method]

    def on_step(self, params: dict) -> dict:
        return {}

    def on_set_state(self, params: dict) -> dict:
        self.opponent.update(params.get("opponent", {}))
        if "fen" in params:
            self.fen = params["fen"]
            self.moves = []
            self.status = "stalemate" if self.fen == STALEMATE_FEN else "ongoing"
        return {}

    def on_perform_action(self, params: dict) -> dict:
        self.moves.append(params["uci"])
        if self.fen == MATE_IN_ONE_FEN and params["uci"] == "a1a8":
            self.status, self.winner = "checkmate", "white"
        return {}

    def on_ui_click(self, params: dict) -> dict:
        name = params["name"]
        if name == "EngineOwn":
            self.opponent["engine"] = "own"
        elif name == "ModeComputer":
            self.opponent["plays"] = "black"
        elif name == "ColorWhite" and self.misread_color:
            self.opponent["plays"] = "white"
        return {"consumed": True}

    def on_get_state(self, params: dict) -> dict:
        taken_by_white = {"pawn": 1 if len(self.moves) >= 3 else 0}
        taken_by_black = {"pawn": 1 if len(self.moves) >= 4 else 0}
        turn = ("black" if len(self.moves) % 2 else "white") if self.moves else {"w": "white", "b": "black"}[self.fen.split()[1]]
        fen = self.fen if not self.moves else f"{self.fen} + {len(self.moves)} moves"
        return {"state": {"fen": fen, "turn": turn, "status": self.status, "winner": self.winner, "opponent": dict(self.opponent), "material": {"balance": 0, "capturedByWhite": taken_by_white, "capturedByBlack": taken_by_black}}}

    def on_screenshot(self, params: dict) -> dict:
        return {"width": self.width, "height": self.height, "png_base64": base64.b64encode(FAKE_PNG).decode("ascii")}


def opener_of(**fake_options):
    """A stand-in for `open_game`: a new fake game for every scene, as the real one starts a new process."""

    @contextlib.contextmanager
    def opener(exe: Path):
        yield FakeChess(**fake_options)

    return opener


def must_not_start(exe: Path):
    raise AssertionError("a game was started")


@pytest.fixture
def exe(tmp_path: Path) -> Path:
    path = tmp_path / "BagiChess"  # not in a build/<preset>/bin folder: nothing is built
    path.write_bytes(b"")
    return path


@pytest.mark.parametrize("scene", list(SCENES))
def test_a_scene_writes_exactly_one_file_with_the_decoded_picture(scene: str, tmp_path: Path):
    game = FakeChess()
    out_dir = tmp_path / "pictures"
    path = ui_shots.play_scene(scene, game, out_dir)
    assert path == out_dir / f"{scene}_{DEFAULT_WINDOW}.png"
    assert list(out_dir.iterdir()) == [path]
    assert path.read_bytes() == FAKE_PNG
    assert game.params_of("screenshot") == [{"maxSize": DEFAULT_MAX_SIZE}]
    assert game.methods()[-1] == "screenshot"


def test_the_start_scene_changes_nothing(tmp_path: Path):
    game = FakeChess()
    ui_shots.play_scene("start", game, tmp_path)
    assert set(game.methods()) == {"step", "get_state", "screenshot"}


def test_the_middlegame_plays_the_scripted_moves_and_settles(tmp_path: Path):
    game = FakeChess()
    ui_shots.play_scene("middlegame", game, tmp_path)
    assert game.moves == list(MIDDLEGAME_MOVES)
    assert {"frames": 4, "dt": 0.1} in game.params_of("step")


def test_the_opponent_panel_clicks_the_buttons_by_name_in_order(tmp_path: Path):
    game = FakeChess()
    ui_shots.play_scene("opponent-panel", game, tmp_path)
    assert [params["name"] for params in game.params_of("ui_click")] == list(OPPONENT_PANEL_CLICKS)
    assert game.opponent["plays"] == "black" and game.opponent["engine"] == "own" and game.opponent["level"] == "medium"


def test_the_opponent_panel_fails_when_the_read_back_is_wrong(tmp_path: Path):
    game = FakeChess(misread_color=True)
    with pytest.raises(ShotError, match="opponent-panel.*plays"):
        ui_shots.play_scene("opponent-panel", game, tmp_path)
    assert list(tmp_path.iterdir()) == []


def test_the_turned_board_sets_the_program_as_white_with_black_to_move_in_one_call(tmp_path: Path):
    game = FakeChess()
    ui_shots.play_scene("turned-board", game, tmp_path)
    (params,) = game.params_of("set_state")
    assert params["fen"] == AFTER_E4_FEN
    assert params["opponent"]["plays"] == "white" and params["opponent"]["engine"] == "own"


def test_the_checkmate_loads_the_mate_and_plays_it(tmp_path: Path):
    game = FakeChess()
    ui_shots.play_scene("checkmate", game, tmp_path)
    assert game.params_of("set_state") == [{"fen": MATE_IN_ONE_FEN}]
    assert game.moves == ["a1a8"]


def test_the_stalemate_loads_the_position(tmp_path: Path):
    game = FakeChess()
    ui_shots.play_scene("stalemate", game, tmp_path)
    assert game.params_of("set_state") == [{"fen": STALEMATE_FEN}]
    assert game.moves == []


def test_a_refused_call_names_the_scene_and_the_call(tmp_path: Path):
    game = FakeChess(refuse="ui_click")
    with pytest.raises(ShotError, match=r"scene opponent-panel.*ui_click.*refused"):
        ui_shots.play_scene("opponent-panel", game, tmp_path)


def test_a_picture_of_another_aspect_is_refused(tmp_path: Path):
    game = FakeChess(width=PICTURE_HEIGHT, height=PICTURE_WIDTH)
    with pytest.raises(ShotError, match="aspect"):
        ui_shots.play_scene("start", game, tmp_path)
    assert list(tmp_path.iterdir()) == []


def test_a_picture_one_pixel_off_the_aspect_is_taken(tmp_path: Path):
    ui_shots.play_scene("start", FakeChess(width=800, height=451), tmp_path)


def test_a_wrong_aspect_ends_the_run_with_exit_1(exe: Path, tmp_path: Path, capsys):
    code = ui_shots.main(["--exe", str(exe), "--out", str(tmp_path / "out"), "--scenes", "start"], opener_of(width=100, height=100))
    assert code == 1
    assert "start" in capsys.readouterr().out


def test_all_scenes_are_taken_into_the_label_folder(exe: Path, tmp_path: Path):
    out = tmp_path / "out"
    code = ui_shots.main(["--exe", str(exe), "--out", str(out), "--label", "after"], opener_of())
    assert code == 0
    assert sorted(path.name for path in (out / "after").iterdir()) == sorted(f"{scene}_{DEFAULT_WINDOW}.png" for scene in SCENES)


def test_the_scenes_option_picks_some_in_its_order(exe: Path, tmp_path: Path):
    out = tmp_path / "out"
    assert ui_shots.main(["--exe", str(exe), "--out", str(out), "--scenes", "checkmate,start"], opener_of()) == 0
    assert sorted(path.name for path in (out / "manual").iterdir()) == [f"checkmate_{DEFAULT_WINDOW}.png", f"start_{DEFAULT_WINDOW}.png"]


def test_another_window_exits_1_before_any_game_starts(exe: Path, tmp_path: Path, capsys):
    code = ui_shots.main(["--exe", str(exe), "--out", str(tmp_path / "out"), "--window", "720x1280"], must_not_start)
    assert code == 1
    assert "720x1280" in capsys.readouterr().out
    assert not (tmp_path / "out").exists()


def test_a_missing_executable_exits_77(tmp_path: Path):
    assert ui_shots.main(["--exe", str(tmp_path / "BagiChess"), "--out", str(tmp_path / "out")], must_not_start) == 77


def test_an_executable_without_the_suffix_is_found_with_it(tmp_path: Path):
    (tmp_path / "BagiChess.exe").write_bytes(b"")
    assert ui_shots.resolve_executable(tmp_path / "BagiChess") == (tmp_path / "BagiChess.exe").absolute()
    assert ui_shots.main(["--exe", str(tmp_path / "BagiChess"), "--out", str(tmp_path / "out"), "--scenes", "start"], opener_of()) == 0


def test_no_executable_at_all_is_77_even_with_the_suffix_missing(tmp_path: Path):
    assert ui_shots.resolve_executable(tmp_path / "BagiChess") is None


def test_an_unknown_scene_names_the_known_ones(exe: Path, tmp_path: Path, capsys):
    with pytest.raises(SystemExit) as stopped:
        ui_shots.main(["--exe", str(exe), "--out", str(tmp_path), "--scenes", "start,nowhere"], must_not_start)
    assert stopped.value.code == 2
    message = capsys.readouterr().err
    assert "nowhere" in message
    assert all(scene in message for scene in SCENES)


def test_list_prints_every_scene_and_needs_no_executable(capsys):
    assert ui_shots.main(["--list"], must_not_start) == 0
    lines = capsys.readouterr().out.splitlines()
    assert [line.split(":")[0] for line in lines] == list(SCENES)


def test_an_output_folder_inside_the_repository_is_refused(exe: Path, capsys):
    code = ui_shots.main(["--exe", str(exe), "--out", str(ui_shots.REPOSITORY_ROOT / "pictures-not-to-be-made")], must_not_start)
    assert code == 1
    assert "git work tree" in capsys.readouterr().out
    assert not (ui_shots.REPOSITORY_ROOT / "pictures-not-to-be-made").exists()


def test_the_default_output_folder_is_in_the_temp_folder_and_outside_a_repository():
    default = ui_shots.make_parser().parse_args([]).out
    assert default.name == ui_shots.OUT_FOLDER_NAME
    assert not ui_shots.inside_git_work_tree(default)


def test_the_build_command_goes_through_vs_env_on_windows():
    engine = Path("the-engine")
    plain = ["cmake", "--build", "--preset", "windows-clang-debug", "--target", "BagiChess"]
    assert ui_shots.build_command("windows-clang-debug", "darwin", engine) == plain
    assert ui_shots.build_command("windows-clang-debug", "linux", engine) == plain
    assert ui_shots.build_command("windows-clang-debug", "win32", engine) == [sys.executable, str(engine / "CI" / "scripts" / "vs_env.py"), *plain]


def test_the_preset_is_taken_from_the_path_of_the_executable(tmp_path: Path):
    exe = tmp_path / "build" / "macos-debug" / "bin" / "BagiChess"
    assert ui_shots.locate_build(exe) == (tmp_path, "macos-debug")
    assert ui_shots.locate_build(tmp_path / "BagiChess") is None
    assert ui_shots.locate_build(tmp_path / "elsewhere" / "bin" / "BagiChess") is None
    assert ui_shots.locate_build(tmp_path / "build" / "macos-debug" / "BagiChess") is None


def test_a_preset_that_was_never_configured_builds_nothing_and_skips(tmp_path: Path, monkeypatch):
    monkeypatch.setattr(ui_shots, "run_build", lambda root, command: pytest.fail("a build was run"))
    exe = tmp_path / "build" / "macos-debug" / "bin" / "BagiChess"
    assert ui_shots.main(["--exe", str(exe), "--out", str(tmp_path / "out")], must_not_start) == 77


def test_a_configured_preset_is_built_before_the_executable_is_looked_for(tmp_path: Path, monkeypatch):
    preset_dir = tmp_path / "build" / "macos-debug"
    (preset_dir / "bin").mkdir(parents=True)
    (preset_dir / "CMakeCache.txt").write_text("")
    built = []

    def fake_build(root: Path, command: list[str]) -> None:
        built.append((root, command))
        (preset_dir / "bin" / "BagiChess").write_bytes(b"")

    monkeypatch.setattr(ui_shots, "run_build", fake_build)
    code = ui_shots.main(["--exe", str(preset_dir / "bin" / "BagiChess"), "--out", str(tmp_path / "out"), "--scenes", "start"], opener_of())
    assert code == 0
    assert built == [(tmp_path, ui_shots.build_command("macos-debug", sys.platform, ui_shots.DEFAULT_ENGINE_DIR))]


def test_no_build_skips_the_build(tmp_path: Path, monkeypatch):
    preset_dir = tmp_path / "build" / "macos-debug"
    (preset_dir / "bin").mkdir(parents=True)
    (preset_dir / "CMakeCache.txt").write_text("")
    (preset_dir / "bin" / "BagiChess").write_bytes(b"")
    monkeypatch.setattr(ui_shots, "run_build", lambda root, command: pytest.fail("a build was run"))
    assert ui_shots.main(["--exe", str(preset_dir / "bin" / "BagiChess"), "--out", str(tmp_path / "out"), "--scenes", "start", "--no-build"], opener_of()) == 0


def test_a_failed_build_exits_1_with_its_output(tmp_path: Path, monkeypatch, capsys):
    preset_dir = tmp_path / "build" / "macos-debug"
    (preset_dir / "bin").mkdir(parents=True)
    (preset_dir / "CMakeCache.txt").write_text("")

    def failing_build(root: Path, command: list[str]) -> None:
        raise ShotError("the build failed: error: no such file")

    monkeypatch.setattr(ui_shots, "run_build", failing_build)
    assert ui_shots.main(["--exe", str(preset_dir / "bin" / "BagiChess"), "--out", str(tmp_path / "out")], must_not_start) == 1
    assert "no such file" in capsys.readouterr().out


# The set of .claude/rules/qa-project.md, read as the engine's shots.py reads it


@pytest.fixture(scope="module")
def shots_set() -> dict:
    return shots.load_set(ui_shots.REPOSITORY_ROOT)


def test_the_set_has_the_scenes_and_sentences_of_the_script(shots_set: dict):
    assert shots_set["scenes"] == {name: about for name, (about, _) in SCENES.items()}


def test_the_set_offers_the_window_of_the_script(shots_set: dict):
    assert DEFAULT_WINDOW in shots_set["sizes"]
    assert shots_set["defaultSize"] == DEFAULT_WINDOW


def test_the_set_leaves_the_build_to_the_script(shots_set: dict):
    assert shots_set["targets"] == []


def test_the_command_of_the_set_expands_to_the_script_with_every_placeholder_filled(shots_set: dict):
    values = {key: f"<{key}>" for key in ("root", "engine", "python", "bin", "exe", "label", "out", "scenes", "window")}
    command = shots.expand(shots_set["command"], values)
    assert "<python>" in command and "<root>/tools/ui_shots.py" in command
    assert not any("{" in part for part in command)
    parsed = ui_shots.make_parser().parse_args(command[command.index("<root>/tools/ui_shots.py") + 1 :])
    assert parsed.exe == "<exe>" and parsed.window == "<window>" and parsed.scenes == "<scenes>"
