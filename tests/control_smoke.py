#!/usr/bin/env python3
"""Smoke test of the control API for Chess: starts BagiChess with --control and plays over WebSocket.

    python tests/control_smoke.py --exe build/macos-debug/BagiChess

ctest runs it as BagiChessControlSmoke (be_add_game_control_tests). It builds on the engine's tools/control/tests/smoke.py,
which it finds through the environment variable BAGI_ENGINE_DIR (set by ctest), or in the submodule engine/. The game is played
on the real clock and with --manual-clock; one scenario plays moves before the first frame.

The scenarios of the own engine, the watch (two own engines), the analysis and the ratings, the opponent panel, the captured
material, the takeback and "without Stockfish" are base ones: the own engine runs inside the game, so they need nothing from the environment.
Stockfish ("opponent" scenarios) is replaced by tests/fake_uci_engine.py: main() writes a wrapper script that starts it into a
temporary directory and puts the script in STOCKFISH of the environment, which the game inherits. STOCKFISH comes before PATH in
the game's search for an engine and the committed config has no path, so a Stockfish installed on this machine is never used. A
config/ChessSettings.local.xml (a person's own path, which would win over STOCKFISH) leaves the Stockfish scenarios out, as does
Windows; the smoke never edits a person's files. A game with no Stockfish to run (a build without it, CHESS_NO_STOCKFISH, the web
build's world or a desktop build that switches it on, or a build with it where no file is found) is recognised by the script
itself, with no environment variable: the get_state of a new game says `opponent.engine` "own" instead of "stockfish". Then the
scenarios that need the fake Stockfish end at once, and the scenario "without Stockfish" checks what such a game does instead; in a
game that found a Stockfish it ends at once. On a machine whose STOCKFISH (or PATH) names a Stockfish, the base scenarios run with it.

The restart scenarios start the game with `--save-dir=<their own temporary folder>` (the flag turns the save on despite the
control flags), play, quit and start again on the same folder. A start with no `--save-dir` is the opening with nobody playing;
the person's own save folder is never named.

What a recorded scenario can state lives in `tests/scenarios/*.jsonl` (ctest `BagiChessScenario.<file>`, the manual clock, played
by the engine's bagi_play.py): the refusals, the mate and the game over, the reset, promotion, the numbers of the captured
material, the clicks of the opponent panel and the takeback. What stays here needs a wait for the program, a picture, the real clock or an
answer that depends on the build.

Exit code: 0 passed, 1 failed, 77 skipped (the executable does not exist).
"""

import os
import stat
import sys
import tempfile
import time
from pathlib import Path


def find_engine_tests_dir() -> Path:
    """tools/control/tests of the engine checkout this script belongs to."""
    here = Path(__file__).resolve()
    candidates = []
    if os.environ.get("BAGI_ENGINE_DIR"):
        candidates.append(Path(os.environ["BAGI_ENGINE_DIR"]))
    candidates.append(here.parents[1] / "engine")  # the engine as a submodule
    for engine in candidates:
        tests_dir = engine / "tools" / "control" / "tests"
        if (tests_dir / "smoke.py").is_file():
            return tests_dir
    raise SystemExit(f"cannot find tools/control/tests/smoke.py of the engine (looked in {[str(c) for c in candidates]}); set BAGI_ENGINE_DIR")


ENGINE_TESTS_DIR = find_engine_tests_dir()
sys.path.insert(0, str(ENGINE_TESTS_DIR))  # smoke.py
sys.path.insert(0, str(ENGINE_TESTS_DIR.parent))  # bagi_control_client.py

from bagi_control_client import BagiControlClient, ControlError  # noqa: E402
from smoke import BOARD_COLORS, check, check_state_push, expect_error, find_ui_element, run_smoke, take_screenshot  # noqa: E402

START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
START_BOARD_TOP = "rnbqkbnr"
START_BOARD_BOTTOM = "RNBQKBNR"
START_MOVE_COUNT = 20
OPENING = ["e2e4", "e7e5", "g1f3", "b8c6"]
OPENING_NAME = {"eco": "C44", "name": "King's Knight Opening: Normal Variation"}
MATE_IN_ONE_FEN = "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"  # Ra8#
PROMOTION_FEN = "8/P6k/8/8/8/8/8/K7 w - - 0 1"

REPO_ROOT = Path(__file__).resolve().parents[1]
FAKE_ENGINE = Path(__file__).resolve().with_name("fake_uci_engine.py")
LOCAL_SETTINGS = REPO_ROOT / "config" / "ChessSettings.local.xml"
FAKE_ENGINE_MOVES = "e7e5,b8c6,g8f6"  # what the fake answers to its 1st, 2nd and 3rd search
OPPONENT_WAIT_SECONDS = 30.0  # a stall of the machine is not a failure; a program that never answers is
POLL_SECONDS = 0.01
SETTLE_FRAMES = 4  # frames of the manual clock after a played move until its slide (0.15 s) is over
SETTLE_FRAME_SECONDS = 0.1
SETTLE_SECONDS = 0.5  # game time on the real clock, three times the slide
FRAMES_WITHOUT_AN_ANSWER = 10


def move(uci: str) -> dict:
    return {"type": "move", "uci": uci}


def check_start_position(client: BagiControlClient) -> None:
    reply = client.call("get_state")
    state = reply["state"]
    check(reply["game"] == "Chess", f"the active game is '{reply['game']}'")
    check(state["fen"] == START_FEN and state["startFen"] == START_FEN, f"unexpected start position {state['fen']}")
    check(state["turn"] == "white" and state["status"] == "ongoing" and state["winner"] is None, f"unexpected standing {state}")
    check(state["board"][0] == START_BOARD_TOP and state["board"][7] == START_BOARD_BOTTOM, f"unexpected board {state['board']}")
    check(state["moves"] == [] and state["legalMoveCount"] == START_MOVE_COUNT, f"unexpected moves {state['moves']} / {state['legalMoveCount']}")
    check(state["opening"] is None, f"the start position has the opening {state['opening']}")

    actions = client.call("list_actions")["actions"]
    check(len(actions) == START_MOVE_COUNT, f"the start position lists {len(actions)} actions, not {START_MOVE_COUNT}")
    check(move("e2e4") in actions and move("g1f3") in actions, f"e2e4 or g1f3 is missing from {actions}")


def play_opening(client: BagiControlClient) -> dict:
    """Four moves, each answered with the standing after it; gives the last answer."""
    version = client.call("get_state")["version"]
    reply = {}
    for index, uci in enumerate(OPENING):
        reply = client.call("perform_action", move(uci))
        outcome = reply["outcome"]
        expected_turn = "black" if index % 2 == 0 else "white"
        check(outcome["move"] == uci and outcome["turn"] == expected_turn and outcome["status"] == "ongoing", f"{uci} was answered with {outcome}")
        check(reply["version"] > version, f"the state version did not grow after {uci}")
        version = reply["version"]

    state = client.call("get_state")
    check(state["version"] == version, "get_state disagrees with perform_action about the version")
    check(state["state"]["moves"] == OPENING and state["state"]["startFen"] == START_FEN, f"unexpected history {state['state']['moves']}")
    check(state["state"]["board"][4] == "....P...", f"e4 is not on the board: {state['state']['board']}")
    check(state["state"]["opening"] == OPENING_NAME, f"unexpected opening {state['state']['opening']}")  # the table is read from assets/openings/ of the game
    return reply


def check_screenshots(client: BagiControlClient) -> None:
    """The picture follows the model: a move changes it, a reset brings it back."""
    client.call("reset")
    start = take_screenshot(client)
    check(len(start.colors()) >= BOARD_COLORS, f"the screenshot has {len(start.colors())} colours, it does not look like a board")
    start_panel = panel_band(start)
    check(band_has_color(start_panel, start.channels, PANEL_PLATE_COLOR), "the plate of the move panel is not drawn at the start")
    check(not band_has_color(start_panel, start.channels, PANEL_MARK_COLOR), "the move panel marks a move at the start, before any is played")

    client.call("perform_action", move("e2e4"))
    moved = take_screenshot(client)
    check(moved.rows != start.rows, "the screenshot did not change after a move")
    check(panel_band(moved) != start_panel, "the move panel did not change after a move")
    check(band_has_color(panel_band(moved), moved.channels, PANEL_MARK_COLOR), "the move panel does not mark the move played")

    client.call("reset")
    again = take_screenshot(client)
    check(again.rows == start.rows, "the screenshot after a reset is not the one of the start position")
    check(panel_band(again) == start_panel, "the move panel after a reset is not the one of the start position")

    # A position that is set from outside is drawn, and a pawn that promoted is drawn as its new piece
    client.call("set_state", {"fen": PROMOTION_FEN})
    before_promotion = take_screenshot(client)
    client.call("perform_action", move("a7a8q"))
    promoted = take_screenshot(client)
    check(promoted.rows != before_promotion.rows, "the screenshot did not change after a promotion")


# One short line of play that gives three cues: the pawn moves are quiet, the next two take a piece, the bishop checks
BOARD_SOUNDS = [("e2e4", "move"), ("d7d5", "move"), ("e4d5", "capture"), ("d8d5", "capture"), ("f1b5", "check")]
SOUND_FRAMES = 2  # the frame after a move plays its cue; one more for margin


def audio_state(client: BagiControlClient) -> dict:
    return client.call("audio_state")


def check_the_board_sounds(client: BagiControlClient) -> None:
    """Every played move asks the audio manager for the clip of its cue on the sfx bus, one request per move; a new game and a takeback ask for nothing.

    `played` is not checked: the backend of a headless run is SDL's dummy driver, and a machine with no audio device plays nothing at all, which is as right."""
    client.call("reset")
    let_frames_pass(client, SOUND_FRAMES)
    quiet = audio_state(client)["requested"]
    for uci, cue in BOARD_SOUNDS:
        before = audio_state(client)["requested"]
        client.call("perform_action", move(uci))
        let_frames_pass(client, SOUND_FRAMES)
        after = audio_state(client)
        check(after["requested"] == before + 1, f"{uci} asked for {after['requested'] - before} sounds, not one")
        last = after["recent"][-1]
        check(last["clip"] == f"assets/sounds/{cue}.wav" and last["bus"] == "sfx", f"{uci} should sound as a {cue}, the request is {last}")
    check(audio_state(client)["requested"] == quiet + len(BOARD_SOUNDS), "the line asked for a sound it should not have")

    before = audio_state(client)["requested"]
    client.call("set_state", {"takeBack": {"plies": 1}})
    let_frames_pass(client, SOUND_FRAMES)
    client.call("reset")
    let_frames_pass(client, SOUND_FRAMES)
    check(audio_state(client)["requested"] == before, "a takeback or a reset asked for a sound")

    check_the_buttons_click(client)
    check_the_levels(client)


CLICK_CLIP = "assets/sounds/click.wav"


def master_level(client: BagiControlClient) -> dict:
    return audio_state(client)["levels"]["master"]


