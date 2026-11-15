# Third-party notices

The game (the code of this repository and its own art) is under the Beerware licence (`LICENSE`, the same wording as the engine's).
This file lists what **this repository** carries that was not written for it, and what the game does with Stockfish. What the engine
carries (SDL, EnTT, {fmt}, the fonts, ...) is in `engine/THIRD_PARTY_NOTICES.md`, with the licence files next to the code: a game
built on the engine contains the rows of that file marked **ships**, so a released Chess carries those notices too.

`tests/check_notices.py` (ctest `BagiChessThirdPartyNotices`) holds this file to the tree: every file of `assets/` must have a row, each row's
path and licence file must exist, `LICENSE` must be the Beerware text, and no Stockfish program or network file may be in the repository. A new asset or library adds its row here in the same commit.

## Files of other authors in this repository

None are established. The sources (`src/`, `uci/`), the tools (`tools/`), the tests (`tests/`), the configs (`config/`) and the scripts
of `CI/` were written for the game; no file of another author is copied into them, and none calls itself vendored. The expected
node counts of `PerftTest` are facts published at chessprogramming.org (numbers, no code). The Python packages that
`CI/setup_venv.*` installs (the reflection generator's, pytest) are tools of a developer's machine and never enter the repository or a
game.

## The game's own art

| Path | What | Licence | Licence file | In a game |
|------|------|---------|--------------|-----------|
| `assets/chess_test.png` | the piece atlas, 700x381, six pieces in two colors (`config/TextureLibrary.xml`) | Beerware, by the owner's decision of 2026-11-15 | `LICENSE` | ships (desktop and web) |

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

What `cmake/ChessWeb.cmake` lets into the browser package from the game is `config/` (four XML files) and `assets/chess_test.png`; the
`.md` files, `*.local.xml` (a Stockfish path), `imgui.ini` and `.py` files stay out (`BagiChessWebPackageCheck` holds the list).
Beside them the engine's `config/`, `assets/` (its fonts with their licence texts) and the `web/config` overlay are packaged, and the wasm module
contains the engine's libraries: their notices are in `engine/THIRD_PARTY_NOTICES.md`. Stockfish, the own UCI program and the control
API are not in the web build.

## Open points for the owner

- **The piece atlas `assets/chess_test.png`** has no record of where the pictures came from: the file carries no author or licence
  metadata, and its history starts with the commit "Chess tests" of the engine repository (2026-03-23), moved into this repository on
  2026-10-31 with the game. The owner decided on 2026-11-15 that it is the game's own art and covers it by `LICENSE`; if the pictures
  were drawn by someone else or taken from a source (a stock or generated set), this row and `LICENSE` must say so and carry that
  source's terms. The name says "test": a final set of pieces replaces it with a row of its own.
