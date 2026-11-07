# BagiEngine-Chess

Chess on the [BagiEngine](https://github.com/b0gdan0v-bagi/BagiEngine): rules as plain C++ (bitboards, perft-tested), a scene with an engine-drawn HUD, a control API for tests and for playing against Stockfish. The engine is the git submodule `engine/`, on the permanent branch `games/chess` of the engine repository.

## Build and test

From a clean `git clone` to a running game and a green test run. The commands are for macOS (what the game is built and tested on); Windows is in "Windows" below, the editor in "VS Code". The build facts that the agents read (targets, when to re-run configure) are in `.claude/rules/build-project.md`.

### What you need

| Need | Why, and what the repository says |
|------|-----------------------------------|
| `git` | The engine is the submodule `engine/` and it has submodules of its own (SDL, SDL_image with its codecs, Vulkan-Headers, googletest): the clone must be recursive. |
| CMake 3.31 or newer | `cmake_minimum_required(VERSION 3.31)` in `CMakeLists.txt` and `cmakeMinimumRequired` 3.31 in `CMakePresets.json`. |
| Ninja | The macOS presets and the `windows-msvc-*` / `windows-clang-*` presets use the Ninja generator (the `windows-vs2022*` presets use Visual Studio 2022 instead). |
| A C++23 compiler | `CMAKE_CXX_STANDARD 23` (`engine/cmake/BagiEngine.cmake`). macOS: Clang, the presets set `clang` / `clang++` (Apple clang 17 from the Xcode command line tools builds it). Windows: MSVC or Clang, see "Windows". |
| [`uv`](https://docs.astral.sh/uv/) and a Python 3 | `CI/setup_venv.*` calls `uv` to create `engine/.venv`, which holds the reflection generator's dependencies (Jinja2) and the Python of the control smoke test. Install it from its page (Homebrew, `pip install uv`, or the installer script). |
| `glslc` (the Vulkan SDK or the `shaderc` tools) | The engine compiles its Vulkan shaders at build time. Configure stops with "glslc not found" when it is on neither `PATH` nor under `VULKAN_SDK` (`engine/src/Modules/Vulkan/CMakeLists.txt`). Install the [Vulkan SDK](https://vulkan.lunarg.com/) or put `glslc` on `PATH`; if it is somewhere else, pass `VULKAN_SDK` (see "Your own settings"). |

Nothing else is installed by hand: SDL, SDL_image, the Vulkan headers and the Vulkan loader are in `engine/external/` and the loader's binaries are in the same folder. The reflection generator is plain Python and needs no LLVM.

### From clone to a running game

1. Clone with the submodules (the clone is large because of them):

   ```bash
   git clone --recurse-submodules <url> BagiEngine-Chess
   cd BagiEngine-Chess
   ```

   A clone made without `--recurse-submodules`, or a pull that moved `engine/`: `git submodule update --init --recursive`.

2. Create the Python environment, once (Windows: `CI\setup_venv.bat` or `CI\setup_venv.ps1`):

   ```bash
   CI/setup_venv.sh
   ```

   It creates `engine/.venv` and installs `engine/requirements.txt` into it (it fetches the engine submodule itself if that is missing). CMake picks `engine/.venv`'s Python by itself when the folder exists (`engine/cmake/BagiEngine.cmake`, "Python Virtual Environment Detection"); without it the configure falls back to the system Python and the reflection generator fails on a missing `jinja2`.

3. Put the venv first on `PATH` for the shell you configure from (a safeguard: it makes the same Python win everywhere, and it is what the failure hint below tells you to do):

   ```bash
   export PATH="$PWD/engine/.venv/bin:$PATH"
   ```

4. Configure. The presets are the engine's (`CMakePresets.json` includes `engine/CMakePresets.json`); `cmake --list-presets` lists the ones that exist on your OS:

   ```bash
   cmake --preset macos-debug
   ```

   Run this again after adding or deleting a source file (the sources are globbed), after changing a class that uses `BE_CLASS` / `BE_REFLECT_FIELD` (the reflection code is generated at configure time) and after the `engine/` pointer moves.

5. Build everything (`BagiChess` and `ChessTests`; the first build also compiles the engine and its libraries and takes a while):

   ```bash
   cmake --build --preset macos-debug
   ```

6. Run the game. The executable lands in `build/<Compiler><Platform>/`, not in the preset's folder (`build/ClangmacOS/` here; Debug and Release builds overwrite each other). Start it from a terminal so the log is visible:

   ```bash
   build/ClangmacOS/BagiChess
   ```

   It opens a window with two people on one screen; "Playing against the computer" below tells how to play the computer.

7. Run the tests:

   ```bash
   ctest --preset macos-debug
   ```

   That is `ChessTests` (the gtest cases, each in its own process) and `BagiChessControlSmoke`, a Python script that starts `BagiChess --control --headless` and plays it over the control API. The smoke test needs the built `BagiChess` and is **skipped** (exit code 77) when the executable is missing, so build the default target as in step 5, or add `BagiChess` to a narrower `--target ChessTests BagiChess`. Stockfish is not needed: the tests that use a real one skip themselves. To run them against yours:

   ```bash
   build/ClangmacOS/ChessTests --gtest_filter='Stockfish*:UciEngineStockfishTest*'   # STOCKFISH or PATH
   ```

Release is the same with `macos-release`. The `*-rmlui-*` presets also build the vendored RmlUi; the game does not need it.

### Windows

The presets for Windows are in `engine/CMakePresets.json`; they are written from that file and not run on a Windows machine for this README. Steps 1 to 3 and 5 to 7 are the same, with these changes:

- Tools: Visual Studio 2022 with the "Desktop development with C++" workload (it brings MSVC), CMake 3.31 or newer and Ninja (for the Ninja presets), `git`, `uv`, and the Vulkan SDK. The `VULKAN_SDK` cache variable defaults to `C:/VulkanSDK/1.4.341.1` (`engine/cmake/BagiEngine.cmake`); with another version installed, pass the real path (see below). Without an SDK the vendored loader in `engine/external/Vulkan-Loader/Windows` is used, but `glslc` is still needed.
- Step 2: `CI\setup_venv.bat` (cmd) or `CI\setup_venv.ps1` (PowerShell); the venv's Python is `engine\.venv\Scripts\python.exe`, and step 3 is `set PATH=%CD%\engine\.venv\Scripts;%PATH%` (cmd) or `$env:PATH = "$PWD\engine\.venv\Scripts;$env:PATH"` (PowerShell).
- Step 4, one of:
  - `windows-msvc-debug` / `windows-msvc-release`: MSVC (`cl`) with Ninja. These presets do not load the Visual Studio environment themselves (`vs-env` is the `external` strategy: the caller does), so configure from a shell where `cl` works, an x64 Visual Studio developer prompt, or let VS Code CMake Tools load it (see "VS Code").
  - `windows-vs2022`: the Visual Studio 2022 generator, x64, MSVC; it needs no developer prompt. The build and test presets are `windows-vs2022-debug` and `windows-vs2022-release` (`cmake --build --preset windows-vs2022-debug`, `ctest --preset windows-vs2022-debug`).
  - `windows-clang-debug` / `windows-clang-release` (Clang with Ninja, same `vs-env` rule) and `windows-vs2022-clangcl` (build presets `windows-vs2022-clangcl-debug` / `-release`) are there too.
- Step 6: the executable is `build/MSVCWindows/BagiChess.exe` for MSVC (`build/ClangWindows/` for Clang); a Visual Studio configuration may add `Debug/` or `Release/` below that folder.
- Stockfish does not play on Windows yet, only the own engine does (see "Playing against the computer").

### VS Code

Open `CI/BagiChess.code-workspace` (File, Open Workspace from File). It recommends the extensions (C/C++, CMake Tools, CodeLLDB), sets `cmake.useCMakePresets` to `always`, and has the launch configurations and build tasks. Then:

1. Run "CMake: Select Configure Preset" and pick the preset for your OS (`macOS Clang Debug`, `Windows MSVC Ninja Debug`, ...), and "CMake: Select Build Preset" for the matching build preset. Only the presets whose host matches your OS are offered.
2. "CMake: Configure" is step 4, "Build" (F7) is step 5. The default build task builds `BagiChess`; the debug configurations (F5) build first and start the CMake launch target. With the `windows-msvc-*` presets CMake Tools loads the Visual Studio environment itself.
3. The workspace does not run `CI/setup_venv.*` for you: do step 2 above once, in a terminal. The workspace does not put the venv on `PATH`, which is fine, CMake finds `engine/.venv` itself.

### Your own settings

`CMakeUserPresets.json` is git-ignored and holds what is specific to your machine: a different `VULKAN_SDK`, `ccache`, a preset of your own. It may inherit from the shared presets, but it needs a build preset of its own to show up in CMake Tools; a worked example is in `.claude/rules/build-project.md`, "Local overrides". The `.env` file at the root (copy `.env.example`) is for the todo tools only, not for the build.

### When it fails

The common causes and their fixes (a missing `engine/external/*`, a missing `jinja2`, `glslc not found`) are in `.claude/rules/build-project.md`, "Failure hints"; the usual first step for anything odd is `git submodule status --recursive` (a line starting with `-` is a submodule that was not fetched) and a fresh `cmake --preset ...`.

## Playing against the computer

The window starts with two people on one screen. To play the computer press **Menu** (top right, beside Reset). A panel opens with four choices, each showing what is picked now: **Opponent** (Two players or Computer), **Play as** (White or Black: the color you play, the computer plays the other one), **Engine** (Stockfish or Own engine) and **Level** (Easy, Medium, Hard or Max). Every press takes effect at once, the panel stays open, and **OK** closes it. Play as and Level can be picked while two people play too: the choice is kept and applies when you pick Computer. If the engine you pick cannot be found (Stockfish is not installed), nothing changes: what played before keeps playing and the panel and the bottom line say "Engine not found"; pick **Own engine** to play without Stockfish. "Computer is thinking" shows while it searches, and a failure ("Engine not found", "Engine did not answer", ...) shows at the bottom and in the panel, and the game waits, until you press **Reset** (a new game; it keeps the opponent) or pick something in the panel. To leave a broken opponent behind for good, pick Two players. The panel does not close by itself, not when a game ends either: press OK. With Computer and Play as Black the board turns, so your Black pieces are at the bottom (the computer plays White and opens); with Two players White stays at the bottom, and a color picked while two people play turns the board only when you pick Computer. The board carries its coordinates, the file letters a to h along the bottom edge and the rank numbers 1 to 8 along the left edge, small in the corners of the edge squares; they turn with the board (h to a and 1 to 8 while it is turned), and there is no setting to hide them yet.

The game starts with "Stockfish" picked and never switches the engine for you. What **Level** means depends on the engine: for Stockfish it is the `Skill Level` and the time per move (from a twentieth of a second at Easy to two seconds at Max); for the own engine it is how many moves deep it looks (1, 2, 3 and 5 plies), so the same position always gets the same answer, and a position that is very hard to search is cut after 10 seconds. **Easy and Medium also make a random legal move now and then on purpose, with either engine** (about 4 moves in 10 at Easy and 3 in 20 at Medium), because even Stockfish at its weakest setting plays much better than a casual player; Hard and Max never do. Easy is meant for a beginner, Medium for a casual player, Hard for a decent amateur and Max is the full strength of the engine. These numbers are hand-tuned guesses, not measured ratings, and are meant to be adjusted by playing (`src/Chess/Engine/OpponentLevels.cpp`); there is no fifth, easier level yet.

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