def check_the_buttons_click(client: BagiControlClient) -> None:
    """A click on a button asks for the click clip on the ui bus, one request per click (the HUD's Resign asks its question alone and No returns to the game;
    the menu button opens the list, Close shuts it)."""
    buttons = (("Resign", "ChessResign"), ("GameSectionConfirmNo", "ChessGameNo"), ("Menu", "ChessOpenMenu"), ("MenuClose", "ChessCloseMenu"))
    for name, action in buttons:
        before = audio_state(client)["requested"]
        clicked = client.call("ui_click", {"name": name})
        check(clicked["consumed"] and clicked["action"] == action, f"the click on {name} was answered with {clicked}")
        let_frames_pass(client, SOUND_FRAMES)
        after = audio_state(client)
        check(after["requested"] == before + 1, f"the click on {name} asked for {after['requested'] - before} sounds, not one")
        last = after["recent"][-1]
        check(last["clip"] == CLICK_CLIP and last["bus"] == "ui", f"the click on {name} should sound as the click on the ui bus, the request is {last}")
    tree = client.call("ui_tree")
    check_the_menu_list_is_hidden(tree)
    check(find_ui_canvas(tree, "GameSection")["visible"] is False, "the question of the HUD's Resign should be hidden after its No")
    check(client.call("get_state")["state"]["status"] == "ongoing", "No after the HUD's Resign ended the game")


def check_the_levels(client: BagiControlClient) -> None:
    """A level set over the control API shows in the state; the request of a sound is recorded at any level (the gain is the backend's). Ends with the defaults put back."""
    client.call("reset")
    let_frames_pass(client, SOUND_FRAMES)
    try:
        levels = client.call("audio_set", {"bus": "master", "volume": 0})
        check(levels["master"]["volume"] == 0, f"audio_set did not set the master volume: {levels}")
        before = audio_state(client)["requested"]
        client.call("perform_action", move("e2e4"))
        let_frames_pass(client, SOUND_FRAMES)
        after = audio_state(client)
        check(after["requested"] == before + 1 and after["recent"][-1]["bus"] == "sfx", f"a move at volume 0 did not ask for one sfx sound: {after['recent'][-1]}")
        check(after["levels"]["master"]["volume"] == 0, f"the master volume moved by itself: {after['levels']}")

        levels = client.call("audio_set", {"bus": "master", "muted": True})
        check(levels["master"]["muted"] is True and levels["master"]["volume"] == 0, f"the mute did not show or lost the volume: {levels}")
    finally:
        client.call("audio_set", {"bus": "master", "volume": 1, "muted": False})
    check(master_level(client) == {"volume": 1, "muted": False}, f"the defaults were not put back: {master_level(client)}")
    client.call("reset")
    let_frames_pass(client, SOUND_FRAMES)


def run_chess_scenario(client: BagiControlClient, headless: bool) -> None:
    status = client.call("status")
    games = client.call("list_games")
    check(games["active"] == "Chess", f"the active game is {games['active']}")
    check(any(game["name"] == "Chess" and game["controllable"] for game in games["games"]), f"Chess is not controllable in {games}")

    # The first frame only records the state version, so a push needs a frame before the move; the manual clock has none yet
    if status["manualClock"]:
        client.call("step")

    client.call("reset")
    check_start_position(client)
    last = play_opening(client)

    # The push of the last move comes at the end of a frame; on the manual clock that frame has to be stepped
    if status["manualClock"]:
        client.call("step")
    check_state_push(client, last)

    if headless:
        check_screenshots(client)

    check_the_board_sounds(client)


def without_squares(image, squares: list, turned: bool = False) -> list:
    """The rows of a screenshot with the cells of the given squares (White at the bottom, or Black with `turned`) blanked: the board marks the last move on them, which a position set from a FEN does not have."""
    scale, left, top = view_in_picture(image)
    rows = [bytearray(row) for row in image.rows]
    for square in squares:
        column, rank = ord(square[0]) - ord("a"), int(square[1]) - 1
        if turned:
            column, rank = 7 - column, 7 - rank
        first_x, last_x = int(left + (BOARD_LEFT + column * SQUARE_SIZE) * scale), int(left + (BOARD_LEFT + (column + 1) * SQUARE_SIZE) * scale) + 1
        first_y, last_y = int(top + (BOARD_TOP + (7 - rank) * SQUARE_SIZE) * scale), int(top + (BOARD_TOP + (8 - rank) * SQUARE_SIZE) * scale) + 1
        for y in range(max(first_y, 0), min(last_y, image.height)):
            rows[y][max(first_x, 0) * image.channels : min(last_x, image.width) * image.channels] = bytes(
                (min(last_x, image.width) - max(first_x, 0)) * image.channels
            )
    return rows


def without_panel(image, rows: list) -> list:
    """The rows (of `without_squares`, say) of a screenshot with the box of the move panel blanked: the list shows the moves played, which a position set from a FEN does not have."""
    scale, left, top = view_in_picture(image)
    first_x, last_x = max(int(left + PANEL_LEFT * scale), 0), min(int(left + PANEL_RIGHT * scale) + 1, image.width)
    first_y, last_y = max(int(top + PANEL_TOP * scale), 0), min(int(top + PANEL_BOTTOM * scale) + 1, image.height)
    blanked = [bytearray(row) for row in rows]
    for y in range(first_y, last_y):
        blanked[y][first_x * image.channels : last_x * image.channels] = bytes((last_x - first_x) * image.channels)
    return blanked


def run_moves_before_the_first_frame(client: BagiControlClient, headless: bool) -> None:
    """A client that acts at once: the scene registers its pieces in the first frame, after these moves, and has to show them."""
    if not headless:
        return  # the check is made on screenshots
    for uci in OPENING:
        client.call("perform_action", move(uci))  # the manual clock has run no frame yet
    client.call("step", {"frames": 2})
    by_moves = take_screenshot(client)

    # The same position set from outside is drawn the same way, and it is not the opening. A position set from a FEN has no last move, so the two squares
    # the board marks for it are left out of the comparison
    client.call("set_state", {"fen": client.call("get_state")["state"]["fen"]})
    client.call("step", {"frames": 2})
    by_fen = take_screenshot(client)
    marked = [OPENING[-1][:2], OPENING[-1][2:4]]
    check(
        without_panel(by_moves, without_squares(by_moves, marked)) == without_panel(by_fen, without_squares(by_fen, marked)),
        "the scene after moves played before the first frame is not the scene of the same position",
    )
    client.call("reset")
    client.call("step", {"frames": 2})
    after_reset = take_screenshot(client)
    check(by_moves.rows != after_reset.rows, "the scene still shows the opening after four moves")
    # The list is what a position set from a FEN does not have: the moves played before the first frame are in the panel, with the last one marked
    check(panel_band(by_moves) != panel_band(after_reset), "the move panel shows no moves after four moves played before the first frame")
    check(band_has_color(panel_band(by_moves), by_moves.channels, PANEL_MARK_COLOR), "the move panel does not mark the last of the moves played before the first frame")
    check(not band_has_color(panel_band(by_fen), by_fen.channels, PANEL_MARK_COLOR), "the move panel marks a move in a position set from a FEN")


PINNED_SEED = 1
VARIETY_SEEDS = [1, 2, 3, 4, 5, 6, 7, 8]
VARIETY_ROUNDS = 3


def get_opponent(client: BagiControlClient) -> dict:
    return client.call("get_state")["state"]["opponent"]


def default_engine(client: BagiControlClient) -> str:
    """The engine a game starts with, which is also how the script tells the world: "stockfish" where a Stockfish is found, "own" where it is not (no file found, or CHESS_NO_STOCKFISH)."""
    engine = get_opponent(client)["engine"]
    check(engine in ("stockfish", "own"), f"the default engine is neither stockfish nor own: {get_opponent(client)}")
    return engine


def get_moves(client: BagiControlClient) -> list:
    return client.call("get_state")["state"]["moves"]


def pin_the_seed(client: BagiControlClient, seed: int = PINNED_SEED) -> None:
    """A reset draws a new seed (it varies the own engine's pick among near-equal moves), so a scenario that compares the program's answers across resets sets the seed after each one."""
    client.call("set_state", {"opponent": {"seed": seed}})
    check(get_opponent(client)["seed"] == seed, f"the seed was not read back: {get_opponent(client)}")


def let_frames_pass(client: BagiControlClient, frames: int) -> None:
    """The game runs this many frames: a step on the manual clock, a wait for the frame counter on the real one."""
    if client.call("status")["manualClock"]:
        client.call("step", {"frames": frames})
        return
    target = client.call("status")["frame"] + frames
    deadline = time.monotonic() + OPPONENT_WAIT_SECONDS
    while client.call("status")["frame"] < target:
        check(time.monotonic() < deadline, f"the game did not run {frames} frames in {OPPONENT_WAIT_SECONDS} s")
        time.sleep(POLL_SECONDS)


def let_the_pieces_settle(client: BagiControlClient) -> None:
    """The pieces of a move that was just played have finished sliding to their squares (ChessPieceComponent, 0.15 s).

    Manual clock: SETTLE_FRAMES frames (the first sees the move, the next ones move the tween). Real clock: until the game time has grown by SETTLE_SECONDS."""
    if client.call("status")["manualClock"]:
        client.call("step", {"frames": SETTLE_FRAMES, "dt": SETTLE_FRAME_SECONDS})
        return
    target = client.call("status")["totalTime"] + SETTLE_SECONDS
    deadline = time.monotonic() + OPPONENT_WAIT_SECONDS
    while client.call("status")["totalTime"] < target:
        check(time.monotonic() < deadline, f"the game did not run {SETTLE_SECONDS} s in {OPPONENT_WAIT_SECONDS} s")
        time.sleep(POLL_SECONDS)


def wait_for_the_program(client: BagiControlClient, expected_moves: list) -> None:
    """Until the moves are exactly these and the program is not thinking."""
    wait_until_the_program_played(client, lambda moves: moves == expected_moves, f"moves {expected_moves}")


def wait_until_the_program_played(client: BagiControlClient, accept, what: str) -> None:
    """Until accept(moves) holds and the program is not thinking. Real clock: poll; manual clock: step a frame per poll. Limited by time, never a fixed sleep."""
    deadline = time.monotonic() + OPPONENT_WAIT_SECONDS
    manual = client.call("status")["manualClock"]
    while True:
        state = client.call("get_state")["state"]
        opponent = state["opponent"]
        check(opponent["error"] is None, f"the opponent failed: {opponent['error']}")
        if accept(state["moves"]) and not opponent["thinking"]:
            return
        check(time.monotonic() < deadline, f"the program did not play ({what}) in {OPPONENT_WAIT_SECONDS} s: moves {state['moves']}, opponent {opponent}")
        if manual:
            client.call("step")
        time.sleep(POLL_SECONDS)


