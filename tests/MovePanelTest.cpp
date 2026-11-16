#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/MovePanel.h>
#include <EASTL/algorithm.h>
#include <tests/ChessTestUtil.h>

/// The pure side of the move panel: the rule that shows it and the text of its rows. No engine services.
namespace BECore::Chess::Tests {

    namespace {

        constexpr float kBeyond = 0.01f;
        constexpr float kWideWidth = 1120.0f;
        constexpr float kWideHeight = 800.0f;

        bool Fits(float left, float top, float right, float bottom) {
            return MovePanelFits(left, top, right, bottom);
        }

        /// @p count rows of a game from the start: "w1" and "b1" with the plies 0 and 1, and so on
        Vector<MoveListRow> RowsOf(int count, int firstNumber = 1) {
            Vector<MoveListRow> rows;
            for (int i = 0; i < count; ++i) {
                const String number = ToString(firstNumber + i);
                MoveListRow row;
                row.number = firstNumber + i;
                row.white = MoveListEntry{String("w") + number, 2 * i};
                row.black = MoveListEntry{String("b") + number, 2 * i + 1};
                rows.push_back(eastl::move(row));
            }
            return rows;
        }

        int CurrentCells(const Vector<MovePanelRow>& panel) {
            int count = 0;
            for (const MovePanelRow& row : panel) {
                count += (row.white.current ? 1 : 0) + (row.black.current ? 1 : 0);
            }
            return count;
        }

        size_t ShownRows(const Vector<MovePanelRow>& panel) {
            size_t count = 0;
            for (const MovePanelRow& row : panel) {
                count += row.shown ? 1 : 0;
            }
            return count;
        }

        void ExpectEmpty(const MovePanelRow& row) {
            EXPECT_FALSE(row.shown);
            EXPECT_TRUE(row.number.empty());
            EXPECT_TRUE(row.white.text.empty());
            EXPECT_TRUE(row.black.text.empty());
            EXPECT_FALSE(row.white.current);
            EXPECT_FALSE(row.black.current);
        }

        Vector<Move> MovesOf(StringView fen, const Vector<StringView>& line) {
            Position position = PositionFromFen(fen);
            Vector<Move> moves;
            for (const StringView uci : line) {
                const Optional<Move> move = MoveGenerator::ParseUci(position, uci);
                if (!move) {
                    ADD_FAILURE() << "Not a legal move: " << String(uci).c_str();
                    break;
                }
                position.DoMove(*move);
                moves.push_back(*move);
            }
            return moves;
        }

    }  // namespace

    // ---- The box and the rule ----

    TEST(MovePanelTest, TheBoxIsRightOfTheBoardAndAsHighAsIt) {
        EXPECT_FLOAT_EQ(kMovePanelLeft, 760.0f);
        EXPECT_FLOAT_EQ(kMovePanelTop, 80.0f);
        EXPECT_FLOAT_EQ(kMovePanelRight, 1080.0f);
        EXPECT_FLOAT_EQ(kMovePanelBottom, 720.0f);
        EXPECT_EQ(kMovePanelRows, 8u);
        EXPECT_GE(kMovePanelLeft, 100.0f + 640.0f) << "the board is 640 units at x = 100";
        EXPECT_FLOAT_EQ(kMovePanelBottom - kMovePanelTop, 640.0f);
    }

    TEST(MovePanelTest, TheWholeWideViewFitsAndTheOtherViewsDoNot) {
        EXPECT_TRUE(Fits(0.0f, 0.0f, kWideWidth, kWideHeight));
        EXPECT_FALSE(Fits(0.0f, 0.0f, 840.0f, 800.0f));
        EXPECT_FALSE(Fits(0.0f, 0.0f, 720.0f, 960.0f));
    }

    TEST(MovePanelTest, TheEdgesOfTheBoxAreInsideAndOneStepBeyondEachEdgeIsNot) {
        EXPECT_TRUE(Fits(kMovePanelLeft, kMovePanelTop, kMovePanelRight, kMovePanelBottom));
        EXPECT_FALSE(Fits(kMovePanelLeft + kBeyond, kMovePanelTop, kMovePanelRight, kMovePanelBottom));
        EXPECT_FALSE(Fits(kMovePanelLeft, kMovePanelTop + kBeyond, kMovePanelRight, kMovePanelBottom));
        EXPECT_FALSE(Fits(kMovePanelLeft, kMovePanelTop, kMovePanelRight - kBeyond, kMovePanelBottom));
        EXPECT_FALSE(Fits(kMovePanelLeft, kMovePanelTop, kMovePanelRight, kMovePanelBottom - kBeyond));
        EXPECT_TRUE(Fits(kMovePanelLeft - kBeyond, kMovePanelTop - kBeyond, kMovePanelRight + kBeyond, kMovePanelBottom + kBeyond));
    }

