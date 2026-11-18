#pragma once

#include <Chess/Game/OpeningBook.h>
#include <filesystem>
#include <fstream>  //-NoStd: engine-file-layer: the table is read from the source tree, which the engine's file layer does not mount in a test
#include <sstream>

/// The shipped `assets/openings/openings.tsv`, found from the source tree like ShippedScene.h finds `config/`: a game's content is not mounted in the test environment, so a test
/// that needs the names gives the book to the game itself (ChessGame::SetOpenings)
namespace BECore::Chess::Tests {

    inline std::filesystem::path ShippedOpeningsFile() {
        return std::filesystem::path(__FILE__).parent_path().parent_path() / "assets/openings/openings.tsv";
    }

    /// The text of the shipped table, read once
    inline const String& ShippedOpeningsTable() {
        static const String table = [] {
            std::ifstream stream(ShippedOpeningsFile(), std::ios::binary);  //-NoStd: engine-file-layer: see the include
            std::ostringstream content;                                     //-NoStd: engine-file-layer: see the include
            content << stream.rdbuf();
            const std::string text = content.str();  //-NoStd: engine-file-layer: see the include
            return String(text.data(), text.size());
        }();
        return table;
    }

    /// The book of the shipped table, parsed once; whether the table parses without a problem is the business of OpeningBookTest
    inline const OpeningBook& ShippedOpeningBook() {
        static const OpeningBook book = OpeningBook::Parse(ShippedOpeningsTable());
        return book;
    }

}  // namespace BECore::Chess::Tests
