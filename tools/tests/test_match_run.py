"""The match runner (tools/uci_match.py): the game loop, a pair and a match against a scripted game and fake engines, the option and
range parsers, the command line. Standard library only, no Stockfish and no BagiChess needed.

    python -m unittest discover -s tools/tests -p "test_match_*.py"

The referee is a `FakeControlServer` that answers from a script (the pattern of test_stockfish.py, copied small); the engines are two
processes of `fake_uci_engine.py`, each with its own table of answers.
"""

import contextlib
import io
import json
import os
import sys
import tempfile
import threading
import unittest
from pathlib import Path

TESTS_DIR = Path(__file__).resolve().parent
TOOLS_DIR = TESTS_DIR.parent
ENGINE_CONTROL_DIR = TOOLS_DIR.parent / "engine" / "tools" / "control"
for _directory in (TOOLS_DIR, ENGINE_CONTROL_DIR, ENGINE_CONTROL_DIR / "tests"):
    sys.path.insert(0, str(_directory))

import uci_match
from bagi_control_client import BagiControlClient
from fake_control_server import FakeControlServer, FakeError
from match_steps import PairRecord, parse_own_step
from stockfish_bridge import UciEngine
from uci_match import MatchConfig, MatchError

FAKE_ENGINE = TESTS_DIR / "fake_uci_engine.py"
START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
START_POSITION = f"position fen {START_FEN}"
AFTER_OPENING = f"{START_POSITION} moves e2e4 e7e5"
AFTER_KNIGHT = f"{AFTER_OPENING} g1f3"
OPENING = ("e2e4", "e7e5")
OPENINGS = (OPENING,)

# The legal moves of each ply of the scripted game (not real chess: only the first two moves are)
LEGAL_BY_PLY = [["e2e4", "d2d4"], ["e7e5", "c7c5"], ["g1f3", "f1c4"], ["b8c6", "g8f6"]]
# What both engines say: g1f3 and then b8c6, whoever has the move; the game then ends
LINE = {AFTER_OPENING: "g1f3", AFTER_KNIGHT: "b8c6"}


class ScriptedGame:
    """The handler of a FakeControlServer that plays a game on a script: the legal moves of every ply are given."""

    def __init__(self, legal_by_ply: list[list[str]], end_status: str = "checkmate", winner: str | None = "white", manual_clock: bool = True):
        self.legal_by_ply = legal_by_ply
        self.end_status = end_status
        self.winner = winner
        self.manual_clock = manual_clock
        self.moves: list[str] = []
        self.lock = threading.Lock()

    def _legal(self) -> list[str]:
        return self.legal_by_ply[len(self.moves)] if len(self.moves) < len(self.legal_by_ply) else []

    def __call__(self, method: str, params: dict | None):
        with self.lock:
            if method == "status":
                return {"frame": 0, "manualClock": self.manual_clock}
            if method == "reset":
                self.moves = []
                return {"game": "Chess", "version": 1}
            if method == "get_state":
                over = len(self.moves) >= len(self.legal_by_ply)
                state = {
                    "fen": f"scripted-{len(self.moves)}",
                    "startFen": START_FEN,
                    "moves": list(self.moves),
                    "turn": "white" if len(self.moves) % 2 == 0 else "black",
                    "status": self.end_status if over else "ongoing",
                    "winner": self.winner if over else None,
                }
                return {"game": "Chess", "version": len(self.moves) + 1, "state": state}
            if method == "list_actions":
                return {"game": "Chess", "version": len(self.moves) + 1, "actions": [{"type": "move", "uci": uci} for uci in self._legal()]}
            if method == "perform_action":
                uci = (params or {}).get("uci")
                if uci not in self._legal():
                    raise FakeError("illegal_action", f"'{uci}' is not legal")
                self.moves.append(uci)
                return {"game": "Chess", "version": len(self.moves) + 1, "outcome": {"move": uci}}
            if method == "step":
                return {"frame": 1}
            raise FakeError("unknown_method", method)