def run_opponent_scenario(client: BagiControlClient, headless: bool) -> None:
    """Two people, then the program plays Black against the fake engine, then two people again."""
    if client.call("status")["manualClock"]:
        client.call("step")  # the scene registers its pieces and the board starts updating the opponent in the first frame
    client.call("reset")
    if default_engine(client) != "stockfish":
        print("NOTICE: no Stockfish is found (the default engine is own): the scenario of the fake Stockfish is left out; 'without Stockfish' checks the game", flush=True)
        return

    opponent = get_opponent(client)
    check(opponent["plays"] is None and opponent["level"] == "medium" and opponent["thinking"] is False and opponent["error"] is None, f"unexpected default opponent {opponent}")
    check(opponent["engine"] == "stockfish", f"the default engine is not stockfish: {opponent}")

    version = client.call("get_state")["version"]
    # "max", not "easy": the low levels replace a share of the program's moves by a random legal move of their own (and then the fake engine is not asked), so
    # only a level without that handicap is sure to bring the scripted reply
    picked = client.call("set_state", {"opponent": {"plays": "black", "level": "max"}})
    check(picked["version"] > version, "picking the opponent did not move the state version")
    opponent = get_opponent(client)
    check(opponent["plays"] == "black" and opponent["level"] == "max" and opponent["error"] is None, f"the pick was not read back: {opponent}")
    check(get_moves(client) == [], "picking the opponent played a move")

    client.call("perform_action", move("e2e4"))
    wait_for_the_program(client, ["e2e4", "e7e5"])
    check(get_opponent(client)["error"] is None, f"the opponent has an error: {get_opponent(client)['error']}")
    check(client.call("get_state")["state"]["turn"] == "white", "it is not the person's turn after the program's move")

    # Two people again: white and black both move by hand, and the program stays out of it
    client.call("set_state", {"opponent": {"plays": None}})
    check(get_opponent(client)["plays"] is None, "the opponent is still picked after plays: null")
    client.call("perform_action", move("g1f3"))
    client.call("perform_action", move("d7d6"))  # the fake would have answered b8c6
    let_frames_pass(client, FRAMES_WITHOUT_AN_ANSWER)
    check(get_moves(client) == ["e2e4", "e7e5", "g1f3", "d7d6"], f"the game is not played by two people: {get_moves(client)}")
    check(get_opponent(client)["thinking"] is False, "the opponent thinks with nobody picked")


def play_a_short_game_against_the_own_engine(client: BagiControlClient, rounds: int) -> list:
    """The person plays e2e4 and then, `rounds - 1` times, the first of the listed moves; the program (Black) answers each. Gives the moves of the game."""
    client.call("perform_action", move("e2e4"))
    for played in range(1, rounds + 1):
        wait_until_the_program_played(client, lambda moves, count=2 * played: len(moves) == count, f"{2 * played} moves")
        if played < rounds:
            legal = [action["uci"] for action in client.call("list_actions")["actions"]]
            check(bool(legal), "no legal move to play by hand")
            client.call("perform_action", move(legal[0]))
    return get_moves(client)


def check_the_seed_varies_the_own_engine(client: BagiControlClient) -> None:
    """After a reset the program plays its own game: the same moves of the person at `hard` give another game for another seed (somewhere among the seeds) and the same game for the same seed."""
    games = {}
    for seed in VARIETY_SEEDS:
        client.call("reset")
        client.call("set_state", {"opponent": {"seed": seed}})
        check(get_opponent(client)["seed"] == seed and get_opponent(client)["plays"] == "black", f"the pick after a reset was not read back: {get_opponent(client)}")
        games[seed] = play_a_short_game_against_the_own_engine(client, VARIETY_ROUNDS)
    check(len({tuple(game) for game in games.values()}) > 1, f"the seeds {VARIETY_SEEDS} all gave the same game {games[VARIETY_SEEDS[0]]}: the seed varies nothing")
    client.call("reset")
    client.call("set_state", {"opponent": {"seed": VARIETY_SEEDS[0]}})
    again = play_a_short_game_against_the_own_engine(client, VARIETY_ROUNDS)
    check(again == games[VARIETY_SEEDS[0]], f"the seed {VARIETY_SEEDS[0]} gave {games[VARIETY_SEEDS[0]]}, then {again}")


def play_against_the_own_engine(client: BagiControlClient) -> str:
    """The person plays e2e4 against the program as Black; gives the program's reply, after checking the standing."""
    client.call("perform_action", move("e2e4"))
    wait_until_the_program_played(client, lambda moves: len(moves) == 2, "two moves")
    state = client.call("get_state")["state"]
    reply = state["moves"][1]
    check(state["moves"][0] == "e2e4" and state["status"] == "ongoing" and state["turn"] == "white", f"unexpected standing after the reply {reply}: {state}")
    check(state["opponent"]["error"] is None, f"the own engine has an error: {state['opponent']['error']}")
    # The model plays only what the generator allows (TryMove), so a reply in the history is legal; here it must also be a move of a black piece
    check(reply[1] in "78", f"the reply {reply} does not start on Black's side of the board")
    return reply


def run_own_engine_scenario(client: BagiControlClient, headless: bool) -> None:
    """The program is the engine of our own: no process, no environment. It answers, answers the same way after a reset, and lets go of the game."""
    if client.call("status")["manualClock"]:
        client.call("step")
    client.call("reset")
    default = default_engine(client)  # "stockfish", or "own" where none is found: the switch back below goes to it

    version = client.call("get_state")["version"]
    picked = client.call("set_state", {"opponent": {"engine": "own", "plays": "black", "level": "easy", "seed": PINNED_SEED}})
    check(picked["version"] > version, "picking the own engine did not move the state version")
    opponent = get_opponent(client)
    check(opponent["engine"] == "own" and opponent["plays"] == "black" and opponent["level"] == "easy" and opponent["error"] is None, f"the pick was not read back: {opponent}")
    check(get_moves(client) == [], "picking the opponent played a move")

    first = play_against_the_own_engine(client)

    # A fixed depth is deterministic, and so is the handicap of "easy" (a pure function of the position and the seed): the same game after a reset and the same seed gets the same reply, and the reset keeps the engine
    client.call("reset")
    pin_the_seed(client)
    check(get_opponent(client)["engine"] == "own" and get_opponent(client)["plays"] == "black", f"a reset lost the pick: {get_opponent(client)}")
    second = play_against_the_own_engine(client)
    check(first == second, f"the own engine answered e2e4 with {first}, then with {second}")

    client.call("set_state", {"opponent": {"level": "hard"}})
    check_the_seed_varies_the_own_engine(client)
    client.call("reset")  # the game below is the one of the first reply
    pin_the_seed(client)
    client.call("set_state", {"opponent": {"level": "easy"}})
    check(play_against_the_own_engine(client) == second, "the own engine's reply changed after the seeds were tried")

    # Back to the engine of the start (Stockfish, or the own engine where none is found) and to two people: the program stays out of the game
    client.call("set_state", {"opponent": {"engine": default, "plays": None}})
    opponent = get_opponent(client)
    check(opponent["engine"] == default and opponent["plays"] is None and opponent["error"] is None, f"the switch back was not read: {opponent}")
    # The moves are taken from the list, not typed: which reply the own engine gave is not this scenario's business
    by_hand = []
    for _ in range(2):
        legal = [action["uci"] for action in client.call("list_actions")["actions"]]
        check(bool(legal), "no legal move to play by hand")
        by_hand.append(legal[0])
        client.call("perform_action", move(legal[0]))
    let_frames_pass(client, FRAMES_WITHOUT_AN_ANSWER)
    check(get_moves(client) == ["e2e4", second] + by_hand, f"the game is not played by two people: {get_moves(client)}")
    check(get_opponent(client)["thinking"] is False, "the opponent thinks with nobody picked")

    if headless:
        check_the_board_turns_with_the_program(client)


WATCH_PLIES = 20
WATCH_SEATS = {"white": {"engine": "own", "level": "easy"}, "black": {"engine": "own", "level": "medium"}}


class WatchedGame:
    """What a client sees of a game of two programs: every read must agree with the ones before it (the version only grows, the moves only extend, the turns alternate)."""

    def __init__(self, client: BagiControlClient) -> None:
        self.client = client
        self.version = 0
        self.moves = []

    def forget_moves(self) -> None:
        """After a reset: the next moves are a new game."""
        self.moves = []

    def see(self) -> dict:
        reply = self.client.call("get_state")
        state = reply["state"]
        check(reply["version"] >= self.version, f"the state version went back from {self.version} to {reply['version']}")
        self.version = reply["version"]
        check(state["moves"][: len(self.moves)] == self.moves, f"the moves {state['moves']} do not extend {self.moves}")
        self.moves = state["moves"]
        if state["status"] == "ongoing":
            expected_turn = "white" if len(self.moves) % 2 == 0 else "black"
            check(state["turn"] == expected_turn, f"the turn is {state['turn']} after {len(self.moves)} moves")
        opponent = state["opponent"]
        check(opponent["error"] is None, f"a program failed: {opponent['error']}")
        return state

    def wait_for(self, accept, what: str) -> dict:
        """Until accept(state) holds; gives that state. Real clock: poll; manual clock: step a frame per poll. Limited by time, never a fixed sleep."""
        deadline = time.monotonic() + OPPONENT_WAIT_SECONDS
        manual = self.client.call("status")["manualClock"]
        while True:
            state = self.see()
            if accept(state):
                return state
            check(time.monotonic() < deadline, f"the game did not reach ({what}) in {OPPONENT_WAIT_SECONDS} s: moves {state['moves']}, opponent {state['opponent']}")
            if manual:
                self.client.call("step")
            time.sleep(POLL_SECONDS)

    def play_to_the_plies(self) -> list:
        """Until WATCH_PLIES moves are played or the game is over; gives the moves."""
        return self.wait_for(lambda state: len(state["moves"]) >= WATCH_PLIES or state["status"] != "ongoing", f"{WATCH_PLIES} moves")["moves"]


def check_the_watch_is_set(client: BagiControlClient, plays) -> None:
    opponent = get_opponent(client)
    check(opponent["plays"] == plays and opponent["error"] is None, f"the opponent is not read back as plays {plays}: {opponent}")
    check(opponent["seats"] == WATCH_SEATS, f"the seats were not kept: {opponent['seats']}")


def check_a_hint_is_refused(client: BagiControlClient, may_be_over: bool = False) -> None:
    """A hint is for the person: while the programs play it is refused (`not_your_turn`), and the switch of that call is not applied either.
    `may_be_over`: the programs keep playing between the previous read and this call, so on the real clock the game may have ended in the meantime;
    then `game_over` is the right answer, but only when the state really says the game is not ongoing."""
    request = {"analysis": {"evaluation": True, "hint": True}}
    if may_be_over:
        try:
            client.call("set_state", request)
            check(False, "a hint while the programs play was accepted")
        except ControlError as error:
            if error.code == "game_over":
                status = client.call("get_state")["state"]["status"]
                check(status != "ongoing", f"a hint was refused as game_over while the game is {status}")
            else:
                check(error.code == "not_your_turn", f"a hint while the programs play was refused as {error.code}: {error.message}")
    else:
        expect_error("not_your_turn", lambda: client.call("set_state", request), "a hint while the programs play")
    analysis = get_analysis(client)
    check(analysis["evaluationOn"] is False and analysis["hintRequested"] is False, f"a refused hint changed the analysis: {analysis}")


