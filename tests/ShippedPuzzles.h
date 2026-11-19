#pragma once

#include <Chess/Game/Puzzle.h>
#include <filesystem>
#include <fstream>  //-NoStd: engine-file-layer: the table is read from the source tree, which the engine's file layer does not mount in a test
#include <sstream>

/// The shipped `assets/puzzles/puzzles.tsv`, found from the source tree like ShippedOpenings.h finds the openings: a game's content is not mounted in the test environment, so a
/// test that needs the puzzles gives the set to the game itself (ChessGame::SetPuzzles)
namespace BECore::Chess::Tests {

    inline std::filesystem::path ShippedPuzzlesFile() {
        return std::filesystem::path(__FILE__).parent_path().parent_path() / "assets/puzzles/puzzles.tsv";
    }

    /// The text of the shipped table, read once
    inline const String& ShippedPuzzlesTable() {
        static const String table = [] {
            std::ifstream stream(ShippedPuzzlesFile(), std::ios::binary);  //-NoStd: engine-file-layer: see the include
            std::ostringstream content;                                    //-NoStd: engine-file-layer: see the include
            content << stream.rdbuf();
            const std::string text = content.str();  //-NoStd: engine-file-layer: see the include
            return String(text.data(), text.size());
        }();
        return table;
    }

    /// The puzzles of the shipped table, parsed once; whether the table parses without a problem is the business of PuzzleShippedTest
    inline const PuzzleSet& ShippedPuzzleSet() {
        static const PuzzleSet set = PuzzleSet::Parse(ShippedPuzzlesTable());
        return set;
    }

}  // namespace BECore::Chess::Tests
