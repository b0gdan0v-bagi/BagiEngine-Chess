# Chess sound (src/Chess/Sound)

The sound of the board (one clip per played move) and of the buttons, and the menu section `Sound`, asked of `CoreManager::GetAudio()` (`engine/src/Modules/BECore/Audio/CLAUDE.md`).

```
Sound/
├── SoundCue.*                   pure: SoundCue (CORE_ENUM), MoveOutcome, CueOfMove, ClipPath, CueVolume, CueBus, ActionClicks; no scene, no audio manager
├── ChessSoundComponent.*        scene glue on the node Sound (config/nodes/ChessSound.xml): plays the cue of a move that was just played and the click of a button
├── SoundActions.h               the name of the action ChessSetVolume, shared by SoundCue and the panel
├── SoundLevels.*                pure: the slider's step, ParseVolume, ParseMuted, VolumeVarText, VolumePercentText
└── ChessSoundPanelComponent.*   scene glue on the node SoundPanel (config/nodes/ChessSoundPanel.xml): the section's answers and variables
```

## The cue of a move

One clip per move, the first row that holds, so a move gets the most important thing it did: the game is over after it (`GameEnd`: mate, stalemate and the draws, whatever else the move was), a promotion, the side to move is in check, castling, a piece left the board (en passant too: it is judged by the piece count, not by the target square), else a plain move. A promotion that mates is the end of the game; a castling that checks is a check. The order and the volume of each cue are in `SoundCue.cpp`, one table; change them there. `MoveOutcome` carries only numbers and flags, so `SoundCueTest` feeds it real positions and needs no scene.

## When the component plays

`ChessSoundComponent` watches the active model on every `SceneUpdateEvent` and plays exactly when the move list of the **same model** grew since the last frame. It keeps the model pointer (compared, never read), the move count and the piece count: observation, not game state, as `ChessPieceComponent` does. So a move gets its sound whoever made it (click, drag, `TryMove`, the program, the control API) and at the first frame after it, while the piece is still sliding.

It asks for **nothing** on: the first sighting of a model; a count that is not larger (`Reset`, a takeback, `set_state {fen}`); a game restored from a save before the first frame; viewing an earlier position (`ShowMoves`, which moves no move list); a selection or a click that moves nothing; a pending promotion (no move yet; the move's cue plays when the piece is chosen); steady frames; no active game. Several moves in one frame play the cue of the last one, the capture judged by the piece count across the frame: an approximation.

A steady frame allocates nothing (`ChessUiAllocTest`): the clip paths are `PoolString`s interned once and the component compares three numbers. The clips are loaded in `OnAttached` through `ResourceManager` (cached by path, the first load is synchronous), so no move waits for a decode. `ChessTests` has no audio backend and does not mount `assets/`: a request is still recorded (`GetRecentPlays()`, `GetRequestCount()`), which is what the tests read; the clip files are checked from the source tree.

## The clips

The seven files of `assets/sounds/` are generated with Stable Audio Open 1.0 through the editor's Generate panel (`/artgen`, seed 7100, the game's presets `chess_foley` for move, capture, castling and the click and `chess_cue` for check, promotion and the end), chosen by the owner by ear and accepted over their paths (mono, 44100 Hz, trimmed and cut to a length per clip, peak -3 dBFS, the click -8). Beside each is `<name>.artgen.json`, its provenance (model, prompt, seed, the processing at Accept): it has a row in `THIRD_PARTY_NOTICES.md` and stays out of the web package (`cmake/ChessWeb.cmake`). To remake a clip, generate with its preset and seed and accept it over the same path; the tests check its numbers (rate, mono, peak below 0.95, at most 2 s, a quiet end), not the sound. A new clip needs its cue in `SoundCue.cpp`, its rows in `THIRD_PARTY_NOTICES.md` and its line in `tests/check_web_package.cmake`; `ChessSoundFilesTest` fails on a `.wav` that no cue names. (The synthesized placeholders of `tools/make_sounds.py` before them are in the git history.)

## The buttons

`ChessSoundComponent` also answers every `InputEvents::ActionEvent` with the `Click` cue on the `Ui` bus (`CueBus`: the bus is a field of the cue's table row, so it cannot drift from the clip), one request per action, except `ChessSetVolume` (`ActionClicks`): the slider emits on every step of a drag, and a click per step is noise. The mute switch is a button and clicks. A disabled button emits no action, so it is silent. `CueOfMove` never gives `Click`. Two components get one event in no defined order: a click on the mute switch may be recorded before or after the mute applies; the request is recorded either way, so no test may depend on the order.

## The options

The menu section `Sound` (`SoundPanel`) has the master volume (a slider, steps of `kVolumeStep`) and the mute, nothing else: `Sfx` and `Ui` keep the config's level and have no row (the saved `audio` slot holds all four, so a row needs no new file). `ChessSoundPanelComponent` answers `ChessSetVolume` and `ChessSetMute` with `SetMasterVolume` / `SetMasterMuted`; a value that does not parse (`SoundLevels`) changes nothing and logs one warning. It publishes `soundVolume`, `soundVolumeText` and `soundMuted` to `UIVars` in `OnDraw` only when `GetLevelsRevision()` moved (and at the first draw), so a level set by `audio_set` or restored from a save shows at the next draw and a steady frame writes nothing; the widgets own no level, and a muted master keeps its volume, so the slider still shows it. Every element name of the node starts with `SoundPanel` (names are found in every canvas).

## The levels between runs

`ChessSaveKeeper` keeps the master and the buses in the engine's `audio` slot beside the game, under the same gate: `src/Chess/Save/CLAUDE.md`.

## Tests

`SoundCueTest` (the rule over real positions, the table), `ChessSoundFilesTest` (each clip decodes, mono, not clipping, a quiet end, no stray file), `ChessUiSoundTest` (the shipped scene: one request per move for each way of playing, one click per button, none for each of the cases above), `SoundLevelsTest`, `ChessUiSoundPanelTest` (the section: matrix, real clicks, the levels, `ScopedAudioLevels` keeps the process-wide manager clean) and the control smoke (`audio_state` shows the clip and the bus after `perform_action` and after `ui_click`; `audio_set` shows a level and a move still records its request; a level survives a quit and a start on one `--save-dir` and is not read without one; `played` is not asserted, a headless run plays to SDL's dummy driver).
