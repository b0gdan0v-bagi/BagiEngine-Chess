# Chess Module Guidelines

## Overview

`ChessGame` is an `IGame` (name `Chess`, scene `ChessScene`). Executable `BagiChess`. Infrastructure and how a game is registered: `engine/src/Modules/Games/CLAUDE.md`. Namespace `BECore::Chess`. The game's content is in `config/` (`ApplicationConfig.xml`, `SceneConfig.xml`, `TextureLibrary.xml`) and `assets/`. `BagiChess` starts in Game mode (`mode="Game"` in its `ApplicationConfig.xml`, no editor); set `mode="Editor"` to open it in the scene editor.

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
│   ├── StatusText.*          the words of the HUD: Turn(model) ("White to move", "White wins", "Draw") and Status(model) ("", "Check", "Checkmate", "Draw: threefold repetition", ...)
│   └── ChessGameModel.*      selected square, legal moves of it, status, move history, version, piece handles; OnSquareClicked drives select / move, TryMove(Move) is the one place a move is applied, SetPosition(Position) starts a game from a FEN
├── Engine/                   the opponent, a program that picks a move (nothing in the game uses it yet); depends on Game/ and on nothing in Components/
│   ├── ChessEngine.h         IChessEngine::FindMove(request, token): the interface; EngineError (a kind from EngineErrorKind and a sentence), SearchLimits (move time, depth)
│   ├── SearchRequest.*       SearchRequest: start FEN + moves + limits, from a model (FromModel) or from a position (FromPosition); Validate() the limits, FinalPosition() the position the moves lead to
│   └── UciProtocol.*         the UCI text, pure: PositionCommand, GoCommand, SetOptionCommand, ParseBestMove, TrimLine
└── Components/               scene glue (IComponent)
    ├── ChessBoardComponent   draws the board, the check and the highlights, turns mouse clicks into OnSquareClicked (and restarts after the game ends), publishes `turn` and `status` to UIVars when the model changed, answers the action ChessReset
    ├── ChessPieceComponent   logical handle of one piece; drives its TransformComponent from the model and switches its sprite after a promotion
    └── ChessResultComponent  on its own node (layer 1, over the pieces): dims the board and draws the result bar once the game is over
