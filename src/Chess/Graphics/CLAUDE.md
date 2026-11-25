# Chess graphics presets (src/Chess/Graphics)

The look of the game is one of two presets: `Classic` (the default: a new player, a missing save) and `Beer`. A preset is two things: the sprites of the pieces (the plain sprites of the classic atlas `assets/chess_test.png`, or the atlas group `pieces`) and the board theme of `config/UIThemes.xml` (`classic`, `tavern`).

```
Graphics/
├── GraphicsPreset.*   pure: GraphicsPreset (CORE_ENUM), PresetId / ParsePreset (exact lower-case ids "classic", "beer"), ThemeOf, SpritePrefix; one table
└── GraphicsChoice.h   the current preset and its revision (starts at 1, grows on a change only, never goes back); no scene, no UI manager
```

## The sprite names

`PieceSpriteId(Piece, GraphicsPreset)` (`Components/PieceSprite.h`) is the one table: `<SpritePrefix><Color><Type>`. Beer has no prefix (`WhiteKing` ...): the Generate panel's Accept names a sprite after its file `assets/artgen/<Name>.png`, so those names stay. Classic is `ClassicWhiteKing` ... `ClassicBlackPawn`.

The choice is the person's look, not part of a game: a reset leaves it alone. The scene glue (the Graphics menu section and the theme) arrives in a later work package of the entry.
