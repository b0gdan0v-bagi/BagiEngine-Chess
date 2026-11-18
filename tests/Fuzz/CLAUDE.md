# tests/Fuzz: fuzz targets for the text Chess reads from outside

Library `ChessFuzzTargets` (namespace `BECore::Chess::Fuzzing`, headers `<tests/Fuzz/...>`; `<Fuzz/...>` is the engine's folder): one plain function `void Fuzz<Name>(const uint8_t*, size_t)` per parser, the same shape as the engine's `BagiFuzzTargets` (`engine/tests/Fuzz/CLAUDE.md`, "A target"). The game has a library of its own because the engine's is built only inside the engine repository; it compiles the engine's `FuzzMutator.cpp` (the xorshift mutator, the same bytes on every platform). Properties are `CHESS_FUZZ_CHECK`s (print and abort in every build, never the engine `ASSERT`, which compiles out); each is written from the header comment of the parser as a second implementation (a grammar, a comparison with `GenerateLegal()`), never by calling the code under test again. A property the comment does not promise is not checked.

## Run

`ctest --preset <p> -R ChessFuzz` (`tests/ChessFuzzCorpusTest.cpp`): every seed of a target, then `BAGI_FUZZ_ITERATIONS` mutations of the seeds in turn, in every ctest on every platform (`kDefaultIterations` is 400000 divided by the target's cost weight, about 2 s a case in Debug). `BAGI_FUZZ_ITERATIONS=2000000` (run from the build directory: the corpus is found from `__FILE__`) is a longer run with no libFuzzer. The libFuzzer executables and the long runs are slice 2 of the entry `chess-fuzz-fen-uci`.

## The targets

| Target | Corpus folder | Guards |
|--------|---------------|--------|
| `Fen` | `fen` | `Position::FromFen` and what it promises (one king per side, no pawn on a back rank, at most 16 pieces, the side that just moved not in check, rights with king and rook in place), `ToFen` a fixed point, every legal move one ply deep |
| `UciMove` | `uci_move` | `MoveGenerator::ParseUci` over the position of `<fen>\n<move text>`: a move exactly when the text is the spelling of a legal move |
| `UciCommand` | `uci_command` | `Uci::ParseCommand` against a token model, `Uci::ParseInt`, the tolerance of edges, and a `position` through `SearchRequest::FinalPosition` (at most 64 moves) |
| `UciOutput` | `uci_output` | `UciProtocol::TrimLine`, `ParseInfo`, `ParseBestMove` against a token model, and `LineCollector` (lines in order, latest exact score per `multipv`, a bound only when no exact one) over the text split at `\n` (at most 256 lines) |

- `ParseCommand` splits words at spaces and tabs, `ParseInfo` / `ParseBestMove` at spaces only: the models keep each its own.
- `position fen` with no fields is a `Position` command with an empty `startFen`; `UciServer` refuses it later in `FromFen`, so a `Position` is not promised a non-empty FEN.
- `ChessFuzzText.h` is the reading both text targets share (edges, words, `ReadInt`); `ChessFuzzBoard.h` is the board walked cell by cell for `Fen`.

## The corpus

`corpus/<folder>/`: small real inputs named for what they are (the perft and test FENs, the commands of `UciCommandTest` and `uci_smoke.py`, a Stockfish transcript and the shapes of `UciProtocolTest`), at most 64 KiB. `.gitattributes` marks `corpus/**` binary: a CR or a missing last newline is part of the seed. Both verdicts must be present in every folder (`SeedsHoldAnInputTheParserTakesAndOneItRefuses`; the `accepts` function of the row says what a take is). A finding becomes a seed `fix-<what>` committed with the fix and a gtest next to the fixed code; seeds are never deleted to make a run pass.

**A new target:** `Fuzz<Name>.cpp` (the glob picks it up), its two declarations and a row in `ChessFuzzTargetList()`, a corpus folder, and a cost weight that keeps the case near 2 s.

**Not covered:** `UciServer` with a live search, the JSON around a FEN in the control API, the process side of `UciProcess`: `uci_smoke.py` and `ChessGameControlTest` hold those.