```

- State lives in `ChessGameModel`. Components never keep game state: they reach the model through `ChessGame::GetActiveModel()` (null when Chess is not the active game, and then they do nothing).
- A piece node in `ChessScene` is a `ChessPieceComponent` + `SpriteRendererComponent` (artwork from `TextureLibrary`, `config/TextureLibrary.xml`) + `TransformComponent`. `OnAttached` registers the piece with the model at its starting square (a piece that registers late, in the first frame, after a client has already moved, is dealt to the position as it stands: `RegisterPiece` re-deals when the game has left the opening); `Reset()` sends every handle back. The model moves a handle with its piece through captures, castling (the rook too), en passant (the passed pawn's handle is the one removed) and promotion (the handle reports the new type and the component switches the sprite to `<Color><Type>`, e.g. `WhiteQueen`).
- **The node is the whole square, the sprite is fitted inside it.** `ChessPieceComponent` writes the node's position and `_width = _height = squareSize` every update, and the sprite of each of the 32 piece nodes is `fit="Contain"` (`SpriteFit`, `engine/src/Modules/BECore/Scene/CLAUDE.md`): the piece is drawn inside the square with the aspect of its atlas cell (116.67 x 190.5, tall), as high as the square and centered. The board fills the selection, legal-move and check highlights over the same full square, so they sit around the piece and `ChessBoardComponent` does not need to know the size of the piece. A new piece node needs the attribute (`Stretch`, the default, squashes the tall cell into the square; a test counts the 32).
- The piece atlas (`assets/chess_test.png`) must keep a real alpha channel: a sprite with an opaque background paints a box over its square and hides the square color and the highlights. A replacement atlas needs a transparent background. Its cells may have any aspect: the fit keeps whatever `srcRect` the library gives, so the cell size in `TextureLibrary.xml` and the art can change without touching the scene.
- Board geometry (`_originX`, `_originY`, `_squareSize`) is reflected so it can be tuned in the scene editor. It is in scene units, not window pixels: `ChessScene` declares `viewWidth="840" viewHeight="800"` (the board 640x640 at (100, 80): 100 units on the sides and 80 above and below, where the HUD sits), so the engine scales the whole scene to the window and centers it. Change the view size together with the board geometry, and map mouse positions with `Scene::ScreenToSceneX/Y` (`OnMouseDown` does).
- **The HUD** is a `SceneView` canvas on the `Hud` node (layer 10, over the end-of-game dim), in the margins of the view: the label `{turn}` and the Reset button (`ChessReset`) in the top margin, the label `{status}` centered in the bottom one. It scales and moves with the board and cannot cover it in any shape of window. The view is 800 high, not 760, for the controls: a 56-unit button is a 32 px touch target at the smallest fit scale of the size matrix (0.57, because the width is 840 units), and the 60-unit margin of the old view left no room for it. The Status label is `76%` wide and centered: a fixed 640 left the safe area on a window with side insets (the matrix caught it). The promotion picker waits for the engine entry `chess-staged-stream`.
- The `Board` node has `layer="-1"`, the pieces stay on layer 0 and the `Result` node has `layer="1"`, so the squares and highlights are drawn first, the sprites on top of them and the end-of-game dim over everything. The order comes from the layer, not from where the node sits in `SceneConfig.xml` (`engine/src/Modules/BECore/Scene/CLAUDE.md`, "Draw order").
- **The opponent** (`Engine/`). `IChessEngine::FindMove` is synchronous and blocking: it returns when the engine has answered, was cancelled (`CancellationToken`) or failed, so a caller that must not wait runs it on a worker. It takes a `SearchRequest` (the start FEN of the game and the moves played, plus the limits), not a `Position`: UCI engines see repetitions only through `position fen <startFen> moves ...`, which a bare `Position` cannot give (`SearchRequest::FromPosition` drops the history, `FromModel` keeps it). Errors are values, `std::expected<Move, EngineError>`; a move that comes back is legal in the position the request leads to. `UciProtocol` writes the same `position` command as `position_command` of `tools/stockfish_bridge.py`.

## Control API

`ChessGame::AsControllable()` hands out `ChessGameControl` (`engine/src/Modules/Games/CLAUDE.md` for the methods and the rules of an adapter). The model belongs to the game, so the adapter holds a reference and has no `no_active_model`. The wire format is in the header comment of `ChessGameControl.h`; in short:

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
- A finished game accepts no move. The window shows the end with the red king (bright on check, dark on mate), the dimmed board and a bar across it (white, black or gray for a draw); the HUD says who won and why ("White wins", "Checkmate"), and the reason also goes to the log. A click on the board after the end, or the Reset button, starts a new game.
- The magic bitboards are searched for on first use (about 90 ms in a Debug build, measured on macOS) with a fixed seed, every candidate checked against the slow ray walk. If that becomes too slow, print the found numbers once and embed them as constants.

New rule code goes into `Game/` with a gtest case; do not patch around the generator in the model or the components.

## Tests

`tests/`, executable `ChessTests` (the rules need no engine services; `ChessUiTest` does). New rule code goes into `Game/` and gets a gtest case; see `engine/.claude/rules/tests-module.md`.

| File | Covers |
|------|--------|
| `BitboardTest`, `MoveTest` | bit helpers and shifts; packing and UCI spelling of a move |
| `AttacksTest` | leaper tables, magic lookups against the ray walk on random blockers, `Between` / `Line`, Zobrist keys |
| `PositionTest` | FEN in and out (valid and rejected), pins, checkers, `DoMove` / `UndoMove`, incremental key = key of the FEN, repetition, insufficient material, `IsLegal` |
| `MoveGeneratorTest` | how each piece moves, evasions, double check, pinned blockers, promotion, en passant, castling, UCI |
| `PerftTest` | node counts of the published perft positions (start, Kiwipete, positions 3 to 6, mirrored position 4): the proof that the rules are right |
| `RulesTest` | `EvaluateStatus`: mates, stalemates, the automatic draws |
| `ChessGameModelTest` | click flow, mate by clicks, no moves after the end, `TryMove` errors, the move history and the version, `SetPosition`, `Reset`, piece handles through capture, castling, en passant, promotion and `SetPosition`, and handles that register after the game has moved |
| `SearchRequestTest` | the request an engine gets: start FEN and moves taken from a model (castling and promotion spelled right) or from a position, the final position equals the model's (key, FEN, repetitions), `BadRequest` for a broken FEN and for a move that is not legal in its turn, the limits (`0/0` and negative ones are refused), and a stub `IChessEngine` used through the interface |
| `UciProtocolTest` | the exact text of `position`, `go` and `setoption`; `ParseBestMove` on `bestmove`, with `ponder`, `(none)`, `0000`, a bare one, other lines and a trailing `\r`; `TrimLine` |
| `StatusTextTest` | the words of the HUD: the opening, the turn after moves, check, a mate for each side, stalemate and every draw, a reset |
| `ChessUiTest` | the shipped `SceneConfig.xml` (read from the source tree) with `ChessGame` started through `GameManager`: the HUD through the size matrix with the longest words, the HUD outside the board in five window shapes, the words after a move, a check and a mate, the action ChessReset, a click on the Reset button where it is drawn, and a click on the board that the canvas does not take; the pieces: all 32 nodes are `fit="Contain"`, a piece keeps the aspect of its cell centered inside its square, and the selection, the legal moves and the check sit around the piece in several window shapes (a texture and a cell are given to the sprite by hand, the library is empty in the test) |
| `ChessHudWebTest` | the HUD written in HTML + CSS (the engine's web front-end, `UIWebCompiler`) against the HUD in the shipped `SceneConfig.xml` (read from the source tree): the same rect of every element in four window shapes, and the size matrix with the longest words. It came from the engine's `UIWebTest` |
| `ChessGameControlTest` | the adapter on a model of its own: state, actions against the generator, every error code, `reset`, `set_state`, the version |

`tests/control_smoke.py` is not a gtest: ctest runs it as `BagiChessControlSmoke` (`be_add_game_control_tests`). It starts `BagiChess --control --headless`, once on the real clock and once with `--manual-clock`, plays four moves, refusals, a mate in one loaded with `set_state`, a promotion, and checks that screenshots follow the model. A third run plays four moves before the first frame (the manual clock has none until `step`) and checks that the scene equals the one set from the same FEN. `tools/stockfish_bridge.py` plays the game against Stockfish and compares its legal moves with `list_actions` (its tests are `tools/tests/`; the control client it uses is described in `engine/tools/control/CLAUDE.md`).

`ChessTestUtil.h` has the helpers (`Sq`, `Mv`, `PositionFromFen`, the perft FENs). `ChessUiTest` needs `ChessModule` linked whole (`WHOLE_ARCHIVE` in `CMakeLists.txt`), since the scene file names components that no test references. The rest of the scene glue (the result bar, the promotion sprite, how the highlights look) draws and is not covered by tests: check it by hand in `BagiChess`.