    TEST(MovePanelTest, ASafeAreaThatIsEmptyOrBackwardsFitsNothing) {
        EXPECT_FALSE(Fits(0.0f, 0.0f, 0.0f, 0.0f));
        EXPECT_FALSE(Fits(900.0f, 500.0f, 100.0f, 100.0f));
    }

    TEST(MovePanelTest, SideInsetsCutTheBoxOfTheNarrowWideWindowsOnly) {
        // The safe area of SafeAreaSimulation::SideInsets (9 % of the short side at the sides, 4 % at the bottom) in view units at the fit scale 0.6 of a 480 px window
        constexpr float kScale = 0.6f;
        constexpr float kShortSide = 480.0f;
        const auto safeIn = [&](float outputWidth) {
            const float viewPixels = kWideWidth * kScale;
            const float bars = (outputWidth - viewPixels) * 0.5f;
            const float inset = kShortSide * 0.09f;
            const float left = eastl::max(0.0f, (inset - bars) / kScale);
            const float right = eastl::min(kWideWidth, (outputWidth - inset - bars) / kScale);
            const float bottom = kWideHeight - kShortSide * 0.04f / kScale;
            return Fits(left, 0.0f, right, bottom);
        };
        EXPECT_FALSE(safeIn(672.0f)) << "1.4, the narrowest Wide window";
        EXPECT_FALSE(safeIn(700.0f));
        EXPECT_TRUE(safeIn(720.0f)) << "3:2";
        EXPECT_TRUE(safeIn(768.0f)) << "16:10";
        EXPECT_TRUE(safeIn(853.0f)) << "16:9";
    }

    TEST(MovePanelTest, TheTallBoxIsUnderTheBoardAndTheMaterialAsWideAsTheBoard) {
        constexpr float kTallBoardBottom = 144.0f + 640.0f;
        constexpr float kLowerMaterialBottom = 832.0f;  // the lower row of the captured material in the Portrait places (Game/MaterialLayout.h)
        EXPECT_FLOAT_EQ(kMovePanelTallLeft, 40.0f) << "the board's left edge";
        EXPECT_FLOAT_EQ(kMovePanelTallRight - kMovePanelTallLeft, 640.0f);
        EXPECT_GE(kMovePanelTallTop, kTallBoardBottom);
        EXPECT_GE(kMovePanelTallTop, kLowerMaterialBottom);
        EXPECT_FLOAT_EQ(kMovePanelTallBottom - kMovePanelTallTop, 4.0f * 56.0f + 8.0f + 56.0f) << "two columns of four rows and the band of the buttons";
    }

    TEST(MovePanelTest, EachViewIsAskedForItsOwnBoxAndHoldsNoOther) {
        constexpr float kTallWidth = 720.0f;
        constexpr float kTallHeight = 1204.0f;
        EXPECT_TRUE(MovePanelFits(0.0f, 0.0f, kTallWidth, kTallHeight, MovePanelView::Tall));
        EXPECT_FALSE(MovePanelFits(0.0f, 0.0f, kTallWidth, kTallHeight, MovePanelView::Wide));
        EXPECT_FALSE(MovePanelFits(0.0f, 0.0f, kWideWidth, kWideHeight, MovePanelView::Tall));
        EXPECT_FALSE(MovePanelFits(0.0f, 0.0f, 720.0f, 960.0f, MovePanelView::Tall)) << "Portrait is too low";
        EXPECT_FALSE(MovePanelFits(0.0f, 0.0f, 840.0f, 800.0f, MovePanelView::Tall));
        EXPECT_EQ(Fits(0.0f, 0.0f, kWideWidth, kWideHeight), MovePanelFits(0.0f, 0.0f, kWideWidth, kWideHeight, MovePanelView::Wide)) << "the Wide box is the default";
    }

