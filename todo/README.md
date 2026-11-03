# TODO

Known problems that are not being worked on yet. One file per problem: where it is, what is wrong, what a fix has to cover. A finished entry is deleted (`git rm`), not marked done.

`engine/tools/todo_sync/` reads this folder to build a progress dashboard and to keep a task tracker (Vikunja) in step with it (its `CLAUDE.md` has the commands). `roadmap.md`, if there is one, is the direction and the list of stages; it is not an entry. This file is not an entry either.

## Entry format

```
---
status: deferred 2026-10-31 free text
stage: 1
plan: magical-leaping-blossom.md
id: old-slug
---
# Title of the entry

**Where:** ...
**What is missing:** ...
**A fix has to cover:** ...
```

- The entry id is the file name without `.md`. The tracker task is found by it, so **renaming a file makes a new entry** for the tracker (the old task is closed, a new one is created); retitling an entry is safe. `id:` keeps an id that differs from the file name (only for a task that was exported before the file got its name).
- The title is the first `# ` line.
- The front matter is optional, and so is every key in it. A line is `key: value`, split on the first colon. An unknown or repeated key is an error. No front matter means an open entry with no stage and no plan.
- `status:` starts with one keyword, `open` (the default), `in progress`, `review` or `deferred`, then a date `YYYY-MM-DD` and free text. `review` is set only by the `todo-loop` skill, in the branch `todo/<id>` that works on the entry: the branch waits for the owner, who merges it; then `/todo-loop close` deletes the entry file (`git rm`), as for any finished entry, and syncs the tracker. A slice of a larger entry says so in the status text (`slice K of M, after the merge set the status back to open`).
- `stage:` is the roadmap number (1 to 6); a problem outside the roadmap order has none.
- `plan:` is the **name** of a plan file in the local plan store (`~/.claude/plans/`, where plan mode writes; no folder in it). Plans are not committed. The `- [ ]` / `- [x]` lines of the file give the progress.
- The summary on the dashboard and in the tracker comes from the `**What is missing**` / `**What is wrong**` paragraph.

## Branches

The `todo-loop` skill works one entry per local branch `todo/<id>` (`todo/<id>-2` for the next slice of a large entry), always from `main`. A branch `todo/<id>*` that is not merged into `main` means the entry is taken. The status of the entry changes only in that branch, so `main` and the other branches do not conflict. The queue for the owner: `git branch --list 'todo/*' --no-merged main`.

## Lifecycle of a plan

Write the plan in plan mode, link it with `plan: NAME.md`, run `todo-sync push` (the tracker task gets the plan's checklist, so the progress is visible from any machine), do the work, delete the plan file and the `plan:` line (the copy leaves the tracker at the next push), and delete the entry when the problem is solved. The tool says so when a linked plan has all its boxes ticked.

## Links

Link other entries with relative links, `[Test gaps and platforms](test-gaps-platforms.md)`, so a moved file shows up as a broken link.
