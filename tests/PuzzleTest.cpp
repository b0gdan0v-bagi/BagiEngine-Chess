#include <Chess/Game/Puzzle.h>
#include <gtest/gtest.h>

/**
 * The puzzle row and the set as pure parsing, on small inline tables: a bad row is a problem line and is skipped, never a crash. The shipped table is
 * `PuzzleShippedTest`'s.
 */
namespace BECore::Chess::Tests {

    namespace {

        constexpr StringView kHeader = "id\tfen\tmoves\trating\tthemes\n";
        constexpr StringView kWhiteSolverFen = "6k1/1p3ppp/8/8/8/8/8/R3K3 b - - 0 1";
        constexpr StringView kBlackSolverFen = "r3k3/8/8/8/8/8/1P3PPP/6K1 w - - 0 1";

        String Row(StringView id, StringView fen, StringView moves, StringView rating, StringView themes) {
            String row;
            for (const StringView part : {id, StringView("\t"), fen, StringView("\t"), moves, StringView("\t"), rating, StringView("\t"), themes, StringView("\n")}) {
                row.append(part.data(), part.size());
            }
            return row;
        }

        String Table(std::initializer_list<String> rows) {
            String table(kHeader.data(), kHeader.size());
            for (const String& row : rows) {
                table += row;
            }
            return table;
        }

        String GoodWhiteRow(StringView id, StringView rating) {
            return Row(id, kWhiteSolverFen, "b7b6 a1a8", rating, "mate mateIn1 backRankMate");
        }

        PuzzleSet Parse(const String& tsv, Vector<String>* problems) {
            return PuzzleSet::Parse(StringView(tsv.data(), tsv.size()), problems);
        }

    }  // namespace

    TEST(PuzzleTest, AGoodRowIsKept) {
        Vector<String> problems;
        const PuzzleSet set = Parse(Table({GoodWhiteRow("00008", "1500")}), &problems);

        EXPECT_TRUE(problems.empty());
        ASSERT_EQ(set.Size(), 1U);
        const Puzzle& puzzle = set.At(0);
        EXPECT_EQ(puzzle.id, "00008");
        EXPECT_EQ(puzzle.startFen, "6k1/1p3ppp/8/8/8/8/8/R3K3 b - - 0 1");
        ASSERT_EQ(puzzle.line.size(), 2U);
        EXPECT_EQ(puzzle.line[0].ToUci(), "b7b6");
        EXPECT_EQ(puzzle.line[1].ToUci(), "a1a8");
        EXPECT_EQ(puzzle.rating, 1500);
        ASSERT_EQ(puzzle.themes.size(), 3U);
        EXPECT_EQ(puzzle.themes[0], "mate");
        EXPECT_EQ(puzzle.themes[1], "mateIn1");
        EXPECT_EQ(puzzle.themes[2], "backRankMate");
    }

    TEST(PuzzleTest, TheSolverIsTheSideToMoveAfterTheSetup) {
        const PuzzleSet set = Parse(Table({GoodWhiteRow("w", "800"), Row("b", kBlackSolverFen, "b2b3 a8a1", "900", "mateIn1")}), nullptr);

        ASSERT_EQ(set.Size(), 2U);
        EXPECT_EQ(set.At(0).SolverColor(), PieceColor::White);
        EXPECT_EQ(set.At(1).SolverColor(), PieceColor::Black);
    }

    TEST(PuzzleTest, TheSolverMovesAreHalfTheLine) {
        const PuzzleSet set = Parse(Table({GoodWhiteRow("one", "800"), Row("two", kWhiteSolverFen, "b7b6 a1a7 g8f8 a7a8", "900", "mateIn2")}), nullptr);

        ASSERT_EQ(set.Size(), 2U);
        EXPECT_EQ(set.At(0).SolverMoveCount(), 1U);
        EXPECT_EQ(set.At(1).SolverMoveCount(), 2U);
    }

    TEST(PuzzleTest, EveryKindOfBadRowIsAProblemAndSkipped) {
        const String table = Table({
            Row("badfen", "not a fen", "b7b6 a1a8", "800", "mate"),
            Row("illegal", kWhiteSolverFen, "b7b6 a1a9", "800", "mate"),
            Row("illegalfirst", kWhiteSolverFen, "a1a8 b7b6", "800", "mate"),
            Row("short", kWhiteSolverFen, "b7b6", "800", "mate"),
            Row("none", kWhiteSolverFen, "", "800", "mate"),
            Row("odd", kWhiteSolverFen, "b7b6 a1a7 g8f8", "800", "mate"),
            Row("rating", kWhiteSolverFen, "b7b6 a1a8", "high", "mate"),
            Row("norating", kWhiteSolverFen, "b7b6 a1a8", "", "mate"),
            Row("", kWhiteSolverFen, "b7b6 a1a8", "800", "mate"),
            String("fewer\tcolumns\there\n"),
            GoodWhiteRow("good", "800"),
        });

        Vector<String> problems;
        const PuzzleSet set = Parse(table, &problems);

        EXPECT_EQ(problems.size(), 10U);
        ASSERT_EQ(set.Size(), 1U);
        EXPECT_EQ(set.At(0).id, "good");
    }

    TEST(PuzzleTest, AProblemNamesItsLine) {
        Vector<String> problems;
        (void)Parse(Table({GoodWhiteRow("a", "800"), Row("b", "not a fen", "b7b6 a1a8", "800", "mate")}), &problems);

        ASSERT_EQ(problems.size(), 1U);
        EXPECT_NE(problems[0].find("line 3"), String::npos);
    }

