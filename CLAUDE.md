# Chess Module Guidelines

## Overview

`ChessGame` is an `IGame` (name `Chess`, scene `ChessScene`, namespace `BECore::Chess`), executable `BagiChess`; how a game is registered: `engine/src/Modules/Games/CLAUDE.md`. Content: `config/` (`ApplicationConfig.xml`, `SceneConfig.xml` with its node files in `config/nodes/`, `TextureLibrary.xml`, `UIThemes.xml`, `ChessSettings.xml`) and `assets/`. It starts in Game mode; `BagiChess --editor` opens the scene editor (never commit `mode="Editor"`: `ChessWebConfigTest` fails with it); `/artgen` generates art and sounds through it. Style is the engine's (`engine/CLAUDE.md`); `BECore::Format`, not `fmt::` (`BagiChessFmtLayerCheck`). Comments: `.claude/rules/code-comments.md`.

- **Licence:** the game and its own art are Beerware (`LICENSE`); what the repository carries of other authors and the Stockfish paragraph (never shipped or linked, a separate process over UCI) are in `THIRD_PARTY_NOTICES.md`, held to the tree by ctest `BagiChessThirdPartyNotices`: a new asset or library adds its row in the same commit. The engine's notices are `engine/THIRD_PARTY_NOTICES.md`.
- **Puzzles:** mates in 1 to 3 from `assets/puzzles/puzzles.tsv` (lichess, CC0: its row in `THIRD_PARTY_NOTICES.md`, `tools/fetch_puzzles.py` makes it again). The mode is the Menu section `Puzzles` and the bar at the bottom left; `PuzzleSession` judges, `ChessGame` owns it and the components only ask (`src/Chess/Game/CLAUDE.md`, `src/Chess/Components/CLAUDE.md`).
- **Profiling:** preset `windows-clang-profile-editor` (zones and allocation counts); the zone names are in `src/Chess/Engine/CLAUDE.md`, the benchmark set of `/perf` in `.claude/rules/perf-project.md`, and `ChessUiAllocTest` holds that a steady frame allocates nothing (profile build only).

## Layout

```
src/Chess/
├── ChessGame.{h,cpp}         IGame; owns the ChessGameModel, the OpponentController and its OpponentChoice, the PositionAnalyst, the control adapter; GetActive*() is how components reach them (null when Chess is not active)
├── ChessGameControl.{h,cpp}  the control API adapter
├── Game/                     the rules, plain C++: board, moves, FEN, legal moves, status, ChessGameModel, pure UI helpers
├── Engine/                   the opponent: IChessEngine, Stockfish over UCI, the own engine, levels, controller, analyst, MoveRater
├── Save/                     the game and the opponent choice kept between runs
├── Sound/                    the sounds of the board and the buttons, the Sound section
├── Uci/                      the own engine as the UCI program BagiChessUci
└── Components/               scene glue: board, pieces, result, material, move panel, HUD, panels
cmake/ChessWeb.cmake          what of config/ and assets/ stays out of the web package
```

State lives in `ChessGameModel`; components keep none. Each folder above (and `tests/`) has its own `CLAUDE.md`: read it before changing code there; `src/Chess/Components/CLAUDE.md` before changing `config/SceneConfig.xml` or `config/nodes/`.

## Control API

`ChessGame::AsControllable()` hands out `ChessGameControl` (the adapter rules: `engine/src/Modules/Games/CLAUDE.md`). **The wire format is the header comment of `src/Chess/ChessGameControl.h`: it is the source.** A control or headless run touches no save without `--save-dir`. What clients rely on:

