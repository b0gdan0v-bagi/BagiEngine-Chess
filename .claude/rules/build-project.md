---
paths:
  - "**/CMakeLists.txt"
  - "CMakePresets.json"
  - "CMakeUserPresets.json"
  - "**/*.cmake"
  - "CI/**/*"
---

# Build, format and include facts of this repository

Read by the skills `build`, `clang-format` and `strip-pch-includes`. The three skills are symlinks into `engine/.claude/skills/` (the same text as in the engine repository); what differs between the repositories is here. This file has the same headings and fields as `engine/.claude/rules/build-project.md`, its template. The todo areas are in `todo-project.md`, not here.

## Terms

| Term | Meaning |
|------|---------|
| `<engine>` | `engine`: the folder with `CI/`, `cmake/` and `.venv`, the git submodule of this game. The same term as in `todo-loop` ("Areas and the repository rules"). |
| `<R>` | the repository to build or format. `.` by default (this repository). `engine` only for engine work done from this checkout; then the skills run from `engine/` and read `engine/.claude/rules/build-project.md`, so `<engine>` is `.` there. Every path below is relative to `<R>`. |
| `<product>` | the game executable of `<R>`, from the field `Product`. |
| `<tests>` | the test targets of `<R>`, from the field `Tests`. |
| `<py>` | the Python interpreter that runs `engine/CI/scripts/*.py`, from the field `Python` (below). |
| `<p>` | the preset the skill picked. |

## Build

Fields the `build` skill reads. The presets are the engine's (`CMakePresets.json` includes `engine/CMakePresets.json`); each builds into its own `build/<p>/`.

| Field | Meaning | Value here |
|-------|---------|------------|
| Product | the executable (target name; the file is `build/<preset>/bin/<name>[.exe]`) | `BagiChess` |
| Tests | the gtest targets; the argument `tests` builds exactly these (`--target <tests>`) | `ChessTests` |
| Default build | what `cmake --build --preset <p>` with no `--target` builds | every target: `BagiChess` and `ChessTests` |
| Source paths | the paths of `git status --porcelain -- <paths>` (a configure trigger) and of the `BE_CLASS` / `BE_EVENT` / `BE_REFLECT_FIELD` search | `src tests` |
| Extra configure triggers | cases besides the common ones (below) that need `cmake --preset <p>` | the submodule pointer `engine` moved (`git status --porcelain -- engine`, or `git diff HEAD -- engine`) |
| Extra failure hints | cause and fix for a configure, compile or link failure that is specific to this repository | see "Failure hints" |
| Extra gotchas | facts about the output, the tests or the run that hold only here | see "Gotchas" |
| Test run | what `ctest --preset <p>` runs here and what it needs | see "Tests" |
| Local overrides, editor | where per-machine settings and the IDE setup are described | below, "Local overrides" and "VS Code" |
| Windows preset | what the `build` skill picks on Windows and how it runs it | `windows-clang-debug` / `windows-clang-release` (Clang + Ninja, `build/windows-clang-{debug,release}/bin/`), every `cmake --preset` and `cmake --build --preset` through `python engine/CI/scripts/vs_env.py` (the x64 Visual Studio environment); MSVC only by explicit name, see "Gotchas" |

The common configure triggers (the skill states them; this repository adds the one above): no `build/<p>/CMakeCache.txt`, the argument `configure`, added, deleted, renamed or untracked files under the source paths (sources are globbed without `CONFIGURE_DEPENDS`), a change in `CMakePresets.json`, `CMakeUserPresets.json` or any `*.cmake`, and a changed or new file under the source paths that contains `BE_CLASS`, `BE_EVENT` or `BE_REFLECT_FIELD` (reflection code is generated only at configure time).

### Failure hints

- A missing `engine/external/*` (the submodule is not initialised): `git -C engine submodule update --init`.
- A missing `jinja2` (the reflection generator): `CI/setup_venv.*` creates `engine/.venv`; `engine/.venv/bin` must be on `PATH` for `cmake --preset` (`export PATH="$PWD/engine/.venv/bin:$PATH"`, as the README does).
- `glslc not found`: install the Vulkan SDK or set `VULKAN_SDK` in `CMakeUserPresets.json` (see "Local overrides").
- Windows, `No CMAKE_RC_COMPILER could be found` (configure) or `CMAKE_AR-NOTFOUND` / `rc.exe` failing on `version.rc` (build): the Ninja preset ran without the Visual Studio environment. Run the command through `python engine/CI/scripts/vs_env.py`; after a configure that failed this way, delete `build/<p>/` first (the cache keeps the `NOTFOUND`).
- Windows, `CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS-NOTFOUND`: a configure of `windows-clang-*` from before the preset turned the module scan off (`no-module-scan`); configure again.

