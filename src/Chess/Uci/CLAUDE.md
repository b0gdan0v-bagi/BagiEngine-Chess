# Chess UCI server (`src/Chess/Uci/`)

Part of the Chess documentation, split out of the root `CLAUDE.md`. The own engine as a UCI program: the executable `BagiChessUci` speaks the **server** side of UCI on stdin / stdout, so that a tournament tool (cutechess-cli, fastchess) or a script can play it against another engine. Namespace `BECore::Chess::Uci`. The **client** side (the game talking to Stockfish) is `Engine/UciProtocol.*`, namespace `UciProtocol`; the two are not mixed. `Uci/` depends on `Game/` and `Engine/` (it calls `IChessEngine` and nothing else of the engine); `ChessGame`, `Components/` and the files of `Engine/` do not know it. `ChessModule` globs `src/**`, so these files are in the game's library; `BagiChess` does not call them.

## Files

| File | What it guarantees |
|------|--------------------|
| `UciCommand.{h,cpp}` | pure. `ParseCommand(line)` turns a line of the GUI into a `UciCommand` (`UciCommandKind`, `GoParams`, `PositionParams`, `SetOptionParams`). Tolerant: spaces at both ends and a trailing `\r` ignored (`UciProtocol::TrimLine`); a first token that is no command, an empty line and a `position` with neither `startpos` nor `fen` are `Unknown`; unknown tokens inside `go` (`searchmoves`, `nodes`, ...) are skipped; a number that is missing or not a number leaves its field empty, a negative one is kept as given. The moves of a `position` are text: the rules are not known here. `ParseInt` is whole-text only |
| `UciClock.{h,cpp}` | pure. `MoveTimeFor(remainingMs, incrementMs, movesToGo)`, integer arithmetic only: `remaining / (movestogo or 30) + 3/4 of the increment`, never more than **half the remaining time** and never more than **remaining minus 100 ms**, never less than 1 ms (a clock at 0 or below gives 1). `movestogo` 0 or less is absent, a negative increment is 0. The constants are named in the header |
| `UciServer.{h,cpp}` | the state machine over an injected `IChessEngine` and a sink (one line, no end of line). `HandleLine` handles one line; a `go` returns at once and the search runs on a worker `std::thread`, so the reader goes on with `isready`, `stop` and `quit`. State: the game (start FEN and moves, a `SearchRequest`), the options, the worker and its token. Every `go` is answered by exactly one `bestmove` |
| `../../uci/main.cpp` | the program: builds a `UciServer` over `OwnEngine`, reads `std::getline(std::cin)`, hands each line to the server, writes each reply line with `\n` and a flush. The end of the input is a `quit` |

`main` is in `uci/`, **outside `src/`**, because the module globs `src/*.cpp` and a second `main` would collide with the engine's. The executable is always built (`CMakeLists.txt`, linked like `ChessTests` minus gtest); it has no window, no config and no engine services.

## Commands

| Command | Answer / effect |
|---------|-----------------|
| `uci` | `id name BagiChess Own`, `id author BagiEngine-Chess`, the three options, `uciok` |
| `isready` | `readyok`, also while a search runs |
| `ucinewgame` | forgets the position (the options stay) |
| `setoption name <name> value <v>` | see Options. A name with spaces is read up to the word `value` |
| `position startpos \| fen <fen> [moves ...]` | checked with `SearchRequest::FinalPosition()` before it replaces the old one: a broken FEN or an illegal move is `info string position refused: ...` and the old position stays |
| `go` | `depth`, `movetime`, `wtime`, `btime`, `winc`, `binc`, `movestogo`, `infinite`, `ponder`; see the search below. Other tokens are skipped |
| `stop` | cancels the search and **waits** for its `bestmove` (synchronous), see below |
| `quit` | cancels, waits, `WantsQuit()` becomes true, the program exits with code 0 without waiting for a long search |
| `ponderhit`, anything else | ignored |

While a search runs, `position`, `ucinewgame`, `setoption` and a second `go` are ignored with an `info string` line. The reply to `stop` or `quit` is complete when `HandleLine` returns, so a `position` and `go` sent right behind a `stop`, before the GUI has read the `bestmove`, find an idle server. The flag "searching" is cleared **before** the `bestmove` line is written, for the same reason.

## Options