    TEST(MovePanelTest, TheEdgesOfTheTallBoxAreInsideAndOneStepBeyondIsNot) {
        const auto fits = [](float left, float top, float right, float bottom) { return MovePanelFits(left, top, right, bottom, MovePanelView::Tall); };
        EXPECT_TRUE(fits(kMovePanelTallLeft, kMovePanelTallTop, kMovePanelTallRight, kMovePanelTallBottom));
        EXPECT_FALSE(fits(kMovePanelTallLeft + kBeyond, kMovePanelTallTop, kMovePanelTallRight, kMovePanelTallBottom));
        EXPECT_FALSE(fits(kMovePanelTallLeft, kMovePanelTallTop + kBeyond, kMovePanelTallRight, kMovePanelTallBottom));
        EXPECT_FALSE(fits(kMovePanelTallLeft, kMovePanelTallTop, kMovePanelTallRight - kBeyond, kMovePanelTallBottom));
        EXPECT_FALSE(fits(kMovePanelTallLeft, kMovePanelTallTop, kMovePanelTallRight, kMovePanelTallBottom - kBeyond));
    }

    // ---- The window of rows ----

    TEST(MovePanelTest, ThereAreAlwaysAsManyEntriesAsAsked) {
        EXPECT_EQ(BuildMovePanelRows({}, -1).size(), kMovePanelRows);
        EXPECT_EQ(BuildMovePanelRows(RowsOf(3), 5).size(), kMovePanelRows);
        EXPECT_EQ(BuildMovePanelRows(RowsOf(30), 59).size(), kMovePanelRows);
        EXPECT_EQ(BuildMovePanelRows(RowsOf(30), 59, 3).size(), 3u);
    }

    TEST(MovePanelTest, NoRowsGiveEightEmptyEntries) {
        const Vector<MovePanelRow> panel = BuildMovePanelRows({}, -1);
        ASSERT_EQ(panel.size(), kMovePanelRows);
        for (const MovePanelRow& row : panel) {
            ExpectEmpty(row);
        }
        EXPECT_EQ(ShownRows(panel), 0u);
    }

    TEST(MovePanelTest, ThreeRowsAreTheFirstThreeEntries) {
        const Vector<MovePanelRow> panel = BuildMovePanelRows(RowsOf(3), 5);
        ASSERT_EQ(panel.size(), kMovePanelRows);
        EXPECT_EQ(ShownRows(panel), 3u);
        for (size_t i = 0; i < 3; ++i) {
            EXPECT_TRUE(panel[i].shown);
        }
        EXPECT_EQ(panel[0].number, "1.");
        EXPECT_EQ(panel[0].white.text, "w1");
        EXPECT_EQ(panel[0].black.text, "b1");
        EXPECT_EQ(panel[2].number, "3.");
        for (size_t i = 3; i < kMovePanelRows; ++i) {
            ExpectEmpty(panel[i]);
        }
    }

    TEST(MovePanelTest, TwelveRowsShowTheLastEight) {
        const Vector<MovePanelRow> panel = BuildMovePanelRows(RowsOf(12), 23);
        ASSERT_EQ(panel.size(), kMovePanelRows);
        EXPECT_EQ(ShownRows(panel), kMovePanelRows);
        EXPECT_EQ(panel[0].number, "5.");
        EXPECT_EQ(panel[0].white.text, "w5");
        EXPECT_EQ(panel[7].number, "12.");
        EXPECT_EQ(panel[7].black.text, "b12");
    }

    TEST(MovePanelTest, ExactlyEightRowsAreAllShown) {
        const Vector<MovePanelRow> panel = BuildMovePanelRows(RowsOf(8), 15);
        EXPECT_EQ(ShownRows(panel), kMovePanelRows);
        EXPECT_EQ(panel[0].number, "1.");
        EXPECT_EQ(panel[7].number, "8.");
    }

    TEST(MovePanelTest, TheWindowEndsAtTheRowOfTheCurrentPly) {
        // The viewed ply 9 is the Black move of row 5: rows 1 to 5 show, nothing after them
        const Vector<MovePanelRow> panel = BuildMovePanelRows(RowsOf(12), 9);
        EXPECT_EQ(ShownRows(panel), 5u);
        EXPECT_EQ(panel[4].number, "5.");
        EXPECT_TRUE(panel[4].black.current);
        ExpectEmpty(panel[5]);

        // Ply 19 is row 10, so the window is rows 3 to 10
        const Vector<MovePanelRow> later = BuildMovePanelRows(RowsOf(12), 19);
        EXPECT_EQ(ShownRows(later), kMovePanelRows);
        EXPECT_EQ(later[0].number, "3.");
        EXPECT_EQ(later[7].number, "10.");
        EXPECT_TRUE(later[7].black.current);
    }

