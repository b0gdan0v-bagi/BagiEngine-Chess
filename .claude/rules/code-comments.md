---
paths:
  - "src/**/*"
  - "tests/**/*"
  - "uci/**/*"
  - "tools/**/*"
---

# Comments in the code of the game

Read before writing or cutting a comment in `src/`, `tests/`, `uci/` or `tools/`.

## The rule

A comment says **why**, or states a **contract** (units, ownership, thread, lifetime, what the caller must hold), a non-obvious **invariant** or a **trap**. It never:

- restates the code, or repeats the value of a named constant;
- tells the history of a change or a measurement ("it was", "measured", "the first try", "no longer", a date): that goes to the commit or the slice plan;
- retells what a test checks: a test comment says why the case exists or what trap it sets;
- cites a `CLAUDE.md` section that does not exist: a citation is kept only when the heading is in the `CLAUDE.md` of the file's folder or of an ancestor (`grep -n '^#' <that file>`).

Doxygen on the public API stays, one or two sentences; `@param` and `@return` only when the name does not say it. Markers that tools read stay exactly as they are: `//-NoStd: <why>`, `NOLINT*`, `clang-format off` / `on`; a `//-NoStd:` or `NOLINTNEXTLINE` marker alone on its line stays on the line right before the code it covers (no comment or blank line between), and the checker fails when that gap changes. An owner's decision or a known deviation is not deleted from a comment unless a `CLAUDE.md` holds it.

## A comment-only stage

1. Read the `CLAUDE.md` of the folder first.
2. A fact that a cut comment carried, that no other doc carries and that a reader cannot get from the code goes into that `CLAUDE.md` in one sentence (`python engine/CI/scripts/check_doc_budget.py --root .` stays green); otherwise it is dropped.
3. A comment the code contradicts is fixed or removed, and the developer reports it as `stale: <file>:<line> <what>`.
4. Only comments change: no name, no include, no code line. Prove it with `python tools/check_comment_only.py --base <rev> --root . <paths>` (exit 0: C++ and Python files compared without comments and whitespace; a `//-NoStd:`, `NOLINT` or `clang-format off/on` marker is code to it), then build and run the tests.

## Temporary

This file and `tools/check_comment_only.py` are the game's own until `Update engine` brings the engine's rule ("Code style" of the engine's `CLAUDE.md`) and the engine's `CI/scripts/check_comment_only.py`. Then this file becomes a pointer to that rule, and the script goes when the engine's does the same.
