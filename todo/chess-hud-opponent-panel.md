---
status: review 2026-11-05 branch todo/chess-hud-opponent-panel: the mode, the color, the engine and the level are separate choice groups (UISegmented) in a modal Opponent panel opened by one HUD button, the three cycle buttons are gone; 586 tests green (528 before; the 4 real-Stockfish tests run and pass), after the owner's live test an unavailable engine is refused (nothing changes) and the Level and Color groups work in Two players mode, the panel through the size matrix, in HTML+CSS against the shipped XML, and a ui_click scenario in the control smoke on both clocks
---
# Chess HUD: the mode and the color of the opponent are one cycle button; they need separate controls

**Where:** `config/SceneConfig.xml` (the `Hud` node: the `Side`, `Engine` and `Level` cycle buttons), `src/Chess/Components/ChessBoardComponent.cpp` (the actions `ChessCycleSide`, `ChessCycleEngine`, `ChessCycleLevel`), `src/Chess/Engine/OpponentController.h` (`CycleSide`, `CycleEngine`, `CycleLevel`; `SetSide`, `SetEngine`, `SetLevel` already take a choice directly), `src/Chess/Engine/OpponentText.h` (`SideLabel`), `tests/ChessUiTest.cpp` and `tests/ChessHudWebTest.cpp` (the HUD layout through the size matrix).

**What is missing:**

1. The owner finds the `Side` button bad UX (2026-11-04): one press changes two different things, who plays (two people or the computer) and which color the person has, and cycles "Two players" -> "You: White" -> "You: Black" so a state cannot be chosen directly. The same holds, to a lesser degree, for the `Engine` and `Level` cycle buttons.
2. The cycle buttons were a stopgap because the engine has no toggle, segmented control or dropdown (engine entries `in-game-ui` and `ui-authoring`), and the margins of the board are full at the narrowest window (the width budget in `CLAUDE.md`, "The HUD"), so a second row of controls does not fit.
3. The controller can already take a direct choice (`SetSide(side or nothing)`, `SetEngine`, `SetLevel`, `Choose`) and so can the control API (`set_state` with `opponent`), so the work is in the HUD, the actions and the words, not in the opponent.

**A fix has to cover:**

- the mode ("Two players" or "Computer") and the color ("Play as White" or "Black") as separate choices, each shown with its current value and each reachable in one press;
- a place for them that keeps the touch size (56-unit controls) and the layout rules of the HUD: probably a small new-game panel opened by one button (opponent, color, engine, level as segmented controls built from buttons), or the engine's own selector once it exists; the panel must also fit the narrowest window and the five window shapes of `ChessUiTest`;
- the `Engine` button of the `chess-vs-engine` entry moves to the same place, and the bottom row of the board keeps only the status;
- the layout tests (`ChessUiTest`, `ChessHudWebTest`) and the words (`HudWords.h`) follow, with every word of the new controls in the size matrix;
- the control API (`opponent.plays`, `engine`, `level`) stays as it is.

A decision for the owner before work starts: a panel built from plain buttons now, or wait for the engine's selector widgets (`in-game-ui`, `ui-authoring`).
