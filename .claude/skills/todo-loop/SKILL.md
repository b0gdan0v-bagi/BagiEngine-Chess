---
name: todo-loop
description: Works through the entries of todo/ one by one, each in its own local branch todo/<id> cut from main, with a classifier, an architect, developers and a reviewer as separate agents, and leaves each branch in status review for the owner. With `close` it finishes merged branches (deletes the entry, syncs the tracker). Use when the user asks to run the todo loop, work through the todo list on its own, take the next todo entry, list what the loop could take, or says a todo branch is merged and its entry should be closed.
---

# todo-loop (BagiEngine-Chess)

You are the **conductor**. You do not read or write code: you run git, edit the status in the front matter of one entry file, start agents (`.claude/agents/todo-*.md`), and keep short reports in your context. All work on code happens in the agents. The entry format is `todo/README.md`; the tool is `engine/tools/todo_sync/CLAUDE.md`.

State lives in git (a branch `todo/<id>*` that is not merged into `main` means the entry is taken), in the entry file and in the slice plan, so the loop can be stopped and started again on this machine.

## Arguments (all optional)

`/todo-loop [N] [<id>] [list] [plan-only] [close] [models=role:model,...]`

| Argument | Effect |
|----------|--------|
| `N` | Take at most N slices. Default 1. |
| `<id>` | Work on this entry. If an unmerged branch `todo/<id>*` exists, continue it from the first WP whose boxes are not ticked. |
| `list` | Only the classifier runs. No branches, no edits. |
| `plan-only` | Classifier and architect. The branch holds one commit (the status); the plan stays in the plan store. |
| `close` | After the owner has merged branches: close their entries and sync the tracker (section "Closing merged entries"). Nothing else runs. |
| `models=classifier:haiku,reviewer:sonnet` | Override the model of a role for this run. Only `haiku` and `sonnet` are allowed; never above `sonnet`. |

## Roles and models

| Role | Agent | Model | Does |
|------|-------|-------|------|
| Classifier | `todo-classifier` | haiku | Chooses the next eligible entry, answers JSON. |
| Architect | `todo-architect` | sonnet | Cuts the slice into work packages (WP), writes `~/.claude/plans/todo-loop-<branch-id>.md`, may answer `BLOCKED`. |
| Developer | `todo-developer` | sonnet | One WP: code, tests, docs, format, commit `WP k/M: ...`. A new agent for every WP. |
| Reviewer | `todo-reviewer` | sonnet | Fresh context, reads the diff, builds and runs the tests, answers `APPROVE` or `CHANGES`. |

Start agents with the Agent tool and `subagent_type` set to the agent's name, one at a time, in the foreground (the checkout is shared, and a background agent cannot ask for permissions). Never use SendMessage to a developer that has finished: each WP gets a new one. Size **S** (from the classifier): skip the architect and give the developer the entry as one WP, with no plan file.

## Preconditions

Stop with a message when any of these fails (for `list` and `close` only the first, the venv and the plan store are checked, not the build):

- `git status --porcelain` is empty (the loop works in this checkout; the owner must not edit files while it runs).
- `engine/.venv/bin/python` exists (`CI/setup_venv.*`), `engine/external/googletest` is initialized, the preset of this OS is in `cmake --list-presets=build`.
- `~/.claude/plans/` exists and can be written.
- `engine/` is initialized (`git submodule update --init --recursive`). The engine is another repository: no agent edits anything inside `engine/`. An entry that needs a change there is skipped (`needs engine change`) or `BLOCKED`; the engine has its own todo-loop, and the owner moves the pointer of `engine/`.

Not a stop, a flag: `test -f ~/.claude/skills/kovalev-review-checklist/SKILL.md`. Pass the reviewer `checklist: available` or `checklist: unavailable` accordingly.

Then remember the starting branch, `git switch main`, and once per run build `BagiChess ChessTests` (the smoke test needs the game) and run `ctest --preset <p>`. The names of the failing tests go to a file in the scratchpad: the **baseline**.
## The loop

