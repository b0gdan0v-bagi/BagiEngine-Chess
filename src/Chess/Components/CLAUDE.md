# Chess scene and UI (src/Chess/Components, config/SceneConfig.xml, config/nodes/)

Read this before changing `config/SceneConfig.xml` or a file of `config/nodes/` (which file holds which node: `config/CLAUDE.md`). Siblings: `src/Chess/Engine/`, `src/Chess/Game/`, `tests/`. Components keep no game state: everything is read from the model (`ChessGame::GetActiveModel()`) on every update or draw.

## Files

```
ChessBoardComponent     the board: squares, highlights and coordinates, clicks and drags, the frame (the opponent's, the analyst's and the draw judge's Update() every SceneUpdateEvent, the keeper, the result log)
ChessHudComponent       on Hud, before its canvas: publishes the UIVars of the HUD and the opponent panel in its draw, greys Resign, answers ChessReset
ChessMenuComponent      on MenuPanel: ChessOpenMenu, ChessOpenSection, ChessCloseMenu, Escape
MenuActions             the names of the menu's actions that several panels answer
ChessOpponentPanelComponent  on OpponentPanel, before its canvas: ChessSet*, the Stockfish segment
ChessPromotionPanelComponent on PromotionPanel, before its canvas: shown while a promotion is pending, ChessPromote, ChessCancelPromotion
ChessMaterialComponent  on the node Material, before its canvas: icons, the material... variables, which set (margin or move panel) and which rows are shown
ChessMovePanelComponent on the node MovePanel, before its canvas: the variables of the move list rows, rows and marks shown or hidden, the box shown where it fits; ChessShowMove, ChessTakeBack
ChessMovesSectionComponent on the node MovesSection (the menu section "Moves"), before its canvas: the same variables, its own rows and marks
ChessGameSectionComponent on the node GameSection ("Resign / Draw"), before its canvas: ChessResign, ChessDraw, ChessGameYes, ChessGameNo
AnalysisRows            the analysis band of the move panel: the words and the bar from the analyst, the grey of the Hint buttons
MoveRows                the part both share: the rows of the game, their words, the rows and marks of one canvas, the takeback
ChessPieceComponent     one piece node: follows its piece in the model, slides a played move, switches its sprite after a promotion
ChessResultComponent    its own node (layer 1): dims the board and draws the result bar once the game is over
```

**Each UI node answers its own actions** with a component of its own; the board knows only the picker (`ChessPromotionPanelComponent::SyncActive()` after a click, since the order in which two components get one event is not defined). **A new panel** is a node file with its own component and its own `tests/ChessUi<Topic>Test.cpp`, no change to the board.

## The board

- **Four views** (`viewVariants` of `ChessScene`: the default, `Tall`, `Portrait` and `Wide`, sizes and aspects in `config/CLAUDE.md`, why in the comment of `SceneConfig.xml`). The board is 640 x 640 in all (a square 80 units), at (100, 80) in the default and Wide view and (40, 144) in Portrait and Tall. **The one rule: a view is widened or heightened only where the other side already limits the view it replaces**: Wide and Tall never shrink a board, font or touch target; the letterbox they take is the move panel's. Tall is Portrait with 244 units more at the bottom, every place of Portrait kept (`HasPortraitPlaces()`). A canvas lays out per view with a variant `orientation:portrait` (conditioned on the canvas's bounds, the view in units; it holds in Tall too); Wide needs no variant, the HUD's anchors move its right end.
- **Geometry** is in scene units of the active view: `_originX/_originY/_squareSize` (default and Wide) and the `_portrait...` triple (Portrait and Tall); change a pair with the numbers of the canvases around it. The component takes one set on `SceneViewChangedEvent` (`HasPortraitPlaces(name)`) and every accessor reads that choice, so the squares, the pieces and the result bar cannot disagree for a frame; a drag in the hand ends with the view. The mouse goes through `Scene::ScreenToSceneX/Y`.
- **Orientation:** the board is drawn from Black's side exactly when the program plays White (`BottomColor(OpponentController::GetPlays())`, read live on every use). `SquareToPixel` and `OnMouseDown` map through `Game/BoardOrientation.h`; everything placed on a square uses them. The HUD, panels, model, control API and FEN do not turn.
- **Coordinates:** 16 labels of a `SceneView` canvas on `BoardCoordinates` (layer -1, after `Board`, no safe area, no `blocksInput`). The rects are fixed per view (a variant sets each label for the Portrait origin), the text (`coordRank<row>`, `coordFile<column>` from `Game/BoardCoordinates.h`) turns. A turn keeps the parity of the grid, so the label colors are static: the token `boardDark` on a light cell, `boardLight` on a dark one; `IsLightSquare` is the one function for it.
- `ChessOpponentPanelComponent` shows the segment `EngineStockfish` exactly while `OpponentController::OffersStockfish()` (a **cached** answer, no file lookup per frame; `RefreshStockfish()` looks again when the opponent section opens, so a Stockfish installed meanwhile shows up then).

