# Chess Module Guidelines

## Overview

`ChessGame` is an `IGame` (name `Chess`, scene `ChessScene`). Executable `BagiChess`. Infrastructure and how a game is registered: `engine/src/Modules/Games/CLAUDE.md`. Namespace `BECore::Chess`. The content is in `config/` (`ApplicationConfig.xml`, `SceneConfig.xml`, `TextureLibrary.xml`, `ChessSettings.xml`) and `assets/`. `BagiChess` starts in Game mode (`mode="Game"` in its `ApplicationConfig.xml`, no editor); set `mode="Editor"` to open it in the scene editor (a local edit only: `ChessWebConfigTest` fails with it). The game has a web target with the own engine only (CMake switch `CHESS_NO_STOCKFISH`, no control API, no editor there): see "The web build".

## Layout

```
src/Chess/
├── ChessGame.{h,cpp}         IGame; owns the shared ChessGameModel, the OpponentController, the OpponentChoice over it, the PositionAnalyst and the control adapter; Start() resets the model and the opponent (nobody plays, default engine: Stockfish, the own engine in a `CHESS_NO_STOCKFISH` build, the person has White) and restores the saved game (ChessSaveKeeper, `Save/CLAUDE.md`), Stop() writes it and destroys the opponent and shuts the analyst down; GetActive*() are how components reach them
├── ChessGameControl.{h,cpp}  IControllableGame: the control API adapter (below)
├── Game/                     plain C++ rules, no scene or renderer: squares, bitboards, moves, position and FEN, legal moves, game status, ChessGameModel, pure UI helpers (src/Chess/Game/CLAUDE.md)
├── Engine/                   the computer opponent: IChessEngine, Stockfish over UCI, the own engine, levels and handicap, settings, the controller and the choice, PositionAnalyst, MoveRater (src/Chess/Engine/CLAUDE.md)
├── Save/                     the game and the opponent choice kept between runs (src/Chess/Save/CLAUDE.md)
├── Uci/                      the own engine as a UCI program, `BagiChessUci`; no game, no scene (src/Chess/Uci/CLAUDE.md)
└── Components/               scene glue: board, hint, pieces, result, captured material, HUD (src/Chess/Components/CLAUDE.md)
cmake/ChessWeb.cmake          the web build: which files of config/ and assets/ go into the preload package
```

- State lives in `ChessGameModel`. Components never keep game state: they reach the model through `ChessGame::GetActiveModel()` (null when Chess is not the active game, and then they do nothing).

**Where the details are** (each file loads when a session works in its folder; read the matching one before changing code there):

| File | Holds |
|------|-------|
| `src/Chess/Game/CLAUDE.md` | every file of the rules and what it guarantees |
| `src/Chess/Engine/CLAUDE.md` | the opponent: the engine interface, UCI and Stockfish, the own engine, the levels and the handicap, the settings file, the controller and the choice, the analysis |
| `src/Chess/Save/CLAUDE.md` | what is saved, the gate (flags, `rememberGame`), the folder, when it writes, a refused save |
| `src/Chess/Uci/CLAUDE.md` | `BagiChessUci`: the commands and options, the `stop` and clock rules, how to run it and its smoke |
| `src/Chess/Components/CLAUDE.md` | the scene and the UI: pieces, board, hint, HUD, the menu and its opponent panel, promotion picker, draw order; **read it before changing `config/SceneConfig.xml`** |
| `tests/CLAUDE.md` | where a test goes, the helpers, the fake UCI engine, the smokes, the build variants |

## Control API

`ChessGame::AsControllable()` hands out `ChessGameControl` (`engine/src/Modules/Games/CLAUDE.md` for the methods and the rules of an adapter). The adapter holds references to what belongs to the game, so it has no `no_active_model`. **The wire format (every member of the state, `set_state`, the errors) is the header comment of `src/Chess/ChessGameControl.h`: read it, it is the source.** A control or headless run neither reads nor writes the save without `--save-dir` (`src/Chess/Save/CLAUDE.md`). The contracts a client relies on:

