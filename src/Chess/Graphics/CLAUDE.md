# Chess graphics presets (src/Chess/Graphics)

The look of the game is one of two presets: `Classic` (the default: a new player, a missing save) and `Beer`. A preset is two things: the sprites of the pieces (the plain sprites of the classic atlas `assets/chess_test.png`, or the atlas group `pieces`) and the board theme of `config/UIThemes.xml` (`classic`, `tavern`).

```
Graphics/
├── GraphicsPreset.*   pure: GraphicsPreset (CORE_ENUM), PresetId / ParsePreset (exact lower-case ids "classic", "beer"), ThemeOf, SpritePrefix; one table
├── GraphicsChoice.h   the current preset and its revision (starts at 1, grows on a change only, never goes back); no scene, no UI manager
└── ChessGraphicsPanelComponent.*  scene glue on the node GraphicsPanel (config/nodes/ChessGraphicsPanel.xml): the section's answer, its variable and the board theme
```

## The sprite names

`PieceSpriteId(Piece, GraphicsPreset)` (`Components/PieceSprite.h`) is the one table: `<SpritePrefix><Color><Type>`. Beer has no prefix (`WhiteKing` ...): the Generate panel's Accept names a sprite after its file `assets/artgen/<Name>.png`, so those names stay. Classic is `ClassicWhiteKing` ... `ClassicBlackPawn`.

The choice is the person's look, not part of a game: a reset leaves it alone (`ChessGame::Start()` sets Classic, a later work package of the entry restores the saved one after that). The scene files name the Classic sprites; the components that show a piece follow the choice's revision (`src/Chess/Components/CLAUDE.md`, "The pieces").

## The section and the theme

The menu section `Graphics` (node `GraphicsPanel`, one `UISegmented` "Classic | Beer") emits `ChessSetGraphics` with the preset id; `ChessGraphicsPanelComponent` (listed before its canvas) sets the game's choice, writes `graphicsPreset` in `OnDraw` and calls `UIThemes::SetTheme(ThemeOf(preset))` in `OnUpdate`, each only when the choice's revision differs from the last one handled. The theme follows the **choice**, not the click, so a pick by the control API or a restored save recolours the board as a click does; `config/UIThemes.xml` has `default="classic"`, the theme of the default preset, so the file and the game agree before the first update. A work package after this one wires the save and the control API.
