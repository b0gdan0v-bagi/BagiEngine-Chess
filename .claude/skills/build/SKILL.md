---
name: build
description: Builds BagiChess through CMakePresets.json (the engine's presets, included by this repository's CMakePresets.json). Picks the preset for this OS, configures only when needed, builds, and reports errors with file:line. Use when the user asks to build, compile, rebuild, configure, switch Debug/Release, or says the project does not build.
---

# Build (BagiChess)

Builds `BagiChess` and the gtest executable `ChessTests` with `CMakePresets.json` from the repository root. The presets are the engine's (`engine/CMakePresets.json`, included); each builds into its own `build/<preset>/`. The engine itself is the submodule `engine/`: the build rules, the preset table and the gotchas are in the engine's `build` skill (`engine/.claude/skills/build/SKILL.md`). Read it for anything this file does not say. This file lists only what differs here.

## Arguments (all optional)

| Argument | Effect |
|----------|--------|
| `debug` / `release` | Build type. Default: `debug`. |
| `clean` | Add `--clean-first`. |
| `configure` | Force a configure step. |
| `tests` | Build only `ChessTests` (`--target ChessTests`). Run the tests afterwards only when asked: `ctest --preset <p>`. |
| `<preset name>` | Use this preset as-is. |

## Workflow

Run from the repository root.

1. **Preset:** an explicit name wins; otherwise Darwin -> `macos-<type>`, Windows -> `windows-msvc-<type>` when `cl` is on PATH, else `windows-vs2022-<type>`. Check it against `cmake --list-presets=build`; if it is missing, stop and show the list.
2. **Configure** (`cmake --preset <p>`) when `build/<p>/CMakeCache.txt` is missing, on the `configure` argument, when `git status --porcelain -- src tests` shows added, deleted, renamed or untracked files (sources are globbed without `CONFIGURE_DEPENDS`), when `CMakePresets.json` or a `*.cmake` changed, when the submodule pointer `engine` moved, or when a changed file under `src/` or `tests/` contains `BE_CLASS`, `BE_EVENT` or `BE_REFLECT_FIELD` (reflection code is generated only at configure time).
3. **Build:** `cmake --build --preset <p>` (the preset builds every target: `BagiChess` and `ChessTests`). The first build of a new `build/<p>/` compiles SDL, ImGui and the engine and takes minutes: run it in the background with the output in a log file and wait for the notification.
4. **On failure** report the first real errors (`grep -E "error:|FAILED:|CMake Error|ld: " <log> | head -20`), name the phase (configure, compile, link) and show each error as `path/File.cpp:line`. A missing `engine/external/*` means `git -C engine submodule update --init`; a missing `jinja2` means `CI/setup_venv.*`.
5. **Report** success only on exit code 0: preset, configure + build or build only, executable path.

## Gotchas

- Executables land in `build/{Clang|MSVC}{macOS|Windows}/BagiChess[.exe]` and `ChessTests`, not in the preset folder; Debug and Release overwrite each other.
- The engine is another repository: a build never edits `engine/`. A missing engine feature is `needs engine change`, not a patch here.
- Do not run the game after a build unless asked (it opens a window); to launch it use the `run` skill. Run the tests only when asked.
- After building C++ changes remind the user about `clang-format` and `strip-pch-includes`; do not run them unasked.