def run_watch_scenario(client: BagiControlClient, headless: bool) -> None:
    """Two own engines (easy against medium, no process) play each other, and the person takes a side over. Needs nothing from the environment."""
    manual = client.call("status")["manualClock"]
    if manual:
        client.call("step")
    client.call("reset")
    game = WatchedGame(client)

    version = client.call("get_state")["version"]
    picked = client.call("set_state", {"opponent": {"plays": "both", "white": WATCH_SEATS["white"], "black": WATCH_SEATS["black"], "seed": PINNED_SEED}})
    check(picked["version"] > version, "picking two programs did not move the state version")
    check_the_watch_is_set(client, "both")
    opponent = get_opponent(client)
    check(opponent["engine"] == "own" and opponent["level"] == "easy", f"the single-seat answer is not White's seat: {opponent}")
    if manual:
        check(get_moves(client) == [], "picking the programs played a move")  # no frame has run since the pick
    check_a_hint_is_refused(client)  # the programs are playing and the game cannot be over yet

    first = game.play_to_the_plies()
    check(len(first) >= 1, "the programs played nothing")
    check_the_watch_is_set(client, "both")
    check_a_hint_is_refused(client, may_be_over=True)  # after moves: the programs may have ended the game since the last read

    # A reset starts a new game and keeps both programs; on the manual clock the same two programs play the same game
    client.call("reset")
    pin_the_seed(client)  # the reset drew another one, and the programs vary their moves by it
    game.forget_moves()
    check_the_watch_is_set(client, "both")
    if manual:
        check(get_moves(client) == [], "a reset left moves behind")
        second = game.play_to_the_plies()
        check(second[:WATCH_PLIES] == first[:WATCH_PLIES], f"the same programs played another game after a reset: {first[:WATCH_PLIES]} then {second[:WATCH_PLIES]}")

    # A takeover: the person has Black, the program plays White and waits for the person's move
    client.call("reset")
    game.forget_moves()
    client.call("set_state", {"opponent": {"plays": "white"}})
    opponent = get_opponent(client)
    check(opponent["plays"] == "white" and opponent["seats"] == WATCH_SEATS, f"the takeover was not read back: {opponent}")
    state = game.wait_for(lambda s: s["turn"] == "black" and not s["opponent"]["thinking"], "the program's move and Black's turn")
    waiting_moves = state["moves"]
    let_frames_pass(client, FRAMES_WITHOUT_AN_ANSWER)
    state = game.see()
    check(state["moves"] == waiting_moves and state["turn"] == "black" and state["opponent"]["thinking"] is False, f"Black's program played or thinks after the takeover: {state}")
    legal = [action["uci"] for action in client.call("list_actions")["actions"]]
    check(bool(legal), "no legal move for the person")
    client.call("perform_action", move(legal[0]))
    state = game.wait_for(lambda s: len(s["moves"]) == len(waiting_moves) + 2 and not s["opponent"]["thinking"], "White's answer to the person's move")
    check(state["moves"][len(waiting_moves)] == legal[0] and state["turn"] == "black", f"unexpected game after the person's move {legal[0]}: {state['moves']}")

    # Nobody: the game stays where it is
    client.call("set_state", {"opponent": {"plays": None}})
    check(get_opponent(client)["plays"] is None and get_opponent(client)["thinking"] is False, f"plays: null did not end the programs: {get_opponent(client)}")
    stopped_moves = get_moves(client)
    let_frames_pass(client, FRAMES_WITHOUT_AN_ANSWER)
    check(get_moves(client) == stopped_moves, "a program played after plays: null")
    check(get_opponent(client)["error"] is None, f"the opponent has an error: {get_opponent(client)['error']}")


def get_analysis(client: BagiControlClient) -> dict:
    return client.call("get_state")["state"]["analysis"]


def wait_for_the_analysis(client: BagiControlClient, accept, what: str) -> dict:
    """Until accept(analysis) holds; gives that analysis. Real clock: poll; manual clock: step a frame per poll. Limited by time, never a fixed sleep."""
    deadline = time.monotonic() + OPPONENT_WAIT_SECONDS
    manual = client.call("status")["manualClock"]
    while True:
        analysis = get_analysis(client)
        check(analysis["error"] is None, f"the analysis failed: {analysis['error']}")
        if accept(analysis):
            return analysis
        check(time.monotonic() < deadline, f"the analysis did not come ({what}) in {OPPONENT_WAIT_SECONDS} s: {analysis}")
        if manual:
            client.call("step")
        time.sleep(POLL_SECONDS)


RATING_REASONS = ("brilliant", "not_best", "forced", "obvious")
PINNED_GAME_REASONS = ["not_best", "brilliant", "not_best", "brilliant"]  # e2e4 e7e5 g1f3 b8c6 by the own engine at depth 5 (MoveRaterTest pins the same)


def check_the_ratings_are_complete(ratings: list, expected_count: int, when: str) -> None:
    check(len(ratings) == expected_count, f"{when}: {len(ratings)} ratings for {expected_count} moves: {ratings}")
    for rating in ratings:
        check(rating is not None, f"{when}: a move is not rated: {ratings}")
        check(isinstance(rating["brilliant"], bool) and rating["reason"] in RATING_REASONS, f"{when}: unexpected rating {rating}")
        check(rating["brilliant"] == (rating["reason"] == "brilliant"), f"{when}: brilliant does not agree with the reason in {rating}")


def play_and_wait_for_the_ratings(client: BagiControlClient, ucis: list) -> list:
    """Plays the moves from the opening position (two people) and gives the ratings once every move has one."""
    for uci in ucis:
        client.call("perform_action", move(uci))
    analysis = wait_for_the_analysis(client, lambda a: len(a["ratings"]) == len(ucis) and all(rating is not None for rating in a["ratings"]), "the ratings of the moves")
    check_the_ratings_are_complete(analysis["ratings"], len(ucis), "the ratings of a played game")
    return analysis["ratings"]


def check_the_ratings(client: BagiControlClient) -> None:
    """The ratings of the moves (always the own engine at depth 5): off at the start, switched on, one entry per move, the same for the same game played twice after a reset. Ends with them off."""
    client.call("reset")
    analysis = get_analysis(client)
    check(analysis["ratingsOn"] is False and analysis["ratings"] == [], f"the default ratings are not off and empty: {analysis}")
    expect_error("invalid_params", lambda: client.call("set_state", {"analysis": {"ratings": 1}}), "ratings that is not a boolean")
    expect_error("invalid_params", lambda: client.call("set_state", {"fen": MATE_IN_ONE_FEN, "analysis": {"ratings": True}}), "ratings with a fen")
    check(get_analysis(client)["ratingsOn"] is False, "a refused call switched the ratings on")

    version = client.call("get_state")["version"]
    switched = client.call("set_state", {"analysis": {"ratings": True}})
    check(switched["version"] > version and get_analysis(client)["ratingsOn"] is True, "switching the ratings on did not show")
    game = ["e2e4", "e7e5", "g1f3", "b8c6"]
    first = play_and_wait_for_the_ratings(client, game)
    check([rating["reason"] for rating in first] == PINNED_GAME_REASONS, f"the own engine rated the game differently from the pinned list {PINNED_GAME_REASONS}: {first}")

    # A reset drops the ratings and keeps the switch; the same game gets the same ratings (a fixed depth, no clock). A frame passes before the moves are replayed: the rater
    # drops the old ratings when it sees the empty game, and a replay before that frame would be taken for the same game going on and keep them
    client.call("reset")
    let_frames_pass(client, 2)
    after_reset = get_analysis(client)
    check(after_reset["ratingsOn"] is True and after_reset["ratings"] == [], f"a reset did not drop the ratings and keep the switch: {after_reset}")
    check(play_and_wait_for_the_ratings(client, game) == first, "the same game was rated differently the second time")

    client.call("set_state", {"analysis": {"ratings": False}})
    check(get_analysis(client)["ratingsOn"] is False, "the ratings are still on")
    client.call("reset")