### Gotchas

- The engine is another repository: a build never edits `engine/`. A missing engine feature is `needs engine change`, not a patch here.
- Executables land in `build/<preset>/bin/` (`BagiChess[.exe]`, `ChessTests`, `BagiChessUci`), static libraries in `build/<preset>/lib/`, so Debug and Release sit side by side (a Visual Studio preset adds `bin/<Config>/`). A folder of the old layout (`build/ClangWindows/`, `build/ClangmacOS/`) is not used any more and can be deleted by hand.
- **`BE_WITH_EDITOR` inside one preset.** `ninja` does not notice that a library was built with the other `BE_WITH_EDITOR` (the editor sources are in one and not in the other): after flipping the option in a build folder, delete `build/<preset>/lib/` before building (symptoms of a mix: link errors for editor classes, crashes in the UI tests). The option and its presets: `engine/cmake/CLAUDE.md`, "The editor option".
- **Why MSVC is not the default on Windows.** `windows-vs2022-*` (MSBuild, `/Zi`) failed the first run now and then with `LNK4020 ... BECoreModule.pdb is corrupted`, `LNK1103 debugging information corrupt`, sometimes `C1090` / `C1041` / `LNK1104` / `LNK1201`, and passed the second: every module takes the PCH of `BECoreModule` (`REUSE_FROM`) with a PDB of its own, MSBuild builds them in parallel, and one `mspdbsrv.exe` serves every build on the machine (the main checkout and the workers). Clang writes no `cl` PDB and uses no `mspdbsrv`. The MSVC presets stay for a run by explicit name.
- **The profile preset with the editor:** `windows-clang-profile-editor` is defined in this repository's `CMakePresets.json` (the engine has only `macos-profile-editor`): the engine's hidden `editor` and `profile` presets on the Windows Clang base, so RelWithDebInfo, asserts and expects off, `ENGINE_PROFILER` and `ENGINE_PROFILER_ALLOC` on, the editor panels built (`editor` before `profile` in `inherits`, so its `BE_WITH_EDITOR=ON` wins). Through `vs_env.py` like the other Ninja presets; output `build/windows-clang-profile-editor/bin/`. The game starts in Game mode: Ctrl+Shift+E switches to the editor and its Profiler panel (`engine/src/Modules/BECore/Profiler/HowToProfile.md`).
- `BagiChess` opens a window, so it is never started to check a build (to launch it use the `run` skill). `ChessTests` opens none.

### Tests

`ctest --preset <p>` runs `ChessTests` (the gtests, each in its own process) and `BagiChessControlSmoke`, a Python script that starts `BagiChess --control --headless` and plays it over the control API. The smoke test is skipped (exit code 77) when the executable is not built, so a `--target` list without `BagiChess` (what the argument `tests` builds) does not run it: add `BagiChess`. `UciEngineStockfishTest` skips itself when there is no Stockfish (`CLAUDE.md`, "Tests"). `BagiChessUciSmoke` and `BagiChessMatchSmoke` need `BagiChessUci` too. A full build (no `--target`) also builds the engine's `BagiRoomServer`, and then ctest runs its smokes `BagiRoomServerSmoke` / `BagiRoomServerHostile`; on Windows they fail (2026-11-14, the room server has not been made to run there yet): engine work, not a Chess failure. Sanitizers: the engine's preset `windows-clang-asan` (`macos-asan` on macOS) builds and runs the same targets under ASan + UBSan (`engine/.claude/rules/build-project.md`, "Sanitizers").

### Local overrides

`CMakeUserPresets.json` (git-ignored) holds per-machine settings. It may inherit from the shared presets, but it needs its own build preset to appear in CMake Tools:

```json
{
    "version": 6,
    "configurePresets": [
        {
            "name": "macos-debug-local",
            "inherits": "macos-debug",
            "cacheVariables": {
                "VULKAN_SDK": "/Users/me/VulkanSDK/1.4.363.0/macOS"
            }
        }
    ],
    "buildPresets": [
        {
            "name": "macos-debug-local",
            "inherits": "macos-debug",
            "configurePreset": "macos-debug-local"
        }
    ]
}
```

