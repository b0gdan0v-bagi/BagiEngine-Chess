# The engine submodule and the todo folder

This repository is a game (Chess) that carries the BagiEngine as the git submodule `engine/`. The skills `todo-loop`, `todo-merge`, `todo-add`, `todo-sync` and the agents `todo-*` work the entries of `todo/` (format: `todo/README.md`).

- **Style and contract:** the code style, DO and DON'T, the architecture and the `be_*` CMake functions are the engine's: read `engine/CLAUDE.md` and the nested `CLAUDE.md` files under `engine/src/` when a file there is read. The game's own `CLAUDE.md` (when present) comes first for anything about Chess.
- **Tools come from the engine:** `engine/tools/todo_sync/todo_sync.py` run with `engine/.venv/bin/python` (`engine/CI/setup_venv.*` creates the venv), from the root of this repository. There is no copy of the tool here.
- **The engine is another repository.** Do not edit anything under `engine/` from this repository's work: a change to the engine is made in the engine's own checkout and its own branch (it has its own todo-loop). Moving the pointer of `engine/` is the owner's job, in a commit of its own.
- **Tracker:** this repository uses its own Vikunja project (`VIKUNJA_PROJECT=BagiEngine-Chess` in the git-ignored `.env`). The tool's default is `BagiEngine`; never run `pull` or `push` against that one.
- **Plans** live in the local store `~/.claude/plans/` and are not committed (`todo/README.md`).