def run_analysis_scenario(client: BagiControlClient, headless: bool) -> None:
    """The engine of our own analyses: the evaluation is switched on and read from White's view, a hint is asked for and bound to its position, and a hint on the program's turn is refused.

    Headless, the hint is also seen on the board: the board band of a screenshot with the hint differs from the one without it, and is the same again once it is taken back or a
    reset has put the opening back. The pictures are compared, not read: how the hint looks is not checked."""
    manual = client.call("status")["manualClock"]
    if manual:
        client.call("step")
    client.call("reset")

    analysis = get_analysis(client)
    check(analysis["evaluationOn"] is False and analysis["hintRequested"] is False and analysis["thinking"] is False, f"unexpected default analysis {analysis}")
    check(analysis["evaluation"] is None and analysis["hint"] is None and analysis["error"] is None, f"the default analysis is not empty {analysis}")
    check(analysis["lines"] == [], f"the default analysis has lines {analysis}")

    # A hint is for the person: on the turn of the side the program plays it is refused, and nothing changes. Only the manual clock has no frame between the move and the
    # request, so the program is surely still to answer; on the real clock the own engine may answer first (ChessGameControlTest checks this deterministically)
    client.call("set_state", {"opponent": {"engine": "own", "plays": "black", "level": "max"}})
    client.call("perform_action", move("e2e4"))
    if manual:
        version = client.call("get_state")["version"]
        expect_error("not_your_turn", lambda: client.call("set_state", {"analysis": {"evaluation": True, "hint": True}}), "a hint on the program's turn")
        check(get_analysis(client)["evaluationOn"] is False and client.call("get_state")["version"] == version, "a refused hint changed something")
    wait_until_the_program_played(client, lambda moves: len(moves) == 2, "two moves")
    client.call("set_state", {"opponent": {"plays": None}})

    # Neither the position nor the opponent travels with the analysis, and a wrong member is refused whole
    expect_error("invalid_params", lambda: client.call("set_state", {"fen": MATE_IN_ONE_FEN, "analysis": {"hint": True}}), "analysis with a fen")
    expect_error("invalid_params", lambda: client.call("set_state", {"opponent": {"plays": None}, "analysis": {"hint": True}}), "analysis with an opponent")
    expect_error("invalid_params", lambda: client.call("set_state", {"analysis": {"evaluation": True, "hint": "yes"}}), "a hint that is not a boolean")
    expect_error("invalid_params", lambda: client.call("set_state", {"analysis": {"evaluation": True, "hnt": True}}), "a member that is not one of the two")
    check(get_analysis(client)["evaluationOn"] is False, "a refused call switched the evaluation on")

    check_the_ratings(client)  # two people play, the own engine rates in the background; it ends with the ratings off and a reset

    client.call("reset")
    version = client.call("get_state")["version"]
    client.call("set_state", {"opponent": {"engine": "own", "plays": None}})
    opening_band = None
    if headless:
        let_frames_pass(client, 2)
        opening_band = board_squares_band(take_screenshot(client))  # the opening with no hint, to compare with after the hint has come and gone
    switched = client.call("set_state", {"analysis": {"evaluation": True}})
    check(switched["version"] > version, "switching the evaluation on did not move the state version")
    check(get_analysis(client)["evaluationOn"] is True, f"the evaluation is not on: {get_analysis(client)}")

    # A mate in one, loaded in a call of its own: the evaluation is White's mate in 1, and no hint was asked for
    client.call("set_state", {"fen": MATE_IN_ONE_FEN})
    analysis = wait_for_the_analysis(client, lambda a: a["evaluation"] is not None, "the evaluation of the mate in one")
    check(analysis["evaluation"]["score"] == {"kind": "mate", "value": 1}, f"the evaluation of a mate in one is not mate 1: {analysis['evaluation']}")
    check(analysis["evaluation"]["text"] == "#1", f"unexpected text {analysis['evaluation']}")
    check(analysis["hint"] is None and analysis["hintRequested"] is False, f"a hint is there that nobody asked for: {analysis}")
    # The two lines come with the evaluation, White's view, best first: the mating move, then another legal move that does not mate in one
    lines = analysis["lines"]
    check(len(lines) == 2, f"the evaluation of the mate in one does not come with two lines: {lines}")
    check(lines[0]["move"] == "a1a8" and lines[0]["score"] == {"kind": "mate", "value": 1} and lines[0]["text"] == "#1", f"unexpected first line {lines[0]}")
    check(lines[1]["move"] != "a1a8" and lines[1]["score"] != {"kind": "mate", "value": 1} and isinstance(lines[1]["depth"], int), f"unexpected second line {lines[1]}")
    listed = [action["uci"] for action in client.call("list_actions")["actions"]]
    check(lines[1]["move"] in listed, f"the second line {lines[1]['move']} is not a listed action")

    plain_band = None
    if headless:
        let_frames_pass(client, 2)
        plain_band = board_squares_band(take_screenshot(client))  # the mate in one with the evaluation but no hint

    before = client.call("get_state")["version"]
    client.call("set_state", {"analysis": {"hint": True}})
    check(client.call("get_state")["version"] > before and get_analysis(client)["hintRequested"] is True, "asking for a hint did not show")
    analysis = wait_for_the_analysis(client, lambda a: a["hint"] is not None, "the hint")
    check(analysis["hint"]["move"] == "a1a8", f"the hint for the mate in one is {analysis['hint']}")
    check(analysis["lines"][0]["move"] == "a1a8", f"the first line is not the hint: {analysis['lines']}")
    check({"type": "move", "uci": "a1a8"} in client.call("list_actions")["actions"], "the hinted move is not one of the listed actions")
    if headless:
        let_frames_pass(client, 2)
        hinted_band = board_squares_band(take_screenshot(client))
        check(hinted_band != plain_band, "the board looks the same with the hint on it: the two squares of a1a8 are not filled")
        # Taking it back is the position without a hint again; asked for once more it is the same picture as the first time (the answer is deterministic)
        client.call("set_state", {"analysis": {"hint": False}})
        let_frames_pass(client, 2)
        check(board_squares_band(take_screenshot(client)) == plain_band, "the board still shows the hint after it was taken back")
        client.call("set_state", {"analysis": {"hint": True}})
        wait_for_the_analysis(client, lambda a: a["hint"] is not None, "the hint again")
        let_frames_pass(client, 2)
        check(board_squares_band(take_screenshot(client)) == hinted_band, "the hint is not drawn the same way twice")

    # A move drops the hint at once and the evaluation follows the new position later (it is Black's turn now: still White's view)
    client.call("perform_action", move("g1h1"))
    analysis = get_analysis(client)
    check(analysis["hint"] is None and analysis["hintRequested"] is False, f"the hint outlived the move: {analysis}")
    if manual:  # on the real clock a fast search may arrive before this read
        check(analysis["evaluation"] is None, f"the evaluation of the old position outlived the move: {analysis}")
    wait_for_the_analysis(client, lambda a: a["evaluation"] is not None, "the evaluation of the next position")

    # A hint can be taken back; the game then ends with the analyst running (after quit the game must exit by itself, with no search holding it)
    client.call("set_state", {"analysis": {"hint": True}})
    client.call("set_state", {"analysis": {"hint": False}})
    check(get_analysis(client)["hintRequested"] is False, "a hint taken back is still asked for")

    # A hint asked for in the mate in one is gone after a move and a reset: the opening is drawn as it was before any hint
    if headless:
        client.call("set_state", {"analysis": {"hint": True}})
        wait_for_the_analysis(client, lambda a: a["hint"] is not None, "the hint before the reset")
        client.call("reset")
        check(get_analysis(client)["hint"] is None, "the hint outlived a reset")
        let_frames_pass(client, 2)
        check(board_squares_band(take_screenshot(client)) == opening_band, "the opening after a reset is not drawn as it was before the hint")
        last_move = "e2e4"  # the opening is back
    else:
        last_move = "g8h8"  # Black is to move in the mate in one after g1h1
    client.call("perform_action", move(last_move))  # a move for the evaluation to follow, so the game quits with a search under way


def run_analysis_buttons_scenario(client: BagiControlClient, headless: bool) -> None:
    """The Hint button and the evaluation cell of the move panel, clicked by name in the Wide view of the headless window: the cell switches the evaluation on and off, the button asks for
    the hint of the position (and the board shows it, seen as a picture of the squares), and on the program's turn it changes nothing. What the analysis says is the scenario above's."""
    manual = client.call("status")["manualClock"]
    if manual:
        client.call("step")  # the scene registers its pieces in the first frame
    client.call("reset")
    client.call("set_state", {"opponent": {"engine": "own", "plays": None}})
    let_frames_pass(client, 2)  # the panel greys and ungreys its buttons in its draw
    check(get_analysis(client)["evaluationOn"] is False, "the evaluation is on at the start")

    clicked = client.call("ui_click", {"name": "MovePanelEval"})
    check(clicked["consumed"] and clicked["action"] == "ChessSetEvaluation" and clicked["value"] == "true", f"the click on the evaluation cell was answered with {clicked}")
    check(get_analysis(client)["evaluationOn"] is True, "the evaluation cell did not switch the evaluation on")
    wait_for_the_analysis(client, lambda a: a["evaluation"] is not None, "the evaluation of the opening")
    clicked = client.call("ui_click", {"name": "MovePanelEval"})
    check(clicked["consumed"] and clicked["action"] == "ChessSetEvaluation" and clicked["value"] == "false", f"the second click on the evaluation cell was answered with {clicked}")
    check(get_analysis(client)["evaluationOn"] is False, "the second click on the evaluation cell did not switch the evaluation off")

    without_hint = None
    if headless:
        let_frames_pass(client, 2)
        without_hint = board_squares_band(take_screenshot(client))
    clicked = client.call("ui_click", {"name": "MovePanelHint"})
    check(clicked["consumed"] and clicked["action"] == "ChessHint", f"the click on the Hint button was answered with {clicked}")
    check(get_analysis(client)["hintRequested"] is True, "the Hint button did not ask for a hint")
    analysis = wait_for_the_analysis(client, lambda a: a["hint"] is not None, "the hint of the opening")
    check({"type": "move", "uci": analysis["hint"]["move"]} in client.call("list_actions")["actions"], f"the hint {analysis['hint']} is not one of the listed actions")
    if headless:
        let_frames_pass(client, 2)
        check(board_squares_band(take_screenshot(client)) != without_hint, "the board looks the same with the hint on it")

    # On the program's turn the button does nothing. Only the manual clock has no frame between the move and the click, so the program is surely still to answer
    if manual:
        client.call("reset")
        client.call("set_state", {"opponent": {"engine": "own", "plays": "black", "level": "max"}})
        client.call("perform_action", move("e2e4"))
        version = client.call("get_state")["version"]
        client.call("ui_click", {"name": "MovePanelHint"})
        check(get_analysis(client)["hintRequested"] is False and client.call("get_state")["version"] == version, "the Hint button asked on the program's turn")
        wait_until_the_program_played(client, lambda moves: len(moves) == 2, "two moves")
    client.call("set_state", {"opponent": {"plays": None}})
    client.call("reset")


# The scene is 840 x 800 units (1120 x 800, its Wide view, from the aspect 1.4 up), the board 640 x 640 at y 80..720; the window fits the view by the smaller scale and centers it
VIEW_WIDTH, WIDE_VIEW_WIDTH, VIEW_HEIGHT = 840, 1120, 800
BOARD_TOP, BOARD_BOTTOM = 80, 720
BOARD_LEFT, SQUARE_SIZE = 100, 80  # the left edge of the board and the side of a square, in view units
BOARD_SIZE = 8 * SQUARE_SIZE
PANEL_LEFT, PANEL_TOP, PANEL_RIGHT, PANEL_BOTTOM = 760, 80, 1080, 720  # the box of the move panel in the Wide view (Game/MovePanel.h), in view units
BOARD_BAND_INSET = 2  # rows kept away from the edge of the board, against rounding
PANEL_INSET = 2  # rows and columns kept away from the edges of the panel, against rounding
PANEL_PLATE_COLOR = (32, 44, 50)  # the plate of the panel (ChessMovePanel.xml), drawn only where the box lies inside the safe area
PANEL_MARK_COLOR = (157, 138, 49)  # the gold mark of the last move (245, 205, 50 at alpha 150) over the plate


def view_width_of(image) -> int:
    """The width in units of the view the scene picks for a picture: Wide from the aspect 1.4 up (SceneConfig.xml), the default view below it."""
    return WIDE_VIEW_WIDTH if image.width / image.height >= WIDE_VIEW_WIDTH / VIEW_HEIGHT else VIEW_WIDTH


def view_in_picture(image) -> tuple:
    """Where the scene is in a screenshot: (scale, left, top)."""
    view_width = view_width_of(image)
    scale = min(image.width / view_width, image.height / VIEW_HEIGHT)
    return scale, (image.width - view_width * scale) / 2, (image.height - VIEW_HEIGHT * scale) / 2


def panel_band(image) -> list:
    """The part of a screenshot that only the move panel covers: its box, kept PANEL_INSET away from the edges, so that what differs between two pictures of it is the list of moves.
    The Wide view only has a panel, so a picture of another view is a defect of the run."""
    check(view_width_of(image) == WIDE_VIEW_WIDTH, f"a picture {image.width}x{image.height} is not in the Wide view, the move panel is not drawn in it")
    scale, left, top = view_in_picture(image)
    first_row, last_row = int(top + PANEL_TOP * scale) + PANEL_INSET, int(top + PANEL_BOTTOM * scale) - PANEL_INSET
    first_column, last_column = int(left + PANEL_LEFT * scale) + PANEL_INSET, int(left + PANEL_RIGHT * scale) - PANEL_INSET
    check(0 <= first_row < last_row <= image.height and 0 <= first_column < last_column <= image.width, f"the panel band is outside a picture {image.width}x{image.height}")
    return [row[first_column * image.channels : last_column * image.channels] for row in image.rows[first_row:last_row]]


def board_rows(image) -> tuple:
    """The first and the last row of a screenshot that only the board covers, kept BOARD_BAND_INSET away from its edges."""
    scale, _, top = view_in_picture(image)
    first = int(top + BOARD_TOP * scale) + BOARD_BAND_INSET
    last = int(top + BOARD_BOTTOM * scale) - BOARD_BAND_INSET
    check(0 <= first < last <= image.height, f"the board band {first}..{last} is outside a picture {image.width}x{image.height}")
    return first, last


def board_band(image) -> list:
    """The rows of a screenshot that only the board covers: the HUD sits in the margins above and below it, and its words (the turn, the status) change with the game."""
    first, last = board_rows(image)
    return image.rows[first:last]


def board_squares_band(image) -> list:
    """The squares of the board and nothing else: `board_band` cut to the columns of the board, so the move panel beside it (its evaluation cell and its Hint button change with the analysis) and the margins stay out."""
    scale, left, _ = view_in_picture(image)
    first_column = int(left + BOARD_LEFT * scale) + BOARD_BAND_INSET
    last_column = int(left + (BOARD_LEFT + BOARD_SIZE) * scale) - BOARD_BAND_INSET
    check(0 <= first_column < last_column <= image.width, f"the board columns {first_column}..{last_column} are outside a picture {image.width}x{image.height}")
    return [row[first_column * image.channels : last_column * image.channels] for row in board_band(image)]


