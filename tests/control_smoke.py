#!/usr/bin/env python3
"""Smoke test of the control API for Chess: starts BagiChess with --control and plays over WebSocket.

    python tests/control_smoke.py --exe build/macos-debug/BagiChess

ctest runs it as BagiChessControlSmoke (be_add_game_control_tests). It builds on the engine's tools/control/tests/smoke.py,
which it finds through the environment variable BAGI_ENGINE_DIR (set by ctest), or in the submodule engine/. The game is played on the real clock, with
--manual-clock, and with moves before its first frame. Exit code: 0 passed, 1 failed, 77 skipped (the executable does not exist).
"""

import os
import sys
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
from smoke import BOARD_COLORS, check, check_state_push, expect_error, run_smoke, take_screenshot  # noqa: E402

START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
START_BOARD_TOP = "rnbqkbnr"
START_BOARD_BOTTOM = "RNBQKBNR"
START_MOVE_COUNT = 20
OPENING = ["e2e4", "e7e5", "g1f3", "b8c6"]
MATE_IN_ONE_FEN = "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"  # Ra8#
PROMOTION_FEN = "8/P6k/8/8/8/8/8/K7 w - - 0 1"


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


SCENARIOS = [
    ("real clock", [], run_chess_scenario),
    ("manual clock", ["--manual-clock"], run_chess_scenario),
    ("moves before the first frame", ["--manual-clock"], run_moves_before_the_first_frame),
]


if __name__ == "__main__":
    sys.exit(run_smoke(__doc__, SCENARIOS))
