#include <tests/ChessUiHarness.h>

/// Viewing an earlier position of the game (ChessGameModel::ShowMoves()): what the board and the rows draw, real clicks on the cells and the step buttons of the move panel and the
/// menu section "Moves", and what a press on the board does. The layout of the panels is ChessUiMovePanelTest's and ChessUiMovesSectionTest's. The scene, the helpers and the fixture
/// ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        const Color kMateColor{150, 20, 20, 240};  // the king's square once mated (ChessBoardComponent.cpp)
        const Color kStepColor{58, 74, 84, 255};   // the step buttons of the move list (ChessMovePanel.xml, ChessMovesSection.xml)
        const Color kMovePanelPlateColor{32, 44, 50, 255};

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

    TEST_F(ChessUiTest, WhileViewingTheStatusSaysWhatIsViewedAndHowToGetBackAndTheGamesLineReturnsWithIt) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "d7d5", "f1b5"}));
        Draw();
        ASSERT_EQ(ShownText(Label("Status")), "Check");
        const String turn = ShownText(Label("Turn"));

        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelBlack0")).Both());
        Draw();
        EXPECT_EQ(ShownText(Label("Status")), "At move 1 - tap board") << "a view moves no version of the game, the HUD sees it all the same";
        EXPECT_EQ(ShownText(Label("Turn")), turn) << "the turn is the game's";
        ASSERT_TRUE(Model().ShowMoves(0));
        Draw();
        EXPECT_EQ(ShownText(Label("Status")), "At the start - tap board");

        Click("e2");
        Draw();
        ASSERT_FALSE(Model().IsViewing());
        EXPECT_EQ(ShownText(Label("Status")), "Check");
    }

    TEST_F(ChessUiTest, AViewWinsOverTheLineOfAnEngineErrorWhichComesBackWithTheGame) {
        const auto engine = New<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));
        Draw();
        const String error = ShownText(Label("Status"));
        ASSERT_EQ(error, OpponentText::Error(*Opponent().GetError()));

        ASSERT_TRUE(Model().ShowMoves(0));
        Draw();
        EXPECT_EQ(ShownText(Label("Status")), "At the start - tap board") << "the board shows another position: the line says how to get back";
        ASSERT_TRUE(Model().EndViewing());
        Draw();
        EXPECT_EQ(ShownText(Label("Status")), error);
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
        ASSERT_TRUE(PlayAQuietGame(Model(), 40)) << "twenty rows: the pages 14..20, 7..13 and 1..6";
        Draw();
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelNumber0")), "14.");
        ASSERT_TRUE(Model().ShowMoves(15));  // the White move of row 8
        Draw();
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelNumber0")), "7.");
        EXPECT_TRUE(MovePanelElement("MovePanelWhiteMark1").active.visible);
        ASSERT_TRUE(Model().ShowMoves(3));  // the White move of row 2
        Draw();
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelNumber0")), "1.");
        EXPECT_FALSE(MovePanelElement("MovePanelRow6").active.visible) << "the first page holds the six rows left";
        EXPECT_TRUE(MovePanelElement("MovePanelWhiteMark1").active.visible);
    }

    TEST_F(ChessUiTest, AClickOnACellOfTheMovePanelShowsThatPositionAndAClickOnTheLastMoveReturns) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5", "g1f3"}));
        Settle();
        const uint64_t version = Model().GetVersion();
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelWhite0")).Both());
        EXPECT_TRUE(Model().IsViewing());
        EXPECT_EQ(Model().GetShownMoveCount(), 1u);
        EXPECT_EQ(ShownMovePanelRows(), "1. [e4] e5 | 2. Nf3");
        EXPECT_EQ(Model().GetVersion(), version);

        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelBlack1")).Both()) << "the empty cell of Black in the last row";
        EXPECT_EQ(Model().GetShownMoveCount(), 1u) << "has no move to show";
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelWhite1")).Both());
        EXPECT_FALSE(Model().IsViewing()) << "the last move is the game itself";
        EXPECT_EQ(ShownMovePanelRows(), "1. e4 e5 | 2. [Nf3]");
    }

    TEST_F(ChessUiTest, TheStepButtonsOfTheMovePanelWalkTheGameAndItsPagesAndLatestReturns) {
        ASSERT_TRUE(PlayAQuietGame(Model(), 40)) << "twenty rows: the pages 14..20, 7..13 and 1..6";
        Settle();
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelStepForward")).Both());
        EXPECT_FALSE(Model().IsViewing()) << "nothing after the game";
        for (size_t step = 1; step <= 14; ++step) {
            ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelStepBack")).Both()) << step;
            EXPECT_EQ(Model().GetShownMoveCount(), 40u - step);
        }
        Draw();
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelNumber0")), "7.") << "ply 25 is in row 13, on the page before the last";
        EXPECT_TRUE(MovePanelElement("MovePanelBlackMark6").active.visible);
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelStepForward")).Both());
        EXPECT_EQ(Model().GetShownMoveCount(), 27u);
        Draw();
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelNumber0")), "14.") << "ply 26 opens row 14, the last page";
        EXPECT_TRUE(MovePanelElement("MovePanelWhiteMark0").active.visible);

        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelLatest")).Both());
        EXPECT_FALSE(Model().IsViewing());
        Draw();
        EXPECT_TRUE(MovePanelElement("MovePanelBlackMark6").active.visible);
    }

    TEST_F(ChessUiTest, AClickOnACellOfTheTallPanelViewsTheMoveOfItsColumn) {
        ASSERT_TRUE(PlayAQuietGame(Model(), 9));
        ResizeWithoutInsets(kNarrowestWidth, kNarrowestTallHeight);
        ASSERT_EQ(GetScene().GetActiveView().name, PoolString::Intern(kTallViewName));
        ASSERT_TRUE(MovePanelElement("MovePanelBox").active.visible);
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelBlack3")).Both()) << "the last row of the left column";
        EXPECT_EQ(Model().GetShownMoveCount(), 8u);
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelStepBack")).Both());
        EXPECT_EQ(Model().GetShownMoveCount(), 7u);
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelLatest")).Both());
        EXPECT_FALSE(Model().IsViewing());
    }

    TEST_F(ChessUiTest, TheMovesSectionViewsAndStepsWithTheSameRowsAndStaysOpen) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5", "g1f3"}));
        ResizeWithoutInsets(1024.0f, 768.0f);
        ASSERT_FALSE(MovePanelElement("MovePanelBox").active.visible) << "a window with no panel: the section is where the list is";
        ASSERT_TRUE(ClickButton(kMenuColor));
        ASSERT_TRUE(ClickMenuList("MenuSectionMoves").Both());
        const auto section = CanvasOf("MovesSection");
        ASSERT_TRUE(section);

        ASSERT_TRUE(ClickControl(ElementOf("MovesSection", "MovesSectionBlack0")).Both());
        EXPECT_EQ(Model().GetShownMoveCount(), 2u);
        EXPECT_EQ(ShownMoveRows("MovesSection"), "1. e4 [e5] | 2. Nf3");
        EXPECT_TRUE(section->_visible) << "the section stays open, the board shows the position under its dim";
        ASSERT_TRUE(ClickControl(ElementOf("MovesSection", "MovesSectionStepBack")).Both());
        EXPECT_EQ(Model().GetShownMoveCount(), 1u);
        ASSERT_TRUE(ClickControl(ElementOf("MovesSection", "MovesSectionStepForward")).Both());
        EXPECT_EQ(Model().GetShownMoveCount(), 2u);
        ASSERT_TRUE(ClickControl(ElementOf("MovesSection", "MovesSectionLatest")).Both());
        EXPECT_FALSE(Model().IsViewing());
        EXPECT_EQ(ShownMoveRows("MovesSection"), "1. e4 e5 | 2. [Nf3]");

        ASSERT_TRUE(ClickControl(ElementOf("MovesSection", "MovesSectionWhite0")).Both());
        ASSERT_TRUE(ClickControl(ElementOf("MovesSection", "MovesSectionClose")).Both());
        EXPECT_TRUE(Model().IsViewing()) << "closing the section keeps the view; the board or a move ends it";
        Click("a2");
        EXPECT_FALSE(Model().IsViewing());
    }

    TEST_F(ChessUiTest, AMoveOfTheProgramEndsAViewInTheFrameItIsPlayed) {
        SKIP_WITHOUT_WORKER_THREADS();
        StartFrom(kBlackCanTakeAPawn);
        ASSERT_TRUE(PlayAll(Model(), {"d5e4", "g1f3"}));
        Settle();
        const auto take = MoveGenerator::ParseUci(Model().GetPosition(), "b8c6");
        ASSERT_TRUE(take.has_value());
        const auto engine = New<GatedEngine>(true, *take);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelBlack0")).Both());
        ASSERT_TRUE(Model().IsViewing());
        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 3; }));
        EXPECT_FALSE(Model().IsViewing()) << "a new move is never hidden behind a view";
        EXPECT_EQ(ShownMovePanelRows(), "2. ... dxe4 | 3. Nf3 [Nc6]");
    }

    TEST_F(ChessUiTest, TheStepButtonsAreDrawnOnThePlateOfTheListAndUnderTheHud) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4"}));
        Settle();
        Draw();
        const auto& calls = _renderer->calls;
        long plate = -1;
        long firstStep = -1;
        long reset = -1;
        for (size_t i = 0; i < calls.size(); ++i) {
            const DrawCall& call = calls[i];
            if (call.kind != DrawKind::Rect) {
                continue;
            }
            const long at = static_cast<long>(i);
            if (SameColor(call.color, kMovePanelPlateColor)) {
                plate = at;
            } else if (SameColor(call.color, kStepColor) && firstStep < 0) {
                firstStep = at;
            } else if (SameColor(call.color, kResetColor)) {
                reset = at;
            }
        }
        ASSERT_GE(plate, 0);
        ASSERT_GE(firstStep, 0) << "the buttons are drawn";
        ASSERT_GE(reset, 0);
        EXPECT_LT(plate, firstStep);
        EXPECT_LT(firstStep, reset);
        EXPECT_EQ(RectCalls(kStepColor).size(), 3u) << "the three of the panel; the section is closed";
    }

}  // namespace BECore::Chess::Tests
