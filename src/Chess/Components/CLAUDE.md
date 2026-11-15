# Chess scene and UI (src/Chess/Components, config/SceneConfig.xml)

Read this before changing `config/SceneConfig.xml`. Siblings: the opponent `src/Chess/Engine/CLAUDE.md`, the rules `src/Chess/Game/CLAUDE.md`, the tests `tests/CLAUDE.md`. Components keep no game state: everything is read from the model (`ChessGame::GetActiveModel()`) on every update or draw.

## Files

```
ChessBoardComponent     the board: squares and highlights, clicks and drags, runs the opponent's and the analyst's Update() every SceneUpdateEvent, publishes the UIVars, answers the UI actions
ChessMaterialComponent  on the node Material, before its canvas: icons, the material... variables, rows shown or hidden
ChessPieceComponent     one piece node: follows its piece in the model, slides a played move, switches its sprite after a promotion
ChessResultComponent    its own node (layer 1): dims the board and draws the result bar once the game is over
```

## The board

- **Geometry** (`_originX`, `_originY`, `_squareSize`) is in scene units: the view is 840 x 800, the board 640 x 640 at (100, 80); change them together. The mouse goes through `Scene::ScreenToSceneX/Y`.
- **Orientation:** the board is drawn from Black's side exactly when the program plays White (`BottomColor(OpponentController::GetPlays())`, read live on every use). `SquareToPixel` and `OnMouseDown` map through `Game/BoardOrientation.h`; everything placed on a square uses them. The HUD, panels, model, control API and FEN do not turn.
- **Coordinates:** 16 labels of a `SceneView` canvas on `BoardCoordinates` (layer -1, after `Board`, no safe area, no `blocksInput`). The rects are fixed, the text (`coordRank<row>`, `coordFile<column>` from `Game/BoardCoordinates.h`) turns. A turn keeps the parity of the grid, so the label colors are static; `IsLightSquare` is the one function for it.
- In a `CHESS_NO_STOCKFISH` build the board hides the segment `EngineStockfish` at its first update or draw.

## Highlights

Order inside the board's `OnDraw()`: squares, the last move (`kLastMoveSquare`, from and to of `GetMoves().back()`; castling marks the king only), the check (`kMateSquare` once over), the hint (`kHintFromSquare` / `kHintToSquare`, while `PositionAnalyst::GetHint(model)` has one and the game is not over), the premoves (`kPremoveSquare`), then the selection and its legal targets last so they stay readable. Each has a hue of its own. Nothing is kept: a move, reset or `set_state` takes a mark off at the next draw.

## The pieces

- A piece node is `ChessPieceComponent` + `SpriteRendererComponent` + `TransformComponent`, the node the whole square and the sprite `fit="Contain"` (a new piece node needs it; the atlas `assets/chess_test.png` must keep its alpha). A piece registering late is dealt to the position as it stands.
- **A played move slides** (`ScopedTween`, `_slideDuration`, reflected, not set in the scene); the model and the control API know nothing of it. Only a change of exactly one move and one version with time in the frame slides; everything else snaps (first sighting, reset, `set_state`, several moves at once), and a frame with `deltaTime` 0 snaps, so UI tests need no waiting. A captured piece leaves when the slide ends.

## The premove

While the program thinks and the person has the other color, `OnMouseDown` hands the click to `ChessGameModel::OnPremoveClicked` (the queue and its rules: `src/Chess/Game/CLAUDE.md`): an own piece selects, a target queues, any other click drops the whole queue. No drag, no promotion panel: a premoved promotion plays a queen. While two programs play the board ignores clicks.

## The drag