def check_the_board_turns_with_the_program(client: BagiControlClient) -> None:
    """The picture is drawn from Black's side while the program plays White, and from White's side otherwise.

    Only the rows of the board are compared (`board_band`): the HUD above and below the board says other words in each picture (the
    turn and the status), so whole pictures differ without the board turning. The same placement of the pieces is drawn with the program as White (Black to move: the
    program does not search), with two people (Black to move) and with the program as Black (White to move: the side to move is not drawn on the board). The last
    two have White at the bottom and must give one board band, which also shows that the band leaves the HUD out; the turned one must differ from them, and drawn
    again after a round trip it must be the same band. What fails when the board does not turn is the comparison of the turned band with each of the two bands that have White at the bottom (`!=`); it can tell only as far as the pieces differ between the two orientations, which they do after any opening move."""
    client.call("reset")
    client.call("set_state", {"opponent": {"engine": "own", "plays": "white", "level": "easy"}})
    wait_until_the_program_played(client, lambda moves: len(moves) == 1, "the opening move of White")
    fen = client.call("get_state")["state"]["fen"]  # Black to move: the program, as White, does not search again
    check(fen.split()[1] == "b", f"Black is not to move after the opening move of White: {fen}")
    let_the_pieces_settle(client)  # the program's own move slides for 0.15 s; the later pictures follow a set_state, which snaps
    rotated = take_screenshot(client)
    opening = get_moves(client)[0]  # the board marks its two squares, which a position set from a FEN does not have: they are left out of the last comparison
    marked = [opening[:2], opening[2:4]]

    client.call("set_state", {"opponent": {"plays": None}})
    client.call("set_state", {"fen": fen})
    let_frames_pass(client, 2)
    two_people = take_screenshot(client)

    # The same pieces with White to move and the program as Black: White stays at the bottom and the program has nothing to answer
    white_to_move = " ".join(fen.split()[:1] + ["w"] + fen.split()[2:])
    client.call("set_state", {"fen": white_to_move})  # first the position, so that the program never has a move to find on the way
    client.call("set_state", {"opponent": {"plays": "black"}})
    let_frames_pass(client, 2)
    program_as_black = take_screenshot(client)
    check(get_moves(client) == [], f"the program moved with White to move and itself as Black: {get_moves(client)}")

    check(board_band(two_people) == board_band(program_as_black), "the board band changes with the HUD or the side to move, so it cannot tell a turned board")
    check(board_band(rotated) != board_band(two_people), "the board is not turned while the program plays White: its rows are the ones of the board with White at the bottom")
    check(board_band(rotated) != board_band(program_as_black), "the board is not turned while the program plays White: its rows are the ones of the board with White at the bottom")

    client.call("set_state", {"opponent": {"plays": None}})
    client.call("set_state", {"fen": fen})
    client.call("set_state", {"opponent": {"plays": "white"}})
    let_frames_pass(client, 2)
    turned_again = take_screenshot(client)
    first, last = board_rows(turned_again)
    check(
        without_panel(turned_again, without_squares(turned_again, marked, turned=True))[first:last] == without_panel(rotated, without_squares(rotated, marked, turned=True))[first:last],
        "the board turned again is not the picture it was the first time",
    )
    client.call("set_state", {"opponent": {"plays": None}})


# The left margin of the view, where the node Material draws the captured pieces and the balance outside the Wide view (and in a Wide window whose side insets cut the move panel): x 0 to 100
MARGIN_WIDTH = 100
MARGIN_INSET = 2  # columns and rows kept away from the edges of the margin, against rounding
# The band of the move panel under its list, where Material draws what the color at the bottom of the board has taken while the panel is shown (ChessMaterial.xml), in view units
LOWER_GROUP_TOP, LOWER_GROUP_BOTTOM = 540, 596
CAPTURE_MOVES = ["e2e4", "d7d5", "e4d5"]  # White takes a pawn of Black
CHIP_COLOR = (232, 210, 170)  # the fill of a row of the display


def margin_band(image) -> list:
    """The part of a screenshot that only the left margin covers: the rows of the board, the columns from the left edge of the view to the board. The HUD above and below
    the board and the board itself are left out, so what differs between two pictures of it is the display of the captured material."""
    scale, left, _ = view_in_picture(image)
    first_row, last_row = board_rows(image)
    first_column = int(left) + MARGIN_INSET
    last_column = int(left + MARGIN_WIDTH * scale) - MARGIN_INSET
    check(0 <= first_column < last_column <= image.width, f"the margin band columns {first_column}..{last_column} are outside a picture {image.width}x{image.height}")
    return [row[first_column * image.channels : last_column * image.channels] for row in image.rows[first_row:last_row]]


def lower_group_band(image) -> list:
    """The part of a screenshot that the band of the move panel under its list covers: the captured material of the color at the bottom of the board. Only the Wide view has the panel."""
    check(view_width_of(image) == WIDE_VIEW_WIDTH, f"a picture {image.width}x{image.height} is not in the Wide view, the move panel is not drawn in it")
    scale, left, top = view_in_picture(image)
    first_row, last_row = int(top + LOWER_GROUP_TOP * scale) + PANEL_INSET, int(top + LOWER_GROUP_BOTTOM * scale) - PANEL_INSET
    first_column, last_column = int(left + PANEL_LEFT * scale) + PANEL_INSET, int(left + PANEL_RIGHT * scale) - PANEL_INSET
    check(0 <= first_row < last_row <= image.height and 0 <= first_column < last_column <= image.width, f"the band of the lower group is outside a picture {image.width}x{image.height}")
    return [row[first_column * image.channels : last_column * image.channels] for row in image.rows[first_row:last_row]]


def band_has_color(band: list, channels: int, color: tuple) -> bool:
    for row in band:
        for start in range(0, len(row), channels):
            if tuple(row[start : start + 3]) == color:
                return True
    return False


def run_material_scenario(client: BagiControlClient, headless: bool) -> None:
    """The captured material in the picture: the headless window is Wide, so the move panel shows it in its band under the list and the left margin stays empty (the numbers of
    `get_state.material` are `tests/scenarios/captured_material.jsonl`).

    That band of a screenshot is compared, not the whole picture: the HUD says other words in each picture. A position set from outside and a reset in between bring the band back to the one of
    the start."""
    if client.call("status")["manualClock"]:
        client.call("step")  # the scene registers its pieces in the first frame
    client.call("reset")
    client.call("set_state", {"opponent": {"plays": None}})
    if not headless:
        return
    let_frames_pass(client, 2)  # the display waits for the first layout of its canvas
    start = take_screenshot(client)
    start_band = lower_group_band(start)
    start_margin = margin_band(start)

    for uci in CAPTURE_MOVES:
        client.call("perform_action", move(uci))
    client.call("set_state", {"fen": "4k3/8/8/8/8/8/8/R3K3 w - - 0 1"})  # the display follows a position set from outside
    client.call("reset")
    for uci in CAPTURE_MOVES:
        client.call("perform_action", move(uci))
    let_frames_pass(client, 2)
    captured = take_screenshot(client)
    captured_band = lower_group_band(captured)
    check(captured_band != start_band, "the band of the move panel under the list did not change after White took a pawn")
    check(band_has_color(captured_band, captured.channels, CHIP_COLOR), "no row of the display is drawn in the move panel after the capture")
    check(margin_band(captured) == start_margin and not band_has_color(margin_band(captured), captured.channels, CHIP_COLOR), "the left margin shows the material while the move panel does")

    client.call("reset")
    let_frames_pass(client, 2)
    check(lower_group_band(take_screenshot(client)) == start_band, "the band of the move panel under the list after a reset is not the one of the start position")


TAKE_BACK_COLOR = (112, 84, 48)  # the fill of the takeback button of the move panel (ChessMovePanel.xml); greyed it is drawn at half its alpha over the plate


def take_back_over_the_api(client: BagiControlClient, expected_moves: list) -> None:
    """set_state {"takeBack": {}} (the button's rule), then the moves must be these and the pieces at rest."""
    version = client.call("get_state")["version"]
    client.call("set_state", {"takeBack": {}})
    reply = client.call("get_state")
    check(reply["state"]["moves"] == expected_moves, f"after a takeback the moves are {reply['state']['moves']}, not {expected_moves}")
    check(reply["version"] > version, "a takeback did not move the state version")
    let_the_pieces_settle(client)


def run_takeback_scenario(client: BagiControlClient, headless: bool) -> None:
    """The takeback over the control API and by the button of the move panel, and in the picture: a position taken back looks like the same position played (the board and the move
    panel, the HUD says other words); the button is greyed while there is nothing to take back. The rules of what is taken back are `tests/scenarios/takeback.jsonl` and the gtests.

    The pictures are taken before the button is clicked: a click leaves the pointer over it, and its hover look would change the band."""
    if client.call("status")["manualClock"]:
        client.call("step")  # the scene registers its pieces in the first frame
    client.call("reset")
    client.call("set_state", {"opponent": {"plays": None}})
    expect_error("invalid_params", lambda: client.call("set_state", {"takeBack": {}}), "a takeback before the first move")
    let_frames_pass(client, 2)  # the panel waits for the first layout of its canvas
    start = take_screenshot(client) if headless else None

    client.call("perform_action", move("e2e4"))
    let_the_pieces_settle(client)
    one = take_screenshot(client) if headless else None
    client.call("perform_action", move("e7e5"))
    state = client.call("get_state")["state"]
    check(state["sanMoves"] == ["e4", "e5"] and state["takeBackPlies"] == 1, f"the state has sanMoves {state['sanMoves']} and takeBackPlies {state['takeBackPlies']}")

    take_back_over_the_api(client, ["e2e4"])
    if headless:
        back = take_screenshot(client)
        check(board_band(back) == board_band(one) and panel_band(back) == panel_band(one), "e4 e5 and a takeback do not look like e4 alone")
        check(band_has_color(panel_band(back), back.channels, TAKE_BACK_COLOR), "the takeback button is greyed while there is a move to take back")
    take_back_over_the_api(client, [])
    if headless:
        back = take_screenshot(client)
        check(board_band(back) == board_band(start) and panel_band(back) == panel_band(start), "the game taken back to its start does not look like the start")
        check(not band_has_color(panel_band(back), back.channels, TAKE_BACK_COLOR), "the takeback button is not greyed with nothing to take back")

    client.call("perform_action", move("d2d4"))
    let_frames_pass(client, 2)  # the button is no longer greyed from the next draw
    version = client.call("get_state")["version"]
    clicked = client.call("ui_click", {"name": "MovePanelTakeBack"})
    check(clicked["consumed"] and clicked["action"] == "ChessTakeBack", f"the click on the takeback button was answered with {clicked}")
    reply = client.call("get_state")
    check(reply["state"]["moves"] == [] and reply["version"] > version, f"the button took back to {reply['state']['moves']}")

    check_a_draw_ends_the_game_and_a_takeback_reopens_it(client)


