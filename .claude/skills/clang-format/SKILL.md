---
name: clang-format
description: Apply the project .clang-format to BagiChess sources. Use when the user asks to format code, apply style, or run clang-format.
---

# Clang-Format (BagiChess)

Applies the root [.clang-format](../../../.clang-format) to the C/C++ sources of this repository. The script is the engine's (`engine/CI/scripts/format_sources.py`); `--root .` makes it work on this repository instead of the engine. It reads [.clang-format-ignore](../../../.clang-format-ignore) of this repository (the engine and `build/` are excluded).

## How to Run

From the repository root, with the engine venv (`CI/setup_venv.*` creates it; on Windows `engine\.venv\Scripts\python`):

```bash
# Only files with uncommitted changes
engine/.venv/bin/python engine/CI/scripts/format_sources.py --changed --root .

# Everything under src/, config/ and tests/
engine/.venv/bin/python engine/CI/scripts/format_sources.py --all --root .
```

Without `--root .` the script formats the engine: never leave it out.

## Requirements

`clang-format` is looked up in `PATH`, then `LIBCLANG_PATH`, then the engine venv (the pip package), then Homebrew LLVM and Xcode on macOS. The script prints which binary and version it uses.

## Safe Workflow

- Prefer `--changed`; use `--all` only for an intentional mass pass, in a commit of its own.
- Check `git status` after `--all`: files you did not change should not move.
