---
paths:
  - "tools/ui_shots.py"
  - "tools/tests/test_ui_shots.py"
---

# Screenshots of the game for the todo loop

Read by `engine/tools/shots/shots.py` (`scenes`, `capture`, `diff`: `engine/tools/shots/CLAUDE.md`) and by the todo agents that take pictures of a slice. The command of the set is `tools/ui_shots.py` (its docstring has the arguments and the exit codes); it starts `BagiChess --control --headless --manual-clock` once per scene, drives the game over the control API and writes one small PNG per scene.

## Screenshots

```json
{
  "preset": "windows-clang-debug",
  "targets": [],
  "exe": "BagiChess",
  "command": ["{python}", "{root}/tools/ui_shots.py", "--exe", "{exe}", "--engine", "{engine}", "--label", "{label}", "--out", "{out}", "--scenes", "{scenes}", "--window", "{window}"],
  "scenes": {
    "start": "the start position of two people: the HUD, the coordinates and the empty margins",
    "middlegame": "a scripted game with a capture by each side: the captured material, the marks of the last move, the HUD",
    "opponent-panel": "the opponent panel open, set to Computer, the own engine, medium level, the person as White",
    "turned-board": "the board turned for a person who plays Black against the program as White",
    "checkmate": "a game won by a checkmate: the red king, the dimmed board, the bar and the HUD saying who won",
    "stalemate": "a game drawn by a stalemate: the gray bar of a draw"
  },
  "sizes": ["1920x1080"],
  "defaultSize": "1920x1080"
}
```

- **The preset** is the Windows one. On macOS pass `--preset macos-debug` to `shots.py capture`: the script takes the preset and the `.exe` suffix from the path of the executable it gets.
- **`targets` is empty on purpose:** `shots.py` runs a plain `cmake --build`, which a Ninja preset on Windows cannot do without the Visual Studio environment, so `ui_shots.py` builds `BagiChess` itself (through `engine/CI/scripts/vs_env.py` there). The picture is then of the build of the slice, not of a leftover.
- **What each scene is for.** `start` is the base of the HUD, the coordinates and the margins. `middlegame` shows the captured material in the left margin (one capture by each side), the marks of the last move and the HUD in the middle of a game. `opponent-panel` is the panel with its four groups, reached by clicking the buttons by name, so a renamed button stops the run. `turned-board` is the board seen from Black (the program plays White, nothing is searched). `checkmate` and `stalemate` are the two ways a game ends: the red king, the dim and the bar, and the gray bar of a draw. A scene reads back what it set up and fails with its name when the game disagrees.
- **One size.** A headless window has the size of the engine's `SDLWindowConfig.xml` (1920x1080) and the control API cannot change it (Chess may not ship a window config of its own, `ChessWebConfigTest`), so `sizes` has one entry and there is **no scene of a narrow window**. The control API has no click on a board square, so there is **no scene of the promotion panel** either. Both are small entries of their own (a start flag for the window size in the engine, a way to leave a promotion pending in the game).
- **What these pictures are not for.** A layout bug at a window shape, a cut word or a control outside the safe area is held by the size matrix of `ChessUiTest` (`tests/CLAUDE.md`), which checks every shape; the pictures show how a change looks at the one size, and the diff says where it moved.
