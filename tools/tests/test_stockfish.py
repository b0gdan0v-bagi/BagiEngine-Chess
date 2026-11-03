"""The Stockfish bridge: the UCI side against a fake engine, the game loop against a scripted game, and (when Stockfish and a
built BagiChess exist) real games.

The fake engine and the scripted game make the tests independent of an installation; the last group is skipped without them.
"""

import glob
import os
import sys
import threading
import time
from pathlib import Path

import pytest

import stockfish_bridge as bridge
from bagi_control_client import BagiControlClient
from fake_control_server import FakeControlServer, FakeError
from game_process import GameProcess, build_game_command, free_port
from stockfish_bridge import BridgeError, RulesMismatch, UciEngine, UciError

TESTS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TESTS_DIR.parents[1]
FAKE_ENGINE = TESTS_DIR / "fake_uci_engine.py"

START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
START_POSITION = f"position fen {START_FEN}"
AFTER_E4_E5 = f"{START_POSITION} moves e2e4 e7e5"

# The legal moves of each ply of the scripted game (not real chess: only the first two moves are)
LEGAL_BY_PLY = [["e2e4", "d2d4"], ["e7e5", "c7c5"], ["g1f3", "f1c4"], ["b8c6", "g8f6"]]

needs_shebang = pytest.mark.skipif(sys.platform == "win32", reason="the fake engine is a script with a shebang")


class ScriptedGame:
    """The handler of a FakeControlServer that plays a Chess game on a script: the legal moves of every ply are given."""

    def __init__(self, legal_by_ply: list[list[str]], manual_clock: bool = True, refuse: bool = False):
        self.legal_by_ply = legal_by_ply
        self.manual_clock = manual_clock
        self.refuse = refuse
        self.moves: list[str] = []
        self.lock = threading.Lock()

    def _legal(self) -> list[str]:
        return self.legal_by_ply[len(self.moves)] if len(self.moves) < len(self.legal_by_ply) else []

    def __call__(self, method: str, params: dict | None):
        with self.lock:
            if method == "status":
                return {"frame": 0, "manualClock": self.manual_clock}
            if method == "get_state":
                over = len(self.moves) >= len(self.legal_by_ply)
                state = {
                    "fen": f"scripted-{len(self.moves)}",
                    "startFen": START_FEN,
                    "moves": list(self.moves),
                    "turn": "white" if len(self.moves) % 2 == 0 else "black",
                    "status": "checkmate" if over else "ongoing",
                    "winner": "white" if over else None,
                }
                return {"game": "Chess", "version": len(self.moves) + 1, "state": state}
            if method == "list_actions":
                return {"game": "Chess", "version": len(self.moves) + 1, "actions": [{"type": "move", "uci": uci} for uci in self._legal()]}
            if method == "perform_action":
                uci = (params or {}).get("uci")
                if self.refuse or uci not in self._legal():
                    raise FakeError("illegal_action", f"'{uci}' is not legal")
                self.moves.append(uci)
                return {"game": "Chess", "version": len(self.moves) + 1, "outcome": {"move": uci}}
            if method == "step":
                return {"frame": 1}
            raise FakeError("unknown_method", method)


@pytest.fixture
def make_game():
    """Starts a scripted game on a fake server; gives (game, server, client). Everything is stopped afterwards."""
    started = []

    def make(legal_by_ply=LEGAL_BY_PLY, **options):
        game = ScriptedGame(legal_by_ply, **options)
        server = FakeControlServer(game)
        client = BagiControlClient(port=server.start())
        client.connect()
        started.append((server, client))
        return game, server, client

    yield make
    for server, client in started:
        client.close()
        server.stop()


@pytest.fixture
def fake_engine(tmp_path, monkeypatch):
    """Makes a fake Stockfish: call it with the JSON-able config; gives (path, log file). Needs a shebang."""
    exe = tmp_path / "fakefish"
    exe.write_text(f"#!{sys.executable}\nimport runpy\nrunpy.run_path({str(FAKE_ENGINE)!r}, run_name='__main__')\n")
    exe.chmod(0o755)
    log = tmp_path / "engine.log"

    def configure(config: dict | None = None) -> tuple[str, Path]:
        import json

        config_path = tmp_path / "engine.json"
        config_path.write_text(json.dumps({"log": str(log), **(config or {})}))
        monkeypatch.setenv("FAKE_UCI_CONFIG", str(config_path))
        return str(exe), log

    return configure


