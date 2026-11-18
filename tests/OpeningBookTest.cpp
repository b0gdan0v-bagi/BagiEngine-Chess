#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/MoveNotation.h>
#include <Chess/Game/OpeningBook.h>
#include <chrono>
#include <iostream>
#include <tests/ChessTestUtil.h>
#include <tests/ShippedOpenings.h>

/**
 * The opening book as a pure lookup: the mechanics on small inline tables, and the data on the shipped `assets/openings/openings.tsv`, found from the source
 * tree. No timing assertion: the parse time of the shipped file is only printed.
 */
namespace BECore::Chess::Tests {

    namespace {

        constexpr StringView kNajdorf = "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6";
        constexpr StringView kNajdorfName = "Sicilian Defense: Najdorf Variation";

        /// The number of rows of the shipped table whose position an earlier row already has; it changes with the data, and a refresh of the file shows here
        constexpr size_t kShippedRowsLostToEarlierRows = 0;

        Vector<StringView> Words(StringView text) {
            Vector<StringView> words;
            size_t at = 0;
            while (at < text.size()) {
                const size_t end = text.find(' ', at);
                const size_t length = (end == StringView::npos ? text.size() : end) - at;
                if (length > 0) {
                    words.push_back(text.substr(at, length));
                }
                if (end == StringView::npos) {
                    break;
                }
                at = end + 1;
            }
            return words;
        }

        /// The moves of @p uciLine played from @p fen; a text that is no legal move fails the test
        Vector<Move> MovesOf(StringView fen, StringView uciLine) {
            Position position = PositionFromFen(fen);
            Vector<Move> moves;
            for (const StringView uci : Words(uciLine)) {
                const Optional<Move> move = MoveGenerator::ParseUci(position, uci);
                if (!move) {
                    ADD_FAILURE() << "Not a legal move: " << String(uci).c_str() << " in " << position.ToFen().c_str();
                    break;
                }
                position.DoMove(*move);
                moves.push_back(*move);
            }
            return moves;
        }

        Vector<Move> MovesOf(StringView uciLine) {
            return MovesOf(Position::kStartFen, uciLine);
        }

        String FenAfter(StringView uciLine) {
            Position position = Position::Start();
            for (const Move move : MovesOf(uciLine)) {
                position.DoMove(move);
            }
            return position.ToFen();
        }

        ZobristKey KeyAfter(StringView uciLine) {
            Position position = Position::Start();
            for (const Move move : MovesOf(uciLine)) {
                position.DoMove(move);
            }
            return position.Key();
        }

        /// The name of the opening of the game @p uciLine from the start, or "-" outside the book
        String NameOf(const OpeningBook& book, StringView uciLine) {
            const Vector<Move> moves = MovesOf(uciLine);
            const Optional<OpeningEntry> entry = OpeningOf(book, Position::kStartFen, moves, moves.size());
            return entry ? entry->name : String("-");
        }

        /// The book of a table made the plain way: every row replayed from the start, no shortcut
        void RowByRow(StringView tsv, HashMap<ZobristKey, OpeningEntry>& entries) {
            size_t at = 0;
            bool header = true;
            while (at < tsv.size()) {
                size_t end = tsv.find('\n', at);
                if (end == StringView::npos) {
                    end = tsv.size();
                }
                const StringView line = tsv.substr(at, end - at);
                at = end + 1;
                if (header || line.empty()) {
                    header = false;
                    continue;
                }
                const size_t first = line.find('\t');
                const size_t second = line.find('\t', first + 1);
                Position position = Position::Start();
                bool ok = true;
                for (const StringView token : Words(line.substr(second + 1))) {
                    if (token.back() == '.') {
                        continue;
                    }
                    const Optional<Move> move = ParseSan(position, token);
                    if (!move) {
                        ok = false;
                        break;
                    }
                    position.DoMove(*move);
                }
                if (ok && !entries.contains(position.Key())) {
                    entries.emplace(position.Key(), OpeningEntry{String(line.data(), first), String(line.data() + first + 1, second - first - 1)});
                }
            }
        }

    }  // namespace

    TEST(OpeningBookTest, ParseReadsTheRowsAndSkipsTheHeader) {
        const OpeningBook book = OpeningBook::Parse("eco\tname\tpgn\nB00\tKing's Pawn\t1. e4\nA00\tAmar Opening\t1. Nh3\n");
        EXPECT_EQ(book.Size(), 2U);
        EXPECT_FALSE(book.IsEmpty());
        const OpeningEntry* kings = book.Find(KeyAfter("e2e4"));
        ASSERT_NE(kings, nullptr);
        EXPECT_EQ(kings->eco, "B00");
        EXPECT_EQ(kings->name, "King's Pawn");
        const OpeningEntry* amar = book.Find(KeyAfter("g1h3"));
        ASSERT_NE(amar, nullptr);
        EXPECT_EQ(amar->name, "Amar Opening");
        EXPECT_EQ(book.Find(KeyAfter("d2d4")), nullptr);
    }

