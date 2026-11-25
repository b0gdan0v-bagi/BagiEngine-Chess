#pragma once

#include <BECore/File/File.h>
#include <BECore/File/Path.h>
#include <Chess/Game/OpeningBook.h>

/// The shipped `assets/openings/openings.tsv`, found from the source tree like ShippedScene.h finds `config/`: a game's content is not mounted in the test environment, so a test
/// that needs the names gives the book to the game itself (ChessGame::SetOpenings)
namespace BECore::Chess::Tests {

    inline Path ShippedOpeningsFile() {
        return Path(__FILE__).Parent().Parent() / "assets/openings/openings.tsv";
    }

    /// The text of the shipped table, read once
    inline const String& ShippedOpeningsTable() {
        static const String table = [] {
            auto text = ReadTextFile(ShippedOpeningsFile().View());
            return text ? std::move(*text) : String();
        }();
        return table;
    }

    /// The book of the shipped table, parsed once; whether the table parses without a problem is the business of OpeningBookTest
    inline const OpeningBook& ShippedOpeningBook() {
        static const OpeningBook book = OpeningBook::Parse(ShippedOpeningsTable());
        return book;
    }

}  // namespace BECore::Chess::Tests
