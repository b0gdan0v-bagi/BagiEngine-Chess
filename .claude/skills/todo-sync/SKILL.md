---
name: todo-sync
description: Refreshes the todo/ progress dashboard, exports todo/ entries to the Vikunja task tracker and pulls closed or new tasks back, through tools/todo_sync. Use when the user asks to update, open or regenerate the TODO dashboard, to sync, push or pull the todo list with Vikunja or the task tracker, or to check that todo/ still parses.
---

# TODO sync (BagiEngine-Chess)

Wraps `engine/tools/todo_sync/todo_sync.py`. Format of the entries, Vikunja settings and what the export owns are in `engine/tools/todo_sync/CLAUDE.md`; the entry format is in `todo/README.md`. The files in `todo/` are the source of truth for the content: edit them, not the tracker (a task closed or created in the tracker comes back with `pull`).

## Arguments (all optional)

| Argument | Command | Effect |
|----------|---------|--------|
| *(none)* / `dashboard` | `dashboard` | Writes `build/todo/index.html` and prints its path. |
| `check` | `check` | Parses `todo/`, one line per entry, exit 1 on a format error. |
| `pull` | `pull` | Reads Vikunja and reports a task closed there while its entry file exists, and a task created there; writes nothing. |
| `pull apply` | `pull --apply` | Deletes the file of a closed task (only if git tracks it and it is clean) and adopts a new task as an entry file. |
| `push dry-run` | `push --dry-run` | Reads Vikunja, prints what would be created, updated, closed or reopened; writes nothing. |
| `push` | `push` | Exports to Vikunja. |

## Workflow

Run from the project root. The Python is the repo venv (`engine/.venv/bin/python`, on Windows `engine\.venv\Scripts\python`); if it is missing, point the user at `engine/CI/setup_venv.*` and stop.

1. Run `check` first. A format error is a mistake in an entry file: show the message and fix the entry only if asked.
2. Order: `pull`, then `push`. `pull --apply` deletes files, so run the report-only `pull` first, show it, and apply only when the user asked for it.
3. For `push`, always run `push --dry-run` first and show the totals. Run the real `push` only when the user asked for it. It writes to the user's tracker, and a wrong `VIKUNJA_PROJECT` would fill the wrong project.
4. Before any `pull` or `push`: `VIKUNJA_PROJECT` (`.env` or the environment) must be set to this repository's own project, `BagiEngine-Chess`. The tool's default is `BagiEngine`, and `push` marks the tasks of entries missing from `todo/` as done, so a run against the engine's project would close the engine's tasks. If it is unset or equals `BagiEngine`, stop and say so.
5. If the settings are missing, the tool says which of `VIKUNJA_URL` / `VIKUNJA_TOKEN` is not set. Do not ask for the token in chat and never print it: the user puts it into the git-ignored `.env`.
6. After a `dashboard`, give the file path. The page cannot be looked at from the agent session, so do not describe how it looks.

A normal `todo-loop` run never runs `push` or `pull`: it makes no network calls and leaves the tracker alone. After the owner merges a `todo/<id>` branch, `/todo-loop close` (or `/todo-merge`, which merges first) deletes the entry and runs `pull --apply` and then `push`; run them here by hand only when that mode is not used.

## Report

State the command, the totals (`N create, N update, N close, N reopen, N unchanged`, or for `pull` `N closed in Vikunja, N created in Vikunja`) and any `[warn]` lines (a `[warn]` that a plan is complete means: delete the plan file and the `plan:` line). Say plainly when a step failed or was skipped.
