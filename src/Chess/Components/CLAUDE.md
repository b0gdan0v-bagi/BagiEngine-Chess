# Chess scene and UI (src/Chess/Components, config/SceneConfig.xml, config/nodes/)

Read this before changing `config/SceneConfig.xml` or a file of `config/nodes/` (which file holds which node: `config/CLAUDE.md`). Siblings: `src/Chess/Engine/`, `src/Chess/Game/`, `tests/`. Components keep no game state: everything is read from the model (`ChessGame::GetActiveModel()`) on every update or draw.

## Files

```
ChessBoardComponent     the board: squares, highlights, coordinates, clicks and drags, the frame (the opponent's, analyst's and draw judge's Update(), the keeper, the result log)
ChessHudComponent       on Hud, before its canvas: publishes the UIVars of the HUD and the opponent panel, greys Resign, answers ChessReset
ChessMenuComponent      on MenuPanel: ChessOpenMenu, ChessOpenSection, ChessCloseMenu, Escape
MenuActions             the menu's actions that several panels answer
ChessOpponentPanelComponent  on OpponentPanel, before its canvas: ChessSet*, the Stockfish segment
ChessPromotionPanelComponent on PromotionPanel, before its canvas: shown while a promotion is pending; ChessPromote, ChessCancelPromotion
ChessMaterialComponent  on Material, before its canvas: icons, the material... variables, which set and rows are shown
ChessMovePanelComponent on MovePanel, before its canvas: the move list's variables, rows and marks, the box where it fits; ChessShowMove, ChessTakeBack
ChessMovesSectionComponent on MovesSection ("Moves"), before its canvas: the same variables, its own rows and marks
ChessGameSectionComponent on GameSection ("Resign / Draw"), before its canvas: ChessResign, ChessDraw, ChessGameYes, ChessGameNo
ChessPositionSectionComponent on PositionSection ("Position"), before its canvas: ChessEditBegin, ChessEditSide, ChessEditCastle, ChessEditPlay, ChessEditCancel
ChessEditorBarComponent on EditorBar, before its canvas: shown while a position is edited; ChessEditPick, ChessEditColor
PieceSprite             `PieceSpriteId(Piece)`: the one table of the piece sprites (pieces, material icons, the editor bar)
AnalysisRows            the move panel's analysis band: the words and bar from the analyst, the grey of the Hint buttons
MoveRows                the part both share: the game's rows, their words, the rows and marks of one canvas, the takeback
ChessPieceComponent     one piece node: follows its piece in the model, slides a played move, switches sprite after a promotion
ChessResultComponent    its own node (layer 1): dims the board and draws the result bar once the game is over
```

**Each UI node answers its own actions** with its own component; the board knows only the picker (`ChessPromotionPanelComponent::SyncActive()` after a click: two components get one event in no defined order). **A new panel** is a node file with its own component and `tests/ChessUi<Topic>Test.cpp`, no change to the board.

## The board