- `get_state` has `fen`, `board`, `turn`, `status`, `winner`, `inCheck`, `legalMoveCount`, `startFen`, `moves` (UCI; with `startFen` they rebuild the game with its repetitions), `opponent`, `material` and `analysis`. `opponent.plays` is the color the **program** plays (`null` for two people, the start of every game, `"both"` for two programs); `level` and `engine` are the choice that would be used, also while nobody plays.
- `list_actions` is one `{"type": "move", "uci": ..}` per legal move (a promotion is four), empty once the game is over. It is **unchanged by the opponent**: every action is a move, which `tools/stockfish_bridge.py` relies on. `perform_action` is not blocked for the program's side (a controller may play for it); only the mouse is kept from it.
- `reset` keeps the opponent. `set_state` takes `fen`, `opponent` or `analysis`; everything is checked first, so `invalid_params` changes nothing; the opponent is applied first, then the position; a pick that changes nothing does nothing (no restart, no new version).
- **A pick that finds no engine is refused as a whole** (`engine_unavailable`, every seat asked first): the position, the sides, the seats and a running search stay as they were, the level of a refused pick is not saved, and `opponent.error` has the reason until the version moves or the next pick. Only Stockfish can fail (no file found); `own` always succeeds, and nothing falls back to the other engine.
- `material` is **derived**: computed from the position at every `get_state`, with no state, no version of its own and no `set_state` member.
- `analysis` (`PositionAnalyst`, `src/Chess/Engine/CLAUDE.md`, "The analysis in the game") is the engine the person picked, never handicapped, at the limits of the strongest level, in a process of its own; the evaluation and the ratings are off at every game start (CPU cost; a reset keeps the switches). A hint belongs to its position (a move, a reset or a `set_state` drops it), is for the person (`not_your_turn` on the program's side, `game_over` when finished) and is never played. `evaluation` and every score in `lines` are **from White's view**, `hint.text` from the side to move's. **`analysis` cannot be combined with `fen` or `opponent` in one call.** A change of `opponent.engine` drops the old engine's answer in the same call.
- **The ratings of the moves** (`MoveRater`, owned by the analyst; `MoveRating.h`): a move is brilliant when it is the best move of the own engine at depth 5 in the position before it and it was a choice (`forced`: one legal move; `obvious`: the second line is 200 centipawns or more behind; `not_best` otherwise; a tie is a gap of 0). It is always the own engine whatever `opponent.engine` is, so the same game gets the same ratings twice. The rater's failure is logged, not in `analysis.error`.
- The state version is `ChessGameModel::GetVersion()` plus `OpponentController::GetRevision()` plus `PositionAnalyst::GetRevision()`. It grows on a played move (by mouse or program too), a reset, a `set_state`, a pick, an error that appears or goes, an analysis switch, a hint asked or dropped, an analysis or a rating that arrives, not on a selection or on `thinking`. It never goes back.
- The scene follows the model: a move, a promotion, a reset and a `set_state` all show in a `screenshot`, so a headless run is checked by eye.

## The web build

Chess builds for the browser with the engine's Emscripten presets (`web-release`, `web-debug`; the steps are in `README.md`, "Browser build"). It is written and checked on the desktop only, with a web-like build (`-DENGINE_WITH_VULKAN=OFF -DENGINE_SINGLE_THREAD=ON -DENGINE_STATIC_SDL=ON -DCHESS_NO_STOCKFISH=ON`, commands in `tests/CLAUDE.md`); nothing of it has run with emsdk or in a browser yet.

- **What is off there:** Stockfish (no child process in a browser: `CHESS_NO_STOCKFISH`, forced ON when `ENGINE_TARGET_WEB`, usable on the desktop too; the game starts with the own engine and `stockfish` is `engine_unavailable`), `BagiChessUci` and its smokes, the control API (the engine drops `RemoteControl` there) and the editor. `ChessTests` and the smokes are behind `ENGINE_BUILD_TESTS`, which the web presets switch off.
- **The config.** The browser build layers the engine's `web/` overlay over the game's content (`BAGI_WEB_ROOT`, the engine's own folder; a game cannot add to it) and a file of the same name replaces the game's whole (configs are not merged). Chess ships **no web config of its own**: its `ApplicationConfig.xml` is the overlay's apart from the `renderer`. `ChessWebConfigTest` keeps that true. If Chess has to differ in the browser the engine has to learn a game's web overlay: an engine change, not an edit here.
- **The search.** The single-thread build runs tasks on the main thread between frames, so the own engine's search is sliced and the opponent, the analyst and the rater `co_await` it so that the page keeps drawing (`src/Chess/Engine/CLAUDE.md`, "The sliced search").
- **The package.** The engine packs every file of `config/` and `assets/` into the preload package; `cmake/ChessWeb.cmake` lists what stays out and `CMakeLists.txt` passes it to the link as `--exclude-file` (**unverified: needs emsdk**; the fallback is a staged copy of the two folders). `BagiChessWebPackageCheck` (`tests/check_web_package.cmake`) fails on any file beyond the four configs, `assets/chess_test.png` and the engine's and overlay's, so a new config or asset file is a conscious change of that list.

## Rules

Standard chess, Chess960 excluded: legal moves only (pins, check, castling, en passant, promotion), checkmate, stalemate and the draws that end the game by themselves: insufficient material, threefold repetition and 50 moves without a capture or a pawn move. FIDE makes a player claim the last two, but the window has no way to claim, so they apply automatically.

- A move is a `Move` from `MoveGenerator::GenerateLegal()`; `ChessGameModel::TryMove()` accepts nothing else and answers `std::expected<void, MoveError>` (`GameOver`, `IllegalMove`). `Position::IsLegal()` assumes a pseudo-legal move from the generator, so do not call it with a hand-made move: parse text with `MoveGenerator::ParseUci()`.
- Castling is the king's move (`e1g1`), as in UCI. A promoting pawn has four moves per target, queen first; a click on the target leaves the promotion pending in the model (`HasPendingPromotion()`) and `ChoosePromotion(piece)` plays it (the modal `PromotionPanel`); `TryMove` / UCI (`e7e8n`) name the piece themselves, as the program and the control API do.
- `Position` keeps the en passant square only when a pawn can capture onto it (so positions that are equal for the rules have equal keys and FEN). `FromFen` rejects positions the rules cannot reach in the ways that would break the lookups: a king count other than one, a pawn on a back rank, more than 16 pieces of one color (the scene has 16 piece nodes per color), the side that just moved in check, castling rights without the king and rook in place.
- A finished game accepts no move. The window shows the end (red king, dimmed board, a result bar), the HUD says who won and why, and the reason goes to the log. A click on the board after the end, or the Reset button, starts a new game.
- The magic bitboards are searched for on first use with a fixed seed, every candidate checked against the slow ray walk. If that becomes too slow, embed the found numbers as constants.

New rule code goes into `Game/` with a gtest case; do not patch around the generator in the model or the components.

## Tests

`tests/`, executable `ChessTests`, plus the control smoke `BagiChessControlSmoke` (`tests/control_smoke.py`) and the recorded scenarios `BagiChessScenario.*` (`tests/scenarios/`) under ctest. Where a test goes, the helpers, the fake UCI engine and the build variants are in `tests/CLAUDE.md`: read it before adding or changing a test. Tests that need Stockfish skip themselves without one.
