# Chess tests

`tests/`, executable `ChessTests`; style and rules: `engine/.claude/rules/tests-module.md`. New rule code goes into `Game/` with a gtest case. **No table of test files here:** a file's name and its header comment say what it covers; this file holds only what the code does not say (the helpers, the fakes, the smokes, the build variants). A new test file adds nothing here unless it brings a new helper or a new rule.

**Where a test goes:** the rules (`Game/`) are pure gtests, `PerftTest` is the proof of the move generator; the engine side (`Engine/`) has a pure test per file plus `UciProcessTest` / `UciEngineTest` against the fake engine and `EngineOpponentTest`, `OpponentControllerTest`, `OpponentSeatsTest`, `OpponentChoiceTest`, `PositionAnalystTest`, `MoveRaterTest` with the real `TaskManager` and doubles; `Stockfish*Test` run a real Stockfish and skip without one; the scene is `ChessUiTest` (the shipped `SceneConfig.xml` started through `GameManager`: the layout validator on the size matrix, real clicks, broken copies of the XML), `ChessPieceAnimationTest` and `ChessHudWebTest` (the HTML + CSS twin of the HUD and the panel; the web side follows the XML, never a looser tolerance); the control adapter is `ChessGameControlTest`; content and packaging are `ChessContentVersionTest` and `ChessWebConfigTest`.

## Helpers and rules

- `ChessTestUtil.h`: `Sq`, `Mv`, `PositionFromFen`, the perft FENs, `MirrorFen`, `PumpTaskFrame()`, `SKIP_WITHOUT_WORKER_THREADS()`. `SeededRandom.h`: a seeded xorshift64. `GamePlay.h`: whole games through a model (`EnginePlayer`, `RandomPlayer`, `PlayGame`). `HudWords.h`: `AllHudWords()`, `Rounds()` / `Publish(round)` for the layout tests.
- `StockfishUtil.h`: `StockfishPath()`, `StockfishSettings(path)`, `Describe(error)`, `SKIP_WITHOUT_STOCKFISH(path)` (declares `path` or skips; `STOCKFISH=/path/to/stockfish`).
- `TestEngines.h`, the engine doubles: `InstantEngine`, `MatingEngine`, `GatedEngine` (holds the answer until `Open()`), `FailingEngine`, `CountingFactory`, `AnalysingEngine` (scripted lines, `Gate`, `FailWith`), `CountingAnalysisFactory`, `WaitUntil` (polls with a time limit; never a fixed sleep).
- **The level matters** in `OpponentControllerTest`: the default factory wraps `easy` and `medium` in the handicap, which may answer without asking the fake, so a test that expects the fake's scripted reply uses `hard` or `max`.
- `ChessUiTest` needs `ChessModule` linked whole (`WHOLE_ARCHIVE` in `CMakeLists.txt`): the scene names components no test references. Not covered by any test (check by hand in `BagiChess`): the result bar, the promotion sprite, how the highlights look.
- **No test depends on machine load.** ctest runs 8 at a time beside other workers' builds, so a result may never hang on wall-clock time: a wait has a generous give-up limit that only ends a broken test, the thing asserted is an event (a log line, a gate, a state), and a check of speed or of a deadline that must pass (`took < X`, a timeout of a few hundred ms) is a disabled yardstick a person runs by hand. A test that fails only under load is a defect of the test, never a "flake to rerun".
- After a threading change run `UciServerTest` with `--gtest_repeat=20`. The disabled yardsticks (`StockfishGameTest.DISABLED_*Yardstick`, `SearchSliceBenchmark.DISABLED_NodesPerSecond`, the latter in **Release** on a quiet machine) are run by hand with `--gtest_also_run_disabled_tests --gtest_filter=...`; their numbers are an order, not a strength.

## The fake UCI engine