`ChessBoardComponent` only; the model knows nothing. The press acts at once (a click). A left press on the square the model has selected, with the game on and no promotion pending, arms a drag; past `kDragThreshold` it is lifted. A release over a legal target calls `OnSquareClicked(target)`; anywhere else nothing is called, so the piece stays selected (and a release on another own piece does not select it, the owner's decision). `IsDragValid` drops it silently when the model moves under it. While lifted `GetLiftedPiece()` places the piece under the pointer on `kDragLayer` (6: over pieces, result and material, under HUD and panels). A dropped move snaps; the program's reply slides.

## The promotion picker

A click on a promotion target leaves it pending in the model; the node `PromotionPanel` (modal `SceneView` canvas, layer 20, `visible="false"`) is shown exactly while `HasPendingPromotion()`, checked after a click and every update, so a reset, `set_state` or someone's move closes a stale one. Buttons `PromoteQueen` .. `PromoteKnight` (`ChessPromote`, the id from `Game/PromotionChoices.*` as `value`) and `PromotionCancel` (`ChessCancelPromotion`: the pawn stays selected). Element names start with `Promotion` or `Promote`.

## The captured material

Node `Material` (layer 5, after `BoardCoordinates`): `ChessMaterialComponent`, then a `SceneView` canvas (no safe area, not modal). Two groups mirrored about the board's middle, `MaterialTop*` and `MaterialBottom*`, each a balance label and five rows (icon + count `{material<Top|Bottom><Type>}`); every name starts with `Material`. The variables are written only when the model, its version or the bottom color changed.

- The lower group is what the color at the bottom has taken (its icons are the other color's); the groups swap when the board turns. The balance (`BalanceText`, "+N" for the side ahead) is the difference on the board (`Game/CapturedMaterial.h`).
- A row shows when its count is above 0 and `MaterialListFits` (the lists' box inside the canvas's `safeBounds`, in view units); nothing shows before the first layout. In the narrowest window the lists hide and the balance stays (the owner's decision).
- **Known limits:** a "+39" beside a rank "8" may read as one number; with a top notch `Turn` (layer 10) covers the upper group's first rows and its balance, which a test asserts; moving the group breaks the mirror, so the fix is the owner's call.

## The HUD

`SceneView` canvas on `Hud` (layer 10), controls 56 units high in the margins: `Turn` (`{turn}`), `Menu` (fixed word, `ChessOpenMenu`; the owner wants no button named after a state, and no element is called `Opponent`), `Reset` (`ChessReset`), `Status` (`{status}`). The widest sentences only just fit the narrowest window (480 px with insets): a longer one needs a reword or a wider box; a sentence says what went wrong, never what to do. `ChessUiTest` holds the layout on the size matrix with every word of `tests/HudWords.h`, `ChessHudWebTest` the HTML twin.

The variables are republished when the model's version, the controller's revision, `IsThinking()` or the person's color changed: `turn` (`StatusText::Turn`, "Computer is thinking"), `status` (the opponent's error while one stands, it wins over "Check"; else the game status), `opponentError`, and the panel bindings `opponentMode`, `opponentColor` (**the person's** color), `opponentEngine`, `opponentLevel`.

## The menu

Node `MenuPanel` (layer 20, modal, `visible="false"`): `MenuDim`, `MenuPlate`, `MenuTitle`, one 56-unit button `MenuSection<Name>` per section (`ChessOpenSection`, the section id as `value`) and `MenuClose`; names start with `Menu`. `ChessOpenMenu` shows the list and hides every section, `ChessOpenSection` swaps the list for the section's canvas (an unknown id logs one warning), `ChessCloseMenu` hides all. The sections are the table `Game/MenuSections.*`. The menu is no state. **A new section** is a table row, a button, its own panel with `Back` and `Close`, and 68 units more in `MenuPlate`; a test fails a button without a row and the reverse.

## The opponent panel

Node `OpponentPanel` (layer 20, modal, `visible="false"`), after the engine's choice-panel template (`engine/src/Modules/BECore/UI/CLAUDE.md`, "Choice panels"): `Dim`, `Plate`, four `UISegmented` rows with the control API ids as segment values, `ModeGroup` (`opponentMode`, `ChessSetMode`), `ColorGroup` (`opponentColor`, `ChessSetColor`), `EngineGroup` (`ChessSetEngine`), `LevelGroup` (`ChessSetLevel`), the `OpponentError` line and `OpponentBack` / `OpponentClose`. A segment needs an explicit `height`.

- The group owns no choice: a press emits the action, the board picks through `ChessGame::GetActiveChoice()` and publishes the variable, so the panel shows what the game has. An id in no table logs one warning.
- **The color is the person's**; `opponent.plays` of the control API is the program's.
- A pick applies at once and the panel stays open. No group is greyed (a disabled group read as a dead button): a pick made while two people play is kept and applies when Computer is picked.
- A refused pick changes nothing (no fallback, the program is not switched off); "Engine not found" stays in `OpponentError` and `Status` until someone's move, a reset, `set_state` or the next pick.
- No name may be in two canvases, hence `ModeGroup`, not `Mode`.

## Draw order

By layer, not by place in `SceneConfig.xml` (`engine/src/Modules/BECore/Scene/CLAUDE.md`, "Draw order"); the same layer draws in scene order. `Board` and then `BoardCoordinates` -1, pieces 0, `Result` 1, `Material` 5, a dragged piece 6, `Hud` 10, the panels and the menu 20. A test reads the draw calls of one frame.