## Highlights

Order inside the board's `OnDraw()`: squares, the last move (castling marks the king only), the check (the mate color only on a checkmate; a game resigned or drawn in check keeps the check's hue), the hint (from and to, while the analyst has one and the game is not over), the premoves, then the selection and its legal targets last so they stay readable. Each has a hue of its own. **The colors are the tokens `board*` of `config/UIThemes.xml`**, read through `BoardPalette`. Nothing is kept: a move, reset or `set_state` takes a mark off at the next draw. **While an earlier position is viewed** (`ChessGameModel::IsViewing()`) the marks are those of `GetShownPosition()` (its move, its check), with no hint, premove, selection or result dim; a press on the board only ends the view, also in a finished game.

## The pieces

- A piece node is a reference of the prefab `config/nodes/ChessPiece.xml` (what it overrides: `config/CLAUDE.md`). The prefab is the piece component, a sprite and a transform: the node is the whole square, the sprite `fit="Contain"`. A piece registering late is dealt to the position as it stands.
- A piece stands on `GetShownSquareOf()`: the viewed position's square while viewing; a view that begins or ends places the pieces at once.
- **A played move slides** (`ScopedTween`, `_slideDuration`, reflected, not set in the scene). Only a change of exactly one move and one version with time in the frame slides; everything else snaps (first sighting, reset, `set_state`, several moves, a frame with `deltaTime` 0). A captured piece leaves when the slide ends.

## The premove

While the program thinks and the person has the other color, `OnMouseDown` hands the click to `ChessGameModel::OnPremoveClicked` (the queue and its rules: `src/Chess/Game/CLAUDE.md`): an own piece selects, a target queues, any other click drops the whole queue. No drag, no promotion panel: a premoved promotion plays a queen. While two programs play the board ignores clicks.

## The drag