class RunnerTestCase(unittest.TestCase):
    """Starts scripted games and fake engines and stops them all afterwards."""

    def setUp(self):
        self._directory = tempfile.TemporaryDirectory()
        self.addCleanup(self._directory.cleanup)
        self.directory = Path(self._directory.name)

    def start_game(self, **options) -> tuple[ScriptedGame, FakeControlServer, BagiControlClient]:
        game = ScriptedGame(LEGAL_BY_PLY, **options)
        server = FakeControlServer(game)
        client = BagiControlClient(port=server.start())
        client.connect()
        self.addCleanup(server.stop)
        self.addCleanup(client.close)
        return game, server, client

    def start_engine(self, name: str, bestmoves: dict | None = None) -> tuple[UciEngine, Path]:
        """A fake engine with its own table of answers; gives it and its log. The config is read when the process starts."""
        log = self.directory / f"{name}.log"
        config = self.directory / f"{name}.json"
        config.write_text(json.dumps({"log": str(log), "bestmoves": LINE if bestmoves is None else bestmoves}))
        previous = os.environ.get("FAKE_UCI_CONFIG")
        os.environ["FAKE_UCI_CONFIG"] = str(config)
        try:
            engine = UciEngine([sys.executable, str(FAKE_ENGINE)])
        finally:
            if previous is None:
                del os.environ["FAKE_UCI_CONFIG"]
            else:
                os.environ["FAKE_UCI_CONFIG"] = previous
        self.addCleanup(engine.close)
        return engine, log

    @staticmethod
    def config(**changes) -> MatchConfig:
        return MatchConfig(openings=OPENINGS, opponent_movetime_ms=10, **changes)


class GameLoopTest(RunnerTestCase):
    def test_the_own_engine_with_white_wins_when_white_mates(self):
        _, _, client = self.start_game()
        own, own_log = self.start_engine("own")
        opponent, opponent_log = self.start_engine("opponent")
        record = uci_match.play_game(client, own, opponent, "depth 3", True, OPENING, self.config())
        self.assertEqual(record.result, "win")
        self.assertEqual(record.own_color, "white")
        self.assertEqual(record.termination, "checkmate")
        self.assertEqual(record.plies, 4)
        self.assertEqual(record.moves, ["e2e4", "e7e5", "g1f3", "b8c6"])
        self.assertEqual(record.opening, OPENING)
        self.assertIn("go depth 3", own_log.read_text().splitlines())
        self.assertIn("go movetime 10", opponent_log.read_text().splitlines())

    def test_the_result_is_from_the_own_side_for_black(self):
        _, _, client = self.start_game()
        own, _ = self.start_engine("own")
        opponent, _ = self.start_engine("opponent")
        record = uci_match.play_game(client, own, opponent, "depth 3", False, OPENING, self.config())
        self.assertEqual((record.result, record.own_color), ("loss", "black"))

    def test_a_draw_status_and_a_stalemate_are_draws(self):
        for status in ("stalemate", "draw_threefold", "draw_fifty_moves", "draw_insufficient_material"):
            with self.subTest(status=status):
                _, _, client = self.start_game(end_status=status, winner=None)
                own, _ = self.start_engine("own")
                opponent, _ = self.start_engine("opponent")
                record = uci_match.play_game(client, own, opponent, "", True, OPENING, self.config())
                self.assertEqual((record.result, record.termination), ("draw", status))

    def test_an_unknown_status_or_a_mate_without_a_winner_is_an_error(self):
        for status, winner in (("exploded", None), ("checkmate", None)):
            with self.subTest(status=status):
                _, _, client = self.start_game(end_status=status, winner=winner)
                own, _ = self.start_engine("own")
                opponent, _ = self.start_engine("opponent")
                with self.assertRaises(MatchError):
                    uci_match.play_game(client, own, opponent, "", True, OPENING, self.config())

    def test_the_cap_makes_a_draw(self):
        game, _, client = self.start_game()
        own, _ = self.start_engine("own")
        opponent, _ = self.start_engine("opponent")
        record = uci_match.play_game(client, own, opponent, "", True, OPENING, self.config(max_plies=3))
        self.assertEqual((record.result, record.termination, record.plies), ("draw", "max_plies", 3))
        self.assertEqual(record.moves, ["e2e4", "e7e5", "g1f3"])

    def test_a_game_that_ends_on_the_last_ply_is_not_cut_by_the_cap(self):
        _, _, client = self.start_game()
        own, _ = self.start_engine("own")
        opponent, _ = self.start_engine("opponent")
        record = uci_match.play_game(client, own, opponent, "", True, OPENING, self.config(max_plies=4))
        self.assertEqual(record.termination, "checkmate")

    def test_a_move_that_is_not_listed_is_a_match_error(self):
        _, _, client = self.start_game()
        own, _ = self.start_engine("own", {AFTER_OPENING: "a1a8"})
        opponent, _ = self.start_engine("opponent")
        with self.assertRaisesRegex(MatchError, "a1a8.*not among"):
            uci_match.play_game(client, own, opponent, "", True, OPENING, self.config())

    def test_no_move_while_the_game_lists_moves_is_a_match_error(self):
        _, _, client = self.start_game()
        own, _ = self.start_engine("own")
        opponent, _ = self.start_engine("opponent", {AFTER_KNIGHT: None})
        with self.assertRaisesRegex(MatchError, "no move"):
            uci_match.play_game(client, own, opponent, "", True, OPENING, self.config())

    def test_the_opening_goes_first_and_the_engines_are_asked_after_it(self):
        _, server, client = self.start_game()
        own, own_log = self.start_engine("own")
        opponent, opponent_log = self.start_engine("opponent")
        uci_match.play_game(client, own, opponent, "", True, OPENING, self.config())

        performed = [params["uci"] for method, params in server.requests if method == "perform_action"]
        self.assertEqual(performed, ["e2e4", "e7e5", "g1f3", "b8c6"])
        methods = [method for method, _ in server.requests if method != "step"]
        self.assertEqual(methods[:3], ["status", "reset", "perform_action"])  # the opening is sent before the first get_state
        self.assertEqual(methods[:5].count("perform_action"), 2)
        for log in (own_log, opponent_log):
            positions = [line for line in log.read_text().splitlines() if line.startswith("position")]
            self.assertTrue(positions[0].startswith(AFTER_OPENING), "the first position an engine sees already holds the opening")

    def test_a_step_follows_each_move_on_the_manual_clock_only(self):
        for manual_clock, expected in ((True, 4), (False, 0)):
            with self.subTest(manual_clock=manual_clock):
                _, server, client = self.start_game(manual_clock=manual_clock)
                own, _ = self.start_engine("own")
                opponent, _ = self.start_engine("opponent")
                uci_match.play_game(client, own, opponent, "", True, OPENING, self.config())
                self.assertEqual(sum(1 for method, _ in server.requests if method == "step"), expected)

    def test_the_opening_the_game_refuses_is_a_match_error(self):
        _, _, client = self.start_game()
        with self.assertRaisesRegex(MatchError, "a2a3"):
            uci_match.play_opening(client, ("e2e4", "a2a3"))


