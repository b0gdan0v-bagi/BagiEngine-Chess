# Chess: the engine analyses the position: an evaluation, a hint mode (best move on request)

**Where:** `src/Chess/Engine/ChessEngine.h` (`IChessEngine::FindMove` returns only a move, no score or lines), `src/Chess/Engine/UciEngine.cpp` and `UciProtocol.*` (`info` lines are skipped; `go depth`, `MultiPV` and `go infinite` are not used), `src/Chess/Engine/Search.*` (the own search computes a score and knows its best line but `OwnEngine` hands out only the move), `src/Chess/Engine/Evaluation.*`, `src/Chess/Components/` and `config/SceneConfig.xml` (no place for a hint or an evaluation), `src/Chess/ChessGameControl.*`.

**What is missing:** nothing in the game tells the person how the position stands or what the best move is. This is the analysis half of the old entry `chess-vs-engine` (its "A fix has to cover: analysis" bullet: an evaluation bar and a best-move hint after each move, lines with `MultiPV`, a review of the game); this entry is the first, bounded part of it, and the part that `chess-brilliant-moves` needs. The engine interface has no way to ask for a score or for several lines, and a search for a hint must not block the window or the game (`EngineOpponent` already shows how a search runs off the main thread and how a stale answer is dropped).

**A fix has to cover:**

- the engine interface: an analysis request next to `FindMove` that returns the best move with its score (centipawns or a mate distance) and optionally several lines (`MultiPV`), for both engines (Stockfish over UCI: read the `info` lines; the own engine: the score `Search` already has), with the same cancellation and error rules;
- a hint mode the person switches on and off in the opponent panel or a place of its own on the HUD: on request (a button) the best move is shown on the board (the two squares highlighted, or an arrow, which the board does not draw yet), never played; the hint is dropped when the position changes; a limit on how often it may be used is a product choice for the owner;
- an evaluation shown as a number or a bar for the position on the board, kept up to date in the background without making the opponent's own search wait, switchable off;
- what hint mode means in a game against the program (a hint is for the person's own turn) and in a game of two people;
- the control API (`get_state` or a new method) to read the evaluation and the hint, so the model and the smoke test can check it without a window;
- tests: the analysis against known positions (a mate in one gives a mate score and the mating move; a hanging queen), with the fake UCI engine for the `info` parsing and, where Stockfish is installed, with the real one; the HUD words through the size matrix; a stale hint after a move.

Not here: the review of a whole game and marking moves (`chess-brilliant-moves`), PGN, the move list (`chess-vs-engine`).
