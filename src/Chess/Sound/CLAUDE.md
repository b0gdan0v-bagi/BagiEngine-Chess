# Chess sound (src/Chess/Sound)

The sound of the board: one clip per played move, asked of `CoreManager::GetAudio()` (`engine/src/Modules/BECore/Audio/CLAUDE.md`).

```
Sound/
├── SoundCue.*               pure: SoundCue (CORE_ENUM), MoveOutcome, CueOfMove, ClipPath, CueVolume, CueBus, ActionClicks; no scene, no audio manager
└── ChessSoundComponent.*    scene glue on the node Sound (config/nodes/ChessSound.xml): plays the cue of a move that was just played
```

## The cue of a move

One clip per move, the first row that holds, so a move gets the most important thing it did: the game is over after it (`GameEnd`: mate, stalemate and the draws, whatever else the move was), a promotion, the side to move is in check, castling, a piece left the board (en passant too: it is judged by the piece count, not by the target square), else a plain move. A promotion that mates is the end of the game; a castling that checks is a check. The order and the volume of each cue are in `SoundCue.cpp`, one table; change them there. `MoveOutcome` carries only numbers and flags, so `SoundCueTest` feeds it real positions and needs no scene.

## When the component plays

`ChessSoundComponent` watches the active model on every `SceneUpdateEvent` and plays exactly when the move list of the **same model** grew since the last frame. It keeps the model pointer (compared, never read), the move count and the piece count: observation, not game state, as `ChessPieceComponent` does. So a move gets its sound whoever made it (click, drag, `TryMove`, the program, the control API) and at the first frame after it, while the piece is still sliding.

It asks for **nothing** on: the first sighting of a model; a count that is not larger (`Reset`, a takeback, `set_state {fen}`); a game restored from a save before the first frame; viewing an earlier position (`ShowMoves`, which moves no move list); a selection or a click that moves nothing; a pending promotion (no move yet; the move's cue plays when the piece is chosen); steady frames; no active game. Several moves in one frame play the cue of the last one, the capture judged by the piece count across the frame: an approximation.

A steady frame allocates nothing (`ChessUiAllocTest`): the clip paths are `PoolString`s interned once and the component compares three numbers. The clips are loaded in `OnAttached` through `ResourceManager` (cached by path, the first load is synchronous), so no move waits for a decode. `ChessTests` has no audio backend and does not mount `assets/`: a request is still recorded (`GetRecentPlays()`, `GetRequestCount()`), which is what the tests read; the clip files are checked from the source tree.

## The clips

`tools/make_sounds.py` synthesizes the six files of `assets/sounds/` (standard library only, a fixed seed per clip, so the bytes are the same on every run); `--check` exits 1 when a file differs from what the script makes. Nobody could listen on the build machine: the numbers are checked (rate, peak below 1 at master 1, a fade at both ends, the lengths), not the sound. To replace a clip by hand drop its entry from the script (else `--check` fails and the next run overwrites it), keep the name and give its row in `THIRD_PARTY_NOTICES.md` the author and the licence. A new clip needs its cue in `SoundCue.cpp`, its row in `THIRD_PARTY_NOTICES.md` and its line in `tests/check_web_package.cmake`; `ChessSoundFilesTest` fails on a `.wav` that no cue names.

## Tests

`SoundCueTest` (the rule over real positions, the table), `ChessSoundFilesTest` (each clip decodes, mono, not clipping, a quiet end, no stray file), `ChessUiSoundTest` (the shipped scene: one request per move for each way of playing, none for each of the cases above) and the control smoke (`audio_state` shows the clip and the bus after `perform_action`; `played` is not asserted, a headless run plays to SDL's dummy driver).
