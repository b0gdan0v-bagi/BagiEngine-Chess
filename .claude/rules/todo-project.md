# The todo areas of this repository

Read by the skills `todo-loop`, `todo-merge`, `todo-add`, `todo-db` and by the agents `todo-*`. The skills and agents are one copy in the submodule (`engine/.claude/`), linked here by symlinks; `engine/.claude/skills/todo-loop/SKILL.md`, "Areas and the repository rules", explains the terms below. The engine is developed from this checkout: its entries are worked here, in the `engine/` submodule.

## Areas

| Area | `<area>` (todo store) | `<R>` (git repository) | `<base>` | Vikunja project (the service's) |
|------|-----------------------|------------------------|----------|---------------------------------|
| game | `chess` | `.` | `main` | `BagiEngine-Chess` |
| engine | `engine` | `engine` | `games/chess` (the `branch` of `.gitmodules`) | `BagiEngine` |

`<engine>` is `engine`. The CLI runs from this repository's root as `python3 engine/tools/todo/todo.py <command>` for both areas (an entry knows its area; `list --area chess|engine` narrows); `TODO_API_URL` and `TODO_API_TOKEN` come from the git-ignored `.env` of this repository (`.env.example`). The Vikunja project of an area is a setting of the store, not of the CLI: there is no push and no pull.

An engine entry's branch `todo/<id>` is cut in `engine/` from `games/chess`; its commits and the merge are in the engine repository, its status is in the store. The game repository only ever changes the pointer of `engine/`, and only `todo-merge` does it, as a commit of its own, after the merge and the owner's yes (`Update engine: <title>`). A game entry that needs engine work waits (skip reason below) until the engine entry for it is merged.

## Build and test (baseline and reviewer)

- Build with a preset of `CMakePresets.json` (it includes the engine's presets): `cmake --build --preset <p> --target BagiChess ChessTests`, then `ctest --preset <p>` (the smoke test needs the game). This build contains the engine as the game uses it; the engine's own `BagiEngineTests` is **not** a target here. For an engine entry the reviewer also configures and builds the engine on its own, in `engine/` (`cd engine && cmake --preset <p> && cmake --build --preset <p> --target BagiEngineTests Match3Tests`, then `ctest --preset <p>` there), and compares its failures with the baseline taken on `games/chess`.
- Read first: `CLAUDE.md` of this repository (the game) and `engine/CLAUDE.md` (style, DO and DON'T, the engine's contract), the nested `CLAUDE.md` of the touched directories, the matching `.claude/rules/` file of `<R>`.
- Format: `python3 engine/CI/scripts/format_sources.py --changed --root <R>` and `strip_pch_includes.py --root <R>` (`<R>` is `.` for the game, `engine` for the engine); see the developer agent; the full commands and the facts of this repository are in `.claude/rules/build-project.md`.

## Zones nobody edits

The vendored code of the engine: `engine/src/Modules/EABase/`, `EnTT/`, `fmt/`, `imgui/`, `nlohmann/`, `pugixml/`, `RmlUi/`, `stb/`, and `engine/external/`. The rest of `engine/` is editable through an entry of the engine area, in its own branch.

## Classifier

`eligible` is a concrete, bounded piece of game or engine work that can be done and checked without the owner. Extra reasons:

- `owner decision`: the entry names a decision, an open design question, a license or a product choice.
- `needs engine change`: an entry of the **game** area whose work needs a change in `engine/` that no merged or eligible engine entry covers. (An entry of the engine area is never skipped for being in `engine/`.)
- Example answer lines: `{"area": "game", "id": "chess-analysis", "verdict": "eligible", "size": "M", "reason": "one line"}`, `{"area": "game", "id": "chess-vs-engine", "verdict": "skip", "size": null, "reason": "needs engine change: it waits for the in-game UI of the engine"}`.