- `opponent.plays` is the color the **program** plays (`null`: two people, the start of every game; `"both"`). `list_actions` is only moves, whoever plays (`tools/stockfish_bridge.py` relies on it); `perform_action` may play for the program's side, only the mouse is kept from it.
- `set_state` (`fen`, `opponent` or `analysis`) checks everything first (`invalid_params` changes nothing), applies the opponent before the position, and a pick that changes nothing does nothing. `analysis` cannot be combined with the other two.
- A pick that finds no engine (only Stockfish can fail: none in the build, or no file found by `stockfishPath`, `$STOCKFISH` and `PATH`) is refused as a whole (`engine_unavailable`), also for a seat nobody plays: nothing changes, nothing falls back, `opponent.error` holds the reason until the version moves or the next pick. A game then starts with `opponent.engine` `own`, as the panel shows (it offers Stockfish only while one is found).
- `set_state {"takeBack": {"plies": n}}` (alone in its call; without `plies` the button's rule, `takeBackPlies` of the state) takes moves back; nothing to take back is `invalid_params`. `sanMoves` is `moves` in SAN; the move the screen views is not in the state.
- `set_state {"resign": {"color"?}}` (alone in its call) ends the game as `resigned` (`winner` is the other color; `draw_agreed` has none). The color is optional: against the program the person's, with two people the side to move; with two programs it is required. The way back is `takeBack`.
- `set_state {"draw": {}}` (alone) is the person's offer: two people agree at once; against the program the call answers ok and the own engine (at a fixed depth, a background task like the analysis) judges, so the verdict comes with the frames: `drawOffer` is `{"state": "pending"}`, then `status` `draw_agreed` or `{"state": "declined"}` until the version moves. The program accepts while its own score is at most `kDrawAcceptCentipawns` in its favour. Two programs or the program's turn is `invalid_params`.
- `opening` (`{"eco", "name"}` or `null`) is the full lichess name of the **last** position of the game that `assets/openings/openings.tsv` knows (so it stays after the game leaves the book, and a transposition has its position's name), derived at every `get_state` from `startFen` and `moves` (the game's position, not the view; no version of its own); `null` at the start, outside the book and when the file is missing (one warning at the start, the game plays).
- `material` is derived from the position at every `get_state`. `analysis` is the picked engine, never handicapped, in a process of its own; evaluation and ratings are off at every game start; a hint belongs to its position, is for the person only (with a puzzle on, the solver's turns only) and is never played; scores are from White's view, `hint.text` from the side to move's. Ratings always use the own engine (the same game rates the same twice).
- `opponent.seed` (an integer, `set_state {"opponent": {"seed": n}}`, a valid pick alone) varies the own engine among near-equal moves and every handicap's blunder choice and move; 0 is no variety (the old answers). A new game (`Reset`, a start) and a takeback (the button, `set_state {takeBack}`) draw a new one, `set_state {fen}` keeps it, it is not saved; Stockfish's own picks are not seeded. A client that compares answers across resets or takebacks sets the seed after each.
- A headless run plays to SDL's dummy driver (and a machine with no device plays nothing), so the engine's `audio_state` is where a control client sees what the game asked for: the clip and the bus of each move's and each button's sound (`src/Chess/Sound/CLAUDE.md`); `audio_set` sets a level, and the saved level is read and written only when the run saves (the gate of `--save-dir`).
- `puzzle` is `null` unless a puzzle is on (judged from the game at every `get_state`: a reset or `set_state fen` ends it at once), else `{id, index, count, rating, themes, solver, state, mistakes, wrong, step, steps}`. `set_state {"puzzle": {"index": n | "id": s | "next": true | "retry": true | "end": true}}` (alone, one member; the last three need a puzzle on, `end` is a reset). While one is on `takeBack`, `resign` and `draw` are `invalid_params`, the opponent gets no frame, nothing is saved and the board turns for the solver; a wrong move is taken back and the program's reply is played at the frames after `kPuzzleReplySeconds` of game time (a headless client steps the clock).
- `editor` is `null` unless the person edits a position in the window (Menu, Position): then `{fen, error, tool}` of the draft, which is not the game (the game's `fen`, `moves` and version stay); a client edits it with `ui_click`, and `set_state {fen}`, a reset, a move or a takeback ends it.
- The state version is the sum of the model's, the controller's, the analyst's, the draw judge's, the puzzle's and the editor's revisions (the editor's counts every draft of the run, so it does not fall when one ends): it grows on every change of the state, not on a selection or `thinking`, and never goes back.

## The web build

Emscripten presets `web-release` / `web-debug` (`README.md`, "Browser build"); built with emsdk and played in a headless Chromium and Edge by `tests/web_chess_smoke.py` (run by hand: it needs emsdk, a browser and a built page; the web-like desktop build of `tests/CLAUDE.md` is the one in ctest). Not tried: a real GPU, a phone, Firefox, Safari, the deploy.

The seven sound clips are in the preload package; Web Audio starts after the first click (sound is not heard by any check).

- **Off there:** Stockfish (`CHESS_NO_STOCKFISH`, forced ON for the web, usable on the desktop: the game starts with the own engine), `BagiChessUci`, the control API, the editor, the tests.
- **Config:** the engine's `web/` overlay replaces files of the same name whole. Chess ships no web config of its own (`ChessWebConfigTest`); to differ in the browser the engine has to learn a game overlay.
- **Search:** single-thread, so the own engine's search is sliced and `co_await`ed (`src/Chess/Engine/CLAUDE.md`, "The sliced search").
- **Package:** `cmake/ChessWeb.cmake` lists what stays out (`--exclude-file`, which emcc applies to a `--preload-file` folder); `tests/check_web_package.cmake` holds it twice and keeps no list of files: the patterns against the tree in ctest, and after every web link the names emcc really packed (a `POST_BUILD` step of `BagiChess`).

## Rules

Standard chess without Chess960. Threefold repetition and the 50-move rule end the game by themselves (the window cannot claim).

- A move is a `Move` from `MoveGenerator::GenerateLegal()`; `ChessGameModel::TryMove()` takes nothing else. `Position::IsLegal()` assumes a generator move: parse text with `MoveGenerator::ParseUci()`.
- Castling is the king's move (`e1g1`). A click on a promotion target leaves it pending in the model until `ChoosePromotion(piece)`; UCI (`e7e8n`) names the piece itself.
- `Position` keeps the en passant square only when a capture onto it is possible (equal positions, equal keys). `FromFen` rejects what would break the lookups: not one king per side, a pawn on a back rank, more than 16 pieces of a color, the side that just moved in check, castling rights without king and rook in place.
- A finished game takes no move; a click on the board or Reset starts a new one.

New rule code goes into `Game/` with a gtest case; never patch around the generator in the model or the components.
