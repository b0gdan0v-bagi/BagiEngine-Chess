# Chess Module Guidelines

## Overview

`ChessGame` is an `IGame` (name `Chess`, scene `ChessScene`). Executable `BagiChess`. Infrastructure and how a game is registered: `src/Modules/Games/CLAUDE.md`. Namespace `BECore::Chess`. The game's content is in `games/Chess/config/` (`ApplicationConfig.xml`, `SceneConfig.xml`, `TextureLibrary.xml`) and `games/Chess/assets/`. `BagiChess` starts in Game mode (`mode="Game"` in its `ApplicationConfig.xml`, no editor); set `mode="Editor"` to open it in the scene editor.

## Layout

```
src/Chess/
├── ChessGame.{h,cpp}         IGame; owns the shared ChessGameModel and its control adapter; Start() and Reset() reset the model; GetActiveModel() is how components reach it
├── ChessGameControl.{h,cpp}  IControllableGame: the state, the legal moves, a way to play one and to load a FEN for the control API (below)
├── Game/                     plain C++ rules, no scene or renderer
│   ├── ChessTypes.h          Square (file, rank), PieceType, PieceColor, Piece
│   ├── Bitboard.h            Bitboard (a1 = bit 0), masks, shifts, PopCount / Lsb / PopLsb
│   ├── Move.h                Move: 16 bits (from, to, promotion piece, kind), ToUci()
│   ├── Attacks.*             AttackTables (SingletonAtomic): knight / king / pawn tables, rook and bishop magic bitboards, Between / Line, Zobrist keys
│   ├── Position.*            pieces as bitboards + 64-cell array, side to move, castling rights, en passant, clocks, Zobrist key; FEN, DoMove / UndoMove, IsLegal, checkers and pins
│   ├── MoveGenerator.*       GenerateLegal(position) -> MoveList; ParseUci(position, text)
│   ├── GameStatus.*          EvaluateStatus(position): Ongoing, Checkmate, Stalemate, DrawInsufficientMaterial, DrawThreefold, DrawFiftyMoves
│   └── ChessGameModel.*      selected square, legal moves of it, status, move history, version, piece handles; OnSquareClicked drives select / move, TryMove(Move) is the one place a move is applied, SetPosition(Position) starts a game from a FEN
└── Components/               scene glue (IComponent)
    ├── ChessBoardComponent   draws the board, the check and the highlights, turns mouse clicks into OnSquareClicked (and restarts after the game ends)
    ├── ChessPieceComponent   logical handle of one piece; drives its TransformComponent from the model and switches its sprite after a promotion
    └── ChessResultComponent  on its own node (layer 1, over the pieces): dims the board and draws the result bar once the game is over
```