`ChessBoardComponent` only; the model knows nothing. The press acts at once (a click); a left press on the selected square, with the game on and no promotion pending, arms a drag, lifted past `kDragThreshold`. A release over a legal target calls `OnSquareClicked(target)`; anywhere else nothing is called, so the piece stays selected (a release on another own piece does not select it, the owner's decision). `IsDragValid` drops the drag silently when the model moves under it. While lifted the piece is under the pointer on `kDragLayer` (over pieces, result and material, under HUD and panels). A dropped move snaps; the program's reply slides.

## The promotion picker

A click on a promotion target leaves it pending in the model; the node `PromotionPanel` (`ChessPromotionPanel.xml`, modal `SceneView` canvas, layer 20, `visible="false"`) is shown by `ChessPromotionPanelComponent` exactly while `HasPendingPromotion()`, checked after a click and every update and draw, so a reset, `set_state` or someone's move closes a stale one. Buttons `PromoteQueen` .. `PromoteKnight` (`ChessPromote`, the id of `Game/PromotionChoices.*` as `value`) and `PromotionCancel` / `PromotionDim` (`ChessCancelPromotion`: the pawn stays selected).

## The captured material

Node `Material` (`ChessMaterial.xml`, layer 5, after `BoardCoordinates`): `ChessMaterialComponent`, then a `SceneView` canvas (no safe area, not modal). Two groups mirrored about the board's middle (in Portrait two rows, above and below the board), `MaterialTop*` and `MaterialBottom*`, each a balance label and five rows (icon + count `{material<Top|Bottom><Type>}`); every name starts with `Material`. The variables are written only when the model, its version or the bottom color changed.

- The lower group is what the color at the bottom has taken (its icons are the other color's); the groups swap when the board turns. The balance (`BalanceText`, "+N" for the side ahead) is the difference on the board (`Game/CapturedMaterial.h`).
- A row shows when its count is above 0 and `MaterialListFits` (the box of the active view, `Game/MaterialLayout.h`, inside the canvas's `safeBounds`); nothing shows before the first layout. With side insets (the narrowest window of either view) the lists hide and the balance stays (the owner's decision).
- **In the move panel** (the owner's decision): the canvas has a second set of both groups, `MaterialPanel*` (horizontal lists of 52-unit chips and a balance), over the panel's bands `MovePanelBandTopPlayer` (the upper group) and `MovePanelBandBottomPlayer` (the lower one), bound to the same variables, its icons given the same sprites. The component shows **exactly one set**, whole: the panel set while `MovePanelFits` holds for the Wide box and the canvas's `safeBounds` (so exactly while the Wide panel is shown; no fit rule of its own: the box lies in the safe area whenever shown), the margin set otherwise and before the first layout (default, Portrait and Tall view, a Wide window whose side insets cut the panel).
- **Known limits:** a "+39" beside a rank "8" may read as one number; with a top notch `Turn` (layer 10) covers the upper group's first rows and its balance (in Portrait only where the view fills the window, a 3:4 one; Tall is low enough to leave 9:16 a bar that takes the notch); moving the group breaks the mirror, so the fix is the owner's call. In the panel a top notch puts `Menu` and `Reset` over the right part of the upper group and its balance; the first chips stay clear.

## The move panel

Node `MovePanel` (`ChessMovePanel.xml`, layer 4, under `Material`, which draws over its bands): `ChessMovePanelComponent`, then a `SceneView` canvas (no safe area, not modal) with one root `MovePanelBox`, the plate right of the board in the **Wide view** (the box of `Game/MovePanel.h`), five bands 6 units apart: `MovePanelBandTopPlayer`, the list of 7 rows of 56 units (`MovePanelRow<i>`; the cell of the last move shows its gold `...Mark<i>`), `MovePanelBandBottomPlayer`, `MovePanelBandButtons` (steps, Take back) and `MovePanelBandAnalysis` (Hint and the evaluation toggle). The players' bands are empty: `Material` draws over them. Every name starts with `MovePanel`. The rows are two columns, of four and three (`MovePanelColumn0/1`), one under the other in Wide; **in the Tall view** a variant of the canvas (`maxAspect:0.6`) puts the box under the board and the lower material row, hides the two player bands (the material keeps its Portrait rows there), sets the columns side by side and, lacking room for a sixth band, swaps `MovePanelBandAnalysis` for the twins `MovePanelTallHint` / `MovePanelTallEval` in the buttons band.

- **Analysis band:** `ChessHint` and `ChessSetEvaluation` are answered by `ChessMovePanelComponent` (class comment); `{analysisText}` is `AnalysisText::Evaluation` over `{analysisBar}`. **Known limit:** the evaluation is of the game's position, not a viewed one; windows without the panel have neither control (the owner's decision).
- The component (with `MoveRows`) rebuilds `BuildMoveList` and `BuildMovePanelRows` only when the model, its version or its shown move count changed (`MoveRowsKey`; a view moves no version), writes the `movePanel*<i>` words of the 7 rows and shows the rows and marks the entries say; the rows are the **page** of the shown move (the engine's UI has no scroll: `Game/MovePanel.h`), its cell marked.
- **Where it is hidden:** `MovePanelBox` is shown only while `MovePanelFits` holds for the box of the active view (`MovePanelView`: Tall for `Tall`, else Wide) and the canvas's `safeBounds`, asked on every draw and never before the first layout. The default and Portrait views hold neither box; side insets cut it in the narrowest Wide windows and in every Tall window. Those windows have the menu section `Moves`: node `MovesSection` (`ChessMovesSection.xml`, layer 20, modal), built like the opponent panel, the same 7 rows and step buttons bound to the same variables (its component writes them too); it is in every window's menu and stays open while it views.
- **Clicks (viewing):** the cells of the moves and the step buttons `‹` `›` `»` (`<prefix>StepBack`, `StepForward`, `Latest`: 56 x 56 at the left of the buttons band) are buttons of one action `ChessShowMove`, its `value` the press of `Game/MovePanel.h` (`white<i>`, `black<i>`, `back`, `forward`, `latest`); `ChessMovePanelComponent` answers it for both canvases (their rows are the same) with `ChessGameModel::ShowMoves`. The cell of the last move and `»` return to the game, so do a press on the board and any change of the game.
- **Take back** (`<prefix>TakeBack`, the rest of the band): action `ChessTakeBack`, answered by `ChessMovePanelComponent` for both canvases with `TakeBack(TakeBackPliesNow())` (`Game/TakeBack.h`); each canvas's component greys its own (clicker `disabled`) while that is 0. While viewing it ends the view and takes back from the game's last move. Nothing else in either list takes a click.
- **Known limit:** the HUD follows the safe area, the box does not. A notch pushes `Menu` and `Reset` down onto the top band; the rows and the two lower bands are clear of the HUD in every window. The home bar of side insets lifts `Status`, so in the Wide view a variant of `ChessHud.xml` makes it 38 units high on the safe edge: never over the box (the owner's rule).

## The HUD

`SceneView` canvas on `Hud` (`ChessHud.xml`, layer 10), controls 56 units high in the margins: `Turn` (`{turn}`), `Resign`, `Menu` (fixed word, `ChessOpenMenu`; no button is named after a state, none is called `Opponent`), `Reset` (`ChessReset`), `Opening` (`{opening}`, bottom left: `OpeningDisplayName` of the shown position; a name that no longer fits is a lower `kOpeningMaxChars` or a smaller font; numbers: `ChessHud.xml`), `Status` (`{status}`; wider in Portrait). The anchors keep the top row above the board and `Status` under it in every view (in Wide at the right end, over and under the move panel's box). The widest sentences only just fit the narrowest window (480 px with insets): a longer one needs a reword or a wider box; an error says what went wrong, never what to do.

**`Resign`** (in Wide left of the panel's box, clear of the chips under a notch) emits the section's `ChessResign` (one rule, `ChessGame::ResigningNow()`, also greys it); the section shows only its plate `GameSectionConfirm` (Yes / No; No returns to the game). Hidden in Portrait and Tall (no room).

`ChessHudComponent` republishes the variables only on a change (class comment): `turn` (`StatusText::Turn`, "Computer is thinking", also while a draw offer is judged), `status` (while viewing `StatusText::Viewing`, over all; else a standing error of the opponent, over "Check"; else "Draw declined" until the version moves; else the game status), `opening`, `opponentError`, and the panel bindings `opponentMode`, `opponentColor` (**the person's** color), `opponentEngine`, `opponentLevel`.

## The menu

Node `MenuPanel` (`ChessMenuPanel.xml`, layer 20, modal, `visible="false"`): `MenuDim`, `MenuPlate`, `MenuTitle`, one 56-unit button `MenuSection<Name>` per section (`ChessOpenSection`, the section id as `value`) and `MenuClose`; names start with `Menu`. `ChessOpenMenu` shows the list and hides every section, `ChessOpenSection` swaps the list for the section's canvas (an unknown id logs one warning), `ChessCloseMenu` hides all. **Escape** (the raw `KeyDownEvent`, `EscapeKey.h`; the engine has no key mapping) emits `ChessCloseMenu` while the list or a section is shown and `ChessCancelPromotion` while a promotion is pending, the button's own action; else nothing. `MenuDim` is a button of its own with the action of `MenuClose`, so a click beside the plate closes the list; so do the dim of every section (`Dim` of the opponent panel, `<Section>Dim` of the others) and `PromotionDim` of the promotion picker, each with `stateLook="false"` (no hover look) and always the first child of its canvas, so a click on a plate never reaches it. The menu is no state; its sections are the table `Game/MenuSections.*`: `Opponent`, `Moves` ("The move panel"), `Game` ("The Game section"; label "Resign / Draw") and `Sound` (`Sound/CLAUDE.md`). **A new section** is a table row, a button, its own panel whose one bottom button is `Back` (`ChessOpenMenu`, the whole row; no `Close`: its dim closes everything), and 68 units more in `MenuPlate`.

## The Game section

Node `GameSection` (`ChessGameSection.xml`, layer 20, modal, hidden), built like `MovesSection`. Resign asks "Resign?", Draw between two people "Agree a draw?" (`{gameQuestion}`, Yes / No); Draw against the program offers at once, the HUD answers. `ChessGameSectionComponent` keeps the **one UI-only state, the open question**, dropped by any menu change, No, and a change of the version or the opponent. Yes asks the rules again (`ChessGame::ResigningNow()`, `DrawOfferRefusalFor`), ends the game and closes the menu; the same rules grey the buttons.

## The opponent panel

Node `OpponentPanel` (`ChessOpponentPanel.xml`, layer 20, modal, `visible="false"`), after the engine's choice-panel template (`engine/src/Modules/BECore/UI/CLAUDE.md`, "Choice panels"): `Dim` (a click on it closes the panel, as in "The menu"), `Plate`, four `UISegmented` rows with the control API ids as segment values, `ModeGroup` (`opponentMode`, `ChessSetMode`), `ColorGroup` (`opponentColor`, `ChessSetColor`), `EngineGroup` (`ChessSetEngine`), `LevelGroup` (`ChessSetLevel`), the `OpponentError` line and `OpponentBack`. A segment needs an explicit `height`. The modal panels are centered plates and fit every view; only this one has a Portrait variant.

- The group owns no choice: a press emits the action, `ChessOpponentPanelComponent` picks through `ChessGame::GetActiveChoice()` and `ChessHudComponent` publishes the variable, so the panel shows what the game has. An id in no table logs one warning.
- **The color is the person's**; `opponent.plays` of the control API is the program's.
- A pick applies at once and the panel stays open. No group is greyed (a disabled group read as a dead button): a pick made while two people play is kept and applies when Computer is picked.
- A refused pick changes nothing (no fallback, the program is not switched off); "Engine not found" stays in `OpponentError` and `Status` until someone's move, a reset, `set_state` or the next pick. The panel never offers a Stockfish that is not found.
- No name may be in two canvases, hence `ModeGroup`, not `Mode`.

## Draw order

By layer, not by place among the references of `SceneConfig.xml` (`engine/src/Modules/BECore/Scene/CLAUDE.md`, "Draw order"); the same layer draws in scene order. `Board` and then `BoardCoordinates` -1, pieces 0, `Result` 1, `MovePanel` 4, `Material` 5, a dragged piece 6, `Hud` 10, the panels and the menu 20.