    TEST(MovePanelTest, AFewerRowsOptionIsHonoured) {
        const Vector<MovePanelRow> panel = BuildMovePanelRows(RowsOf(5), 9, 2);
        ASSERT_EQ(panel.size(), 2u);
        EXPECT_EQ(panel[0].number, "4.");
        EXPECT_EQ(panel[1].number, "5.");
    }

    // ---- The numbers and the text ----

    TEST(MovePanelTest, NumbersAreTheNumberAndADot) {
        EXPECT_EQ(BuildMovePanelRows(RowsOf(1), 1)[0].number, "1.");
        EXPECT_EQ(BuildMovePanelRows(RowsOf(1, 12), 1)[0].number, "12.");
        EXPECT_EQ(BuildMovePanelRows(RowsOf(1, 100), 1)[0].number, "100.");
    }

    TEST(MovePanelTest, AGameThatBeganWithBlackToMoveHasWhiteAsDots) {
        const char* fen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 7";
        const Vector<MoveListRow> rows = BuildMoveList(fen, MovesOf(fen, {"g8f6", "b1c3"}));
        for (const int ply : {-1, 0, 1}) {
            const Vector<MovePanelRow> panel = BuildMovePanelRows(rows, ply);
            EXPECT_EQ(panel[0].number, "7.") << ply;
            EXPECT_EQ(panel[0].white.text, "...") << ply;
            EXPECT_FALSE(panel[0].white.current) << ply << ": the absent move is never current";
            EXPECT_EQ(panel[0].black.text, "Nf6") << ply;
        }
        const Vector<MovePanelRow> panel = BuildMovePanelRows(rows, 0);
        EXPECT_TRUE(panel[0].black.current);
        EXPECT_EQ(CurrentCells(panel), 1);
    }

    TEST(MovePanelTest, AnAbsentBlackMoveIsEmptyAndNeverCurrent) {
        Vector<MoveListRow> rows = RowsOf(1);
        rows.push_back(MoveListRow{2, MoveListEntry{"Nf3", 2}, {}});
        for (const int ply : {-1, 2}) {
            const Vector<MovePanelRow> panel = BuildMovePanelRows(rows, ply);
            EXPECT_EQ(panel[1].white.text, "Nf3") << ply;
            EXPECT_TRUE(panel[1].black.text.empty()) << ply;
            EXPECT_FALSE(panel[1].black.current) << ply << ": an absent move has the ply -1, which a current ply of -1 must not find";
            EXPECT_TRUE(panel[1].shown) << ply;
        }
    }

    TEST(MovePanelTest, TheTextIsTheSanOfTheGame) {
        const char* fen = "6k1/4P3/8/8/8/8/8/R3K2R w KQ - 0 1";
        const Vector<MovePanelRow> panel = BuildMovePanelRows(BuildMoveList(fen, MovesOf(fen, {"e1g1", "g8h8", "e7e8q"})), 2);
        EXPECT_EQ(panel[0].white.text, "O-O");
        EXPECT_EQ(panel[0].black.text, "Kh8");
        EXPECT_EQ(panel[1].white.text, "e8=Q+");
    }

    // ---- The current cell ----

    TEST(MovePanelTest, OneCellIsCurrentOnAWhiteLastMove) {
        const Vector<MovePanelRow> panel = BuildMovePanelRows(RowsOf(3), 4);
        EXPECT_EQ(CurrentCells(panel), 1);
        EXPECT_TRUE(panel[2].white.current);
    }

    TEST(MovePanelTest, OneCellIsCurrentOnABlackLastMove) {
        const Vector<MovePanelRow> panel = BuildMovePanelRows(RowsOf(3), 5);
        EXPECT_EQ(CurrentCells(panel), 1);
        EXPECT_TRUE(panel[2].black.current);
    }

    TEST(MovePanelTest, NoCurrentMoveMarksNothingAndShowsTheEndOfTheList) {
        const Vector<MovePanelRow> panel = BuildMovePanelRows(RowsOf(12), -1);
        EXPECT_EQ(CurrentCells(panel), 0);
        EXPECT_EQ(panel[0].number, "5.");
        EXPECT_EQ(panel[7].number, "12.");
    }

    TEST(MovePanelTest, APlyNoRowHasMarksNothing) {
        const Vector<MovePanelRow> panel = BuildMovePanelRows(RowsOf(3), 40);
        EXPECT_EQ(CurrentCells(panel), 0);
        EXPECT_EQ(ShownRows(panel), 3u);
    }

}  // namespace BECore::Chess::Tests
