# TODO

The entries are not files any more. The known problems and ideas of the game (area `chess`) and of the engine (area `engine`), their status, the plans and the reviews live in the **todo store**: a service on Railway over Postgres, described in [engine/tools/todo/CLAUDE.md](../engine/tools/todo/CLAUDE.md). Nothing of it is committed, so this folder holds only this note.

- Read: `python3 engine/tools/todo/todo.py list --area chess`, `show <id>`, `dashboard` (the pages), or Vikunja (project `BagiEngine-Chess`), which the service keeps in step by itself in both directions.
- Add: `/todo-add <idea>` (or `todo.py add <id> --area chess ...`). Work: `/todo-loop`, `/todo-merge`. Everything else: `/todo-db`.
- A finished entry is closed, not deleted; its history stays (`todo.py history <id>`).