- **Four views** (`viewVariants` of `ChessScene`: the default, `Tall`, `Portrait` and `Wide`; sizes in `config/CLAUDE.md`, why in the comment of `SceneConfig.xml`). The board is 640 x 640 in all (a square 80 units), at (100, 80) in the default and Wide view and (40, 144) in Portrait and Tall. **The one rule: a view is widened or heightened only where the other side already limits the view it replaces**: Wide and Tall never shrink a board, font or touch target; the letterbox they take is the move panel's. Tall is Portrait with 244 units more at the bottom, every place of Portrait kept (`HasPortraitPlaces()`). A canvas lays out per view with a variant `orientation:portrait` (on the canvas's bounds, the view in units; it holds in Tall too); Wide needs no variant of its own, the HUD's anchors move its right end.
- **Geometry** is in scene units of the active view: `_originX/_originY/_squareSize` (default and Wide) and the `_portrait...` triple (Portrait and Tall), one set taken on `SceneViewChangedEvent` (`HasPortraitPlaces(name)`) that every accessor reads, so the squares, pieces and result bar never disagree; a drag in the hand ends with the view.
- **Orientation:** the board is drawn from Black's side exactly when the program plays White, or while a puzzle is on that Black solves (`ChessGame::BoardBottomColor()`, read live, for the board and the material lists). While a puzzle is on the click flow asks `ChessGame::ProgramToMoveNow()` (every turn but the solver's: no click, no premove, whatever the opponent is) and `OnUpdate` runs `UpdatePuzzle()` and skips the opponent's `Update()`. `SquareToPixel` and `OnMouseDown` map through `Game/BoardOrientation.h`; everything placed on a square uses them. The HUD, panels, model, control API and FEN do not turn.
- **Coordinates:** 16 labels of a `SceneView` canvas on `BoardCoordinates` (layer -1, no safe area, no `blocksInput`). The rects are fixed per view, the text (`coordRank<row>`, `coordFile<column>`, `Game/BoardCoordinates.h`) turns; a turn keeps the grid's parity, so the colors are static: `boardDark` on a light cell, `boardLight` on a dark one.
- `ChessOpponentPanelComponent` shows the segment `EngineStockfish` exactly while `OpponentController::OffersStockfish()` (a **cached** answer; `RefreshStockfish()` looks again when the opponent section opens).

## Highlights

Order inside the board's `OnDraw()`: squares, the last move (castling marks the king only), the check, the hint (while the analyst has one and the game is not over), the premoves, then the selection and its legal targets, last so they stay readable. **The colors are the tokens `board*` of `config/UIThemes.xml`**, read through `BoardPalette`. **While an earlier position is viewed** (`ChessGameModel::IsViewing()`) the marks are those of `GetShownPosition()` (its move, its check), with no hint, premove, selection or result dim; a press on the board only ends the view.

## The pieces

- A piece node is a reference of the prefab `config/nodes/ChessPiece.xml` (`config/CLAUDE.md`): the piece component, a sprite and a transform; the node is the whole square, the sprite `fit="Contain"`.
- A piece stands on `GetShownSquareOf()`: the viewed position's square while viewing; a view that begins or ends places them at once.
- **A played move slides** (`ScopedTween`, `_slideDuration`, reflected, not set in the scene). Only a change of exactly one move and one version with time in the frame slides; everything else snaps (first sighting, reset, `set_state`, several moves, a frame with `deltaTime` 0). A captured piece leaves when the slide ends.

## The premove

While the program thinks and the person has the other color, `OnMouseDown` hands the click to `ChessGameModel::OnPremoveClicked` (rules: `src/Chess/Game/CLAUDE.md`): an own piece selects, a target queues, any other click drops the queue. No drag, no promotion panel (a premoved promotion plays a queen). While two programs play the board ignores clicks.

## The drag