class PairAndMatchTest(RunnerTestCase):
    def test_a_pair_of_two_games_plays_both_colors_and_adds_up(self):
        _, _, client = self.start_game()
        own, own_log = self.start_engine("own")
        opponent, opponent_log = self.start_engine("opponent")
        pair = PairRecord("depth=2", 1500)
        uci_match.play_pair(client, own, opponent, parse_own_step("depth=2,blunder=15"), 1500, self.config(games=2), pair)

        self.assertEqual([game.own_color for game in pair.games], ["white", "black"])
        self.assertEqual([game.result for game in pair.games], ["win", "loss"])  # white mates in the script
        self.assertEqual((pair.wins, pair.draws, pair.losses), (1, 0, 1))
        self.assertEqual(pair.scores(), [1.0, 0.0])
        own_lines = own_log.read_text().splitlines()
        self.assertIn("setoption name Depth value 2", own_lines)
        self.assertIn("setoption name BlunderPercent value 15", own_lines)
        self.assertIn("go depth 2", own_lines)
        opponent_lines = opponent_log.read_text().splitlines()
        self.assertLess(opponent_lines.index("setoption name UCI_LimitStrength value true"), opponent_lines.index("setoption name UCI_Elo value 1500"))
        self.assertEqual(opponent_lines.count("ucinewgame"), 2)

    def test_a_failed_game_keeps_the_games_before_it(self):
        _, _, client = self.start_game()
        own, _ = self.start_engine("own")
        opponent, _ = self.start_engine("opponent")
        pair = PairRecord("depth=1", 1500)
        # the own engine is fine in games 0 and 1 (one search each) and plays an unlisted move in game 2
        calls = {"count": 0}
        original = own.search

        def search(position, go_arguments, timeout):
            calls["count"] += 1
            return "a1a8" if calls["count"] > 2 else original(position, go_arguments, timeout)

        own.search = search
        with self.assertRaises(MatchError):
            uci_match.play_pair(client, own, opponent, parse_own_step("depth=1"), 1500, self.config(games=4), pair)
        self.assertEqual(len(pair.games), 2)

    def test_the_match_calls_back_once_per_pair_and_the_record_is_valid_json(self):
        _, _, client = self.start_game()
        own, _ = self.start_engine("own")
        opponent, _ = self.start_engine("opponent")
        steps = [parse_own_step("depth=1"), parse_own_step("depth=2")]
        pairs: list[PairRecord] = []
        out = self.directory / "match.json"
        seen = []

        def finished(pair: PairRecord) -> None:
            seen.append((pair.own_label, pair.opponent_elo, len(pair.games)))
            uci_match.write_record(out, {"games_per_pair": 2}, pairs)
            self.assertEqual(len(json.loads(out.read_text())["pairs"]), len(pairs))  # the file holds every finished pair at once

        uci_match.play_match(client, own, opponent, steps, [1500, 1600], self.config(games=2), pairs, finished)

        self.assertEqual(seen, [("depth=1", 1500, 2), ("depth=1", 1600, 2), ("depth=2", 1500, 2), ("depth=2", 1600, 2)])
        record = json.loads(out.read_text())
        self.assertEqual(len(record["pairs"]), 4)
        self.assertEqual(sorted(record["summary"]), ["depth=1", "depth=2"])
        self.assertIn("elo", record["summary"]["depth=1"])
        self.assertEqual(record["settings"], {"games_per_pair": 2})
        self.assertFalse((self.directory / "match.json.tmp").exists())

    def test_verify_openings_reports_ok_and_the_line_the_game_refuses(self):
        _, _, client = self.start_game()
        out = io.StringIO()
        self.assertFalse(uci_match.verify_openings(client, (OPENING, ("e2e4", "a2a3")), out))
        text = out.getvalue()
        self.assertIn("ok  e2e4 e7e5", text)
        self.assertIn("refused", text)
        self.assertIn("a2a3", text)
        out = io.StringIO()
        self.assertTrue(uci_match.verify_openings(client, (OPENING,), out))


