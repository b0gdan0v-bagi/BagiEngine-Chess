# Save: the game and the opponent choice between runs

`ChessSaveState` (the file's content, `Capture`, `Validate`, `Apply`), `SaveSettings` (the switch and the folder policy), `ChessSaveKeeper` (owned by `ChessGame`). The storage is the engine's `SaveGameService` (`engine/src/Modules/BECore/Save/CLAUDE.md`; read "What a consumer does with a refused save"). The headers carry the contracts; this file the decisions around them.

## What is saved

Slot `game`, schema `{"Chess", 1, 1}`: the start FEN, the moves (UCI, one space-separated string), which colors the program plays, the engine and level id of each seat (the control API's ids, from the existing id tables) and the person's color. **Not saved:** selection, a pending promotion, the analysis switches, hints, ratings (they start off, as the root `CLAUDE.md` says), the takeback stack. A **finished** game is not restored (a new game starts); the opponent choice still is. A new field needs no version (a file without it keeps the default); raise the version when a meaning changes, then an older program refuses the file as `VersionTooNew` and leaves it alone.

## The gate (`ResolveSaveDirectory`)

A run reads and writes only when `config/ChessSettings.xml` (or `ChessSettings.local.xml`, which wins) says `rememberGame="true"` (the committed file does) **and** the run is not a control or headless one. `--control`, `--control-port=`, `--headless` and `--manual-clock` switch it off, so a control run neither reads nor overwrites a person's game; the flags are the contract because `Start()` runs before the control server and cannot read its resolved settings. `--no-save` switches it off everywhere; `--save-dir=<path>` is the explicit ask and turns it on in any run (this is how the smoke and the gtests exercise it, always with a temporary folder). `config/` is not mounted in `ChessTests`, so a gtest with no flag is clean. The folder: `--save-dir`, else `PersistentSaveDirectory()` (the browser), else `saves` under `SDL_GetPrefPath("BagiEngine", "BagiChess")` (on Windows `%APPDATA%\BagiEngine\BagiChess\saves`). A folder that cannot be made is one warning, persistence off, the game runs.

## When it reads and writes

- **Read:** once, at the end of `ChessGame::Start()`, into the fresh model and choice, with no dialog. **Write:** `ChessGame::UpdateKeeper()` (from `ChessBoardComponent::OnUpdate`, never a draw) when the key (model version, controller revision, person's color: no allocation) moved, at most once per `kSaveDebounceSeconds` of game time; and `Stop()` flushes first, before the opponent is shut down. The browser tab that closes does not reach `Stop()`, which is why the debounced write exists.
- `Stop()` sets a flag: frames that still run after it write nothing, so the `Shutdown()` of the controller (two people) never becomes the saved choice. `Start()` clears it.
- What `Start()` restored is taken as already written, so a start writes nothing until the game changes.

## A refused save

Everything is checked on scratch objects first (FEN with `Position::FromFen`, each move with `MoveGenerator::ParseUci` on a scratch position, each id and level against its table); only then it is applied, so a refusal changes nothing. One warning with the slot and the reason, never the contents. **The file is left alone** (not deleted, not rewritten) until the game's own next change writes a good one. `NotFound` is silent (the first run); another game or a newer version is a refusal, never a crash. A pick the controller refuses at restore (Stockfish saved, none now) is not a failure of the restore: the position comes back, the opponent stays two people, `opponent.error` says "Engine not found", nothing falls back to the other engine, and the next write saves what the game really has.

## Testing

`ChessSaveStateTest` (round trips through a real `SaveGameService`, the damage list), `ChessSaveKeeperTest` (settings, policy, debounce with `CoreManager::GetTime().Advance`, never a sleep), `ChessSaveGameTest` (the real `GameManager` and scene, `--save-dir` set in the test), and the restart scenarios of `tests/control_smoke.py` (a real quit and start on one folder). A test that names a folder always uses a temporary one, never the person's.
