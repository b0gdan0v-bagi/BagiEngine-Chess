# Chess: even the Easy level of Stockfish is too strong for a casual player

**Where:** `src/Chess/Engine/OpponentLevels.{h,cpp}` (the table of strengths: `Skill Level` and a move time for Stockfish, `ownDepth` for the own engine; `LimitsOf`, `EngineOptionsOf`), `src/Chess/Engine/UciEngine.*` (the options sent at the handshake), `tests/OpponentLevelsTest.cpp` (pins the table).

**What is missing:** the owner played the game in the window against a real Stockfish (2026-11-05) and found that even `easy` is very strong. The numbers of the table (`easy`, `medium`, `hard`, `max`) were first guesses and have never been tuned by playing, so the bottom of the ladder is not beatable for a casual person. The own engine's levels (depth 1 / 3 / 4 / 5) have not been judged by a person either.

**A fix has to cover:**
- a really weak `easy` for Stockfish, using UCI options and limits only (`Skill Level`, `UCI_LimitStrength` / `UCI_Elo` if the installed Stockfish has them (its lowest Elo may still be too strong), a short `go movetime`, a depth or node limit); no Stockfish code, binary or text enters the repository (GPL);
- a sensible ladder for a casual person, possibly one more level at the bottom (the Level group is a `UISegmented` held by `ChessUiTest` on the size matrix and the narrowest window, so a new segment needs the width budget checked), with the ids `easy`, `medium`, `hard`, `max` of the control API kept valid;
- the numbers in one table, easy to tune, and the table pinned by `OpponentLevelsTest`; the owner confirms the real strength by playing.
