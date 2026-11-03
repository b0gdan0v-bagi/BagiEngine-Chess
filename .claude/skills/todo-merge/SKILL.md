---
name: todo-merge
description: Merges a reviewed todo/<id> branch (left by todo-loop in status review) into main. Prints a short report of what was done, asks the owner whether to merge, and only after a yes merges with --no-ff, closes the entry (as `/todo-loop close` does) and deletes the branch. Use when the user asks to merge a todo branch, accept the result of the todo loop, says "смержи ветку", "todo-merge", or wants a report before merging a todo branch.
---

# todo-merge (BagiEngine-Chess)

You are the **conductor** and do everything yourself, no agents. The skill takes one branch `todo/<branch-id>` that `todo-loop` left in status `review`, shows the owner what it holds, **asks**, and only then touches `main`. The entry format is `todo/README.md`; the tool is `engine/tools/todo_sync/CLAUDE.md`; the closing steps are in `.claude/skills/todo-loop/SKILL.md` ("Closing merged entries").

`/todo-merge [<id or branch>]`

Before the answer of step 5 you change nothing: only reads, `git switch main` and `git merge-tree`.

## Steps

1. **Preconditions.** `git status --porcelain` is empty and `engine/.venv/bin/python` exists; otherwise stop with a message. Remember the starting branch. Never `fetch` and never `push`.
2. **Choose the branch.** `git switch main`. Run `engine/.venv/bin/python engine/tools/todo_sync/todo_sync.py parse`. Candidates: branches of `git branch --list 'todo/*' --no-merged main` whose entry has a `status` that starts with `review` and names that branch (`branch todo/<branch-id>`). Unmerged branches that are not candidates (for example `in progress ... stopped`) are only named, with the status.
   - With an argument (`<id>` or `todo/<branch-id>`): take that branch. If it is not a candidate, stop and say why: no such branch, already merged into `main`, or the status is not `review`.
   - Without an argument: one candidate is taken as is; several candidates go to the owner in one AskUserQuestion (title, branch, the one line of the status); none: say "nothing to merge" and stop.
3. **Dry check.** `git merge-tree --write-tree main <branch>`: a non-zero exit code means conflicts, so report the files and stop (the skill never resolves conflicts). Compare `git merge-base main <branch>` with the tip of `main`: if they differ, "main moved since the branch was cut" is a flag for the report and for step 6.
4. **Report.** In Russian, short, printed before the question:
   - the title of the entry and the branch;
   - if the status text has `slice K of M, after the merge set the status back to open`: a warning that the merge does not finish the entry (closing sets it back to `open` instead of deleting it);
   - what was done and how it was checked: the one line from `status: review ...`;
   - `git log --oneline main..<branch>` and `git diff --stat main...<branch>`, plus two to four lines of your own on what changed, from the commit subjects and a look at the diff of the non-test files;
   - from the slice plan `~/.claude/plans/todo-loop-<branch-id>.md`, if it exists: "Review notes" (non-blocking findings) and what the architect left for the owner to check by hand;
   - the `merge-tree` result (clean) and the "main moved" flag.
5. **Ask.** One AskUserQuestion: `Merge + close + delete branch`, `Merge only (no close, the branch stays)`, `No`. `No` ends the skill with nothing changed.
6. **Merge.** `git merge --no-ff -m "Merge branch '<branch>'"` with the Co-Authored-By line the conversation gives (the history of `main` uses this form). If the merge fails for any reason: `git merge --abort`, report, stop. If `main` had moved (step 3), also build with a preset of `CMakePresets.json` and run `ctest --preset <p>` on the result; red tests: stop before step 7, name the failing tests, leave the merge commit (it is local; no reset, the owner decides).
7. **Close** (only for the full answer). Do steps 1 to 6 of "Closing merged entries" in `todo-loop/SKILL.md`, as written there. Narrowing: only the entry of this branch; other entries that its step 2 would find are named in the report and left alone. Without Vikunja settings, or when `VIKUNJA_PROJECT` is not this repository's own project (never `BagiEngine`, see `todo-loop/SKILL.md`, "Closing merged entries"), the steps for `pull` and `push` are skipped and the report says so.
8. **Delete the branch** (only for the full answer, after step 7, because closing finds entries through `git branch --merged main`): `git branch -d <branch>`. On failure report it and leave the branch.
9. **Report the result.** In Russian: the merge commit, the close commit, the output of `pull` and `push`, files left uncommitted by `pull --apply` (the owner commits them), whether the branch was deleted, whether build and tests ran. End by saying that `git push` of `main` is the owner's.

## Forbidden

`git push`, `fetch`, `--force`, `reset --hard`, `branch -D`, `clean`, `stash`, `--no-verify`; resolving conflicts; editing code or any file other than what step 7 edits; merging a branch that is not in status `review`; any change before the answer of step 5.