`tests/fake_uci_engine.py` (not `tools/tests/fake_uci_engine.py`, the bridge's) stands in for Stockfish: standard library only, scripted moves (`--moves`) and scores (`--scores`), several lines (`--lines`), and every way to misbehave (hang, die, stay silent, split or garble lines, `--noisy-info`, `--no-score`, refuse to quit); `--log FILE` records what it receives. The C++ side is `FakeEngineUtil.h` (`FakeEngineLaunch({flags})`, `TempDirectory`, `ReadFileLines`, `WaitForLogLine`, `SKIP_WITHOUT_FAKE_ENGINE()`); the interpreter comes as `CHESS_TEST_PYTHON` (empty: skip).
## The smokes and script tests (ctest, not gtest)

- **Scenarios** (`tests/scenarios/*.jsonl`, ctest `BagiChessScenario.<file>`): sessions `engine/tools/control/bagi_play.py` plays on the manual clock, the person's clicks only. A scenario cannot wait, so whatever needs a reply of the program or the analyst, a picture, the real clock or the fake Stockfish stays in the control smoke. Adding one: `engine/tools/control/CLAUDE.md`, "Scenario files".
- **`control_smoke.py`** (`BagiChessControlSmoke`) drives `BagiChess --control --headless` on the real and on the manual clock: the opening, pictures that follow the model, the own engine, the panel, a watch of two programs, analysis, ratings, the material, and an opponent scenario against the fake (a `#!/bin/sh` wrapper in `STOCKFISH`, so skipped on Windows and when `config/ChessSettings.local.xml` exists). Waits are polled (30 s), pictures are compared, the game must exit 0 after `quit`. It detects a `CHESS_NO_STOCKFISH` build by itself; run it there from a desktop build with only `-DCHESS_NO_STOCKFISH=ON`: `python tests/control_smoke.py --exe build/<preset>/bin/BagiChess.exe`.
- **`uci_smoke.py`** (`BagiChessUciSmoke`) talks UCI to `BagiChessUci` over pipes as a tournament tool does; **`match_smoke.py`** (`BagiChessMatchSmoke`) runs `tools/uci_match.py` with `BagiChessUci` on both sides and `BagiChess` as referee (`src/Chess/Uci/CLAUDE.md`). Both exit 77 (skip) when a program is not built.
- **`check_web_package.cmake`** (`BagiChessWebPackageCheck`, `cmake -P`): the package takes exactly the listed files of `config/` and `assets/`; a new file fails it until the list changes on purpose. Whether emcc applies `--exclude-file` needs emsdk.

## Build variants

**`CHESS_NO_STOCKFISH`** (`src/Chess/Engine/CLAUDE.md`): `StockfishUtil.h` and `FakeEngineUtil.h` test `CHESS_HAS_UCI_PROCESS` (not `_WIN32`), so every Stockfish and fake-engine test skips. A test that assumes Stockfish is the default uses `kDefaultOpponentEngine` / `kStockfishAvailable`; `ChessUiTest` cases that click `EngineStockfish` skip with `SKIP_WITHOUT_STOCKFISH_SEGMENT()`.

**`ENGINE_SINGLE_THREAD`** (the browser's inline task pool): a test that needs a worker beside the test thread (a gate a worker holds, a wait for `IsThinking()`, a search cancelled from outside) **must** begin with `SKIP_WITHOUT_WORKER_THREADS()`; without it the inline pool runs the engine inside the first `Update`, the double gives up (`kGateGiveUp`) and the test passes on a `Timeout` without testing anything. A test that runs no frame and waits for a search calls `PumpTaskFrame()`. A test near 10 s in this build misses the macro. The check on the desktop (Windows through `vs_env.py`; `BagiChess` does not start there, so smokes are left out):

```bash
python engine/CI/scripts/vs_env.py cmake --preset windows-clang-debug -B build/webcheck -DENGINE_WITH_VULKAN=OFF -DENGINE_SINGLE_THREAD=ON -DENGINE_STATIC_SDL=ON -DCHESS_NO_STOCKFISH=ON
python engine/CI/scripts/vs_env.py cmake --build build/webcheck --target ChessTests
ctest --test-dir build/webcheck -E "Smoke|Scenario|ToolTests" -j8 --timeout 60
```

Each gtest is a ctest test of its own there (`-R ChessTests` finds none); remove `build/webcheck` when done.