class ParserTest(unittest.TestCase):
    OPTION = "option name UCI_Elo type spin default 1320 min 1320 max 3190"

    def test_the_elo_range_is_read_from_the_option_line(self):
        lines = ["id name Stockfish 17", "option name Threads type spin default 1 min 1 max 1024", self.OPTION, "uciok"]
        self.assertEqual(uci_match.parse_elo_range(lines, "UCI_Elo"), (1320, 3190))
        self.assertEqual(uci_match.parse_elo_range(lines, "uci_elo"), (1320, 3190))
        self.assertEqual(uci_match.parse_elo_range([self.OPTION], "Depth"), None)
        self.assertEqual(uci_match.parse_elo_range(["option name Depth type spin default 0 min 0 max 64"], "Depth"), (0, 64))
        self.assertIsNone(uci_match.parse_elo_range([], "UCI_Elo"))

    def test_the_engine_name_is_the_id_name_line(self):
        self.assertEqual(uci_match.engine_name(["id name Stockfish 17", "id author x"]), "Stockfish 17")
        self.assertEqual(uci_match.engine_name(["uciok"]), "")

    def test_a_step_outside_the_range_is_refused(self):
        uci_match.check_elo_steps([1320, 3190], (1320, 3190), "UCI_Elo")
        uci_match.check_elo_steps([100, 9000], None, "UCI_Elo")  # no range known: nothing to check against
        with self.assertRaisesRegex(ValueError, r"\[1200\].*1320\.\.3190"):
            uci_match.check_elo_steps([1200, 1500], (1320, 3190), "UCI_Elo")

    def test_two_own_steps_with_the_same_options_are_refused(self):
        self.assertEqual([step.label for step in uci_match.parse_own_steps(["easy", "depth=2,blunder=15"])], ["easy", "depth=2,blunder=15"])
        with self.assertRaisesRegex(ValueError, "same"):
            uci_match.parse_own_steps(["depth=2,blunder=15", "blunder=15,depth=2"])
        with self.assertRaisesRegex(ValueError, "same"):
            uci_match.parse_own_steps(["hard", "HARD"])
        with self.assertRaises(ValueError):
            uci_match.parse_own_steps(["fast"])

    def test_a_step_with_no_depth_is_refused(self):
        from match_steps import OwnStep

        with self.assertRaisesRegex(ValueError, "no depth"):
            uci_match.check_own_step(OwnStep("custom", {"BlunderPercent": "10"}, None))
        uci_match.check_own_step(OwnStep("custom", {"Depth": "3"}, None))
        uci_match.check_own_step(parse_own_step("max"))


