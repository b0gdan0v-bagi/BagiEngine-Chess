#include <tests/ChessUiHarness.h>

/// Viewing an earlier position of the game on the board (ChessGameModel::ShowMoves()): what the board draws and what a press on it does. The rows and buttons that start a view are
/// ChessUiMovePanelTest's and ChessUiMovesSectionTest's. The scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        const Color kMateColor{150, 20, 20, 240};  // the king's square once mated (ChessBoardComponent.cpp)

    }  // namespace

    TEST_F(ChessUiTest, WhileViewingTheBoardMarksTheViewedMoveAndNoneBeforeTheFirst) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5", "g1f3"}));
        ASSERT_TRUE(Model().ShowMoves(1));
        Draw();
        auto fills = LastMoveFills();
        ASSERT_EQ(fills.size(), 2u);
        EXPECT_TRUE(SameRect(fills[0]->rect, SquareRect("e2")));
        EXPECT_TRUE(SameRect(fills[1]->rect, SquareRect("e4")));

        ASSERT_TRUE(Model().ShowMoves(0));
        Draw();
        EXPECT_TRUE(LastMoveFills().empty()) << "the start position has no move to mark";

        ASSERT_TRUE(Model().EndViewing());
        Draw();
        fills = LastMoveFills();
        ASSERT_EQ(fills.size(), 2u);
        EXPECT_TRUE(SameRect(fills[1]->rect, SquareRect("f3")));
    }

    TEST_F(ChessUiTest, WhileViewingTheCheckIsTheViewedPositionsAndAFinishedGameIsNeitherDimmedNorMate) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "d7d5", "f1b5", "c7c6"}));
        ASSERT_TRUE(Model().ShowMoves(3));
        Draw();
        const auto checks = RectCalls(kCheckColor);
        ASSERT_EQ(checks.size(), 1u) << "Bb5+ checks the king on e8";
        EXPECT_TRUE(SameRect(checks[0]->rect, SquareRect("e8")));
        ASSERT_TRUE(Model().EndViewing());
        Draw();
        EXPECT_TRUE(RectCalls(kCheckColor).empty()) << "c6 has blocked it in the game";

        Model().Reset();
        ASSERT_TRUE(PlayAll(Model(), {"f2f3", "e7e5", "g2g4", "d8h4"}));
        ASSERT_TRUE(Model().IsGameOver());
        Draw();
        ASSERT_FALSE(RectCalls(kBoardDimColor).empty());
        ASSERT_TRUE(Model().ShowMoves(3));
        Draw();
        EXPECT_TRUE(RectCalls(kBoardDimColor).empty()) << "the earlier position is seen without the result over it";
        EXPECT_TRUE(RectCalls(kCheckColor).empty());
        EXPECT_TRUE(RectCalls(kMateColor).empty());
    }

    TEST_F(ChessUiTest, APressOnTheBoardWhileViewingOnlyReturnsToTheGame) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        ASSERT_TRUE(Model().ShowMoves(0));
        Click("g1");
        EXPECT_FALSE(Model().IsViewing());
        EXPECT_FALSE(Model().HasSelection()) << "the knight is not taken on a board the person did not see";
        Click("g1");
        EXPECT_TRUE(Model().HasSelection()) << "the next press plays as ever";

        // A finished game: the press brings its last position back, and only the next one starts a new game
        Model().Reset();
        ASSERT_TRUE(PlayAll(Model(), {"f2f3", "e7e5", "g2g4", "d8h4"}));
        ASSERT_TRUE(Model().ShowMoves(2));
        Click("e2");
        EXPECT_FALSE(Model().IsViewing());
        EXPECT_EQ(Model().GetMoves().size(), 4u);
        Click("e2");
        EXPECT_TRUE(Model().GetMoves().empty());
    }

    TEST_F(ChessUiTest, WhileViewingNoHintIsDrawnAndItComesBackWithTheGame) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4"}));
        CountingAnalysisFactory factory({New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e7e5"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(ShowTheHint());
        ASSERT_TRUE(Model().ShowMoves(0));
        Draw();
        EXPECT_TRUE(HintFills().first.empty());
        EXPECT_TRUE(HintFills().second.empty());
        ASSERT_TRUE(Model().EndViewing());
        Draw();
        EXPECT_EQ(HintFills().second.size(), 1u) << "a view does not move the version, so the hint of the game still holds";
    }

    TEST_F(ChessUiTest, WhileViewingTheQueuedPremovesAreNotDrawnAndStayQueued) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; })) << "the program has the search and waits at its gate";
        Click("d2");
        Click("d4");
        ASSERT_EQ(Model().GetPremoves().size(), 1u);

        ASSERT_TRUE(Model().ShowMoves(0));
        Draw();
        EXPECT_TRUE(RectCalls(kPremoveColor).empty());
        EXPECT_EQ(Model().GetPremoves().size(), 1u);
        EXPECT_TRUE(Opponent().IsThinking()) << "the search goes on: a view is no change of the game";

        Click("h7");
        EXPECT_FALSE(Model().IsViewing());
        EXPECT_EQ(Model().GetPremoves().size(), 1u) << "the press that returns to the game is no premove click";
        Draw();
        EXPECT_EQ(RectCalls(kPremoveColor).size(), 2u);
        engine->Open();
    }

    TEST_F(ChessUiTest, TheRowsOfThePanelAndOfTheSectionMarkTheViewedMove) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5", "g1f3"}));
        ASSERT_EQ(ShownMovePanelRows(), "1. e4 e5 | 2. [Nf3]");
        ASSERT_TRUE(ClickButton(kMenuColor));
        ASSERT_TRUE(ClickMenuList("MenuSectionMoves").Both());
        ASSERT_TRUE(Model().ShowMoves(2));
        EXPECT_EQ(ShownMovePanelRows(), "1. e4 [e5] | 2. Nf3") << "in the frame after the view: no version moved";
        EXPECT_EQ(ShownMoveRows("MovesSection"), "1. e4 [e5] | 2. Nf3");
        ASSERT_TRUE(Model().ShowMoves(0));
        EXPECT_EQ(ShownMovePanelRows(), "1. e4 e5 | 2. Nf3") << "the start position marks no move";
        ASSERT_TRUE(Model().EndViewing());
        EXPECT_EQ(ShownMovePanelRows(), "1. e4 e5 | 2. [Nf3]");
    }

    TEST_F(ChessUiTest, ViewingAnEarlyMoveOfALongGameShowsItsPageOfRows) {
        Settle();
        ASSERT_TRUE(PlayAQuietGame(Model(), 40)) << "twenty rows: the pages 13..20, 5..12 and 1..4";
        Draw();
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelNumber0")), "13.");
        ASSERT_TRUE(Model().ShowMoves(15));  // the White move of row 8
        Draw();
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelNumber0")), "5.");
        EXPECT_TRUE(MovePanelElement("MovePanelWhiteMark3").active.visible);
        ASSERT_TRUE(Model().ShowMoves(3));  // the White move of row 2
        Draw();
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelNumber0")), "1.");
        EXPECT_FALSE(MovePanelElement("MovePanelRow4").active.visible) << "the first page holds the four rows left";
        EXPECT_TRUE(MovePanelElement("MovePanelWhiteMark1").active.visible);
    }

}  // namespace BECore::Chess::Tests