    TEST(OpeningBookTest, ParseToleratesCarriageReturnsMoveNumbersAndAMissingFinalNewline) {
        const OpeningBook book = OpeningBook::Parse("eco\tname\tpgn\r\nC20\tKing's Pawn Game\t1. e4 e5\r\nC44\tKing's Knight\t1.e4 e5 2. Nf3\r\nC00\tCastles\t1. e4 e6 12... d5");
        EXPECT_EQ(book.Size(), 2U);
        const OpeningEntry* entry = book.Find(KeyAfter("e2e4 e7e5"));
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->name, "King's Pawn Game");
    }

    TEST(OpeningBookTest, ParseReportsAndSkipsWhatItCannotRead) {
        Vector<String> problems;
        const OpeningBook book = OpeningBook::Parse("eco\tname\tpgn\nB00\tFine\t1. e4\nB00\tTwo columns\nB01\tNo such move\t1. e4 e5 2. Nf6\nB02\tAlso fine\t1. d4\n", &problems);
        EXPECT_EQ(book.Size(), 2U);
        ASSERT_EQ(problems.size(), 2U);
        EXPECT_NE(problems[0].find("line 3"), String::npos) << problems[0].c_str();
        EXPECT_NE(problems[1].find("line 4"), String::npos) << problems[1].c_str();
        EXPECT_EQ(book.Find(KeyAfter("e2e4 e7e5")), nullptr);
    }

    TEST(OpeningBookTest, AnEmptyTableIsAnEmptyBook) {
        EXPECT_TRUE(OpeningBook::Parse("").IsEmpty());
        EXPECT_TRUE(OpeningBook::Parse("eco\tname\tpgn\n").IsEmpty());
        EXPECT_FALSE(OpeningOf(OpeningBook{}, Position::kStartFen, MovesOf("e2e4"), 1).has_value());
    }

    TEST(OpeningBookTest, TheFirstRowOfAPositionWins) {
        // The same position by two move orders, and the same line twice
        const OpeningBook book = OpeningBook::Parse("eco\tname\tpgn\nA00\tFirst\t1. Nf3 d5 2. d4\nA01\tSecond\t1. d4 d5 2. Nf3\nA02\tThird\t1. Nf3 d5 2. d4\n");
        EXPECT_EQ(book.Size(), 1U);
        const OpeningEntry* entry = book.Find(KeyAfter("g1f3 d7d5 d2d4"));
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->name, "First");
    }

    TEST(OpeningBookTest, SharingThePliesOfTheRowBeforeGivesTheSameBookAsReplayingEveryRow) {
        // Neighbours that share a prefix, a row that diverges early, a row that breaks in the shared part (what follows it must not reuse its plies), a transposition
        // and a row that goes back to a shorter line
        const String table = "eco\tname\tpgn\n"
                             "B20\tSicilian Defense\t1. e4 c5\n"
                             "B27\tSicilian Defense: Hyper-Accelerated Dragon\t1. e4 c5 2. Nf3 g6\n"
                             "C20\tBroken\t1. e4 c5 2. Nf3 Nf6 3. Qh8\n"
                             "B28\tSicilian Defense: Wing Gambit\t1. e4 c5 2. Nf3 Nf6 3. e5\n"
                             "A45\tIndian Game\t1. d4 Nf6\n"
                             "A46\tIndian Game: Knights Variation\t1. d4 Nf6 2. Nf3\n"
                             "B50\tSicilian Defense: Modern Variations\t1. e4 c5 2. Nf3 d6\n"
                             "A46\tTransposition\t1. Nf3 Nf6 2. d4\n"
                             "B20\tSicilian Defense\t1. e4 c5\n"
                             "B01\tScandinavian Defense\t1. e4 d5 2. exd5 Qxd5 3. Nc3\n"
                             "B01\tScandinavian Defense: Main Line\t1. e4 d5 2. exd5 Qxd5 3. Nc3 Qa5\n"
                             "B00\tKing's Pawn\t1. e4\n";
        HashMap<ZobristKey, OpeningEntry> expected;
        RowByRow(table, expected);

        Vector<String> problems;
        const OpeningBook book = OpeningBook::Parse(table, &problems);
        EXPECT_EQ(problems.size(), 1U);
        ASSERT_EQ(book.Size(), expected.size());
        for (const auto& [key, entry] : expected) {
            const OpeningEntry* found = book.Find(key);
            ASSERT_NE(found, nullptr) << entry.name.c_str();
            EXPECT_EQ(found->eco, entry.eco);
            EXPECT_EQ(found->name, entry.name);
        }
    }

    TEST(OpeningBookTest, TheShippedTableParsesWithoutAProblem) {
        Vector<String> problems;
        const auto start = std::chrono::steady_clock::now();
        const OpeningBook book = OpeningBook::Parse(ShippedOpeningsTable(), &problems);
        const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        std::cout << "[ openings ] parse of the shipped table: " << took.count() << " ms\n";
        for (const String& problem : problems) {
            ADD_FAILURE() << problem.c_str();
        }
        EXPECT_FALSE(book.IsEmpty());
    }

    TEST(OpeningBookTest, TheRowsThatLostTheirPositionToAnEarlierRowAreCounted) {
        size_t rows = 0;
        for (const char c : ShippedOpeningsTable()) {
            rows += c == '\n' ? 1 : 0;
        }
        --rows;  // the header
        EXPECT_EQ(rows - ShippedOpeningBook().Size(), kShippedRowsLostToEarlierRows);
    }

    TEST(OpeningBookTest, TheNajdorfLineIsNamed) {
        const OpeningBook& book = ShippedOpeningBook();
        const Vector<Move> moves = MovesOf(kNajdorf);
        const Optional<OpeningEntry> entry = OpeningOf(book, Position::kStartFen, moves, moves.size());
        ASSERT_TRUE(entry.has_value());
        EXPECT_EQ(entry->eco, "B90");
        EXPECT_EQ(entry->name, kNajdorfName);
    }

    TEST(OpeningBookTest, EachEcoFileHasItsLineNamed) {
        const OpeningBook& book = ShippedOpeningBook();
        EXPECT_EQ(NameOf(book, "g1f3"), "Zukertort Opening");
        EXPECT_EQ(NameOf(book, "e2e4 d7d5"), "Scandinavian Defense");
        EXPECT_EQ(NameOf(book, "e2e4 e7e5 g1f3 b8c6 f1c4"), "Italian Game");
        EXPECT_EQ(NameOf(book, "d2d4 d7d5 c2c4"), "Queen's Gambit");
        EXPECT_EQ(NameOf(book, "d2d4 g8f6 c2c4 e7e6 g2g3"), "Catalan Opening");
    }

    TEST(OpeningBookTest, ATranspositionHasTheNameOfItsPosition) {
        const OpeningBook& book = ShippedOpeningBook();
        // 1.Nf3 c5 2.e4 is 1.e4 c5 2.Nf3
        const String other = "g1f3 c7c5 e2e4 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6";
        EXPECT_EQ(FenAfter(other), FenAfter(kNajdorf));
        EXPECT_EQ(NameOf(book, other), kNajdorfName);
        const OpeningEntry* entry = book.Find(KeyAfter(other));
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->eco, "B90");
    }

    TEST(OpeningBookTest, TheStartPositionAndAPositionOutsideTheBookAreNamedNothing) {
        const OpeningBook& book = ShippedOpeningBook();
        EXPECT_FALSE(OpeningOf(book, Position::kStartFen, {}, 0).has_value());
        EXPECT_FALSE(OpeningOf(book, "8/8/4k3/8/8/4K3/4P3/8 w - - 0 1", {}, 0).has_value());
        EXPECT_FALSE(OpeningOf(book, "not a fen", {}, 0).has_value());
    }

    TEST(OpeningBookTest, TheNameStaysAfterTheGameLeavesTheBook) {
        const OpeningBook& book = ShippedOpeningBook();
        const String aside = String(kNajdorf) + " a2a3 h7h6";
        const Vector<Move> moves = MovesOf(aside);
        ASSERT_EQ(moves.size(), 12U);
        ASSERT_EQ(book.Find(KeyAfter(aside)), nullptr) << "the two moves must be outside the book for this test";

        const Optional<OpeningEntry> entry = OpeningOf(book, Position::kStartFen, moves, moves.size());
        ASSERT_TRUE(entry.has_value());
        EXPECT_EQ(entry->name, kNajdorfName);

        const Optional<OpeningEntry> earlier = OpeningOf(book, Position::kStartFen, moves, 4);
        ASSERT_TRUE(earlier.has_value());
        EXPECT_EQ(earlier->name, NameOf(book, "e2e4 c7c5 g1f3 d7d6"));
        EXPECT_NE(earlier->name, kNajdorfName);
    }

    TEST(OpeningBookTest, ACountAboveTheMovesMeansAllOfThem) {
        const OpeningBook& book = ShippedOpeningBook();
        const Vector<Move> moves = MovesOf(kNajdorf);
        const Optional<OpeningEntry> entry = OpeningOf(book, Position::kStartFen, moves, moves.size() + 100);
        ASSERT_TRUE(entry.has_value());
        EXPECT_EQ(entry->name, kNajdorfName);
    }

    TEST(OpeningBookTest, AGameFromAFenThatIsInTheBookIsNamedAtOnce) {
        const OpeningBook& book = ShippedOpeningBook();
        const String fen = FenAfter("e2e4 c7c5");
        const Optional<OpeningEntry> entry = OpeningOf(book, fen, {}, 0);
        ASSERT_TRUE(entry.has_value());
        EXPECT_EQ(entry->name, "Sicilian Defense");

        // And a game that goes on from there reaches a later line of the table
        const Vector<Move> rest = MovesOf(fen, "g1f3 d7d6");
        const Optional<OpeningEntry> later = OpeningOf(book, fen, rest, rest.size());
        ASSERT_TRUE(later.has_value());
        EXPECT_EQ(later->name, NameOf(book, "e2e4 c7c5 g1f3 d7d6"));
    }

    TEST(OpeningDisplayNameTest, AShortNameIsShownWhole) {
        EXPECT_EQ(OpeningDisplayName("Italian Game", 20), "Italian Game");
        EXPECT_EQ(OpeningDisplayName("Italian Game", 12), "Italian Game");
        EXPECT_EQ(OpeningDisplayName("", 12), "");
    }

    TEST(OpeningDisplayNameTest, ALongNameIsCutToItsFamily) {
        EXPECT_EQ(OpeningDisplayName(kNajdorfName, 16), "Sicilian Defense");
        EXPECT_EQ(OpeningDisplayName(kNajdorfName, 35), kNajdorfName);
        EXPECT_EQ(OpeningDisplayName(kNajdorfName, 34), "Sicilian Defense");
    }

    TEST(OpeningDisplayNameTest, ANameWithoutAFamilyIsCutAtItsComma) {
        EXPECT_EQ(OpeningDisplayName("Queen's Indian Defense, with e3, Bb4+ Line", 25), "Queen's Indian Defense");
        EXPECT_EQ(OpeningDisplayName("Polish Opening: King's Indian Variation, Schiffler Attack", 30), "Polish Opening");
    }

    TEST(OpeningDisplayNameTest, AFamilyThatIsTooLongIsCutAtAWord) {
        EXPECT_EQ(OpeningDisplayName("Grob Opening: Grob Gambit", 9), "Grob");
        EXPECT_EQ(OpeningDisplayName("Queen's Indian Defense, with e3", 14), "Queen's Indian");
        EXPECT_EQ(OpeningDisplayName("Queen's Indian Defense, with e3", 15), "Queen's Indian");
        EXPECT_EQ(OpeningDisplayName("Queen's Indian Defense, with e3", 16), "Queen's Indian");
        EXPECT_EQ(OpeningDisplayName("Queen's Indian Defense, with e3", 22), "Queen's Indian Defense");
    }

    TEST(OpeningDisplayNameTest, AWordLongerThanTheLimitIsCutAtTheLimit) {
        EXPECT_EQ(OpeningDisplayName("Zilbermints Gambit", 5), "Zilbe");
        EXPECT_EQ(OpeningDisplayName("Zilbermints", 0), "");
    }

    TEST(OpeningDisplayNameTest, TheLimitCountsCharactersNotBytes) {
        // "Döry Defense" is 12 characters and 13 bytes; the ö is two bytes
        EXPECT_EQ(OpeningDisplayName("D\xC3\xB6ry Defense", 12), "D\xC3\xB6ry Defense");
        EXPECT_EQ(OpeningDisplayName("D\xC3\xB6ry Defense", 11), "D\xC3\xB6ry");
        EXPECT_EQ(OpeningDisplayName("D\xC3\xB6ry Defense: Gambit", 12), "D\xC3\xB6ry Defense");
        EXPECT_EQ(OpeningDisplayName("D\xC3\xB6ry", 3), "D\xC3\xB6r");
    }

}  // namespace BECore::Chess::Tests