1. **Choose.** `git switch main`. First look for entries that wait to be closed (see "Closing merged entries", step 2). If there are any, ask the owner once (AskUserQuestion): close them now and then continue, or continue without closing. Run `close` only on a "close" answer; never on your own. Run `engine/.venv/bin/python engine/tools/todo_sync/todo_sync.py check` and `parse`. Taken ids: `git branch --list 'todo/*' --no-merged main` (a merged branch does not take an entry; strip `todo/` and a `-K` suffix). Candidates: entries with `status == "open"` and no taken id, ordered: no stage first, then stages 1 to 6, then by `id`. Entries `in progress` are the owner's; the loop never takes them. Give the classifier the candidates (`id`, `title`, `stage`, `summary`, `file`). Check its `next` yourself: the entry is in `parse`, its status is `open`, no taken branch. With `<id>`, skip the classifier unless `list`. No eligible entry: stop the loop.
2. **Branch.** `git switch -c todo/<id> main` (`todo/<id>-2`, `-3` for the next slice of an entry whose earlier branch is merged: the first free name). First commit changes only the front matter of `todo/<id>.md`: `status: in progress <YYYY-MM-DD> branch todo/<branch-id>` (create the front matter if the entry has none; keep `stage:` and `plan:`).
3. **Plan.** Start the architect (not for size S) with the entry, branch, size and the `plan`, `planFound`, `planProgress` of the entry. `BLOCKED` goes to "Stopping a task". The plan file is not committed, and the entry's `plan:` line is not changed (it points to the owner's plan; the slice plan is a separate file). With `plan-only`, end the task here with the status commit and go to step 7.
4. **Implement, WP by WP.** For each WP a new developer, with the WP text, the plan path and the branch. If it ends with `WP incomplete: ...`, start a new developer for the rest (at most 2 times; then the architect cuts again or the task stops).
5. **Review, WP by WP.** After each WP a new reviewer, with the commit range of that WP, the plan path, the WP text, the baseline file and the checklist flag. `CHANGES` with blocking findings: a new developer gets the findings, then a new review; at most 2 rounds per WP. Non-blocking findings go to "Review notes" in the plan file. After the last WP, when there is more than one, one integration pass: a reviewer on `main...HEAD` (diff stat, full build and `ctest`, is the plan closed, are the docs and the entry consistent).
6. **Finish.** In the front matter of `todo/<id>.md`: `status: review <YYYY-MM-DD> branch todo/<branch-id>: <one line: what was done, how it was checked>`. If the slice does not close the entry, append `, slice K of M, after the merge set the status back to open`. The body of the entry stays. Run `check`; commit `Mark <title> ready for review` (with the Co-Authored-By line the conversation gives). The plan file stays in the store, and its path goes to the report.
7. **Next.** `git switch main` and go to step 1 until a stop condition. At the end `git switch <starting branch>`.

The edits to the front matter are the only edits you make to a file. Everything else is the agents'.

## Closing merged entries (`close`)

The owner starts it after merging (or answers "close" to the offer in step 1). It is the only mode that commits on `main`, runs `git rm` on an entry file and calls the tracker. No agents: you do it yourself.

1. Preconditions: `git status --porcelain` is empty, `engine/.venv/bin/python` exists. `git switch main`. Without Vikunja settings (`.env` or the environment), or when `VIKUNJA_PROJECT` is not set to this repository's own project (never `BagiEngine`: `push` closes the tracker tasks of every entry that is not in `todo/`, so it would close the engine's tasks), steps 5 and 6 are skipped and the report says so.
2. Find the entries: for each branch of `git branch --list 'todo/*' --merged main`, the entry in `parse` whose `status` starts with `review`, names that branch (`branch todo/<branch-id>`) and whose file still exists. An entry in another status (for example `in progress ... stopped`) is only named in the report. None found: say "nothing to close" and stop, changing nothing.
3. Per entry: if the status text has `slice K of M, after the merge set the status back to open`, remove the `status:` line from the front matter (keep `stage:`, `plan:`, `id:`; drop the `---` block when it is left empty) and leave the body alone. Otherwise `git rm todo/<file>`.
4. `todo_sync.py check`, then one commit on `main`: `Close <titles>` (with the Co-Authored-By line the conversation gives).
5. `todo_sync.py pull` (report only); show it.
6. `todo_sync.py pull --apply`, then `todo_sync.py push`. Stop at the first failing command and report it; no retries. Files that `pull --apply` deleted or adopted stay uncommitted: name them in the report for the owner to commit.

Never in `close`: git `push`, `fetch`, `branch -d` or `-D`, or any entry other than the ones found in step 2.

## Stopping a task (status stays `in progress`)

Commit what exists, set `status: in progress <date> branch todo/<branch-id>, stopped: <reason>`, `git switch main`, take the next entry. Reasons: `BLOCKED` from the architect; the same failure after 3 attempts; 2 review rounds without `APPROVE`; a WP not closed after 2 restarts of the developer; the work grew beyond the entry.

## Stopping the whole loop

No eligible entry; N slices done; the baseline on `main` is red; the working tree is dirty; the same environment failure (permissions, missing toolchain) on two tasks in a row.

## Forbidden (also written into the agents)

`git push`, `git fetch`, `--force`, `reset --hard`, `branch -D`, `clean`, `stash`, `--no-verify`; editing `main`; editing anything inside `engine/` (a submodule: another repository); editing outside the entry and its WPs; `git rm` of an entry file; any `todo_sync.py push` or `pull` (a normal run makes no network call and does not touch the tracker); putting Homebrew LLVM on `PATH` for `cmake --preset`. The exceptions are the `close` mode, started by the owner after a merge, and the `todo-merge` skill (merge into `main`, `branch -d` of the merged branch, only after the owner's yes), which runs the same closing steps: there the conductor may commit on `main`, `git rm` the entry files it found and run `pull` and `push`. The agents keep the whole list, always.

## Report

For the owner, in Russian, short: a table "entry, branch, commits, build/tests, reviewer verdict, plan path"; the skipped entries with reasons; what was not checked (platforms, manual items that the architect left for the owner, the checklist when it was unavailable). End with the owner's queue: `git branch --list 'todo/*' --no-merged main`, and the offer: run `/todo-merge` (report, ask, merge, close, delete the branch), or merge by hand and run `/todo-loop close`, which deletes the entry file (for a slice, sets the status back to `open`), then runs `pull --apply` and `push`. For `close` itself the report names the closed entries, the commit, the output of `pull` and `push`, and the files left uncommitted.