def received(log: Path) -> list[str]:
    return log.read_text().splitlines()


# --- UCI -------------------------------------------------------------------------------------------------------------


@needs_shebang
def test_the_engine_is_started_with_its_options(fake_engine):
    path, log = fake_engine()
    with UciEngine(path, {"Skill Level": "0", "Threads": "1"}):
        pass
    lines = received(log)
    assert lines[0] == "uci"
    assert "setoption name Skill Level value 0" in lines and "setoption name Threads value 1" in lines
    assert lines.index("uci") < lines.index("setoption name Skill Level value 0") < lines.index("isready")
    assert lines[-1] == "quit"


@needs_shebang
def test_best_move_sends_the_position_and_reads_the_answer(fake_engine):
    path, log = fake_engine({"bestmoves": {AFTER_E4_E5: "g1f3"}})
    with UciEngine(path) as engine:
        assert engine.best_move(AFTER_E4_E5, 250) == "g1f3"
    lines = received(log)
    assert lines[lines.index(AFTER_E4_E5) + 1] == "go movetime 250"


@needs_shebang
def test_no_best_move_in_a_finished_position_is_none(fake_engine):
    path, _ = fake_engine({"bestmoves": {START_POSITION: None}})
    with UciEngine(path) as engine:
        assert engine.best_move(START_POSITION, 10) is None


@needs_shebang
def test_legal_moves_are_read_from_perft_1(fake_engine):
    path, log = fake_engine({"perft": {START_POSITION: ["e2e4", "d2d4", "e7e8q"]}})
    with UciEngine(path) as engine:
        assert engine.legal_moves(START_POSITION) == {"e2e4", "d2d4", "e7e8q"}
        assert engine.legal_moves(AFTER_E4_E5) == set()  # a position the engine has no moves for
    assert "go perft 1" in received(log)


@needs_shebang
def test_an_engine_that_never_answers_is_a_uci_error(fake_engine, monkeypatch):
    monkeypatch.setattr(bridge, "UCI_ANSWER_SLACK_SECONDS", 0.3)
    path, _ = fake_engine({"hang_on_go": True})
    with UciEngine(path) as engine:
        with pytest.raises(UciError, match="bestmove"):
            engine.best_move(START_POSITION, 10)


@needs_shebang
def test_an_engine_that_dies_is_a_uci_error(fake_engine):
    path, _ = fake_engine({"crash_on_go": True})
    with UciEngine(path) as engine:
        with pytest.raises(UciError, match="exited"):
            engine.best_move(START_POSITION, 10)


def test_a_missing_engine_is_a_uci_error(tmp_path):
    with pytest.raises(UciError, match="cannot start"):
        UciEngine(str(tmp_path / "no-such-engine"))


def test_find_stockfish_prefers_the_argument_then_the_environment(tmp_path, monkeypatch):
    explicit = tmp_path / "explicit"
    explicit.write_text("")
    from_environment = tmp_path / "environment"
    from_environment.write_text("")
    monkeypatch.setenv("STOCKFISH", str(from_environment))
    assert bridge.find_stockfish(str(explicit)) == str(explicit)
    assert bridge.find_stockfish(None) == str(from_environment)
    assert bridge.find_stockfish(str(tmp_path / "missing")) is None  # an argument that points nowhere is not replaced by a guess
    monkeypatch.delenv("STOCKFISH")
    monkeypatch.setenv("PATH", str(tmp_path))
    assert bridge.find_stockfish(None) is None


def test_the_position_command_carries_the_start_and_the_moves():
    assert bridge.position_command({"startFen": START_FEN, "moves": []}) == START_POSITION
    assert bridge.position_command({"startFen": START_FEN, "moves": ["e2e4", "e7e5"]}) == AFTER_E4_E5


