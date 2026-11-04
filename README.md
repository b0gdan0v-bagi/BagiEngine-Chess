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
build/ClangmacOS/BagiChess                    # the game; the executable folder depends on compiler and OS
```

## Playing against the computer

The window starts with two people on one screen. Two buttons in the margins of the board pick the opponent: **Side** (bottom left) goes round "Two players", "You: White" (the computer plays Black) and "You: Black" (the computer plays White), and **Level** (top right, beside Reset, shown only while the computer plays) goes round Easy, Medium, Hard and Max. "Computer is thinking" shows while it searches, and a failure ("Engine not found", "Engine did not answer", ...) shows at the bottom, and the game waits, until you press **Reset** (a new game; it keeps the opponent) or change the side or the level. To leave a broken opponent behind for good, press **Side** until it says "Two players". The board does not turn yet: with "You: Black" White stays at the bottom.

The computer is [Stockfish](https://stockfishchess.org/), which you install yourself: it is not shipped with the game, not linked into it and none of its code is in this repository. The game uses the first of these three that is **set**:

1. `stockfishPath` in `config/ChessSettings.local.xml` (git ignores it; the committed `config/ChessSettings.xml` carries no path): `<ChessSettings stockfishPath="/full/path/to/stockfish"/>`. Use an absolute path; `~` is not expanded.
2. the environment variable `STOCKFISH`.
3. `stockfish` on `PATH`.

A value that is set but names no file is not skipped: a wrong `stockfishPath` does not fall through to `$STOCKFISH` or `PATH`, and a wrong `$STOCKFISH` does not fall through to `PATH`. The game then says "Engine not found" and the log explains: for a wrong `stockfishPath` it names the path it tried, for a wrong `$STOCKFISH` it says the variable must name a file. Fix the value or remove it.

With none of the three the game still runs as two people: picking a side says "Engine not found" and changes nothing else. The game starts Stockfish as a child process, which is only written for macOS and Linux so far (macOS is what it is tested on); on Windows the computer cannot play yet (the status line says "Engine not supported here").

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
