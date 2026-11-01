# Chess Module Guidelines

## Overview

`ChessGame` is an `IGame` (name `Chess`, scene `ChessScene`). Executable `BagiChess`. Infrastructure and how a game is registered: `src/Modules/Games/CLAUDE.md`. Namespace `BECore::Chess`. The game's content is in `games/Chess/config/` (`ApplicationConfig.xml`, `SceneConfig.xml`, `TextureLibrary.xml`) and `games/Chess/assets/`. `BagiChess` starts in Game mode (`mode="Game"` in its `ApplicationConfig.xml`, no editor); set `mode="Editor"` to open it in the scene editor.

## Layout

```
src/Chess/
├── ChessGame.{h,cpp}         IGame; owns the shared ChessGameModel; Start() and Reset() reset the model
├── Game/                     plain C++ rules, no scene or renderer
│   ├── ChessTypes.h          Square (file, rank), PieceType, PieceColor
│   ├── ChessBoard.*          8x8 storage and accessors only, no rules
│   ├── MoveGenerator.*       static Generate(board, from) -> pseudo-legal target squares
│   └── ChessGameModel.*      turn, selected square, legal moves, piece handles; OnSquareClicked drives select/move
└── Components/               scene glue (IComponent)
    ├── ChessBoardComponent   draws the board and highlights, turns mouse clicks into OnSquareClicked
    └── ChessPieceComponent   logical handle of one piece; drives its TransformComponent from the model
```

- State lives in `ChessGameModel`. Components never keep game state: they reach the model through `GameManager::GetInstance().GetActive()->Cast<ChessGame>()` and do nothing when Chess is not the active game.
- A piece node in `ChessScene` is a `ChessPieceComponent` + `SpriteRendererComponent` (artwork from `TextureLibrary`, `games/Chess/config/TextureLibrary.xml`) + `TransformComponent`. `OnAttached` registers the piece with the model at its starting square; `Reset()` sends every handle back.
- The piece atlas (`assets/chess_test.png`) must keep a real alpha channel: a sprite with an opaque background paints a box over its square and hides the square color and the highlights. A replacement atlas needs a transparent background.
- Board geometry (`_originX`, `_originY`, `_squareSize`) is reflected so it can be tuned in the scene editor. It is in scene units, not window pixels: `ChessScene` declares `viewWidth="840" viewHeight="760"` (the board 640x640 at (100, 60) with an equal margin on every side), so the engine scales the whole scene to the window and centers it. Change the view size together with the board geometry, and map mouse positions with `Scene::ScreenToSceneX/Y` (`OnMouseDown` does).
- The `Board` node has `layer="-1"`, the pieces stay on layer 0, so the squares and highlights are drawn first and the sprites on top. The order comes from the layer, not from where the node sits in `SceneConfig.xml` (`Scene/CLAUDE.md`, "Draw order").

## Rules scope (MVP)

`MoveGenerator` produces pseudo-legal moves only: no check or checkmate, no castling, no en passant, no promotion. Do not assume legality beyond that, and extend the generator (with tests) rather than patching around it in the model or components.

## Tests

`games/Chess/tests/MoveGeneratorTest.cpp`, executable `ChessTests` (pure logic, no engine services needed). New rule code goes into `Game/` and gets a gtest case; see `.claude/rules/tests-module.md`.