| Option | Type | Meaning |
|--------|------|---------|
| `Depth` | spin 0..64, default 0 | the search depth; 0 is no limit from the option |
| `BlunderPercent` | spin 0..100, default 0 | the share of moves replaced by a random legal move (`BlunderEngine`, the handicap of the game's low levels) |
| `Level` | combo `custom` (default), `easy`, `medium`, `hard`, `max` (the ids of `GetOpponentLevels()`) | picking a level sets `Depth` to its `ownDepth` and `BlunderPercent` to its `blunderPercent`; `custom` changes nothing. The later `setoption` of either of the two wins. Only these two numbers are taken: the game's move time cap `kOwnMoveTimeCapMs` is **not** applied |

Names and the combo values are matched case-insensitively. An unknown name, a value that is not a number or is out of range, an unknown level: `info string option refused: ...` and nothing changes.

The engine that answers is `BlunderEngine(engine, BlunderPercent)`, made for each `go`, so a level is played exactly as the game plays it.

## The search and `stop`

- **Bounded `go`** (a depth, `movetime` or a clock): **one** `FindMove` call. The depth is the smaller of `go depth` and the `Depth` option (when it is above 0); the time is `go movetime`, else `MoveTimeFor()` of the clock of the side to move (the side comes from the final position). Depth and time given together stop at the earlier. The move is the one the game would play with those limits.
- **Deepening driver:** `go infinite`, `go ponder`, and a `go` with no depth and no time (also `go depth 0` / `go movetime 0` with no clock) run `Analyse` at depth 1, 2, 3, ... with one token, up to `kMaxSearchDepth` (64). It ends early on a mate score (so a mate is answered by itself, where Stockfish would wait for `stop`) or when there is no move. `Analyse` **bypasses the handicap**: `go infinite` plays at full strength whatever `Level` is.
- **`stop`** cancels the token (`OwnEngine` drops a cancelled answer, so there is no best-so-far inside it). The answer is the move of the **last depth that finished** for the driver; for a bounded `go`, or a driver stopped before depth 1 finished, it is the move of a search of depth 1 (through `FindMove`, so it may be a blunder at `BlunderPercent` above 0). `stop` is for forfeits and analysis; a tournament tool does not use it in normal play.
- **The injected `IChessEngine` must honour the cancellation token.** `stop` and `quit` wait for the worker; an engine that ignores the token blocks the reader. `OwnEngine` honours it.
- **Errors:** an `EngineError` (not `Cancelled`, not `NoMove`) is `info string <detail>` followed by a `bestmove` of the first legal move, because a GUI must always get a `bestmove`. No legal move (mate, stalemate) is `bestmove (none)`. A promotion is `e7e8q`.
- **Not supported:** pondering (no `Ponder` option, no `ponder` move in a `bestmove`, `ponderhit` ignored), `info depth/score/pv` lines (`Analyse` bypasses the handicap, so a score line would need a second search), `nodes`, `searchmoves`, `UCI_Elo`. The server writes `info string` only, for errors and refusals.

## Nothing but UCI on stdout

A stray log line would break a tournament tool. Every line `BagiChessUci` writes to stdout starts with `id`, `option`, `uciok`, `readyok`, `bestmove` or `info` (the smoke checks it for every line of every run); diagnostics go to stderr or to `info string`. The sink is called under a mutex, because the worker and the reader both write. `HandleLine` is for one thread.

## Running it

```bash
cmake --build --preset macos-debug --target BagiChessUci
printf 'uci\nisready\nposition startpos moves e2e4\ngo depth 2\nquit\n' | build/ClangmacOS/BagiChessUci
```

prints

```
id name BagiChess Own
id author BagiEngine-Chess
option name Depth type spin default 0 min 0 max 64
option name BlunderPercent type spin default 0 min 0 max 100
option name Level type combo default custom var custom var easy var medium var hard var max
uciok
readyok
bestmove b8c6
```

In a tournament tool: the command is the path of `BagiChessUci`, protocol `uci`, with `option.Level=hard` or `option.Depth=3` as the strength.

## Tests

`tests/UciCommandTest.cpp` and `tests/UciClockTest.cpp` (pure), `tests/UciServerTest.cpp` (the server over `OwnEngine` and a gated fake engine defined in the file; the `bestmove` is read with `UciProtocol::ParseBestMove`, so the two sides of the protocol check each other), and the ctest `BagiChessUciSmoke` (`tests/uci_smoke.py`), which drives the real executable over pipes. Rows in `tests/CLAUDE.md`.

## What comes next

This folder is slice 1 of the measurement of the own engine. The measuring script (a match against Stockfish at fixed `UCI_Elo` steps, an Elo with an error bar, results recorded) and the README section for it are the next slice. The ladder (`OpponentLevels`, `ownDepth`) is the owner's to retune after that measurement; nothing here changes it. Nothing of Stockfish is shipped.