- State lives in `ChessGameModel`. Components never keep game state: they reach the model through `ChessGame::GetActiveModel()` (null when Chess is not the active game, and then they do nothing).
- A piece node in `ChessScene` is a `ChessPieceComponent` + `SpriteRendererComponent` (artwork from `TextureLibrary`, `games/Chess/config/TextureLibrary.xml`) + `TransformComponent`. `OnAttached` registers the piece with the model at its starting square; `Reset()` sends every handle back. The model moves a handle with its piece through captures, castling (the rook too), en passant (the passed pawn's handle is the one removed) and promotion (the handle reports the new type and the component switches the sprite to `<Color><Type>`, e.g. `WhiteQueen`).
- The piece atlas (`assets/chess_test.png`) must keep a real alpha channel: a sprite with an opaque background paints a box over its square and hides the square color and the highlights. A replacement atlas needs a transparent background.
- Board geometry (`_originX`, `_originY`, `_squareSize`) is reflected so it can be tuned in the scene editor. It is in scene units, not window pixels: `ChessScene` declares `viewWidth="840" viewHeight="760"` (the board 640x640 at (100, 60) with an equal margin on every side), so the engine scales the whole scene to the window and centers it. Change the view size together with the board geometry, and map mouse positions with `Scene::ScreenToSceneX/Y` (`OnMouseDown` does).
- The `Board` node has `layer="-1"`, the pieces stay on layer 0 and the `Result` node has `layer="1"`, so the squares and highlights are drawn first, the sprites on top of them and the end-of-game dim over everything. The order comes from the layer, not from where the node sits in `SceneConfig.xml` (`Scene/CLAUDE.md`, "Draw order").

## Control API

`ChessGame::AsControllable()` hands out `ChessGameControl` (`Games/CLAUDE.md` for the methods and the rules of an adapter). The model belongs to the game, so the adapter holds a reference and has no `no_active_model`. The wire format is in the header comment of `ChessGameControl.h`; in short:

| Method | What it does |
|--------|--------------|
| `get_state` | `{fen, board, turn, status, winner, inCheck, legalMoveCount, startFen, moves}`: `board` is 8 strings, rank 8 first, white upper case, `.` empty; `status` is `ongoing`, `checkmate`, `stalemate`, `draw_insufficient_material`, `draw_threefold` or `draw_fifty_moves`; `startFen` plus `moves` (UCI) rebuild the game with its repetitions |
| `list_actions` | `{"type": "move", "uci": "e2e4"}` for every legal move (a promotion is four); empty once the game is over |
| `perform_action` | plays one of them and answers `{move, status, turn, winner, inCheck}`; errors `illegal_action`, `promotion_required` (`e7e8` without the piece), `game_over`, `invalid_params` (not a UCI move) |
| `reset` | the opening position; takes no options |
| `set_state` | `{"fen": ".."}`: starts a game from any position `FromFen` accepts, finished ones included (the pieces in the scene are dealt out again, `SetPosition`) |

- The state version (`ChessGameModel::GetVersion()`) grows on a played move, a reset and a `set_state`, not on a selection. A person's move by mouse grows it too, so `state_changed` reports it.
- The scene follows the model: a move, a promotion, a reset and a `set_state` all show in a `screenshot`, which is how a headless run is checked by eye.

## Rules

Standard chess, Chess960 excluded: legal moves only (pins, check, castling, en passant, promotion), checkmate, stalemate and the draws that end the game by themselves: insufficient material, threefold repetition and 50 moves without a capture or a pawn move. FIDE makes a player claim the last two (it ends a game by itself only at fivefold and 75 moves), but the window has no way to claim, so they apply automatically. Both thresholds are named constants in `GameStatus.cpp`.

- A move is a `Move` from `MoveGenerator::GenerateLegal()`; `ChessGameModel::TryMove()` accepts nothing else and answers `std::expected<void, MoveError>` (`GameOver`, `IllegalMove`). `Position::IsLegal()` assumes a pseudo-legal move from the generator (while in check it relies on the generator having restricted the targets), so do not call it with a hand-made move: parse text with `MoveGenerator::ParseUci()`.
- Castling is the king's move (`e1g1`), as in UCI. A promoting pawn has four moves per target, queen first; a click on the target plays the queen, the other pieces are available through `TryMove` / UCI (`e7e8n`) only.
- `Position` keeps the en passant square only when a pawn can capture onto it (so positions that are equal for the rules have equal keys and FEN). `FromFen` rejects positions the rules cannot reach in the ways that would break the lookups: a king count other than one, a pawn on a back rank, more than 16 pieces of one color (the scene has 16 piece nodes per color, so any accepted position can be shown), the side that just moved in check, castling rights without the king and rook in place.
- A finished game accepts no move. The window shows the end with the red king (bright on check, dark on mate), the dimmed board and a bar across it (white, black or gray for a draw); the renderer has no text, so the reason goes to the log. A click on the board after the end starts a new game.
- The magic bitboards are searched for on first use (about 90 ms in a Debug build, measured on macOS) with a fixed seed, every candidate checked against the slow ray walk. If that becomes too slow, print the found numbers once and embed them as constants.

New rule code goes into `Game/` with a gtest case; do not patch around the generator in the model or the components.

## Tests

`games/Chess/tests/`, executable `ChessTests` (pure logic, no engine services needed). New rule code goes into `Game/` and gets a gtest case; see `.claude/rules/tests-module.md`.

| File | Covers |
|------|--------|
| `BitboardTest`, `MoveTest` | bit helpers and shifts; packing and UCI spelling of a move |
| `AttacksTest` | leaper tables, magic lookups against the ray walk on random blockers, `Between` / `Line`, Zobrist keys |
| `PositionTest` | FEN in and out (valid and rejected), pins, checkers, `DoMove` / `UndoMove`, incremental key = key of the FEN, repetition, insufficient material, `IsLegal` |
| `MoveGeneratorTest` | how each piece moves, evasions, double check, pinned blockers, promotion, en passant, castling, UCI |
| `PerftTest` | node counts of the published perft positions (start, Kiwipete, positions 3 to 6, mirrored position 4): the proof that the rules are right |
| `RulesTest` | `EvaluateStatus`: mates, stalemates, the automatic draws |
| `ChessGameModelTest` | click flow, mate by clicks, no moves after the end, `TryMove` errors, the move history and the version, `SetPosition`, `Reset`, piece handles through capture, castling, en passant, promotion and `SetPosition` |
| `ChessGameControlTest` | the adapter on a model of its own: state, actions against the generator, every error code, `reset`, `set_state`, the version |

`tests/control_smoke.py` is not a gtest: ctest runs it as `BagiChessControlSmoke` (`be_add_game_control_tests`). It starts `BagiChess --control --headless`, once on the real clock and once with `--manual-clock`, plays four moves, refusals, a mate in one loaded with `set_state`, a promotion, and checks that screenshots follow the model.

`ChessTestUtil.h` has the helpers (`Sq`, `Mv`, `PositionFromFen`, the perft FENs). The scene glue (check highlight, result bar, promotion sprite) draws and is not covered by tests: check it by hand in `BagiChess`.