# --- the game loop ---------------------------------------------------------------------------------------------------


@needs_shebang
def test_stockfish_plays_white_against_a_scripted_opponent(make_game, fake_engine):
    game, server, client = make_game()
    path, log = fake_engine({"bestmoves": {START_POSITION: "d2d4", f"{START_POSITION} moves d2d4 e7e5": "f1c4"}})
    with UciEngine(path) as engine:  # the opponent takes the first action it is offered
        report = bridge.play_game(client, engine, {"white"}, movetime_ms=33, opponent=lambda actions: actions[0])
    assert game.moves == ["d2d4", "e7e5", "f1c4", "b8c6"]
    assert report.plies == 4 and report.status == "checkmate" and report.winner == "white"
    lines = received(log)
    assert lines.count("ucinewgame") == 1
    assert START_POSITION in lines and "go movetime 33" in lines
    assert len([line for line in lines if line.startswith("go movetime")]) == 2  # only the white plies went to Stockfish


@needs_shebang
def test_the_engine_sees_the_game_so_far_in_every_position(make_game, fake_engine):
    game, server, client = make_game()
    path, log = fake_engine({"bestmoves": {START_POSITION: "e2e4", AFTER_E4_E5: "g1f3"}})
    with UciEngine(path) as engine:
        bridge.play_game(client, engine, {"white"}, opponent=lambda actions: actions[0])
    assert game.moves == ["e2e4", "e7e5", "g1f3", "b8c6"]
    positions = [line for line in received(log) if line.startswith("position")]
    assert positions == [START_POSITION, AFTER_E4_E5]


@needs_shebang
def test_both_sides_can_be_played_by_the_engine(make_game, fake_engine):
    game, server, client = make_game()
    path, log = fake_engine({"bestmoves": {
        START_POSITION: "e2e4",
        f"{START_POSITION} moves e2e4": "e7e5",
        AFTER_E4_E5: "g1f3",
        f"{AFTER_E4_E5} g1f3": "b8c6",
    }})
    with UciEngine(path) as engine:
        report = bridge.play_game(client, engine, {"white", "black"})
    assert game.moves == ["e2e4", "e7e5", "g1f3", "b8c6"]
    assert report.moves == game.moves and report.status == "checkmate"
    assert len([line for line in received(log) if line.startswith("go movetime")]) == 4


@needs_shebang
def test_a_move_the_game_does_not_list_is_a_rules_mismatch(make_game, fake_engine):
    game, server, client = make_game()
    path, _ = fake_engine({"bestmoves": {START_POSITION: "e2e5"}})
    with UciEngine(path) as engine:
        with pytest.raises(RulesMismatch, match="e2e5"):
            bridge.play_game(client, engine, {"white"})
    assert game.moves == []  # it was not even sent


@needs_shebang
def test_no_move_from_stockfish_in_an_ongoing_game_is_an_error(make_game, fake_engine):
    game, server, client = make_game()
    path, _ = fake_engine({"bestmoves": {START_POSITION: None}})
    with UciEngine(path) as engine:
        with pytest.raises(BridgeError, match="no move"):
            bridge.play_game(client, engine, {"white"})


@needs_shebang
def test_a_move_the_game_refuses_is_a_bridge_error(make_game, fake_engine):
    game, server, client = make_game(refuse=True)
    path, _ = fake_engine({"bestmoves": {START_POSITION: "e2e4"}})
    with UciEngine(path) as engine:
        with pytest.raises(BridgeError, match="refused e2e4"):
            bridge.play_game(client, engine, {"white"})


@needs_shebang
def test_the_cross_check_passes_when_the_move_lists_agree(make_game, fake_engine):
    game, server, client = make_game(LEGAL_BY_PLY[:1])
    path, log = fake_engine({"bestmoves": {START_POSITION: "e2e4"}, "perft": {START_POSITION: ["d2d4", "e2e4"]}})
    with UciEngine(path) as engine:
        report = bridge.play_game(client, engine, {"white"}, cross_check=True)
    assert report.plies == 1 and game.moves == ["e2e4"]
    assert "go perft 1" in received(log)


