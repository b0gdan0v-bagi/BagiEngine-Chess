# Chess: a move that is the engine's best move is marked as a brilliant move

**Where:** `src/Chess/Game/ChessGameModel.*` (keeps the move history, `GetMoves()`, `GetStartFen()`), `src/Chess/Engine/` (no score from an engine yet: see `chess-analysis-hints`), `src/Chess/Components/ChessBoardComponent.cpp` and `ChessResultComponent` (the board and the result bar), `config/SceneConfig.xml`.

**What is missing:** the game never says whether a move was good. The owner asked (2026-11-05) for a "brilliant move" mark and said that a brilliant move is **the best move**: the move the engine picks as best in the position. There is no rating of moves at all: no best move to compare with, no evaluation before and after a move, and nowhere on the screen to show it. It needs the analysis of the entry `chess-analysis-hints` (a score for a position and the best line) and, for a review after the game, a move list on the screen (`chess-vs-engine`, which waits for the in-game UI of the engine).

**A fix has to cover:**

- the definition, as the owner gave it: a move is brilliant when it is the engine's best move in the position it was played in (the move equals the first move of the best line at the fixed depth, or, if two moves score the same, either of them: decide and write it down). The chess-site meaning (a good piece sacrifice, a move that is not obvious) is NOT asked for; the entry can add it later as a stricter mark. Whether a forced move (the only legal move) or an obvious recapture counts is left to the first slice, with a test that pins the choice. Other ratings of the same scale ("great", "inaccuracy", "mistake", "blunder") are the same computation with other thresholds and are not part of this entry unless the owner asks;
- how a move is rated: the best move of the position before the move is found (from `chess-analysis-hints`) and compared with the move played, at a fixed depth so that the own engine is deterministic and the tests are stable; which engine rates (the own one, the picked one, or Stockfish if found) and what is shown when none is available;
- when it runs: right after a move in the background (a live mark on the move), or in a review after the game that evaluates every position (`chess-vs-engine` item "review"); the search must not delay the opponent's move;
- how it is shown: a mark on the destination square of the last move or on a move list, with its own words/icon through the size matrix (the engine has no icon or sound control for it yet: the entry `in-game-ui` lists what is missing);
- the control API: the rating of each move in `get_state` (or a method), so the logic is testable with no window;
- tests on known positions: playing the engine's best move (a mate in one, a hanging queen taken) is marked, playing another move is not; the same result on the manual clock twice (own engine, fixed depth).

Depends on: `chess-analysis-hints` (the best move of a position). The definition is decided; a small question for the owner is left: whether a forced or obvious best move counts.