def check_a_draw_ends_the_game_and_a_takeback_reopens_it(client: BagiControlClient) -> None:
    """Two people: set_state {"draw": {}} agrees a draw at once (no judge, no frame needed), and the takeback of the move list puts the game on again."""
    client.call("reset")
    client.call("set_state", {"opponent": {"plays": None}})
    client.call("perform_action", move("e2e4"))
    client.call("perform_action", move("e7e5"))
    version = client.call("get_state")["version"]

    client.call("set_state", {"draw": {}})
    reply = client.call("get_state")
    state = reply["state"]
    check(state["status"] == "draw_agreed" and state["winner"] is None and state["drawOffer"] is None, f"an agreed draw reads {state['status']}, winner {state['winner']}, offer {state['drawOffer']}")
    check(reply["version"] > version, "an agreed draw did not move the state version")
    check(client.call("list_actions")["actions"] == [], "a game ended by agreement still lists moves")
    expect_error("game_over", lambda: client.call("set_state", {"draw": {}}), "a draw offered in a game that was agreed")

    client.call("set_state", {"takeBack": {}})
    state = client.call("get_state")["state"]
    check(state["status"] == "ongoing" and state["moves"] == ["e2e4"], f"after the takeback the game reads {state['status']} with the moves {state['moves']}")
    let_the_pieces_settle(client)


def find_ui_canvas(tree: dict, node: str) -> dict:
    """The canvas of the node of that name in a `ui_tree` answer."""
    for canvas in tree["canvases"]:
        if canvas["node"] == node:
            return canvas
    raise AssertionError(f"no canvas on the node {node} in {[canvas['node'] for canvas in tree['canvases']]}")


def panel_element(tree: dict, name: str) -> dict:
    """An element of the opponent panel (never one of the HUD that has the same name), or a failed check."""
    canvas = find_ui_canvas(tree, "OpponentPanel")
    found = find_ui_element({"canvases": [canvas]}, name)
    check(found is not None, f"the opponent panel has no element {name}")
    return found


def press_in_the_panel(client: BagiControlClient, name: str, action: str, value: str) -> dict:
    """A click on the element as a user makes it; gives the tree after two frames. The click must be taken and emit this action and value."""
    clicked = client.call("ui_click", {"name": name})
    check(clicked["consumed"] and clicked["action"] == action and clicked["value"] == value, f"the click on {name} was answered with {clicked}, not {action} {value!r}")
    let_frames_pass(client, 2)
    return client.call("ui_tree")


def close_the_opponent_panel(client: BagiControlClient) -> dict:
    """A click on the dim of the opponent panel at its left edge, beside the plate (a section has no Close button); gives the tree after two frames."""
    clicked = client.call("ui_click", {"name": "Dim", "canvas": "OpponentPanel", "at": 0})
    check(clicked["consumed"] and clicked["action"] == "ChessCloseMenu", f"the click on the dim of the panel was answered with {clicked}, not ChessCloseMenu")
    let_frames_pass(client, 2)
    return client.call("ui_tree")


def check_the_menu_list_is_hidden(tree: dict) -> None:
    """The list of the menu is a hidden modal canvas (the start, and after Close)."""
    menu = find_ui_canvas(tree, "MenuPanel")
    check(menu["visible"] is False and menu["modal"] is True, f"the list of the menu should be hidden and modal: {menu}")


def check_the_menu_list_is_shown(tree: dict) -> None:
    """The list of the menu is shown and modal, and no section panel is shown beside it."""
    menu = find_ui_canvas(tree, "MenuPanel")
    check(menu["visible"] is True and menu["modal"] is True, f"the list of the menu should be shown and modal: {menu}")
    check(find_ui_canvas(tree, "OpponentPanel")["visible"] is False, "a section panel is shown beside the list")


def check_the_menu_buttons(tree: dict) -> None:
    """The buttons of the list: one per section the build has (Opponent, Moves, Resign / Draw), Close and the dim that closes it like Close; no Puzzles or Editor button."""
    names = []

    def collect(element: dict) -> None:
        if element.get("action"):
            names.append(element["name"])
        for child in element["children"]:
            collect(child)

    for element in find_ui_canvas(tree, "MenuPanel")["elements"]:
        collect(element)
    check(sorted(names) == ["MenuClose", "MenuDim", "MenuSectionGame", "MenuSectionMoves", "MenuSectionOpponent", "MenuSectionSound"], f"the buttons of the list: {names}")
    dim = find_ui_element({"canvases": [find_ui_canvas(tree, "MenuPanel")]}, "MenuDim")
    check(dim["action"] == "ChessCloseMenu", f"the dim of the list: {dim}")
    found = find_ui_element({"canvases": [find_ui_canvas(tree, "MenuPanel")]}, "MenuSectionOpponent")
    check(found["text"] == "Opponent" and found["action"] == "ChessOpenSection" and found["value"] == "opponent", f"the Opponent button of the list: {found}")


def check_selected(tree: dict, selected: list, unselected: list) -> None:
    """The segments of the panel that the variables name, and those they do not."""
    for name in selected:
        check(panel_element(tree, name)["selected"] is True, f"the segment {name} is not selected")
    for name in unselected:
        check(panel_element(tree, name)["selected"] is False, f"the segment {name} is selected")


def check_no_group_disabled(tree: dict) -> None:
    """No group of the panel is greyed in any mode: a greyed group read as a dead button."""
    for group in ("ModeGroup", "ColorGroup", "EngineGroup", "LevelGroup"):
        check(panel_element(tree, group)["disabled"] is False, f"{group} is disabled")


def run_opponent_panel_scenario(client: BagiControlClient, headless: bool) -> None:
    """What the opponent panel shows in this build and the program's play behind its buttons (ui_click, the own engine, which needs nothing from the environment).

    The clicks themselves and what the game reads back from them (level, side, engine, mode, the dim that closes it, the closed panel refusing a click) are
    `tests/scenarios/opponent_panel.jsonl`. Here stay what a scenario cannot say: the tree read by element name where the build decides (the Stockfish segment, the one that is
    selected at the start), and the replies of the program, which come on a worker thread: the answer to the person's move, the opening move of the program as White, and a game
    of two people in which the program stays out."""
    if client.call("status")["manualClock"]:
        client.call("step")
    client.call("reset")
    let_frames_pass(client, 2)

    # The start: the panel is hidden and modal, the HUD button says "Menu"
    tree = client.call("ui_tree")
    panel = find_ui_canvas(tree, "OpponentPanel")
    check(panel["visible"] is False and panel["modal"] is True, f"the opponent panel should start hidden and modal: {panel}")
    menu_button = find_ui_element({"canvases": [canvas for canvas in tree["canvases"] if canvas["node"] == "Hud"]}, "Menu")
    check(menu_button is not None and menu_button["text"] == "Menu" and menu_button["action"] == "ChessOpenMenu", f"the Menu button: {menu_button}")

    check_the_menu_list_is_hidden(tree)

    # The HUD button opens the list of the menu and only it; the list has the sections of the build and Close, no feature that does not exist
    tree = press_in_the_panel(client, "Menu", "ChessOpenMenu", "")
    check_the_menu_list_is_shown(tree)
    check_the_menu_buttons(tree)

    # Back: the Opponent button hides the list and opens the panel, Back returns to the list, Close of the list hides it
    tree = press_in_the_panel(client, "MenuSectionOpponent", "ChessOpenSection", "opponent")
    check(find_ui_canvas(tree, "OpponentPanel")["visible"] is True and find_ui_canvas(tree, "MenuPanel")["visible"] is False, "the Opponent button did not swap the list for the panel")
    tree = press_in_the_panel(client, "OpponentBack", "ChessOpenMenu", "")
    check_the_menu_list_is_shown(tree)
    tree = press_in_the_panel(client, "MenuClose", "ChessCloseMenu", "")
    check_the_menu_list_is_hidden(tree)
    check(find_ui_canvas(tree, "OpponentPanel")["visible"] is False, "Close of the list opened the panel")

    # Again to the panel, which shows what the game has: two people, White, Stockfish (the own engine where no Stockfish is found or in a build without it, which hides the
    # Stockfish segment so that Own engine fills the group), medium
    stockfish = default_engine(client) == "stockfish"
    press_in_the_panel(client, "Menu", "ChessOpenMenu", "")
    tree = press_in_the_panel(client, "MenuSectionOpponent", "ChessOpenSection", "opponent")
    check(find_ui_canvas(tree, "OpponentPanel")["visible"] is True, "the Opponent button of the list did not open the panel")
    if stockfish:
        check_selected(tree, ["ModeTwo", "ColorWhite", "EngineStockfish", "LevelMedium"], ["ModeComputer", "ColorBlack", "EngineOwn", "LevelEasy", "LevelHard", "LevelMax"])
    else:
        check_selected(tree, ["ModeTwo", "ColorWhite", "EngineOwn", "LevelMedium"], ["ModeComputer", "ColorBlack", "LevelEasy", "LevelHard", "LevelMax"])
        check(panel_element(tree, "EngineStockfish")["visible"] is False, f"the Stockfish segment is shown where no Stockfish is offered: {panel_element(tree, 'EngineStockfish')}")
        check(panel_element(tree, "EngineOwn")["visible"] is True, f"the Own engine segment is not shown: {panel_element(tree, 'EngineOwn')}")
    check_no_group_disabled(tree)
    check(panel_element(tree, "OpponentError")["text"] == "", f"the error line is not empty: {panel_element(tree, 'OpponentError')}")

    # The own engine and Computer, with the person as White: the person's move is answered
    tree = press_in_the_panel(client, "EngineOwn", "ChessSetEngine", "own")
    check_selected(tree, ["EngineOwn"], ["EngineStockfish"] if stockfish else [])
    press_in_the_panel(client, "LevelEasy", "ChessSetLevel", "easy")
    tree = press_in_the_panel(client, "ModeComputer", "ChessSetMode", "computer")
    check_selected(tree, ["ModeComputer", "LevelEasy", "ColorWhite"], ["ModeTwo"])
    check_no_group_disabled(tree)
    play_against_the_own_engine(client)

    # Black for the person: the program plays White and opens the new game
    client.call("reset")
    let_frames_pass(client, FRAMES_WITHOUT_AN_ANSWER)
    check(get_moves(client) == [], f"the program, playing Black, moved first: {get_moves(client)}")
    tree = press_in_the_panel(client, "ColorBlack", "ChessSetColor", "black")
    check_selected(tree, ["ColorBlack"], ["ColorWhite"])
    wait_until_the_program_played(client, lambda moves: len(moves) == 1, "the opening move")
    state = client.call("get_state")["state"]
    check(state["opponent"]["plays"] == "white" and state["turn"] == "black" and state["moves"][0][1] in ("1", "2"), f"the program did not open as White: {state}")
    hud_button = find_ui_element({"canvases": [canvas for canvas in client.call("ui_tree")["canvases"] if canvas["node"] == "Hud"]}, "Menu")
    check(hud_button["text"] == "Menu", f"the Menu button is not a fixed word: {hud_button['text']}")

    # Back to two people: no group is greyed, the person's color is kept, the program stays out of the game
    tree = press_in_the_panel(client, "ModeTwo", "ChessSetMode", "two")
    check_selected(tree, ["ModeTwo", "ColorBlack", "EngineOwn", "LevelEasy"], ["ModeComputer", "ColorWhite"])
    check_no_group_disabled(tree)
    moves = get_moves(client)
    let_frames_pass(client, FRAMES_WITHOUT_AN_ANSWER)
    check(get_moves(client) == moves and get_opponent(client)["thinking"] is False, "the program moved with nobody picked")
    tree = close_the_opponent_panel(client)
    check(find_ui_canvas(tree, "OpponentPanel")["visible"] is False, "the dim did not close the panel")


