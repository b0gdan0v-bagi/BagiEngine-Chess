#!/usr/bin/env python3
"""Smoke test of the control API for Chess: starts BagiChess with --control and plays over WebSocket.

    python tests/control_smoke.py --exe build/macos-debug/BagiChess

ctest runs it as BagiChessControlSmoke (be_add_game_control_tests). It builds on the engine's tools/control/tests/smoke.py,
which it finds through the environment variable BAGI_ENGINE_DIR (set by ctest), or in the submodule engine/. The game is played on the real clock, with
--manual-clock, and with moves before its first frame. The computer opponent is played on both clocks in two ways. The engine of our own ("own
engine" scenarios, base ones) needs nothing from the environment: it runs inside the game, with no process, so it also runs where the next kind is left out;
it answers the same move to the same position (a fixed depth), which the scenario checks after a reset. The opponent panel ("opponent panel" scenarios, base
ones too) is driven only by `ui_click` on the buttons and segments found by name, as a person would: open it with the Opponent button, pick the own engine,
Computer, a level and a color, read the pick back from `get_state` and from `ui_tree`, play, close it with OK. Stockfish ("opponent" scenarios) is replaced by
tests/fake_uci_engine.py: main() writes a wrapper script that starts it into a temporary directory and puts the script in STOCKFISH of the environment,
which the game inherits. STOCKFISH comes before PATH in the game's search for an engine and the committed config has no path, so a Stockfish installed on
this machine is never used. A config/ChessSettings.local.xml (a person's own path, which would win over STOCKFISH) leaves the Stockfish scenarios out, as
does Windows; the smoke never edits a person's files. Exit code: 0 passed, 1 failed, 77 skipped (the executable does not exist).
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

from bagi_control_client import BagiControlClient  # noqa: E402
from smoke import BOARD_COLORS, check, check_state_push, expect_error, find_ui_element, run_smoke, take_screenshot  # noqa: E402

START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
START_BOARD_TOP = "rnbqkbnr"
START_BOARD_BOTTOM = "RNBQKBNR"
START_MOVE_COUNT = 20
OPENING = ["e2e4", "e7e5", "g1f3", "b8c6"]
MATE_IN_ONE_FEN = "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"  # Ra8#
PROMOTION_FEN = "8/P6k/8/8/8/8/8/K7 w - - 0 1"

REPO_ROOT = Path(__file__).resolve().parents[1]
FAKE_ENGINE = Path(__file__).resolve().with_name("fake_uci_engine.py")
LOCAL_SETTINGS = REPO_ROOT / "config" / "ChessSettings.local.xml"
FAKE_ENGINE_MOVES = "e7e5,b8c6,g8f6"  # what the fake answers to its 1st, 2nd and 3rd search
OPPONENT_WAIT_SECONDS = 30.0  # a stall of the machine is not a failure; a program that never answers is
POLL_SECONDS = 0.01
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
    return reply


def check_refusals(client: BagiControlClient) -> None:
    """What is refused changes nothing: not a legal move, not a move at all, not a position."""
    before = client.call("get_state")

    expect_error("illegal_action", lambda: client.call("perform_action", move("e7e5")), "a move of the side that is not to move")
    expect_error("illegal_action", lambda: client.call("perform_action", move("a1a8")), "a rook through its own pawn")
    expect_error("invalid_params", lambda: client.call("perform_action", move("banana")), "a move that is not UCI")
    expect_error("invalid_params", lambda: client.call("perform_action", {"type": "resign"}), "an action that is not a move")
    expect_error("invalid_params", lambda: client.call("set_state", {"fen": "not a fen"}), "a position that is not a FEN")
    expect_error("invalid_params", lambda: client.call("set_state", {}), "a state without a FEN")

    after = client.call("get_state")
    check(after == before, "a refused call changed the state")


def check_mate(client: BagiControlClient) -> None:
    """A position set from outside, mate in one, and a game that takes no more moves once it is over."""
    loaded = client.call("set_state", {"fen": MATE_IN_ONE_FEN})
    state = client.call("get_state")
    check(state["version"] == loaded["version"], "set_state and get_state disagree about the version")
    check(state["state"]["startFen"] == MATE_IN_ONE_FEN and state["state"]["moves"] == [], f"the loaded position is not the start of a game: {state['state']}")
    check(move("a1a8") in client.call("list_actions")["actions"], "the mating move is not listed")

    outcome = client.call("perform_action", move("a1a8"))["outcome"]
    check(outcome["status"] == "checkmate" and outcome["winner"] == "white" and outcome["inCheck"] is True, f"the mate was answered with {outcome}")

    state = client.call("get_state")["state"]
    check(state["status"] == "checkmate" and state["winner"] == "white" and state["legalMoveCount"] == 0, f"unexpected standing after the mate: {state}")
    check(client.call("list_actions")["actions"] == [], "a finished game still lists actions")
    expect_error("game_over", lambda: client.call("perform_action", move("g8f8")), "a move after the mate")

    client.call("reset")
    check_start_position(client)


def check_promotion(client: BagiControlClient) -> None:
    client.call("set_state", {"fen": PROMOTION_FEN})
    expect_error("promotion_required", lambda: client.call("perform_action", move("a7a8")), "a promotion without a piece")
    client.call("perform_action", move("a7a8q"))
    board = client.call("get_state")["state"]["board"]
    check(board[0].startswith("Q"), f"the queen is not on a8: {board}")


def check_screenshots(client: BagiControlClient) -> None:
    """The picture follows the model: a move changes it, a reset brings it back."""
    client.call("reset")
    start = take_screenshot(client)
    check(len(start.colors()) >= BOARD_COLORS, f"the screenshot has {len(start.colors())} colours, it does not look like a board")

    client.call("perform_action", move("e2e4"))
    moved = take_screenshot(client)
    check(moved.rows != start.rows, "the screenshot did not change after a move")

    client.call("reset")
    again = take_screenshot(client)
    check(again.rows == start.rows, "the screenshot after a reset is not the one of the start position")

    # A position that is set from outside is drawn, and a pawn that promoted is drawn as its new piece
    client.call("set_state", {"fen": PROMOTION_FEN})
    before_promotion = take_screenshot(client)
    client.call("perform_action", move("a7a8q"))
    promoted = take_screenshot(client)
    check(promoted.rows != before_promotion.rows, "the screenshot did not change after a promotion")


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

    check_refusals(client)
    check_mate(client)
    check_promotion(client)

    if headless:
        check_screenshots(client)


def run_moves_before_the_first_frame(client: BagiControlClient, headless: bool) -> None:
    """A client that acts at once: the scene registers its pieces in the first frame, after these moves, and has to show them."""
    if not headless:
        return  # the check is made on screenshots
    for uci in OPENING:
        client.call("perform_action", move(uci))  # the manual clock has run no frame yet
    client.call("step", {"frames": 2})
    by_moves = take_screenshot(client)

    # The same position set from outside is drawn the same way, and it is not the opening
    client.call("set_state", {"fen": client.call("get_state")["state"]["fen"]})
    client.call("step", {"frames": 2})
    by_fen = take_screenshot(client)
    check(by_moves.rows == by_fen.rows, "the scene after moves played before the first frame is not the scene of the same position")
    client.call("reset")
    client.call("step", {"frames": 2})
    check(by_moves.rows != take_screenshot(client).rows, "the scene still shows the opening after four moves")


def get_opponent(client: BagiControlClient) -> dict:
    return client.call("get_state")["state"]["opponent"]


def get_moves(client: BagiControlClient) -> list:
    return client.call("get_state")["state"]["moves"]


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
    check(get_opponent(client)["engine"] == "stockfish", f"the default engine is not stockfish: {get_opponent(client)}")

    version = client.call("get_state")["version"]
    picked = client.call("set_state", {"opponent": {"engine": "own", "plays": "black", "level": "easy"}})
    check(picked["version"] > version, "picking the own engine did not move the state version")
    opponent = get_opponent(client)
    check(opponent["engine"] == "own" and opponent["plays"] == "black" and opponent["level"] == "easy" and opponent["error"] is None, f"the pick was not read back: {opponent}")
    check(get_moves(client) == [], "picking the opponent played a move")

    first = play_against_the_own_engine(client)

    # A fixed depth is deterministic, and so is the handicap of "easy" (a pure function of the position): the same game after a reset gets the same reply, and the reset keeps the engine
    client.call("reset")
    check(get_opponent(client)["engine"] == "own" and get_opponent(client)["plays"] == "black", f"a reset lost the pick: {get_opponent(client)}")
    second = play_against_the_own_engine(client)
    check(first == second, f"the own engine answered e2e4 with {first}, then with {second}")

    # Back to Stockfish and to two people: the program stays out of the game
    client.call("set_state", {"opponent": {"engine": "stockfish", "plays": None}})
    opponent = get_opponent(client)
    check(opponent["engine"] == "stockfish" and opponent["plays"] is None and opponent["error"] is None, f"the switch back was not read: {opponent}")
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


# The scene is 840 x 800 units, the board 640 x 640 at y 80..720; the window fits the scene by the smaller scale and centers it
VIEW_WIDTH, VIEW_HEIGHT = 840, 800
BOARD_TOP, BOARD_BOTTOM = 80, 720
BOARD_BAND_INSET = 2  # rows kept away from the edge of the board, against rounding


def board_band(image) -> list:
    """The rows of a screenshot that only the board covers: the HUD sits in the margins above and below it, and its words change with the opponent."""
    scale = min(image.width / VIEW_WIDTH, image.height / VIEW_HEIGHT)
    offset = (image.height - VIEW_HEIGHT * scale) / 2
    first = int(offset + BOARD_TOP * scale) + BOARD_BAND_INSET
    last = int(offset + BOARD_BOTTOM * scale) - BOARD_BAND_INSET
    check(0 <= first < last <= image.height, f"the board band {first}..{last} is outside a picture {image.width}x{image.height}")
    return image.rows[first:last]


def check_the_board_turns_with_the_program(client: BagiControlClient) -> None:
    """The picture is drawn from Black's side while the program plays White, and from White's side otherwise.

    Only the rows of the board are compared (`board_band`): the Opponent button says "You: Black" in one picture and "Two players" in the other, so
    whole pictures differ without the board turning. The same placement of the pieces is drawn three times: with the program as White (Black to move: the
    program does not search), with two people (Black to move) and with the program as Black (White to move, the same pieces: the side to move is not
    drawn on the board). The last two have White at the bottom and must give one board band, which also shows that the band leaves the HUD out; the
    turned one must differ from them, and drawn again after a round trip it must be the same band. Equality with a reference of "the white pieces at the
    bottom" proves the turn only as far as the pieces differ between the two orientations, which they do after any opening move."""
    client.call("reset")
    client.call("set_state", {"opponent": {"engine": "own", "plays": "white", "level": "easy"}})
    wait_until_the_program_played(client, lambda moves: len(moves) == 1, "the opening move of White")
    fen = client.call("get_state")["state"]["fen"]  # Black to move: the program, as White, does not search again
    check(fen.split()[1] == "b", f"Black is not to move after the opening move of White: {fen}")
    let_frames_pass(client, 2)
    rotated = take_screenshot(client)

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
    check(board_band(take_screenshot(client)) == board_band(rotated), "the board turned again is not the picture it was the first time")
    client.call("set_state", {"opponent": {"plays": None}})


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
    """The person's way to an opponent, through the buttons of the screen only (ui_click): open the panel, pick the own engine, Computer, a level and a color, play, close.
    The own engine runs in the game, so this needs nothing from the environment."""
    if client.call("status")["manualClock"]:
        client.call("step")
    client.call("reset")
    let_frames_pass(client, 2)

    # The start: the panel is hidden and modal, the HUD button says what the opponent is
    tree = client.call("ui_tree")
    panel = find_ui_canvas(tree, "OpponentPanel")
    check(panel["visible"] is False and panel["modal"] is True, f"the opponent panel should start hidden and modal: {panel}")
    opponent_button = find_ui_element({"canvases": [canvas for canvas in tree["canvases"] if canvas["node"] == "Hud"]}, "Opponent")
    check(opponent_button is not None and opponent_button["text"] == "Two players" and opponent_button["action"] == "ChessOpenOpponent", f"the Opponent button: {opponent_button}")
    check(get_opponent(client)["plays"] is None, f"unexpected opponent at the start: {get_opponent(client)}")

    # One press on the HUD button opens the panel, which shows what the game has: two people, White, Stockfish, medium
    tree = press_in_the_panel(client, "Opponent", "ChessOpenOpponent", "")
    check(find_ui_canvas(tree, "OpponentPanel")["visible"] is True, "the Opponent button did not open the panel")
    check_selected(tree, ["ModeTwo", "ColorWhite", "EngineStockfish", "LevelMedium"], ["ModeComputer", "ColorBlack", "EngineOwn", "LevelEasy", "LevelHard", "LevelMax"])
    check_no_group_disabled(tree)
    check(panel_element(tree, "OpponentError")["text"] == "", f"the error line is not empty: {panel_element(tree, 'OpponentError')}")

    # Two people play and the Level and Play as groups take clicks all the same: the pick is remembered and read back (it applies when Computer is picked)
    tree = press_in_the_panel(client, "LevelHard", "ChessSetLevel", "hard")
    check_selected(tree, ["LevelHard"], ["LevelMedium"])
    check_no_group_disabled(tree)
    opponent = get_opponent(client)
    check(opponent["level"] == "hard" and opponent["plays"] is None, f"a level picked for two people was not read back: {opponent}")
    tree = press_in_the_panel(client, "ColorBlack", "ChessSetColor", "black")
    check_selected(tree, ["ColorBlack"], ["ColorWhite"])
    check(get_opponent(client)["plays"] is None, f"a color picked for two people made the program play: {get_opponent(client)}")
    tree = press_in_the_panel(client, "ColorWhite", "ChessSetColor", "white")
    check_selected(tree, ["ColorWhite", "LevelHard"], ["ColorBlack"])

    # The engine, then the mode: nothing is asked of Stockfish, the own engine is made when the program is picked
    tree = press_in_the_panel(client, "EngineOwn", "ChessSetEngine", "own")
    check_selected(tree, ["EngineOwn"], ["EngineStockfish"])
    opponent = get_opponent(client)
    check(opponent["engine"] == "own" and opponent["plays"] is None and opponent["error"] is None, f"the engine was not read back: {opponent}")

    tree = press_in_the_panel(client, "ModeComputer", "ChessSetMode", "computer")
    check_selected(tree, ["ModeComputer", "LevelHard"], ["ModeTwo"])
    check_no_group_disabled(tree)
    opponent = get_opponent(client)
    check(opponent["plays"] == "black" and opponent["engine"] == "own" and opponent["level"] == "hard" and opponent["error"] is None,
          f"Computer with White for the person and the level picked before is not read back: {opponent}")
    hud_button = find_ui_element({"canvases": [canvas for canvas in tree["canvases"] if canvas["node"] == "Hud"]}, "Opponent")
    check(hud_button["text"] == "You: White", f"the Opponent button does not say the pick: {hud_button['text']}")

    tree = press_in_the_panel(client, "LevelEasy", "ChessSetLevel", "easy")
    check_selected(tree, ["LevelEasy"], ["LevelMedium"])
    check(get_opponent(client)["level"] == "easy", f"the level was not read back: {get_opponent(client)}")

    # The person has White (the program Black): a click on White again changes nothing, and the person's move is answered
    tree = press_in_the_panel(client, "ColorWhite", "ChessSetColor", "white")
    check_selected(tree, ["ColorWhite"], ["ColorBlack"])
    check(get_opponent(client)["plays"] == "black", f"White again changed the side: {get_opponent(client)}")
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
    hud_button = find_ui_element({"canvases": [canvas for canvas in client.call("ui_tree")["canvases"] if canvas["node"] == "Hud"]}, "Opponent")
    check(hud_button["text"] == "You: Black", f"the Opponent button does not say the pick: {hud_button['text']}")

    # Back to two people: no group is greyed, the person's color is kept, the program stays out of the game
    tree = press_in_the_panel(client, "ModeTwo", "ChessSetMode", "two")
    check_selected(tree, ["ModeTwo", "ColorBlack", "EngineOwn", "LevelEasy"], ["ModeComputer", "ColorWhite"])
    check_no_group_disabled(tree)
    check(get_opponent(client)["plays"] is None, f"two people: the opponent is {get_opponent(client)}")
    tree = press_in_the_panel(client, "LevelMax", "ChessSetLevel", "max")
    check_selected(tree, ["LevelMax"], ["LevelEasy"])
    check(get_opponent(client)["level"] == "max", f"a click on Level with two people was not read back: {get_opponent(client)}")
    moves = get_moves(client)
    let_frames_pass(client, FRAMES_WITHOUT_AN_ANSWER)
    check(get_moves(client) == moves and get_opponent(client)["thinking"] is False, "the program moved with nobody picked")

    # OK closes the panel
    tree = press_in_the_panel(client, "Ok", "ChessCloseOpponent", "")
    check(find_ui_canvas(tree, "OpponentPanel")["visible"] is False, "OK did not close the panel")
    expect_error("not_clickable", lambda: client.call("ui_click", {"name": "ModeComputer"}), "a click on a segment of the closed panel")


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
    ("opponent panel, real clock", [], run_opponent_panel_scenario),
    ("opponent panel, manual clock", ["--manual-clock"], run_opponent_panel_scenario),
]

OPPONENT_SCENARIOS = [
    ("opponent, real clock", [], run_opponent_scenario),
    ("opponent, manual clock", ["--manual-clock"], run_opponent_scenario),
]


def main() -> int:
    scenarios = list(BASE_SCENARIOS)
    with tempfile.TemporaryDirectory() as temp_dir:
        if sys.platform == "win32":
            print("NOTICE: the opponent scenarios are left out on Windows (UciProcess is POSIX only)", flush=True)
        elif local_settings_exist():
            print(f"NOTICE: the opponent scenarios are left out: {LOCAL_SETTINGS} exists and its path would win over STOCKFISH (the smoke does not edit your files)", flush=True)
        else:
            os.environ["STOCKFISH"] = str(write_stockfish_wrapper(Path(temp_dir)))  # the game inherits the environment
            scenarios += OPPONENT_SCENARIOS
        return run_smoke(__doc__, scenarios)


if __name__ == "__main__":
    sys.exit(main())
