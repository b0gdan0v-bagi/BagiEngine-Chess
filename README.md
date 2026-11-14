# BagiEngine-Chess

Chess on the [BagiEngine](https://github.com/b0gdan0v-bagi/BagiEngine): rules as plain C++ (bitboards, perft-tested), a scene with an engine-drawn HUD, a control API for tests and for playing against Stockfish. The engine is the git submodule `engine/`, on the permanent branch `games/chess` of the engine repository.

## Build and test

From a clean `git clone` to a running game and a green test run. The commands are for macOS (what the game is built and tested on); Windows is in "Windows" below, the editor in "VS Code". The build facts that the agents read (targets, when to re-run configure) are in `.claude/rules/build-project.md`.

### What you need

| Need | Why, and what the repository says |
|------|-----------------------------------|
| `git` | The engine is the submodule `engine/` and it has submodules of its own (SDL, SDL_image with its codecs, Vulkan-Headers, googletest): the clone must be recursive. |
| CMake 3.31 or newer | `cmake_minimum_required(VERSION 3.31)` in `CMakeLists.txt` and `cmakeMinimumRequired` 3.31 in `CMakePresets.json`. |
| Ninja | The macOS presets and the `windows-clang-*` / `windows-msvc-*` presets use the Ninja generator (the `windows-vs2022*` presets use Visual Studio 2022 instead). |
| A C++23 compiler | `CMAKE_CXX_STANDARD 23` (`engine/cmake/BagiEngine.cmake`). macOS: Clang, the presets set `clang` / `clang++` (Apple Clang, e.g. from Xcode or its command line tools, builds it). Windows: Clang (the default) or MSVC, see "Windows". |
| [`uv`](https://docs.astral.sh/uv/) and a Python 3 | `CI/setup_venv.*` calls `uv` to create `engine/.venv`, which holds the reflection generator's dependencies (Jinja2) and the Python of the control smoke test. Install it from its page (Homebrew, `pip install uv`, or the installer script). |
| `glslc` (from the Vulkan SDK or the `shaderc` tools); on macOS the Vulkan SDK itself | The engine compiles its Vulkan shaders at build time. Configure stops with "glslc not found" when it is on neither `PATH` nor under `VULKAN_SDK` (`engine/src/Modules/Vulkan/CMakeLists.txt`). Install the [Vulkan SDK](https://vulkan.lunarg.com/) or put `glslc` on `PATH`; if it is somewhere else, pass `VULKAN_SDK` (see "Your own settings"). **On macOS install the Vulkan SDK**, a recent one (1.4.363.0 is the version this game is run with): `BagiChess` opens its window through Vulkan (`renderer="VulkanRendererBackend"` in `config/ApplicationConfig.xml`, with no fallback renderer), and macOS has no Vulkan driver of its own, the vendored loader is only the loader. The driver it runs on is KosmicKrisp (`libkosmickrisp_icd.json`), which the SDK installs (an optional component of its installer) next to MoltenVK. **MoltenVK alone does not work** (Homebrew `molten-vk`, or an SDK without KosmicKrisp): its driver is a portability driver, the engine does not enable portability enumeration, and `vkCreateInstance` fails with `VK_ERROR_INCOMPATIBLE_DRIVER`. The loader finds the driver, and `glslc` is on `PATH`, because the installer's "System Global Installation" component puts the driver manifests in `/usr/local/share/vulkan/icd.d` and `glslc` in `/usr/local/bin`; without it, source the SDK's `setup-env.sh` (it sets `VK_DRIVER_FILES`) and pass `VULKAN_SDK` (the SDK's `macOS` folder) to configure. `glslc` from `shaderc` alone is enough to configure, build and run the tests, which need no Vulkan driver and no GPU (the gtests record their draws, and the control smoke runs headless on the SDL renderer), but not to open the game window. |

Beyond that, SDL, SDL_image, the Vulkan headers and the Vulkan loader library are in `engine/external/` (the loader library for macOS, only an import library for Windows, see "Windows"), so they are not installed by hand. The reflection generator is plain Python and needs no LLVM.

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

   Run this again after adding or deleting a source file (the sources are globbed), after changing a class that uses `BE_CLASS`, `BE_EVENT` or `BE_REFLECT_FIELD` (the reflection code is generated at configure time), after a change to `CMakePresets.json`, `CMakeUserPresets.json` or a `*.cmake` file, and after the `engine/` pointer moves.

5. Build everything (`BagiChess`, `BagiChessUci` and `ChessTests`; the first build also compiles the engine and its libraries and takes a while):

   ```bash
   cmake --build --preset macos-debug
   ```

6. Run the game. The executable lands in `build/<preset>/bin/` (`build/macos-debug/bin/` here; Debug and Release sit side by side). Start it from a terminal so the log is visible:

   ```bash
   build/macos-debug/bin/BagiChess
   ```

   It opens a window with two people on one screen; "Playing against the computer" below tells how to play the computer.

7. Run the tests:

   ```bash
   ctest --preset macos-debug
   ```

   That is `ChessTests` (the gtest cases, each in its own process) `BagiChessControlSmoke`, a Python script that starts `BagiChess --control --headless` and plays it over the control API, `BagiChessScenario.*`, the control sessions recorded in `tests/scenarios/` and played back on the manual clock, and `BagiChessUciSmoke`, which starts `BagiChessUci` and talks UCI to it over pipes. A smoke or scenario test needs its built executable and is **skipped** (exit code 77) when it is missing, so build the default target as in step 5, or add `BagiChess` and `BagiChessUci` to a narrower `--target ChessTests BagiChess BagiChessUci`. Stockfish is not needed: the tests that use a real one skip themselves. To run them against yours:

   ```bash
   build/macos-debug/bin/ChessTests --gtest_filter='Stockfish*:UciEngineStockfishTest*'   # STOCKFISH or PATH
   ```

Release is the same with `macos-release`. The `*-rmlui-*` presets also build the vendored RmlUi; the game does not need it.

### Windows

The presets for Windows are in `engine/CMakePresets.json`. `windows-clang-debug` is the one built and tested on Windows (Clang 19, Visual Studio 2022, Ninja); the MSVC presets are kept as fallbacks. Steps 1 to 3 and 5 to 7 are the same, with these changes:

- Tools: Visual Studio 2022 with the "Desktop development with C++" workload (it brings the Windows SDK, the linker and MSVC), LLVM (`clang` / `clang++` on `PATH`, 19 or newer), CMake 3.31 or newer and Ninja (for the Ninja presets), `git`, `uv`, and the Vulkan SDK. The `VULKAN_SDK` cache variable defaults to `C:/VulkanSDK/1.4.341.1` (`engine/cmake/BagiEngine.cmake`); with another version installed, pass the real path (see below). Without an SDK the build links the vendored import library `engine/external/Vulkan-Loader/Windows/Lib/vulkan-1.lib` (no loader DLL is vendored for Windows: `vulkan-1.dll` comes from the GPU driver or the SDK), and `glslc` is still needed.
- Step 2: `CI\setup_venv.bat` (cmd) or `CI\setup_venv.ps1` (PowerShell); the venv's Python is `engine\.venv\Scripts\python.exe`, and step 3 is `set PATH=%CD%\engine\.venv\Scripts;%PATH%` (cmd) or `$env:PATH = "$PWD\engine\.venv\Scripts;$env:PATH"` (PowerShell).
- Steps 4 and 5: `windows-clang-debug` / `windows-clang-release`, Clang with Ninja. The Ninja presets do not load the Visual Studio environment themselves (`vs-env` is the `external` strategy: the caller does), and Clang needs it for the resource compiler `rc` and the archiver `lib`. From a plain shell run configure and build through `engine/CI/scripts/vs_env.py`, which finds Visual Studio (vswhere), loads `vcvarsall.bat x64` and runs the command (in a developer prompt it only runs it); VS Code CMake Tools loads the environment itself (see "VS Code"). `ctest` needs no environment:

  ```bat
  python engine/CI/scripts/vs_env.py cmake --preset windows-clang-debug
  python engine/CI/scripts/vs_env.py cmake --build --preset windows-clang-debug
  ctest --preset windows-clang-debug
  ```

  The fallbacks, MSVC:
  - `windows-msvc-debug` / `windows-msvc-release`: MSVC (`cl`) with Ninja, the same environment rule.
  - `windows-vs2022`: the Visual Studio 2022 generator, x64, MSVC; it needs no developer prompt. The build and test presets are `windows-vs2022-debug` and `windows-vs2022-release`. `windows-vs2022-clangcl` (build presets `windows-vs2022-clangcl-debug` / `-release`) is there too.
  - MSVC is not the default because its debug information (`/Zi`, a PDB per module beside the shared PCH, one `mspdbsrv` for every build on the machine) breaks a parallel build now and then (`LNK1103`, `LNK4020 ... pdb is corrupted`, `C1090`); a second run passes.
- Step 6: the executable is `build/windows-clang-debug/bin/BagiChess.exe` (the folder of the preset for MSVC too; a Visual Studio configuration adds `Debug/` or `Release/` below `bin/`).
- Stockfish plays on Windows as on macOS: install it yourself and point `STOCKFISH` (or `stockfishPath`) at the `.exe` (see "Playing against the computer").

### VS Code

Open `CI/BagiChess.code-workspace` (File, Open Workspace from File). It recommends the extensions (C/C++, CMake Tools, `twxs.cmake`, CodeLLDB), sets `cmake.useCMakePresets` to `always`, and has the launch configurations and build tasks. Then:

1. Run "CMake: Select Configure Preset" and pick the preset for your OS (`macOS Clang Debug`, `Windows Clang Ninja Debug`, ...), and "CMake: Select Build Preset" for the matching build preset. Only the presets whose host matches your OS are offered.
2. "CMake: Configure" is step 4, "Build" (F7) is step 5. The default build task builds `BagiChess`; the debug configurations (F5) build first, except the one named "No Build", and start the CMake launch target. With the Windows Ninja presets (`windows-clang-*`, `windows-msvc-*`) CMake Tools loads the Visual Studio environment itself.
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

With none of the three the game still runs as two people, and picking Computer with "Stockfish" says "Engine not found" and changes nothing else (the panel stays on Two players). The way out is the **Engine** row of the panel: pick "Own engine" (that clears the message), then Computer again. The game starts Stockfish as a child process on macOS, Linux and Windows (checked on macOS and Windows; on Windows point `stockfishPath` or `STOCKFISH` at the `.exe`, for example a release unpacked under `%LOCALAPPDATA%\Programs\Stockfish`). The game and the choice are remembered between runs: on start the unfinished game and the opponent come back with no dialog, and the Reset button starts a new game (which is what the next start then shows). The save is `saves/game.save` under the per-user folder of the game (on Windows `%APPDATA%\BagiEngine\BagiChess`). To start fresh once, press Reset; to keep nothing, run with `--no-save` or put `rememberGame="false"` in `config/ChessSettings.local.xml`. A run with `--control` or `--headless` never touches it unless `--save-dir=<path>` is given.

## Browser build

Chess can be built for the browser with the engine's Emscripten presets (`web-release`, `web-debug`). **None of this part has been run with emsdk yet** (slice 2 of the todo entry `chess-web-build`, which waits for the engine's own browser build, the entry `web-build`): the steps below are taken from the engine's `CLAUDE.md` ("Build" table) and `engine/tools/web/CLAUDE.md` and are **not yet run**. What has been checked is the web-like build on the desktop (below).

1. Install the Emscripten SDK as the engine's documentation says and run its `emsdk_env`, so that the environment variable `EMSDK` is set (the web presets are shown only then). The setup is the engine's, not this repository's: see `engine/CLAUDE.md` and the `build` skill.
2. Configure and build, from the root of this repository: `cmake --preset web-release`, then `cmake --build --preset web-release --target BagiChess`. The output is `build/web-release/bin/BagiChess.html` with its script, `.wasm` and the preloaded content package `BagiChess.data` (`config/` and `assets/`; `cmake/ChessWeb.cmake` keeps the docs, `*.local.xml` and `imgui.ini` out of it).
3. Package it: `python engine/tools/web/package.py build/web-release/bin dist --game BagiChess` (`dist/` is git-ignored).
4. Serve it: `python engine/tools/web/serve.py --root dist` (add `--cross-origin-isolation` for a threaded build), then open http://localhost:8080.

**What is different in the browser.** Only the **own engine** plays: there is no Stockfish (a browser cannot start a program; the Engine row of the panel has just "Own engine" and `stockfish` is refused as unavailable), no control API (the engine drops `RemoteControl` on the web) and no scene editor. The single-thread build has no worker thread, so the own engine's search is cut into slices of 256 nodes that yield to the frame budget (`chess-search-resumable`), and the page should keep drawing and taking clicks while the computer thinks. **This is checked on the desktop in the web-like build only, not yet in a browser** (the frame budget is the engine's default, 4 ms; reading it from the config is the entry `tasks-frame-budget`). The browser's `ApplicationConfig.xml` is the engine's own overlay; Chess's differs from it only in the renderer, and a test keeps it so (root `CLAUDE.md`, "The web build").

**The web-like check on the desktop** (no emsdk needed): the same code with the web options, `-DENGINE_WITH_VULKAN=OFF -DENGINE_SINGLE_THREAD=ON -DENGINE_STATIC_SDL=ON -DCHESS_NO_STOCKFISH=ON`, in a build folder of its own, runs `ChessTests` (the tests that need a worker thread skip themselves). `BagiChess` does not start in that build (its `ApplicationConfig.xml` names the Vulkan renderer). The commands are in `tests/CLAUDE.md`, "Build variants". A desktop build with only `-DCHESS_NO_STOCKFISH=ON` (Vulkan on) starts the game without Stockfish and `python tests/control_smoke.py --exe <BagiChess>` checks it.

## UCI engine

`BagiChessUci` is the own engine as a console program that speaks UCI on stdin and stdout, so that a tournament tool (cutechess-cli, fastchess) or a script can play it against another engine. It is built with everything else and opens no window:

```bash
build/macos-debug/bin/BagiChessUci
```

Type `uci`, then for example `position startpos moves e2e4` and `go depth 3`; it answers `bestmove ...`. It understands `position`, `go` (`depth`, `movetime`, the clocks, `movestogo`, `infinite`), `stop`, `isready`, `ucinewgame`, `setoption` and `quit`. Its options are `Depth` (0 to 64, 0 is no limit), `BlunderPercent` (0 to 100, the share of random legal moves) and `Level` (`easy`, `medium`, `hard`, `max`, which sets the other two as the game plays that level). Details, and what is not supported, are in `src/Chess/Uci/CLAUDE.md`. To measure it against Stockfish, install Stockfish yourself (nothing of it is shipped) and see "Measuring the strength".

## Measuring the strength

`tools/uci_match.py` plays `BagiChessUci` against Stockfish (or any other UCI engine that has an Elo option) and prints, for every step of the own engine, an Elo with a 95 % error bar. Standard library only; the games are judged by the game itself: the tool starts `BagiChess --control --headless` as the referee, so repetition, the 50-move rule and insufficient material are decided by our rules and every move of both engines is checked against the legal moves.

**What the number means.** It is an Elo **on Stockfish's `UCI_Elo` scale at the chosen move time** (default 100 ms): not a CCRL rating and not the rating of a person. It moves with the Stockfish version, the machine and the build type. Use it to compare the steps of the own engine with each other and to place them roughly on Stockfish's scale, not as a title.

Stockfish is your own install (it is not shipped and no code of it is here): the tool takes `--opponent`, then `$STOCKFISH`, then `stockfish` on `PATH`, and exits with code 77 (nothing done) when there is none. Use a **Release** build to measure: a Debug build searches several times slower, so the numbers are of a slower program and a match takes much longer.

```bash
# macOS
python3 tools/uci_match.py --referee build/macos-debug/bin/BagiChess --own build/macos-debug/bin/BagiChessUci \
    --opponent /opt/homebrew/bin/stockfish --opponent-elo 1400 --opponent-elo 1700 --opponent-elo 2000 --games 20 --out match.json
```

Windows (the interpreter is `python`; cmd syntax, `^` continues the line):

```bat
python tools/uci_match.py --referee build/windows-clang-release/bin/BagiChess.exe --own build/windows-clang-release/bin/BagiChessUci.exe ^
    --opponent C:/tools/stockfish/stockfish.exe --games 20 --out match.json
```

`python3 tools/uci_match.py --referee ... --verify-openings` first checks that the game takes every opening line of the tool (it needs no opponent). Start small, for example `--games 4 --own-step hard`: the full default match (4 steps x 3 Elo steps x 20 games, 12 pairs) takes **an hour or more** at 100 ms a move.

| Option | Meaning (default) |
|--------|-------------------|
| `--referee PATH` | the `BagiChess` executable, started headless on the manual clock; or `--port N` (and `--host`) to join a game already running with `--control`, whose opponent must play nobody |
| `--own PATH` | the `BagiChessUci` executable |
| `--own-step TEXT` | a strength of the own engine: `easy`, `medium`, `hard`, `max`, or `depth=N[,blunder=P]`; repeatable (default: the four levels) |
| `--opponent PATH` | the opponent engine (`$STOCKFISH`, then `PATH`) |
| `--opponent-elo N` | an Elo step of the opponent; repeatable (1400 1700 2000); a step outside the engine's `UCI_Elo` range is refused before the first game; **pick steps near the expected strength** |
| `--opponent-elo-option NAME` | the UCI option that holds the step (`UCI_Elo`) |
| `--opponent-option NAME=VALUE` | a UCI option set before each pair; repeatable; giving any replaces the default (`UCI_LimitStrength=true`) |
| `--opponent-movetime MS` | time per move of the opponent (100); the own engine plays as the game plays it (the depth of its level, no clock) |
| `--games N` | games per pair (own step x opponent step), even (20); every opening is played from both sides |
| `--max-plies N` | a game that reaches this many plies is a draw, marked `max_plies` (300) |
| `--out FILE` | the record as JSON, rewritten after every finished pair (settings, every game with its moves, the summary per step); a match that stops early keeps what was played, with `"stopped_early": true` |
| `--own-timeout S`, `--trace`, `--verify-openings` | the own engine's time for one move (120 s); print the UCI conversation to stderr; check the openings and exit |

**Reading the output.** A table goes to stdout: one row per pair (W/D/L, score, the Elo of that pair) and one line per step, for example `hard  Elo 1523 +/- 87  (60 games)`: the Elo that scores as the step did against all the opponent steps, with the 95 % bar (1.96 standard errors). A step that won every game, or lost every game, has no estimate: it is printed as a bound (`>= 1810 (saturated)` / `<= 1250 (saturated)`; in the JSON `saturated` is true and `bound` is `">="` or `"<="`), so **move the Elo steps** (higher or lower) and run again. Three cautions: a bound from very few games says little (one won game is already `>=` the opponent's Elo); a step whose games are all draws, or nearly all, against several close opponents gets a finite bar that is much too small; and all draws against one opponent print `+/- ?` (the bar is unknown; `null` in the JSON). Prefer more games over more steps.

The ladder (`OpponentLevels`, `ownDepth`) is retuned only by the owner, from numbers like these; the tool changes nothing. `BagiChessUci` can also be put into cutechess-cli or fastchess as an engine (command: its path, protocol `uci`, `option.Level=hard`); that is not tested here, `tools/uci_match.py` is the documented way.

## Layout

| Path | What |
|------|------|
| `src/Chess/` | the game: rules (`Game/`), scene components, the control adapter. `CLAUDE.md` describes it |
| `uci/` | `main.cpp` of `BagiChessUci`, the own engine as a UCI program (the server is `src/Chess/Uci/`) |
| `dev.json` | what the `bagi-dev` panel of VS Code and `engine/tools/dev/dev.py` build and run: the game, `BagiChessUci`, the tests and the browser page, and the tools (the Stockfish bridge, the match, the screenshots, format, the docs budget). The browser page has not been run with emsdk (see "Browser build") |
| `config/`, `assets/` | scene, texture library, application config, piece atlas |
| `cmake/` | `ChessWeb.cmake`: the files of the browser package (see "Browser build") |
| `tests/` | gtest cases, `control_smoke.py`, the recorded control scenarios `scenarios/*.jsonl`, `uci_smoke.py` and `match_smoke.py` |
| `tools/` | the Stockfish bridge and its pytest tests (`engine/.venv/bin/python -m pytest tools/tests`), the match tool `uci_match.py` (with `elo_rating.py` and `match_steps.py`) and its unit tests (`python3 -m unittest discover -s tools/tests -p "test_match_*.py"`), and `ui_shots.py`, the screenshot command (`python tools/ui_shots.py --list`) |
| `.claude/rules/qa-project.md` | the screenshot set of the game (scenes, sizes) that the todo reviewer takes with `engine/tools/shots/shots.py`; the command is `tools/ui_shots.py` |
| `todo/`, `TODO.md` | known problems, worked by the `todo-loop` skill (`todo/README.md`) |
| `CI/` | environment setup (`setup_venv.*`) and the VS Code workspace `BagiChess.code-workspace` (CMake Tools on the engine's presets, CodeLLDB / MSVC launch, build tasks) |
| `engine/` | the engine (submodule); not edited from here |

## The engine

A change the game needs from the engine is made in the engine's own checkout, on `games/chess`, and then the pointer here is moved in a commit of its own: `git submodule update --remote engine`. Details: `.claude/rules/engine-and-todo.md`.
