#include <tests/ChessUiHarness.h>

/// The takeback button of the move list (MovePanelTakeBack, MovesSectionTakeBack; the action ChessTakeBack): real clicks in the Wide and the Tall panel and in the menu section "Moves",
/// what a takeback does to the game, the program, a view and the premoves, its grey look and where it is drawn. Its layout on the size matrix is ChessUiMovePanelTest's and
/// ChessUiMovesSectionTest's. The scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        const Color kTakeBackColor{112, 84, 48, 255};  // the fill of the button (ChessMovePanel.xml, ChessMovesSection.xml); a greyed one is drawn at half its alpha
        const Color kMovePanelPlateColor{32, 44, 50, 255};
        constexpr int kWaitingFrames = 100;  // frames in which a program that wrongly took the turn would have answered at once (InstantEngine)

    }  // namespace

    TEST_F(ChessUiTest, TakeBackIsGreyBeforeTheFirstMoveAndTakesBackOneMoveOfTwoPeople) {
        Settle();
        const UIElement& button = MovePanelElement("MovePanelTakeBack");
        ASSERT_NE(ClickerOf(button), nullptr);
        EXPECT_TRUE(ClickerOf(button)->disabled);
        Draw();
        EXPECT_TRUE(RectCalls(kTakeBackColor).empty()) << "the grey look has half the alpha";
        EXPECT_TRUE(ClickControl(button).Both()) << "a grey button still takes the press, so the board under nothing gets it";
        EXPECT_TRUE(Model().GetMoves().empty());

        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        Settle();
        EXPECT_FALSE(ClickerOf(button)->disabled);
        Draw();
        EXPECT_EQ(RectCalls(kTakeBackColor).size(), 1u) << "only the panel's: the section is closed";
        Click("g1");
        ASSERT_TRUE(Model().HasSelection());
        const uint64_t version = Model().GetVersion();

        ASSERT_TRUE(ClickControl(button).Both());
        EXPECT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetTurn(), PieceColor::Black);
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_GT(Model().GetVersion(), version);
        EXPECT_EQ(ShownMovePanelRows(), "1. [e4]");

        ASSERT_TRUE(ClickControl(button).Both());
        EXPECT_TRUE(Model().GetMoves().empty());
        Settle();
        EXPECT_TRUE(ClickerOf(button)->disabled);
    }

    TEST_F(ChessUiTest, AgainstTheProgramTakeBackTakesItsAnswerAndTheMoveOfThePerson) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));
        Settle();

        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelTakeBack")).Both());
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetTurn(), PieceColor::White) << "the person is on the move again";
        for (int frame = 0; frame < kWaitingFrames; ++frame) {
            _manager->UpdateAll();
        }
        EXPECT_TRUE(Model().GetMoves().empty()) << "and the program waits for the person";
    }

    TEST_F(ChessUiTest, TakeBackWhileTheProgramThinksTakesBackTheMoveDropsTheSearchAndThePremoves) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        Click("d2");
        Click("d4");
        ASSERT_EQ(Model().GetPremoves().size(), 1u);

        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelTakeBack")).Both());
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_TRUE(Model().GetPremoves().empty());
        _manager->UpdateAll();
        EXPECT_FALSE(Opponent().IsThinking());
        EXPECT_TRUE(FramesUntil([&] { return engine->WasCancelled(); })) << "the stale search was cancelled";
        EXPECT_TRUE(Model().GetMoves().empty());
    }

    TEST_F(ChessUiTest, TakeBackReopensAFinishedGameAndEndsAView) {
        ASSERT_TRUE(PlayAll(Model(), {"f2f3", "e7e5", "g2g4", "d8h4"}));
        ASSERT_TRUE(Model().IsGameOver());
        ASSERT_TRUE(Model().ShowMoves(1));
        Settle();

        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelTakeBack")).Both());
        EXPECT_FALSE(Model().IsViewing()) << "the takeback is of the game, from its last move, never to the viewed one";
        EXPECT_EQ(Model().GetMoves().size(), 3u);
        EXPECT_FALSE(Model().IsGameOver());
        Draw();
        EXPECT_TRUE(RectCalls(kBoardDimColor).empty());
        EXPECT_EQ(ShownMovePanelRows(), "1. f3 e5 | 2. [g4]");
    }

    TEST_F(ChessUiTest, TheTakeBackOfTheTallPanelAndOfTheMovesSectionTakeBackToo) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5", "g1f3"}));
        ResizeWithoutInsets(kNarrowestWidth, kNarrowestTallHeight);
        ASSERT_EQ(GetScene().GetActiveView().name, PoolString::Intern(kTallViewName));
        ASSERT_TRUE(MovePanelElement("MovePanelBox").active.visible);
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelTakeBack")).Both());
        EXPECT_EQ(Model().GetMoves().size(), 2u);

        ResizeWithoutInsets(1024.0f, 768.0f);
        ASSERT_TRUE(ClickButton(kMenuColor));
        ASSERT_TRUE(ClickMenuList("MenuSectionMoves").Both());
        const UIElement& button = ElementOf("MovesSection", "MovesSectionTakeBack");
        ASSERT_TRUE(ClickControl(button).Both());
        EXPECT_EQ(Model().GetMoves().size(), 1u) << "one press, one takeback: only the panel's component answers it";
        EXPECT_EQ(ShownMoveRows("MovesSection"), "1. [e4]");
        EXPECT_TRUE(CanvasOf("MovesSection")->_visible) << "the section stays open";
        ASSERT_TRUE(ClickControl(button).Both());
        Settle();
        EXPECT_TRUE(ClickerOf(button)->disabled) << "the section greys its own button";
    }

    TEST_F(ChessUiTest, TheTakeBackButtonIsDrawnOnThePlateOfTheListAndUnderTheHud) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4"}));
        Settle();
        Draw();
        const auto& calls = _renderer->calls;
        long plate = -1;
        long takeBack = -1;
        long reset = -1;
        for (size_t i = 0; i < calls.size(); ++i) {
            const DrawCall& call = calls[i];
            if (call.kind != DrawKind::Rect) {
                continue;
            }
            const long at = static_cast<long>(i);
            if (SameColor(call.color, kMovePanelPlateColor)) {
                plate = at;
            } else if (SameColor(call.color, kTakeBackColor)) {
                takeBack = at;
            } else if (SameColor(call.color, kResetColor)) {
                reset = at;
            }
        }
        ASSERT_GE(plate, 0);
        ASSERT_GE(takeBack, 0);
        ASSERT_GE(reset, 0);
        EXPECT_LT(plate, takeBack);
        EXPECT_LT(takeBack, reset);
    }

}  // namespace BECore::Chess::Tests
