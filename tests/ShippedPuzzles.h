#pragma once

#include <BECore/File/File.h>
#include <BECore/File/Path.h>
#include <Chess/Game/Puzzle.h>

/// The shipped `assets/puzzles/puzzles.tsv`, found from the source tree like ShippedOpenings.h finds the openings: a game's content is not mounted in the test environment, so a
/// test that needs the puzzles gives the set to the game itself (ChessGame::SetPuzzles)
namespace BECore::Chess::Tests {

    inline Path ShippedPuzzlesFile() {
        return Path(__FILE__).Parent().Parent() / "assets/puzzles/puzzles.tsv";
    }

    /// The text of the shipped table, read once
    inline const String& ShippedPuzzlesTable() {
        static const String table = [] {
            auto text = ReadTextFile(ShippedPuzzlesFile().View());
            return text ? std::move(*text) : String();
        }();
        return table;
    }

    /// The puzzles of the shipped table, parsed once; whether the table parses without a problem is the business of PuzzleShippedTest
    inline const PuzzleSet& ShippedPuzzleSet() {
        static const PuzzleSet set = PuzzleSet::Parse(ShippedPuzzlesTable());
        return set;
    }

}  // namespace BECore::Chess::Tests
