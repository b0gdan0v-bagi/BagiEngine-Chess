# BagiEngine-Chess

Chess on the [BagiEngine](https://github.com/b0gdan0v-bagi/BagiEngine): rules as plain C++ (bitboards, perft-tested), a scene with an engine-drawn HUD, a control API for tests and for playing against Stockfish. The engine is the git submodule `engine/`, on the permanent branch `games/chess` of the engine repository.

## Get it

```bash
git clone --recurse-submodules <url> BagiEngine-Chess
cd BagiEngine-Chess
CI/setup_venv.sh          # Windows: CI\setup_venv.bat or .ps1; creates engine/.venv, needs uv
```

An existing clone: `git submodule update --init --recursive`.

## Build and test

The presets are the engine's (`CMakePresets.json` includes `engine/CMakePresets.json`):

```bash
export PATH="$PWD/engine/.venv/bin:$PATH"     # the reflection generator needs jinja2
cmake --preset macos-debug                    # list: cmake --list-presets
cmake --build --preset macos-debug
ctest --preset macos-debug                    # ChessTests and BagiChessControlSmoke
build/ClangmacOS/ChessTests --gtest_filter='Stockfish*:UciEngineStockfishTest*'   # against a real Stockfish (STOCKFISH or PATH); skipped without one
build/ClangmacOS/BagiChess                    # the game; the executable folder depends on compiler and OS
```

## Playing against the computer

The window starts with two people on one screen. To play the computer press **Opponent** (top right, beside Reset; its text says what is picked now: "Two players", "You: White" or "You: Black"). A panel opens with four choices, each showing the current one: **Opponent** (Two players or Computer), **Play as** (White or Black: the color you play, the computer plays the other one), **Engine** (Stockfish or Own engine) and **Level** (Easy, Medium, Hard or Max). Every press takes effect at once, the panel stays open, and **OK** closes it. Play as and Level can be picked while two people play too: the choice is kept and applies when you pick Computer. If the engine you pick cannot be found (Stockfish is not installed), nothing changes: what played before keeps playing and the panel and the bottom line say "Engine not found"; pick **Own engine** to play without Stockfish. "Computer is thinking" shows while it searches, and a failure ("Engine not found", "Engine did not answer", ...) shows at the bottom and in the panel, and the game waits, until you press **Reset** (a new game; it keeps the opponent) or pick something in the panel. To leave a broken opponent behind for good, pick Two players. The panel does not close by itself, not when a game ends either: press OK. The board does not turn yet: with "You: Black" White stays at the bottom.

The game starts with "Stockfish" picked and never switches the engine for you. What **Level** means depends on the engine: for Stockfish it is the `Skill Level` and the time per move (from a tenth of a second at Easy to two seconds at Max); for the own engine it is how many moves deep it looks (1, 3, 4 and 5 plies), so the same position always gets the same answer, and a position that is very hard to search is cut after 10 seconds.

**Own engine** needs nothing: it runs inside the game, on every target the game builds for, and does not play like a master. **Stockfish** is [Stockfish](https://stockfishchess.org/), which you install yourself: it is not shipped with the game, not linked into it and none of its code is in this repository. The game uses the first of these three that is **set**:

1. `stockfishPath` in `config/ChessSettings.local.xml` (git ignores it; the committed `config/ChessSettings.xml` carries no path): `<ChessSettings stockfishPath="/full/path/to/stockfish"/>`. Use an absolute path; `~` is not expanded.
2. the environment variable `STOCKFISH`.
3. `stockfish` on `PATH`.

A value that is set but names no file is not skipped: a wrong `stockfishPath` does not fall through to `$STOCKFISH` or `PATH`, and a wrong `$STOCKFISH` does not fall through to `PATH`. The game then says "Engine not found" and the log explains: for a wrong `stockfishPath` it names the path it tried, for a wrong `$STOCKFISH` it says the variable must name a file. Fix the value or remove it.

With none of the three the game still runs as two people, and picking Computer with "Stockfish" says "Engine not found" and changes nothing else (the panel stays on Two players). The way out is the **Engine** row of the panel: pick "Own engine" (that clears the message), then Computer again. The game starts Stockfish as a child process, which is only written for macOS and Linux so far (macOS is what it is tested on); on Windows Stockfish cannot play yet (the status line says "Engine not supported here"), but the own engine can. The choice is not remembered between runs.

## Layout

| Path | What |
|------|------|
| `src/Chess/` | the game: rules (`Game/`), scene components, the control adapter. `CLAUDE.md` describes it |
| `config/`, `assets/` | scene, texture library, application config, piece atlas |
| `tests/` | gtest cases and `control_smoke.py` |
| `tools/` | the Stockfish bridge and its pytest tests (`engine/.venv/bin/python -m pytest tools/tests`) |
| `todo/`, `TODO.md` | known problems, worked by the `todo-loop` skill (`todo/README.md`) |
| `CI/` | environment setup (`setup_venv.*`) and the VS Code workspace `BagiChess.code-workspace` (CMake Tools on the engine's presets, CodeLLDB / MSVC launch, build tasks) |
| `engine/` | the engine (submodule); not edited from here |

## The engine

A change the game needs from the engine is made in the engine's own checkout, on `games/chess`, and then the pointer here is moved in a commit of its own: `git submodule update --remote engine`. Details: `.claude/rules/engine-and-todo.md`.