`ChessBoardComponent` only; the model knows nothing. The press acts at once (a click); a left press on the selected square, with the game on and no promotion pending, arms a drag, lifted past `kDragThreshold`. A release over a legal target calls `OnSquareClicked(target)`; anywhere else the piece stays selected (a release on another own piece does not select it, the owner's decision). `IsDragValid` drops the drag silently when the model moves under it. While lifted the piece is on `kDragLayer` (over pieces, result and material, under HUD and panels).

## The promotion picker

A click on a promotion target leaves it pending in the model; the node `PromotionPanel` (`ChessPromotionPanel.xml`, modal `SceneView` canvas, layer 20, `visible="false"`) is shown by `ChessPromotionPanelComponent` exactly while `HasPendingPromotion()`, checked after a click and every update and draw, so a reset, `set_state` or someone's move closes a stale one. Buttons `PromoteQueen` .. `PromoteKnight` (`ChessPromote`, the id of `Game/PromotionChoices.*` as `value`) and `PromotionCancel` / `PromotionDim` (`ChessCancelPromotion`: the pawn stays selected).

## The captured material

Node `Material` (`ChessMaterial.xml`, layer 5): `ChessMaterialComponent`, then a `SceneView` canvas (no safe area, not modal). Two groups mirrored about the board's middle (in Portrait two rows, above and below the board), `MaterialTop*` and `MaterialBottom*`, each a balance label and five rows (icon + count `{material<Top|Bottom><Type>}`). The variables are written only when the model, its version or the bottom color changed.

- The lower group is what the color at the bottom has taken (its icons are the other color's); the groups swap when the board turns. The balance (`BalanceText`, "+N" for the side ahead) is the difference on the board (`Game/CapturedMaterial.h`).
- A row shows when its count is above 0 and `MaterialListFits` (`Game/MaterialLayout.h`: the active view's box inside the canvas's `safeBounds`); nothing shows before the first layout. With side insets the lists hide and the balance stays (the owner's decision).
- **In the move panel** (the owner's decision): a second set, `MaterialPanel*` (horizontal lists of 52-unit chips and a balance), over the panel's player bands, on the same variables and sprites. The component shows **exactly one set**, whole: the panel set while `MovePanelFits` holds for the Wide box, the margin set otherwise and before the first layout.
- **Known limits** (moving a group breaks the mirror: the owner's call): a "+39" beside a rank "8" may read as one number; a top notch lets `Turn` cover the upper group (Portrait 3:4), and in the panel `Menu` and `Reset` cover its right.

## The move panel

Node `MovePanel` (`ChessMovePanel.xml`, layer 4, under `Material`, which draws over its bands): `ChessMovePanelComponent`, then a `SceneView` canvas (no safe area, not modal) with one root `MovePanelBox`, the plate right of the board in the **Wide view** (`Game/MovePanel.h`), five bands 6 units apart: `MovePanelBandTopPlayer`, the list of 7 rows of 56 units (`MovePanelRow<i>`), `MovePanelBandBottomPlayer`, `MovePanelBandButtons` (steps, Take back) and `MovePanelBandAnalysis` (Hint, the evaluation toggle). The rows are two columns, of four and three (`MovePanelColumn0/1`), one under the other in Wide; **in the Tall view** a variant of the canvas (`maxAspect:0.6`) puts the box under the board and the lower material row, hides the two player bands (the material keeps its Portrait rows), sets the columns side by side and swaps `MovePanelBandAnalysis` for the twins `MovePanelTallHint` / `MovePanelTallEval` in the buttons band.

- **Analysis band:** `ChessHint` and `ChessSetEvaluation` are answered by `ChessMovePanelComponent`; **Known limit:** the evaluation is of the game's position, not a viewed one; windows without the panel have neither control (the owner's decision).
- The component (with `MoveRows`) rebuilds the rows only when the model, its version or its shown move count changed (`MoveRowsKey`); the rows are the **page** of the shown move (the engine's UI has no scroll: `Game/MovePanel.h`), its cell marked.
- **Where it is hidden:** `MovePanelBox` is shown only while `MovePanelFits` holds for the box of the active view (`MovePanelView`: Tall for `Tall`, else Wide) and the canvas's `safeBounds`, asked on every draw, never before the first layout. The default and Portrait views hold neither box; side insets cut it in the narrowest Wide windows and every Tall one. The menu section `Moves` (node `MovesSection`, `ChessMovesSection.xml`, layer 20, modal, built like the opponent panel) has the same rows and step buttons on the same variables; it is in every window's menu and stays open while it views.
- **Clicks (viewing):** the move cells and the step buttons `‹` `›` `»` (`<prefix>StepBack`, `StepForward`, `Latest`: 56 x 56 at the left of the buttons band) are buttons of one action `ChessShowMove`, its `value` the press of `Game/MovePanel.h`, answered for both canvases with `ChessGameModel::ShowMoves`. The last move's cell and `»` return to the game, so do a press on the board and any change of the game.
- **Take back** (`<prefix>TakeBack`, the rest of the band): action `ChessTakeBack`, answered by `ChessMovePanelComponent` for both canvases with `TakeBack(TakeBackPliesNow())` (`Game/TakeBack.h`), greyed (clicker `disabled`) while that is 0. While viewing it ends the view and takes back from the game's last move.
- **Known limit:** the HUD follows the safe area, the box does not: a notch pushes `Menu` and `Reset` onto the top band. The home bar of side insets lifts `Status`, so in Wide a variant of `ChessHud.xml` makes it 38 units high: never over the box (the owner's rule).

## The HUD

`SceneView` canvas on `Hud` (`ChessHud.xml`, layer 10), controls 56 units high in the margins: `Turn` (`{turn}`), `Resign`, `Menu` (a fixed word, `ChessOpenMenu`), `Reset` (`ChessReset`), `Opening` (`{opening}`, bottom left: `OpeningDisplayName` of the shown position), `Status` (`{status}`; wider in Portrait). The anchors keep the top row above the board and `Status` under it in every view. The widest sentences only just fit the narrowest window: a longer one needs a reword; an error says what went wrong, never what to do.

**`Resign`** (in Wide left of the panel's box) emits the section's `ChessResign` (one rule, `ChessGame::ResigningNow()`, also greys it); the section shows only its plate `GameSectionConfirm` (Yes / No; No returns to the game). Hidden in Portrait and Tall (no room).

`ChessHudComponent` republishes the variables only on a change: `turn` (`StatusText::Turn`, "Computer is thinking", also while a draw offer is judged), `status` (while viewing `StatusText::Viewing`, over all; else a standing error of the opponent, over "Check"; else "Draw declined" until the version moves; else the game status), `opening`, `opponentError`, and the panel bindings `opponentMode`, `opponentColor` (**the person's** color), `opponentEngine`, `opponentLevel`.

## The menu

Node `MenuPanel` (`ChessMenuPanel.xml`, layer 20, modal, `visible="false"`): `MenuDim`, `MenuPlate`, `MenuTitle`, one 56-unit button `MenuSection<Name>` per section (`ChessOpenSection`, the section id as `value`) and `MenuClose`; names start with `Menu`. `ChessOpenMenu` shows the list and hides every section, `ChessOpenSection` swaps the list for the section's canvas, `ChessCloseMenu` hides all. **Escape** (the raw `KeyDownEvent`, `EscapeKey.h`) emits `ChessCloseMenu` while the list or a section is shown and `ChessCancelPromotion` while a promotion is pending, the button's own action; else nothing. `MenuDim`, the dim of every section (`Dim` of the opponent panel, `<Section>Dim` of the others) and `PromotionDim` are buttons that close the list (`MenuClose`'s action; `ChessCancelPromotion` for the picker), with `stateLook="false"` and the first child of their canvas, so a click on a plate never reaches them. The menu is no state; its sections are the table `Game/MenuSections.*`: `Opponent`, `Moves` ("The move panel"), `Game` ("The Game section"; label "Resign / Draw"), `Sound` (`Sound/CLAUDE.md`) and `Position` ("The position editor"). **A new section** is a table row, a button, its own panel whose one bottom button is `Back` (`ChessOpenMenu`, the whole row), and 68 units more in `MenuPlate`.

## The Game section

Node `GameSection` (`ChessGameSection.xml`, layer 20, modal, hidden), built like `MovesSection`. Resign asks "Resign?", Draw between two people "Agree a draw?" (`{gameQuestion}`, Yes / No); Draw against the program offers at once, the HUD answers. `ChessGameSectionComponent` keeps the **one UI-only state, the open question**, dropped by any menu change, No, and a change of the version or the opponent. Yes asks the rules again (`ChessGame::ResigningNow()`, `DrawOfferRefusalFor`, which also grey the buttons), ends the game and closes the menu.

## The opponent panel

Node `OpponentPanel` (`ChessOpponentPanel.xml`, layer 20, modal, `visible="false"`), after the engine's choice-panel template (`engine/src/Modules/BECore/UI/CLAUDE.md`, "Choice panels"): `Dim`, `Plate`, four `UISegmented` rows with the control API ids as segment values, `ModeGroup` (`opponentMode`, `ChessSetMode`), `ColorGroup` (`opponentColor`, `ChessSetColor`), `EngineGroup` (`ChessSetEngine`), `LevelGroup` (`ChessSetLevel`), the `OpponentError` line and `OpponentBack`. A segment needs an explicit `height`.

- The group owns no choice: a press emits the action, `ChessOpponentPanelComponent` picks through `ChessGame::GetActiveChoice()` and `ChessHudComponent` publishes the variable, so the panel shows what the game has. An id in no table logs one warning.
- **The color is the person's**; `opponent.plays` is the program's.
- A pick applies at once and the panel stays open. No group is greyed (it read as a dead button): a pick made while two people play is kept and applies when Computer is picked.
- A refused pick changes nothing (no fallback); "Engine not found" stays in `OpponentError` and `Status` until someone's move, a reset, `set_state` or the next pick. The panel never offers a Stockfish that is not found.
- No name may be in two canvases, hence `ModeGroup`, not `Mode`.

## The position editor

While `ChessGame::IsEditing()` (the model shows a draft, `Game/CLAUDE.md`) the scene shows the draft and reads nothing of the game's position:

- **Board:** a left press puts the picked piece (the same piece again or the eraser takes it off), a right press takes the piece off, each then `EditChanged()`. No select, drag, premove or promotion; squares and coordinates only, no mark. `OnUpdate` skips the opponent's and the draw judge's `Update()` (a move of the program would end the draft), the analyst runs.
- **Pieces** snap, as under a view (`ShowsOtherPosition`). No result dim; the material's sets and the move panel's box are hidden; `ChessShowMove` and Hint do nothing (they would end the draft).
- **The section `Position`** (node `PositionSection`, layer 20, modal, built like `GameSection`): the three starts (each closes the menu), the side and four castling switches (greyed while the king or rook is off its square), Copy / Paste FEN, Play from here (a refusal keeps the menu open, its reason in `{editorMessage}`), Cancel. Play, Cancel and the switches are grey with no editor. Pieces are not placed here (a modal panel hides the board).
- **Copy / Paste:** Copy writes the draft's FEN (else the game's) to `CoreManager::GetClipboard()`. Paste and Ctrl+V (`PasteEvent`, taken only while editing or with the section shown) call `ChessGame::PasteFen`: it begins the editor when none is open and loads the text, a bad one changes nothing. What the clipboard or the rules say is the message (`EditorText`, also with no editor: it is dropped when the menu closes or returns to the list). The read's callback holds no pointer: the game is found at the answer.
- **The bar** (node `EditorBar`, layer 20, non-modal; shown exactly while editing, checked on every update and draw): six piece buttons (icons follow the picked color), Erase, the color, Play (`ChessEditPlay`, the section's) and More (opens the section); the picked tool is the `selected` button. A wrapping stack of 56-unit buttons in the bottom margin where `Opening` / `Status` stand; two rows in Portrait and Tall, at the right of the board in Wide. **Known limit:** where a home bar lifts the safe edge in a default-view window (an inset 4:3 one) the row covers 20 units of the lowest rank.
- **HUD:** `turn` is `StatusText::Editing()`, `status` and `opening` are empty; republished when the editing flag, the editor's refusal or the message changes (none moves a version).

## Draw order

By layer, not by place among the references of `SceneConfig.xml` (`engine/src/Modules/BECore/Scene/CLAUDE.md`, "Draw order"); the same layer draws in scene order. `Board` and then `BoardCoordinates` -1, pieces 0, `Result` 1, `MovePanel` 4, `Material` 5, a dragged piece 6, `Hud` 10, the panels, the menu and the editor bar 20 (the bar is listed before the menu, so a panel draws over it).
