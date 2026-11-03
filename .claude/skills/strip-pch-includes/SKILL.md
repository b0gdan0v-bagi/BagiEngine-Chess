---
name: strip-pch-includes
description: Remove redundant #include lines from BagiChess sources that are already in the engine's PCH. Use when cleaning includes, stripping PCH duplicates, or optimizing includes.
---

# Strip PCH Includes (BagiChess)

Removes `#include` lines that the precompiled header of the engine (`engine/src/Modules/BECore/pch.h`) already has. The script is the engine's (`engine/CI/scripts/strip_pch_includes.py`); `--root .` makes it work on this repository (the PCH is still the engine's).

## How to Run

From the repository root, with the engine venv:

```bash
# Dry run on all sources (report only)
engine/.venv/bin/python engine/CI/scripts/strip_pch_includes.py --dry-run --all --root .

# Only changed files
engine/.venv/bin/python engine/CI/scripts/strip_pch_includes.py --changed --root .

# One folder
engine/.venv/bin/python engine/CI/scripts/strip_pch_includes.py --all --root . --dir src/Chess/Game
```

Without `--root .` the script strips the engine's sources: never leave it out.

## Recommended Order

1. Dry-run first. 2. Apply to a bounded folder. 3. Build and run `ChessTests` for that scope. 4. Widen only after that passes.