@needs_shebang
def test_the_cross_check_names_the_moves_only_one_side_has(make_game, fake_engine):
    game, server, client = make_game()
    path, _ = fake_engine({"bestmoves": {START_POSITION: "e2e4"}, "perft": {START_POSITION: ["e2e4", "a2a3"]}})
    with UciEngine(path) as engine:
        with pytest.raises(RulesMismatch) as mismatch:
            bridge.play_game(client, engine, {"white"}, cross_check=True)
    assert "only the game has:      ['d2d4']" in str(mismatch.value)
    assert "only Stockfish has:     ['a2a3']" in str(mismatch.value)
    assert game.moves == []


@needs_shebang
def test_the_game_stops_at_max_plies(make_game, fake_engine):
    game, server, client = make_game()
    path, _ = fake_engine({"bestmoves": {START_POSITION: "e2e4"}})
    with UciEngine(path) as engine:
        report = bridge.play_game(client, engine, {"white"}, max_plies=2, opponent=lambda actions: actions[0])
    assert report.plies == 2 and report.reached_max_plies and report.status == "ongoing"


@needs_shebang
def test_the_manual_clock_gets_a_frame_after_each_move_and_the_real_clock_does_not(make_game, fake_engine):
    path, _ = fake_engine({"bestmoves": {START_POSITION: "e2e4"}})
    manual_game, manual_server, manual_client = make_game(LEGAL_BY_PLY[:2], manual_clock=True)
    with UciEngine(path) as engine:
        bridge.play_game(manual_client, engine, {"white"}, opponent=lambda actions: actions[0])
    assert [params for name, params in manual_server.requests if name == "step"] == [{"frames": 1}, {"frames": 1}]

    real_game, real_server, real_client = make_game(LEGAL_BY_PLY[:2], manual_clock=False)
    with UciEngine(path) as engine:
        bridge.play_game(real_client, engine, {"white"}, opponent=lambda actions: actions[0])
    assert not [name for name, _ in real_server.requests if name == "step"]


@needs_shebang
def test_it_waits_for_a_person_to_move_the_other_side(make_game, fake_engine):
    game, server, client = make_game(LEGAL_BY_PLY[:2])
    path, _ = fake_engine({"bestmoves": {START_POSITION: "e2e4"}})

    def the_person_moves():
        time.sleep(0.3)
        with BagiControlClient(port=server_port) as person:
            person.call("perform_action", {"type": "move", "uci": "c7c5"})

    server_port = client.port
    person = threading.Thread(target=the_person_moves)
    person.start()
    with UciEngine(path) as engine:
        report = bridge.play_game(client, engine, {"white"}, poll_seconds=0.02, wait_timeout=10)
    person.join()
    assert game.moves == ["e2e4", "c7c5"]
    assert report.plies == 1  # the person's move is not counted as a move sent by this run
    assert report.status == "checkmate"


@needs_shebang
def test_waiting_for_a_person_gives_up_after_the_timeout(make_game, fake_engine):
    game, server, client = make_game()
    path, _ = fake_engine({"bestmoves": {START_POSITION: "e2e4"}})
    with UciEngine(path) as engine:
        with pytest.raises(BridgeError, match="did not move"):
            bridge.play_game(client, engine, {"white"}, poll_seconds=0.02, wait_timeout=0.3)
    assert game.moves == ["e2e4"]


# --- the command line ------------------------------------------------------------------------------------------------


def test_without_stockfish_the_command_line_skips(capsys, tmp_path, monkeypatch):
    monkeypatch.delenv("STOCKFISH", raising=False)
    monkeypatch.setenv("PATH", str(tmp_path))
    assert bridge.main(["--port", "1"]) == bridge.EXIT_SKIPPED
    assert "Stockfish is not installed" in capsys.readouterr().err