class UciEngineTest(RunnerTestCase):
    def test_a_command_given_as_a_list_is_started_and_its_uci_reply_kept(self):
        engine, _ = self.start_engine("one")
        self.assertIn("id name FakeFish", engine.uci_lines)
        self.assertEqual(uci_match.engine_name(engine.uci_lines), "FakeFish")
        self.assertEqual(uci_match.parse_elo_range(engine.uci_lines, "UCI_Elo"), None)

    def test_search_takes_any_go_arguments_and_best_move_still_uses_movetime(self):
        engine, log = self.start_engine("one")
        self.assertEqual(engine.search(AFTER_OPENING, "depth 4", 5.0), "g1f3")
        self.assertEqual(engine.best_move(AFTER_KNIGHT, 25), "b8c6")
        self.assertEqual(engine.set_option("Skill Level", "3"), [])
        lines = log.read_text().splitlines()
        self.assertEqual(lines[lines.index(AFTER_OPENING) + 1], "go depth 4")
        self.assertEqual(lines[lines.index(AFTER_KNIGHT) + 1], "go movetime 25")
        self.assertIn("setoption name Skill Level value 3", lines)

    def test_search_without_arguments_sends_a_bare_go(self):
        engine, log = self.start_engine("one")
        engine.search(AFTER_OPENING, "", 5.0)
        self.assertIn("go", log.read_text().splitlines())

    def test_no_move_is_none(self):
        engine, _ = self.start_engine("one", {AFTER_OPENING: None})
        self.assertIsNone(engine.search(AFTER_OPENING, "depth 1", 5.0))


class CommandLineTest(RunnerTestCase):
    def run_main(self, *arguments: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = uci_match.main(list(arguments))
        return code, out.getvalue(), err.getvalue()

    def test_an_odd_number_of_games_exits_1(self):
        code, _, err = self.run_main("--games", "3", "--verify-openings")
        self.assertEqual(code, 1)
        self.assertIn("even", err)

    def test_a_bad_step_exits_1_with_a_message(self):
        code, _, err = self.run_main("--own-step", "fast", "--verify-openings")
        self.assertEqual(code, 1)
        self.assertIn("fast", err)

    def test_a_bad_elo_and_a_bad_option_exit_1(self):
        self.assertEqual(self.run_main("--opponent-elo", "x", "--verify-openings")[0], 1)
        self.assertEqual(self.run_main("--opponent-option", "novalue", "--verify-openings")[0], 1)
        self.assertEqual(self.run_main("--max-plies", "0", "--verify-openings")[0], 1)

    def test_a_missing_opponent_engine_exits_77(self):
        own = self.directory / "own"
        own.write_text("")
        code, _, err = self.run_main("--own", str(own), "--opponent", str(self.directory / "no-such-engine"))
        self.assertEqual(code, 77)
        self.assertIn("no opponent engine", err)

    def test_a_missing_own_engine_or_referee_exits_1(self):
        opponent = self.directory / "opponent"
        opponent.write_text("")
        self.assertEqual(self.run_main("--opponent", str(opponent), "--own", str(self.directory / "missing"))[0], 1)
        self.assertEqual(self.run_main("--verify-openings", "--referee", str(self.directory / "missing"))[0], 1)

    def test_verify_openings_over_a_running_game_exits_1_on_the_line_it_refuses(self):
        # the scripted game takes e2e4 e7e5 g1f3 b8c6 and g8f6 but not the third line of the table (its fourth ply)
        game = ScriptedGame(LEGAL_BY_PLY)
        server = FakeControlServer(game)
        port = server.start()
        self.addCleanup(server.stop)
        code, out, _ = self.run_main("--verify-openings", "--port", str(port))
        self.assertEqual(code, 1)
        self.assertEqual(out.count("ok  "), 2)
        self.assertIn("refused", out)


if __name__ == "__main__":
    unittest.main()
