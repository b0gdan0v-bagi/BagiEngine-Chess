# Chess Module Guidelines

## Overview

`ChessGame` is an `IGame` (name `Chess`, scene `ChessScene`). Executable `BagiChess`. Infrastructure and how a game is registered: `engine/src/Modules/Games/CLAUDE.md`. Namespace `BECore::Chess`. The game's content is in `config/` (`ApplicationConfig.xml`, `SceneConfig.xml`, `TextureLibrary.xml`, `ChessSettings.xml`) and `assets/`. `BagiChess` starts in Game mode (`mode="Game"` in its `ApplicationConfig.xml`, no editor); set `mode="Editor"` to open it in the scene editor.

## Layout

```
src/Chess/
├── ChessGame.{h,cpp}         IGame; owns the shared ChessGameModel, the OpponentController, the OpponentChoice over it and the control adapter; Start() resets the model, loads OpponentSettings, starts with nobody as the opponent and Stockfish as the engine and forgets the person's color (White), Reset() resets the model, Stop() destroys the opponent; GetActive() / GetActiveModel() / GetActiveOpponent() / GetActiveChoice() are how components reach them
├── ChessGameControl.{h,cpp}  IControllableGame: the state, the legal moves, a way to play one, to load a FEN and to pick the computer opponent for the control API (below); holds the model and the `OpponentController`
├── Game/                     plain C++ rules, no scene or renderer (src/Chess/Game/CLAUDE.md)
│   ├── ChessTypes.h, Bitboard.h, Move.h, Attacks.*   squares, pieces, bitboards, moves, attack tables and magics
│   ├── Position.*, MoveGenerator.*, GameStatus.*      position and FEN, legal moves and UCI, mate / stalemate / draws
│   ├── ChessGameModel.*      the state of the game: selection, moves, history, version, piece handles, pending promotion
│   └── StatusText.*, BoardOrientation.h, BoardCoordinates.h, PromotionChoices.*, CapturedMaterial.*, MaterialLayout.h   pure helpers of the UI
├── Engine/                   the computer opponent (src/Chess/Engine/CLAUDE.md)
│   ├── ChessEngine.h, Analysis.*, SearchRequest.*     IChessEngine (FindMove, Analyse), the request and the analysis
│   ├── UciProtocol.*, UciProcess.*, UciEngine.*       Stockfish over UCI in a child process (POSIX)
│   ├── Evaluation.*, Search.*, TranspositionTable.*, OwnEngine.*            the engine of our own
│   ├── BlunderEngine.*, OpponentLevels.*, OpponentEngines.*, OpponentText.*, OpponentSettings.*   handicap, levels, engines, words, settings
│   └── EngineOpponent.*, OpponentController.*, OpponentChoice.*   the opponent in a game, its seat, the person's choice
└── Components/               scene glue: board, pieces, result, captured material (src/Chess/Components/CLAUDE.md)
```

- State lives in `ChessGameModel`. Components never keep game state: they reach the model through `ChessGame::GetActiveModel()` (null when Chess is not the active game, and then they do nothing).

**Where the details are** (each file loads when a session works in its folder; read the matching one before changing code there):

| File | Holds |
|------|-------|
| `src/Chess/Game/CLAUDE.md` | every file of the rules and what it guarantees |
| `src/Chess/Engine/CLAUDE.md` | the opponent: the engine interface, UCI and Stockfish, the own engine, the levels and the handicap, the settings file, the controller and the choice, the opponent in the running game |
| `src/Chess/Components/CLAUDE.md` | the scene and the UI: the pieces, the board geometry and orientation, the coordinates, the captured material, the HUD, the opponent panel, the promotion picker, the words of the HUD, the draw order; **read it before changing `config/SceneConfig.xml`** |
| `tests/CLAUDE.md` | every test file and what it covers, the control smoke, the fake UCI engine, the test helpers |

## Control API

`ChessGame::AsControllable()` hands out `ChessGameControl` (`engine/src/Modules/Games/CLAUDE.md` for the methods and the rules of an adapter). The model and the opponent controller belong to the game, so the adapter holds references and has no `no_active_model`. The wire format is in the header comment of `ChessGameControl.h`; in short:

| Method | What it does |
|--------|--------------|
| `get_state` | `{fen, board, turn, status, winner, inCheck, legalMoveCount, startFen, moves, opponent, material}`: `board` is 8 strings, rank 8 first, white upper case, `.` empty; `status` is `ongoing`, `checkmate`, `stalemate`, `draw_insufficient_material`, `draw_threefold` or `draw_fifty_moves`; `startFen` plus `moves` (UCI) rebuild the game with its repetitions; `material` is `{balance, capturedByWhite, capturedByBlack}` (below); `opponent` is `{plays, level, engine, thinking, error}`: `plays` the color the **program** plays (`"white"`, `"black"`, or `null` for two people, the start of every game), `level` the id of the strength (`easy`, `medium`, `hard`, `max`; the one that would be used, also while `plays` is null), `engine` the id of the program (`stockfish`, the start of every game, or `own`; also the one that would be used while `plays` is null), `thinking` whether a search runs, `error` `null` or `{kind, detail}` (`kind` in snake_case: `launch_failed`, `timeout`, `process_died`, `protocol_error`, `illegal_move`, `no_move`, `bad_request`, `cancelled`, `unsupported`; `detail` the sentence of the log). A pick of Stockfish succeeds whenever a Stockfish file is found, so a launch that fails shows only at the first search, as `error` `launch_failed`; a pick of `own` always succeeds |
| `list_actions` | `{"type": "move", "uci": "e2e4"}` for every legal move (a promotion is four); empty once the game is over |
| `perform_action` | plays one of them and answers `{move, status, turn, winner, inCheck}`; errors `illegal_action`, `promotion_required` (`e7e8` without the piece), `game_over`, `invalid_params` (not a UCI move) |
| `reset` | the opening position; takes no options; **keeps the opponent** (side, level and engine; a standing error goes by the version rule) |
| `set_state` | `{"fen": ".."}` and/or `{"opponent": {"plays": "white"\|"black"\|null, "level": "easy"\|"medium"\|"hard"\|"max", "engine": "stockfish"\|"own"}}` (inside `opponent` a missing member means unchanged; `{}` changes nothing). A FEN starts a game from any position `FromFen` accepts, finished ones included (the pieces in the scene are dealt out again, `SetPosition`). Neither of the two, a side that is not `white` / `black` / null, an unknown level, an engine that is not exactly `stockfish` or `own` (`"Own"`, `""`, null, a number), a member of the wrong type: `invalid_params`, and nothing is changed (everything is checked first). The opponent is applied first, then the position. No engine for the pick (no Stockfish found; only Stockfish can fail): `engine_unavailable` with the engine's sentence; the pick is **refused as a whole**: the position is **not** replaced and the opponent is **unchanged** (side, level, engine and a running search stay as they were, also when the program played before: a client that asks for stockfish while `own` plays as black gets `engine_unavailable` and `own` still plays black; the level of a refused pick is not saved either, so `{"opponent": {"engine": "own", "plays": "black"}}` is the way on from two people) and `opponent.error` has the reason (until the version moves by someone else or the next pick; before this change the program played nobody after a refusal). A pick that changes nothing does nothing (no restart, no new version); `plays: null` clears an error |

- `material` is `{"balance": int, "capturedByWhite": {"queen", "rook", "bishop", "knight", "pawn"}, "capturedByBlack": {...}}`, a counts-per-type object for each side. `balance` is White's points minus Black's on the board (positive: White is ahead; a piece made by a promotion counts, so it is not "points taken"); `capturedByWhite` is the Black pieces that are gone and `capturedByBlack` the White ones (`Game/CapturedMaterial.h`, position-only: see its line in the layout). **It is derived:** computed from the model's position at every `get_state` call, with no state, no version of its own and no member in `set_state`; the state version already moves with every move, reset and `set_state` that can change it. Decided (the entry left it open): yes, because it is a pure function of the position, costs one JSON member and lets the headless smoke check the feature without a screenshot.
- The state version is `ChessGameModel::GetVersion()` plus `OpponentController::GetRevision()`. It grows on a played move, a reset, a `set_state`, a pick of the side, level or engine and an error that appears or goes, not on a selection or on a search that starts or ends (`thinking`). A person's move by mouse and the program's move grow it too, so `state_changed` reports them. It never goes back.
- `list_actions` is unchanged by the opponent: every action is a move, which `tools/stockfish_bridge.py` relies on. `perform_action` is not blocked for the program's side: a controller may play for it (a search that this makes stale is dropped by the version); only the mouse is kept from the program's side.
- The scene follows the model: a move, a promotion, a reset and a `set_state` all show in a `screenshot`, which is how a headless run is checked by eye.

## Rules

Standard chess, Chess960 excluded: legal moves only (pins, check, castling, en passant, promotion), checkmate, stalemate and the draws that end the game by themselves: insufficient material, threefold repetition and 50 moves without a capture or a pawn move. FIDE makes a player claim the last two (it ends a game by itself only at fivefold and 75 moves), but the window has no way to claim, so they apply automatically. Both thresholds are named constants in `GameStatus.cpp`.

- A move is a `Move` from `MoveGenerator::GenerateLegal()`; `ChessGameModel::TryMove()` accepts nothing else and answers `std::expected<void, MoveError>` (`GameOver`, `IllegalMove`). `Position::IsLegal()` assumes a pseudo-legal move from the generator (while in check it relies on the generator having restricted the targets), so do not call it with a hand-made move: parse text with `MoveGenerator::ParseUci()`.
- Castling is the king's move (`e1g1`), as in UCI. A promoting pawn has four moves per target, queen first; a click on the target leaves the promotion pending in the model (`ChessGameModel::HasPendingPromotion()`) and `ChoosePromotion(piece)` plays it (the window's modal `PromotionPanel` makes that call, see "The promotion picker"); `TryMove` / UCI (`e7e8n`) name the piece themselves, as the program and the control API do.
- `Position` keeps the en passant square only when a pawn can capture onto it (so positions that are equal for the rules have equal keys and FEN). `FromFen` rejects positions the rules cannot reach in the ways that would break the lookups: a king count other than one, a pawn on a back rank, more than 16 pieces of one color (the scene has 16 piece nodes per color, so any accepted position can be shown), the side that just moved in check, castling rights without the king and rook in place.
- A finished game accepts no move. The window shows the end with the red king (bright on check, dark on mate), the dimmed board and a bar across it (white, black or gray for a draw); the HUD says who won and why ("White wins", "Checkmate"), and the reason also goes to the log. A click on the board after the end, or the Reset button, starts a new game.
- The magic bitboards are searched for on first use (about 90 ms in a Debug build, measured on macOS) with a fixed seed, every candidate checked against the slow ray walk. If that becomes too slow, print the found numbers once and embed them as constants.

New rule code goes into `Game/` with a gtest case; do not patch around the generator in the model or the components.

## Tests

`tests/`, executable `ChessTests`, plus the control smoke `BagiChessControlSmoke` (`tests/control_smoke.py`) under ctest. What each test file covers, the fake UCI engine and the helpers are in `tests/CLAUDE.md`: read it before adding or changing a test. New rule code goes into `Game/` with a gtest case. Tests that need Stockfish skip themselves without one.