@needs_shebang
def test_the_command_line_plays_a_game(make_game, fake_engine, capsys):
    game, server, client = make_game()
    path, _ = fake_engine({"bestmoves": {START_POSITION: "e2e4"}})
    code = bridge.main(["--stockfish", path, "--port", str(client.port), "--side", "white", "--opponent", "random", "--seed", "1", "--max-plies", "2", "--movetime", "10", "--option", "Skill Level=0"])
    output = capsys.readouterr()
    assert code == bridge.EXIT_PASSED, output.err
    assert "result:" in output.out and "stopped at --max-plies" in output.out
    assert game.moves[0] == "e2e4" and len(game.moves) == 2


@needs_shebang
def test_the_command_line_fails_on_a_rules_mismatch(make_game, fake_engine, capsys):
    game, server, client = make_game()
    path, _ = fake_engine({"bestmoves": {START_POSITION: "e2e5"}})
    code = bridge.main(["--stockfish", path, "--port", str(client.port), "--side", "white"])
    assert code == bridge.EXIT_FAILED
    assert "e2e5" in capsys.readouterr().err


def test_a_malformed_option_fails(capsys, tmp_path):
    engine = tmp_path / "engine"
    engine.write_text("")
    assert bridge.main(["--stockfish", str(engine), "--option", "Skill Level"]) == bridge.EXIT_FAILED
    assert "NAME=VALUE" in capsys.readouterr().err


# --- real games: need Stockfish and a built BagiChess ----------------------------------------------------------------

MATE_IN_ONE_FEN = "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"  # Ra8#


def find_chess_exe() -> Path | None:
    if os.environ.get("BAGI_CHESS_EXE"):
        return Path(os.environ["BAGI_CHESS_EXE"])
    found = sorted(glob.glob(str(REPO_ROOT / "build" / "*" / "BagiChess")) + glob.glob(str(REPO_ROOT / "build" / "*" / "BagiChess.exe")))
    return Path(found[0]) if found else None


@pytest.fixture
def real_game():
    """BagiChess headless on the manual clock: gives the client. Skips without Stockfish or the executable."""
    if bridge.find_stockfish(None) is None:
        pytest.skip("Stockfish is not installed (put it on PATH or set STOCKFISH)")
    exe = find_chess_exe()
    if exe is None or not exe.is_file():
        pytest.skip("BagiChess is not built (set BAGI_CHESS_EXE or build into build/<preset>/)")
    exe = exe.resolve()
    port = free_port()
    with GameProcess(build_game_command(exe, port), cwd=exe.parent) as process:
        client = process.connect(port)
        try:
            yield client
        finally:
            try:
                client.call("quit")
            except Exception:
                pass
            client.close()


def test_stockfish_plays_both_sides_and_agrees_on_every_position(real_game):
    stockfish = bridge.find_stockfish(None)
    with UciEngine(stockfish, {"Skill Level": "3"}) as engine:
        report = bridge.play_game(real_game, engine, {"white", "black"}, movetime_ms=20, max_plies=60, cross_check=True)
    # Every move Stockfish chose was accepted by the game (a refusal raises), and every position had the same legal moves
    assert report.plies > 0 and len(report.moves) == report.plies
    assert report.reached_max_plies or report.status != "ongoing"


def test_stockfish_beats_random_moves_without_a_disagreement(real_game):
    import random

    stockfish = bridge.find_stockfish(None)
    rng = random.Random(7)
    with UciEngine(stockfish, {"Skill Level": "3"}) as engine:
        report = bridge.play_game(real_game, engine, {"white"}, movetime_ms=20, max_plies=120, cross_check=True, opponent=lambda actions: rng.choice(actions))
    assert report.plies > 0
    assert report.reached_max_plies or report.status != "ongoing"


def test_stockfish_gives_mate_in_one_and_the_game_ends(real_game):
    real_game.call("set_state", {"fen": MATE_IN_ONE_FEN})
    stockfish = bridge.find_stockfish(None)
    with UciEngine(stockfish) as engine:
        report = bridge.play_game(real_game, engine, {"white"}, movetime_ms=50, cross_check=True)
    assert report.moves == ["a1a8"]
    assert report.status == "checkmate" and report.winner == "white"