`VULKAN_SDK` is only needed when `glslc` is not on PATH. ccache needs no preset: the build uses it by itself when it is installed (`ENGINE_CCACHE`, `engine/cmake/BagiCompilerCache.cmake`; `-DENGINE_CCACHE=OFF` switches it off). To share hits between this checkout and the worker folders, set `ENGINE_CCACHE_BASE_DIR` to a folder that holds all of them (`worker.py create` does it for new workers).

On Windows the same with the default Windows preset: `"inherits": "windows-clang-debug"`, configured and built through `python engine/CI/scripts/vs_env.py` like the shared preset.

`.claude/settings.local.json` is git-ignored and per machine too.

### VS Code

For explaining to the user. The workspace is `CI/BagiChess.code-workspace`: CMake Tools on the engine's presets (`cmake.useCMakePresets: always`), CodeLLDB / MSVC launch configurations and build tasks.

- **CMake: Select Configure Preset** and **CMake: Select Build Preset** pick the preset (also in the status bar).
- **CMake: Configure** / **CMake: Delete Cache and Reconfigure** are the configure step of the `build` skill. **Build** (F7) is the build step.
- F5 runs a debug configuration and builds first.

## Format

Fields the `clang-format` skill reads (the `strip-pch-includes` skill uses `Python` and `Format scope` too).

| Field | Meaning | Value here |
|-------|---------|------------|
| Python | the interpreter `<py>`. The two scripts use only the standard library, so no venv is needed; `uv run python` also works. A repository may name its venv interpreter instead | `python3` (Windows: `engine\.venv\Scripts\python` when the venv exists, else `python`; `python3` there is the Microsoft Store stub, "Python was not found") |
| `<engine>` and `<R>` | the values of the two terms for the default run | `engine` and `.` (so `engine/CI/scripts/...` and `--root .`) |
| Format scope | the folders `--all` covers (also for `strip-pch-includes`) | `src/`, `config/`, `tests/` (`--all` covers `tests/` here) |
| Ignore file | the `.clang-format-ignore` in the root of `<R>`: required, one path or prefix per line, `#` comments, not merged with a default | the game's own: `engine/`, `build/`, `.venv/` |
| `--root` | never left out where it matters | **required**: without `--root .` the scripts format the engine |
| Launcher | whether a GUI runs the same scripts | none, see "Launcher" |

The commands, written with the values of this game (run from the repository root):

```bash
python3 engine/CI/scripts/format_sources.py --changed --root .
python3 engine/CI/scripts/format_sources.py --all --root .
```

On Windows write the interpreter of the field `Python` in place of `python3`, here and in every command of this file.

For engine work done from this checkout (`<R>` = `engine`), run the engine's commands from `engine/` with the rules file of `engine/`.

`clang-format` lookup (the script prints the binary and the version it chose): `PATH`, `LIBCLANG_PATH`, `.venv` of `<R>` and of the engine, then on macOS Homebrew LLVM (keg-only, not on `PATH`) and Xcode. `--changed` skips untracked files (it reads `git diff --name-only HEAD`), so a new file is formatted by a direct call with `--style=file` (the `clang-format` skill shows the line). Homebrew LLVM goes on `PATH` for that one command only, never for `cmake --preset`. The todo-developer agent (step 5) uses the same commands.

## Strip PCH includes

Fields the `strip-pch-includes` skill reads (it shares `Python`, `Format scope` and the ignore file with "Format").

| Field | Meaning | Value here |
|-------|---------|------------|
| PCH | the header whose includes are dropped from sources; always the engine's, relative to `<engine>` | `engine/src/Modules/BECore/pch.h` (the script's default) |
| Strip example dir | a bounded folder of `<R>` for the dry run and the first real pass | `src/Chess/Game` |
| After a pass | what to build and run to validate the scope | `cmake --build --preset <p>` (the `build` skill), then build and run `ChessTests` (`ctest --preset <p>`) |

The commands, written with the values of this game:

```bash
python3 engine/CI/scripts/strip_pch_includes.py --dry-run --changed --root .
python3 engine/CI/scripts/strip_pch_includes.py --dry-run --all --root . --dir src/Chess/Game
python3 engine/CI/scripts/strip_pch_includes.py --all --root . --dir src/Chess/Game
```

`--root .` is never left out: without it the script strips the engine's sources. The order stays: dry run, a bounded folder, the build and the tests, then a wider scope.

## Launcher

none. The engine launcher (`!start.cmd`, `!start.command`) is the engine repository's; this game has none.
