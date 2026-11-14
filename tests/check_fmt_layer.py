#!/usr/bin/env python3
"""Check that the game writes no `fmt::` of its own: it formats through the engine's layer (`BECore::Format` and its companions).

    python tests/check_fmt_layer.py [--engine engine]

ctest runs it as BagiChessFmtLayerCheck (CMakeLists.txt). It runs `tools/codemod/codemod.py --family fmt --fail-on-change --report` of the engine
over `src`, `tests` and `uci`. The check fails on a call the tool would rewrite and on one it reports and leaves (`fmt::join`, an
`#include <fmt/...>`, `using namespace fmt`): a real boundary is marked `//-NoStd: <why>` and the tool then ignores the line.
Python 3, standard library only. Exit code: 0 passed, 1 failed, 77 skipped (the engine's codemod is not there).
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

SKIP_EXIT_CODE = 77
GAME_ROOT = Path(__file__).resolve().parent.parent
ROOTS = ("src", "tests", "uci")
SUMMARY = re.compile(r"would change (\d+) files, (\d+) replacements; (\d+) notes")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--engine", default="engine", help="the engine folder, relative to the game root")
    args = parser.parse_args()

    tool = GAME_ROOT / args.engine / "tools" / "codemod" / "codemod.py"
    if not tool.is_file():
        print(f"skipped: {tool} is not there")
        return SKIP_EXIT_CODE

    failed = False
    for root in ROOTS:
        folder = GAME_ROOT / root
        if not folder.is_dir():
            continue
        result = subprocess.run(
            [sys.executable, str(tool), "--root", str(folder), "--family", "fmt", "--fail-on-change", "--report"],
            capture_output=True, text=True, check=False)
        match = SUMMARY.search(result.stdout)
        clean = result.returncode == 0 and match is not None and match.group(3) == "0"
        print(f"{root}: {'ok' if clean else 'FAILED'}")
        if not clean:
            failed = True
            print(result.stdout + result.stderr)
    if failed:
        print("the game formats through BECore::Format and its companions, not fmt:: (engine/src/Modules/BECore/Format/CLAUDE.md); "
              "a real boundary is marked //-NoStd: <why>")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
