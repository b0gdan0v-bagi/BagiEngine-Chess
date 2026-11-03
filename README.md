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
