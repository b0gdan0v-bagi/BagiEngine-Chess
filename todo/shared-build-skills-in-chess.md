# Chess: the build, clang-format and strip-pch-includes skills are still copies; switch to the shared text in `engine/`

**Where:** `.claude/skills/{build,clang-format,strip-pch-includes}/` (real folders, the old copies), `.claude/rules/engine-and-todo.md` (the bullet "Skills and agents are one copy"), `README.md` ("Build and test"), `CLAUDE.md`, `engine/.claude/rules/build-project.md` (the template to copy), `.clang-format-ignore`.

**What is missing:** the engine half of the engine entry `shared-build-format-skills` is merged (slice 1 of 2): `engine/.claude/skills/{build,clang-format,strip-pch-includes}` are now one generic text that starts with "read `.claude/rules/build-project.md` of the repository you run in", and the engine has its own `engine/.claude/rules/build-project.md`. This repository still has its own copies of the three skills, so every fix has to be made twice and they drift. They stay as they are until this entry is done: a shared skill without the game's `.claude/rules/build-project.md` has no values to read. The `todo-*` skills are already symlinks.

**A fix has to cover** (one commit, in this order, so the shared skills never run without their rules file):

1. Create `.claude/rules/build-project.md` of the game, with every field of the engine's file (`engine/.claude/rules/build-project.md` is the template; no `paths:` frontmatter). Values:
   - Product `BagiChess`; Tests `ChessTests` (the argument `tests` builds `--target ChessTests`); Default build: every target; Source paths `src tests`;
   - Extra configure triggers: the submodule pointer `engine` moved; `CMakePresets.json` or a `*.cmake` changed;
   - Extra failure hints: a missing `engine/external/*` means `git -C engine submodule update --init`; a missing `jinja2` means `CI/setup_venv.*`;
   - Extra gotchas: the build never edits `engine/` (a missing engine feature is `needs engine change`); executables `build/{Clang|MSVC}{macOS|Windows}/BagiChess[.exe]` and `ChessTests`; Test run: `ctest --preset <p>` runs `ChessTests` and `BagiChessControlSmoke`;
   - Format: Python `python3` (Windows: `engine\.venv\Scripts\python`); `<engine>` = `engine`, `<R>` = `.`; `--root .` is never left out (without it the scripts format the engine); Format scope `src/`, `config/`, `tests/`; Ignore file: the game's `.clang-format-ignore` excludes `engine/`, `build/`, `.venv/`; Launcher: none;
   - Strip PCH includes: PCH `engine/src/Modules/BECore/pch.h`; Strip example dir `src/Chess/Game`; After a pass: build and run `ChessTests`.
2. Replace the three copies with symlinks, as the `todo-*` ones: `git rm -r .claude/skills/{build,clang-format,strip-pch-includes}`, then `ln -s ../../engine/.claude/skills/<name> .claude/skills/<name>` for the three. Before the removal compare the old game copies with the shared text and the new rules file for a fact that has no field (the old copies are in git history).
3. `README.md` ("Build and test") against the shared `build` skill: the commands (`cmake --preset macos-debug`, `--build`, `ctest`, the `PATH` with `engine/.venv/bin` for jinja2) must agree; fix the drift on the side of the one that is wrong.
4. `CLAUDE.md` of the game: nothing may name the old copies.
5. `.claude/rules/engine-and-todo.md`: the bullet "Skills and agents are one copy" loses the sentence "`build`, `clang-format` and `strip-pch-includes` are still copies" and lists the three in the symlinked set; name `.claude/rules/build-project.md` next to `todo-project.md` as the place of the differences.
6. Dry checks that need no session: `python3 engine/CI/scripts/strip_pch_includes.py --dry-run --all --root . --dir src/Chess/Game` and `python3 engine/CI/scripts/format_sources.py --help` exit 0; the build and `ctest --preset <p>` are green.

**Left to the owner, not for the loop:** in a fresh Claude Code session in this checkout, `/build`, `/clang-format` and `/strip-pch-includes` must be found through the symlinks and must read `.claude/rules/build-project.md` (the `todo-*` skills already work this way, which is a good sign). The same check in the `engine/` repository alone. If a session does not follow a symlinked skill folder, keep the three as copies and say so in `engine-and-todo.md`. The loop does steps 1 to 6 and stops at `review`; the owner does this check before the merge.

Related: the engine entry `shared-build-format-skills` (its body still describes both halves; this entry is the game half of it; the plan of the engine slice is `~/.claude/plans/todo-loop-shared-build-format-skills.md`, section "Left for the owner").