    TEST(PuzzleTest, ADuplicateIdKeepsTheFirstRow) {
        Vector<String> problems;
        const PuzzleSet set = Parse(Table({GoodWhiteRow("same", "700"), GoodWhiteRow("same", "1900")}), &problems);

        EXPECT_EQ(problems.size(), 1U);
        ASSERT_EQ(set.Size(), 1U);
        EXPECT_EQ(set.At(0).rating, 700);
    }

    TEST(PuzzleTest, TheRowsKeepTheOrderOfTheFile) {
        const PuzzleSet set = Parse(Table({GoodWhiteRow("c", "900"), GoodWhiteRow("a", "1900"), GoodWhiteRow("b", "700")}), nullptr);

        ASSERT_EQ(set.Size(), 3U);
        EXPECT_EQ(set.At(0).id, "c");
        EXPECT_EQ(set.At(1).id, "a");
        EXPECT_EQ(set.At(2).id, "b");
    }

    TEST(PuzzleTest, AMissingTrailingNewlineKeepsTheLastRow) {
        String table = Table({GoodWhiteRow("a", "800"), GoodWhiteRow("last", "900")});
        table.pop_back();

        const PuzzleSet set = Parse(table, nullptr);

        ASSERT_EQ(set.Size(), 2U);
        EXPECT_EQ(set.At(1).id, "last");
        EXPECT_EQ(set.At(1).themes.size(), 3U);
    }

    TEST(PuzzleTest, CarriageReturnsAreTolerated) {
        String table;
        for (const char c : Table({GoodWhiteRow("a", "800"), GoodWhiteRow("b", "900")})) {
            if (c == '\n') {
                table.push_back('\r');
            }
            table.push_back(c);
        }

        Vector<String> problems;
        const PuzzleSet set = Parse(table, &problems);

        EXPECT_TRUE(problems.empty());
        ASSERT_EQ(set.Size(), 2U);
        EXPECT_EQ(set.At(1).themes.back(), "backRankMate");
    }

    TEST(PuzzleTest, AnEmptyTextAndAHeaderOnlyGiveAnEmptySet) {
        EXPECT_TRUE(PuzzleSet::Parse("", nullptr).IsEmpty());
        EXPECT_TRUE(PuzzleSet::Parse(kHeader, nullptr).IsEmpty());
    }

    TEST(PuzzleTest, EmptyLinesAreSkippedWithoutAProblem) {
        String table = Table({GoodWhiteRow("a", "800")});
        table += "\n\n";
        table += GoodWhiteRow("b", "900");

        Vector<String> problems;
        const PuzzleSet set = Parse(table, &problems);

        EXPECT_TRUE(problems.empty());
        EXPECT_EQ(set.Size(), 2U);
    }

    TEST(PuzzleTest, AnEmptyThemesColumnIsAPuzzleWithoutThemes) {
        const PuzzleSet set = Parse(Table({Row("a", kWhiteSolverFen, "b7b6 a1a8", "800", "")}), nullptr);

        ASSERT_EQ(set.Size(), 1U);
        EXPECT_TRUE(set.At(0).themes.empty());
    }

    TEST(PuzzleTest, AColumnBeyondTheFifthIsIgnored) {
        String row = Row("a", kWhiteSolverFen, "b7b6 a1a8", "800", "mateIn1");
        row.insert(row.size() - 1, "\textra");

        const PuzzleSet set = Parse(Table({row}), nullptr);

        ASSERT_EQ(set.Size(), 1U);
        EXPECT_EQ(set.At(0).themes.size(), 1U);
        EXPECT_EQ(set.At(0).themes[0], "mateIn1");
    }

    TEST(PuzzleTest, FindIndexByIdFindsTheRowOfTheFile) {
        const PuzzleSet set = Parse(Table({GoodWhiteRow("x", "900"), GoodWhiteRow("y", "700")}), nullptr);

        ASSERT_TRUE(set.FindIndexById("y").has_value());
        EXPECT_EQ(*set.FindIndexById("y"), 1U);
        EXPECT_EQ(*set.FindIndexById("x"), 0U);
        EXPECT_FALSE(set.FindIndexById("z").has_value());
        EXPECT_FALSE(set.FindIndexById("").has_value());
    }

    TEST(PuzzleTest, FromFieldsRefusesWithAReason) {
        const auto refused = Puzzle::FromFields("a", kWhiteSolverFen, "b7b6", "800", "mate");
        ASSERT_FALSE(refused.has_value());
        EXPECT_FALSE(refused.error().empty());

        const auto made = Puzzle::FromFields("a", kWhiteSolverFen, "b7b6 a1a8", "800", "mate");
        EXPECT_TRUE(made.has_value());
    }

    TEST(PuzzleTest, APromotionInTheLineIsKept) {
        const auto puzzle = Puzzle::FromFields("p", "4k3/P6p/8/8/8/8/8/4K3 b - - 0 1", "h7h6 a7a8q", "1000", "mateIn1");

        ASSERT_TRUE(puzzle.has_value());
        EXPECT_TRUE(puzzle->line[1].IsPromotion());
        EXPECT_EQ(puzzle->line[1].ToUci(), "a7a8q");
    }

}  // namespace BECore::Chess::Tests