def run_without_stockfish_scenario(client: BagiControlClient, headless: bool) -> None:
    """No Stockfish to run (a build without it, CHESS_NO_STOCKFISH, or a build with it and no file found: STOCKFISH, the settings and PATH name none): the game starts with the own
    engine, `stockfish` is refused everywhere it can be named, the panel does not offer it and the own engine plays. A game that found a Stockfish has nothing to check here."""
    if client.call("status")["manualClock"]:
        client.call("step")
    client.call("reset")
    if default_engine(client) == "stockfish":
        print("NOTICE: this game found a Stockfish: the scenario 'without Stockfish' has nothing to check", flush=True)
        return

    opponent = get_opponent(client)
    check(opponent["plays"] is None and opponent["engine"] == "own" and opponent["error"] is None, f"unexpected default opponent {opponent}")

    # The panel: no Stockfish segment, Own engine selected
    let_frames_pass(client, 2)
    press_in_the_panel(client, "Menu", "ChessOpenMenu", "")
    tree = press_in_the_panel(client, "MenuSectionOpponent", "ChessOpenSection", "opponent")
    check(panel_element(tree, "EngineStockfish")["visible"] is False, f"the Stockfish segment is shown: {panel_element(tree, 'EngineStockfish')}")
    check(panel_element(tree, "EngineOwn")["visible"] is True, f"the Own engine segment is not shown: {panel_element(tree, 'EngineOwn')}")
    check_selected(tree, ["EngineOwn"], [])
    tree = close_the_opponent_panel(client)
    check(find_ui_canvas(tree, "OpponentPanel")["visible"] is False, "the dim did not close the panel")

    # Stockfish is refused wherever it can be named, also while nobody plays; the opponent stays as it was and the error says why
    refused = [
        ("engine alone", {"engine": "stockfish"}),
        ("engine with a side", {"plays": "black", "engine": "stockfish"}),
        ("White's seat", {"plays": "black", "white": {"engine": "stockfish"}}),
        ("Black's seat", {"black": {"engine": "stockfish", "level": "hard"}}),
    ]
    for what, pick in refused:
        version = client.call("get_state")["version"]
        expect_error("engine_unavailable", lambda pick=pick: client.call("set_state", {"opponent": pick}), what)
        state = client.call("get_state")
        opponent = state["state"]["opponent"]
        check(opponent["plays"] is None and opponent["engine"] == "own" and opponent["level"] == "medium", f"{what}: the refused pick changed the opponent: {opponent}")
        check(opponent["seats"]["white"]["engine"] == "own" and opponent["seats"]["black"]["engine"] == "own", f"{what}: the refused pick changed a seat: {opponent['seats']}")
        # "unsupported" where the build has no Stockfish, "launch_failed" where it has and none is found
        check(opponent["error"] is not None and opponent["error"]["kind"] in ("unsupported", "launch_failed"), f"{what}: the reason is not in opponent.error: {opponent['error']}")
        check(state["version"] > version, f"{what}: the refusal did not move the state version")
        check(get_moves(client) == [], f"{what}: a refused pick played a move")
        client.call("set_state", {"opponent": {"plays": None}})  # a pick that changes nothing but clears the standing error
        check(get_opponent(client)["error"] is None, f"{what}: plays null did not clear the error")
    # A position next to the refused pick is not set either
    expect_error("engine_unavailable", lambda: client.call("set_state", {"fen": MATE_IN_ONE_FEN, "opponent": {"engine": "stockfish"}}), "a fen with the refused pick")
    check(client.call("get_state")["state"]["fen"] == START_FEN, "the position was replaced by a refused call")
    client.call("set_state", {"opponent": {"plays": None}})

    # The way on is a side with the own engine: the program answers
    client.call("set_state", {"opponent": {"plays": "black"}})
    opponent = get_opponent(client)
    check(opponent["plays"] == "black" and opponent["engine"] == "own" and opponent["error"] is None, f"a side alone was not read back: {opponent}")
    play_against_the_own_engine(client)


def local_settings_exist() -> bool:
    return LOCAL_SETTINGS.exists()


def write_stockfish_wrapper(directory: Path) -> Path:
    """A script that starts the fake UCI engine, for STOCKFISH: the game takes it for a Stockfish."""
    wrapper = directory / "stockfish"
    wrapper.write_text(f'#!/bin/sh\nexec "{sys.executable}" "{FAKE_ENGINE}" --moves {FAKE_ENGINE_MOVES}\n')
    wrapper.chmod(wrapper.stat().st_mode | stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
    return wrapper


BASE_SCENARIOS = [
    ("real clock", [], run_chess_scenario),
    ("manual clock", ["--manual-clock"], run_chess_scenario),
    ("moves before the first frame", ["--manual-clock"], run_moves_before_the_first_frame),
    ("own engine, real clock", [], run_own_engine_scenario),
    ("own engine, manual clock", ["--manual-clock"], run_own_engine_scenario),
    ("watch, real clock", [], run_watch_scenario),
    ("watch, manual clock", ["--manual-clock"], run_watch_scenario),
    ("analysis, real clock", [], run_analysis_scenario),
    ("analysis, manual clock", ["--manual-clock"], run_analysis_scenario),
    ("analysis buttons, real clock", [], run_analysis_buttons_scenario),
    ("analysis buttons, manual clock", ["--manual-clock"], run_analysis_buttons_scenario),
    ("without Stockfish, real clock", [], run_without_stockfish_scenario),
    ("without Stockfish, manual clock", ["--manual-clock"], run_without_stockfish_scenario),
    ("opponent panel, real clock", [], run_opponent_panel_scenario),
    ("opponent panel, manual clock", ["--manual-clock"], run_opponent_panel_scenario),
    ("captured material, real clock", [], run_material_scenario),
    ("captured material, manual clock", ["--manual-clock"], run_material_scenario),
    ("takeback, real clock", [], run_takeback_scenario),
    ("takeback, manual clock", ["--manual-clock"], run_takeback_scenario),
]

OPPONENT_SCENARIOS = [
    ("opponent, real clock", [], run_opponent_scenario),
    ("opponent, manual clock", ["--manual-clock"], run_opponent_scenario),
]


SAVED_CHOICE = {"engine": "own", "plays": "black", "level": "easy"}
SAVED_VOLUME = 0.4
VOLUME_TOLERANCE = 1e-6  # the level is a float32, the wire a double


class RestartPhase:
    """A game that is closed with `quit` and started again on the same `--save-dir` comes back: the position and the opponent choice (the flag turns the save on despite --control and --headless).

    Each step is a game of its own (run_smoke starts one per scenario); `self.before` carries what the first one saw. The folder is the phase's own temporary one, never the person's."""

    def __init__(self, folder: Path, clock_flags: list) -> None:
        self.folder = folder
        self.flags = [f"--save-dir={folder}", *clock_flags]
        self.before = {}

    def scenarios(self, label: str) -> list:
        return [
            (f"restart, {label}: play", self.flags, self.play),
            (f"restart, {label}: the game is back", self.flags, self.come_back),
            (f"restart, {label}: a reset is kept", self.flags, self.keep_the_fresh_game),
        ]

    @staticmethod
    def read(client: BagiControlClient) -> dict:
        state = client.call("get_state")["state"]
        read = {key: state[key] for key in ("fen", "moves", "startFen", "opponent")}
        read["opponent"] = {key: value for key, value in read["opponent"].items() if key != "seed"}  # the seed is not saved: a started game draws its own
        return read

    def play(self, client: BagiControlClient, headless: bool) -> None:
        check(not any(self.folder.iterdir()), "the folder of the save is not empty before the first game")
        if client.call("status")["manualClock"]:
            client.call("step")
        client.call("set_state", {"opponent": SAVED_CHOICE})
        play_against_the_own_engine(client)
        self.before = self.read(client)
        check(len(self.before["moves"]) == 2, f"unexpected game {self.before}")
        client.call("audio_set", {"bus": "master", "volume": SAVED_VOLUME})
        # quit (by play()) goes through ChessGame::Stop(), which writes the game and the levels

    def come_back(self, client: BagiControlClient, headless: bool) -> None:
        if client.call("status")["manualClock"]:
            client.call("step")
        after = self.read(client)
        check(after == self.before, f"the game did not come back after a restart: {self.before} then {after}")
        check(abs(master_level(client)["volume"] - SAVED_VOLUME) < VOLUME_TOLERANCE, f"the master volume did not come back after a restart: {master_level(client)}")
        client.call("audio_set", {"bus": "master", "volume": 1})  # the exit writes the levels: the next start finds the defaults
        client.call("reset")  # the exit writes the new game

    def keep_the_fresh_game(self, client: BagiControlClient, headless: bool) -> None:
        if client.call("status")["manualClock"]:
            client.call("step")
        check_start_position(client)
        opponent = get_opponent(client)
        check(
            opponent["plays"] == SAVED_CHOICE["plays"] and opponent["engine"] == SAVED_CHOICE["engine"] and opponent["level"] == SAVED_CHOICE["level"],
            f"a reset and a restart lost the opponent choice: {opponent}",
        )


def run_without_a_save_folder(client: BagiControlClient, headless: bool) -> None:
    """The usual smoke run names no folder: a control run never reads a person's game, so the opening comes up with nobody playing."""
    if client.call("status")["manualClock"]:
        client.call("step")
    check_start_position(client)
    opponent = get_opponent(client)
    check(opponent["plays"] is None and opponent["error"] is None, f"a game started with no save folder has an opponent: {opponent}")
    check(master_level(client) == {"volume": 1.0, "muted": False}, f"a game started with no save folder read the levels of a save: {master_level(client)}")


def main() -> int:
    scenarios = list(BASE_SCENARIOS)
    restart_scenarios = []
    with tempfile.TemporaryDirectory() as temp_dir:
        for label, clock_flags in (("real clock", []), ("manual clock", ["--manual-clock"])):
            folder = Path(temp_dir) / ("saves " + label.replace(" ", "_"))
            folder.mkdir()
            restart_scenarios += RestartPhase(folder, clock_flags).scenarios(label)
        restart_scenarios.append(("restart: no save folder", [], run_without_a_save_folder))
        if sys.platform == "win32":
            print("NOTICE: the opponent scenarios are left out on Windows (UciProcess is POSIX only)", flush=True)
        elif local_settings_exist():
            print(f"NOTICE: the opponent scenarios are left out: {LOCAL_SETTINGS} exists and its path would win over STOCKFISH (the smoke does not edit your files)", flush=True)
        else:
            os.environ["STOCKFISH"] = str(write_stockfish_wrapper(Path(temp_dir)))  # the game inherits the environment
            scenarios += OPPONENT_SCENARIOS
        scenarios += restart_scenarios
        return run_smoke(__doc__, scenarios)


if __name__ == "__main__":
    sys.exit(main())
