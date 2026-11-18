# Third-party notices

The game (the code of this repository and its own art) is under the Beerware licence (`LICENSE`, the same wording as the engine's).
This file lists what **this repository** carries that was not written for it, and what the game does with Stockfish. What the engine
carries (SDL, EnTT, {fmt}, the fonts, ...) is in `engine/THIRD_PARTY_NOTICES.md`, with the licence files next to the code: a game
built on the engine contains the rows of that file marked **ships**, so a released Chess carries those notices too.

`tests/check_notices.py` (ctest `BagiChessThirdPartyNotices`) holds this file to the tree: every file of `assets/` must have a row, each row's
path and licence file must exist, `LICENSE` must be the Beerware text, and no Stockfish program or network file may be in the repository. A new asset or library adds its row here in the same commit.

## Files of other authors in this repository

Two are established, below. The sources (`src/`, `uci/`), the tools (`tools/`), the tests (`tests/`), the configs (`config/`) and the scripts
of `CI/` were written for the game; no file of another author is copied into them, and none calls itself vendored. The expected
node counts of `PerftTest` are facts published at chessprogramming.org (numbers, no code). The Python packages that
`CI/setup_venv.*` installs (the reflection generator's, pytest) are tools of a developer's machine and never enter the repository or a
game.

| Path | What | Licence | Licence file | In a game |
|------|------|---------|--------------|-----------|
| `assets/openings/openings.tsv` | the names of the openings (ECO code, name, moves) of the lichess `chess-openings` table, commit `3ba6b20d31b2e6125cb0e6a2e792f400717d9d39`, joined from its five files by `tools/fetch_openings.py` | CC0 1.0 (public domain dedication) | `licenses/lichess-chess-openings-CC0.txt` | ships (desktop and web) |
| `assets/puzzles/puzzles.tsv` | the puzzles of the Puzzles mode (id, position, solution, rating, themes): 130 mate-in-1, 2 and 3 rows chosen by `tools/fetch_puzzles.py` from the lichess puzzle database `lichess_db_puzzle.csv.zst` of 2026-11-01, rows unchanged | CC0 1.0 (public domain dedication) | `licenses/lichess-puzzles-CC0.txt` | ships (desktop and web) |

## The game's own art

| Path | What | Licence | Licence file | In a game |
|------|------|---------|--------------|-----------|
| `assets/chess_test.png` | the piece atlas, 700x381, six pieces in two colors (`config/TextureLibrary.xml`) | Beerware, by the owner's decision of 2026-11-15 | `LICENSE` | ships (desktop and web) |
| `assets/sounds/capture.wav` | the sound of a capture (a piece knocked aside onto the board), generated with Stable Audio Open 1.0 (seed 7100, `/artgen`), chosen by the owner by ear | Beerware; the model's output under the Stability AI Community License (commercial use while the yearly revenue is under USD 1M, `engine/THIRD_PARTY_NOTICES.md`) | `LICENSE` | ships (desktop and web) |
| `assets/sounds/capture.artgen.json` | the provenance of `capture.wav`: model files, prompt, seed, preset, the processing at Accept | Beerware | `LICENSE` | desktop only (kept out of the web package) |
| `assets/sounds/castle.wav` | the sound of castling (two knocks), generated with Stable Audio Open 1.0 (seed 7100, `/artgen`), chosen by the owner by ear | Beerware; the model's output under the Stability AI Community License (commercial use while the yearly revenue is under USD 1M, `engine/THIRD_PARTY_NOTICES.md`) | `LICENSE` | ships (desktop and web) |
| `assets/sounds/castle.artgen.json` | the provenance of `castle.wav`: model files, prompt, seed, preset, the processing at Accept | Beerware | `LICENSE` | desktop only (kept out of the web package) |
| `assets/sounds/check.wav` | the sound of a check (two rising marimba notes), generated with Stable Audio Open 1.0 (seed 7100, `/artgen`), chosen by the owner by ear | Beerware; the model's output under the Stability AI Community License (commercial use while the yearly revenue is under USD 1M, `engine/THIRD_PARTY_NOTICES.md`) | `LICENSE` | ships (desktop and web) |
| `assets/sounds/check.artgen.json` | the provenance of `check.wav`: model files, prompt, seed, preset, the processing at Accept | Beerware | `LICENSE` | desktop only (kept out of the web package) |
| `assets/sounds/click.wav` | the sound of a button (a dry wooden tick), generated with Stable Audio Open 1.0 (seed 7100, `/artgen`), chosen by the owner by ear | Beerware; the model's output under the Stability AI Community License (commercial use while the yearly revenue is under USD 1M, `engine/THIRD_PARTY_NOTICES.md`) | `LICENSE` | ships (desktop and web) |
| `assets/sounds/click.artgen.json` | the provenance of `click.wav`: model files, prompt, seed, preset, the processing at Accept | Beerware | `LICENSE` | desktop only (kept out of the web package) |
| `assets/sounds/game_end.wav` | the sound of the end of a game (a falling chord that rings out), generated with Stable Audio Open 1.0 (seed 7100, `/artgen`), chosen by the owner by ear | Beerware; the model's output under the Stability AI Community License (commercial use while the yearly revenue is under USD 1M, `engine/THIRD_PARTY_NOTICES.md`) | `LICENSE` | ships (desktop and web) |
| `assets/sounds/game_end.artgen.json` | the provenance of `game_end.wav`: model files, prompt, seed, preset, the processing at Accept | Beerware | `LICENSE` | desktop only (kept out of the web package) |
| `assets/sounds/move.wav` | the sound of a move (a wooden piece set down), generated with Stable Audio Open 1.0 (seed 7100, `/artgen`), chosen by the owner by ear | Beerware; the model's output under the Stability AI Community License (commercial use while the yearly revenue is under USD 1M, `engine/THIRD_PARTY_NOTICES.md`) | `LICENSE` | ships (desktop and web) |
| `assets/sounds/move.artgen.json` | the provenance of `move.wav`: model files, prompt, seed, preset, the processing at Accept | Beerware | `LICENSE` | desktop only (kept out of the web package) |
| `assets/sounds/promotion.wav` | the sound of a promotion (a rising chime), generated with Stable Audio Open 1.0 (seed 7100, `/artgen`), chosen by the owner by ear | Beerware; the model's output under the Stability AI Community License (commercial use while the yearly revenue is under USD 1M, `engine/THIRD_PARTY_NOTICES.md`) | `LICENSE` | ships (desktop and web) |
| `assets/sounds/promotion.artgen.json` | the provenance of `promotion.wav`: model files, prompt, seed, preset, the processing at Accept | Beerware | `LICENSE` | desktop only (kept out of the web package) |

## Stockfish

Stockfish (GPL-3.0, https://stockfishchess.org/) is an optional opponent. The game does not carry it: a person installs it themselves, the
game finds it through `stockfishPath` of `config/ChessSettings.local.xml`, the environment variable `STOCKFISH` or `stockfish` on `PATH`
(`README.md`, "Playing against the computer"), starts it as a **separate process** and talks to it in lines of the UCI text protocol
over its standard input and output. The game never links it, never loads it as a library, never copies a line of its code or its
network files, and does not ship, download or install it: nothing of Stockfish is in this repository, in a build of the game or in the
web package (the browser build has no Stockfish at all, `CHESS_NO_STOCKFISH`). `tools/stockfish_bridge.py` and `tools/uci_match.py`
start it the same way, and the tests that use it skip themselves when there is none.

Why that does not make the game GPL: the GPL covers Stockfish and what is derived from it, and it obliges whoever **distributes**
Stockfish. Two separate programs that exchange text through pipes are separate works, and the game works without Stockfish (the own
engine plays instead). The game's licence is therefore the owner's alone. The person who installs Stockfish, or whoever puts it into
a package beside the game, takes it under its GPL and carries its source offer; this repository does not distribute it. This is the
owner's reading of the licence, not legal advice.

## The web package

What `cmake/ChessWeb.cmake` lets into the browser package from the game is `config/` (four XML files), `assets/chess_test.png`, the seven `assets/sounds/*.wav`, `assets/openings/openings.tsv` and `assets/puzzles/puzzles.tsv`; the
`.md` files, `*.local.xml` (a Stockfish path), `imgui.ini`, `.py` files and the `*.artgen.json` provenance files (paths of the owner's machine, no use to a player) stay out (`tests/check_web_package.cmake` checks it in ctest and after every web link).
Beside them the engine's `config/`, `assets/` (its fonts with their licence texts) and the `web/config` overlay are packaged, and the wasm module
contains the engine's libraries: their notices are in `engine/THIRD_PARTY_NOTICES.md`. Stockfish, the own UCI program and the control
API are not in the web build.

## Open points for the owner

- **The piece atlas `assets/chess_test.png`** has no record of where the pictures came from: the file carries no author or licence
  metadata, and its history starts with the commit "Chess tests" of the engine repository (2026-03-23), moved into this repository on
  2026-10-31 with the game. The owner decided on 2026-11-15 that it is the game's own art and covers it by `LICENSE`; if the pictures
  were drawn by someone else or taken from a source (a stock or generated set), this row and `LICENSE` must say so and carry that
  source's terms. The name says "test": a final set of pieces replaces it with a row of its own.
