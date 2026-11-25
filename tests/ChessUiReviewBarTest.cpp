#include <BECore/UI/UIEnvironment.h>
#include <Chess/Components/ChessReviewBarComponent.h>
#include <tests/ChessUiHarness.h>

/// The Review button of the result bar: the node ReviewBar (config/nodes/ChessReviewBar.xml), shown exactly while a finished game may be reviewed (ChessGame::ReviewStartAllowedNow()).
/// The review's own rules are GameReviewerTest's, the section it starts is ChessUiReviewSectionTest's; the scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kBarNode = "ReviewBar";
        constexpr const char* kBarButton = "ReviewBarStart";

        /// The result bar is one square high across the board, centred on it (ChessResultComponent)
        constexpr float kBarTopInSquares = 3.5f;
        constexpr float kBarHeightInSquares = 1.0f;

        /// White solves: Black's setup h8g8, then Ra1a7, the reply g8h8 and Rb2b8#
        constexpr StringView kMateInTwo = "ladder\t7k/8/8/8/8/8/1R6/R6K b - - 0 1\th8g8 a1a7 g8h8 b2b8\t1200\tmateIn2\n";

        constexpr int kFrameLimit = 40;  ///< frames of a quarter second: far more than a puzzle's reply needs

        PuzzleSet MateInTwoSet() {
            String tsv = "id\tfen\tmoves\trating\tthemes\n";
            tsv.append(kMateInTwo.data(), kMateInTwo.size());
            Vector<String> problems;
            PuzzleSet set = PuzzleSet::Parse(StringView(tsv.data(), tsv.size()), &problems);
            EXPECT_TRUE(problems.empty());
            return set;
        }

        /// What is wrong with the button as laid out: cut text or a touch target under the minimum. It lies on the board by design, so the rule of BarProblems (the editor bar and the
        /// puzzle bar keep off the board) does not apply
        String ButtonProblems(const UICanvasLayout& layout) {
            String problems;
            const UIElement* button = layout.root->FindByName(PoolString::Intern(kBarButton));
            if (!button || button->rect.IsEmpty()) {
                return "the button has no rect\n";
            }
            const Rect& rect = button->rect;
            const UIContentFit fit = button->CheckContent(rect.w - 2.0f * kButtonAir, rect.h, layout.GetResolved().scale);
            if (fit.hasContent && !fit.fits) {
                problems += Format("the button cuts '{}': {:.1f} units wide\n", ShownText(*button).c_str(), rect.w);
            }
            if (rect.h < kSegmentHeight - kPixelTolerance || rect.w < kMinBarWidth - kPixelTolerance) {
                problems += Format("the button is {:.1f} x {:.1f} units, under {:.0f}\n", rect.w, rect.h, kSegmentHeight);
            }
            return problems;
        }

    }  // namespace

    TEST_F(ChessUiTest, TheReviewBarPassesTheSizeMatrix) {
        Draw();
        auto canvas = CanvasOf(kBarNode);
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        UICanvasLayout& layout = canvas->GetLayout();
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;

        String report;
        size_t reported = 0;
        const auto failures = ValidateOnMatrix(layout, options, ViewContext(), SceneViews());
        if (!failures.empty()) {
            reported++;
            report += DescribeMatrixFailures(failures).c_str();
        }
        for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
            layout.Update(ContextOf(matrixCase, true));
            const String problems = ButtonProblems(layout);
            if (!problems.empty() && reported++ < kReportedCases) {
                report += Format("{}:\n{}", matrixCase.label.c_str(), problems.c_str());
            }
        }
        layout.Invalidate();
        EXPECT_TRUE(report.empty()) << report.c_str();
    }

    TEST_F(ChessUiTest, TheReviewBarIsAHiddenNonModalCanvasWithItsComponentFirstAndNamesOfItsOwn) {
        const auto canvas = CanvasOf(kBarNode);
        ASSERT_TRUE(canvas);
        EXPECT_FALSE(canvas->_visible);
        EXPECT_FALSE(canvas->_modal);
        EXPECT_EQ(canvas->_space, CanvasSpace::SceneView);
        EXPECT_FALSE(canvas->_useSafeArea) << "the board stands at fixed units, an inset must not move the button";

        const SceneNode* node = nullptr;
        for (const auto& each : GetScene().GetRootNode().GetChildren()) {
            if (each->GetName() == PoolString::Intern(kBarNode)) {
                node = each.Get();
            }
        }
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->GetLayer(), 20) << "over the board, the result bar and the HUD";
        // The component comes before the canvas: the canvas is shown in the frame the game ends
        const auto& components = node->GetComponents();
        const auto behind = node->GetComponent<ChessReviewBarComponent>();
        ASSERT_TRUE(behind);
        ASSERT_EQ(components.size(), 2u);
        EXPECT_EQ(components[0].Get(), static_cast<IComponent*>(behind.Get()));
        EXPECT_EQ(components[1].Get(), static_cast<IComponent*>(canvas.Get()));

        Vector<PoolString> names;
        CollectNames(canvas->GetRoot(), names);
        ASSERT_FALSE(names.empty());
        for (const PoolString& name : names) {
            EXPECT_TRUE(name.ToStringView().starts_with(kBarNode)) << name.CStr() << ": elements are found by name in every canvas";
            for (const auto& other : GetScene().GetRootNode().GetChildren()) {
                const auto otherCanvas = other->GetComponent<UICanvasComponent>();
                if (otherCanvas && otherCanvas != canvas) {
                    EXPECT_EQ(otherCanvas->GetRoot().FindByName(name), nullptr) << name.CStr() << " is also in " << other->GetName().CStr();
                }
            }
        }
        const UIElement* button = canvas->GetRoot().FindByName(PoolString::Intern(kBarButton));
        ASSERT_NE(button, nullptr);
        const UIInputClicker* clicker = ClickerOf(*button);
        ASSERT_NE(clicker, nullptr);
        EXPECT_EQ(clicker->action, "ChessReviewStart"_intern);
    }

    TEST_F(ChessUiTest, TheReviewButtonLiesInsideTheResultBarInEveryView) {
        Draw();
        auto canvas = CanvasOf(kBarNode);
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = canvas->GetLayout();
        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            LayOutInTheNarrowestWindow(layout, shape);
            const UIElement* button = layout.root->FindByName(PoolString::Intern(kBarButton));
            ASSERT_NE(button, nullptr);
            const Rect bar{shape.boardLeft, shape.boardTop + kBarTopInSquares * kCellSize, kBoardUnits, kBarHeightInSquares * kCellSize};
            EXPECT_TRUE(LiesIn(button->rect, bar)) << "button " << button->rect.x << "," << button->rect.y << " " << button->rect.w << "x" << button->rect.h << ", bar " << bar.x << "," << bar.y << " "
                                                   << bar.w << "x" << bar.h;
            EXPECT_NEAR(button->rect.x + button->rect.w * 0.5f, bar.x + bar.w * 0.5f, kPixelTolerance) << "centred across the board";
            EXPECT_NEAR(button->rect.y + button->rect.h * 0.5f, bar.y + bar.h * 0.5f, kPixelTolerance) << "centred in the bar";
        }
        layout.Invalidate();
    }

    /// The fixture of the cases below: the finished game is the fool's mate, played in code, and the bar is what is clicked
    class ChessUiReviewBarTest : public ChessUiTest {
    protected:
        ChessGame& Game() const {
            ChessGame* game = ChessGame::GetActive();
            EXPECT_NE(game, nullptr);
            return *game;
        }

        IntrusivePtr<UICanvasComponent> Bar() const {
            return CanvasOf(kBarNode);
        }

        Taken ClickBar() {
            return ClickControl(ElementOf(kBarNode, kBarButton));
        }

        /// One update and one draw: the component compares in both, and the canvas is laid out by the draw
        bool ShownAfterAFrame() {
            _manager->UpdateAll();
            Draw();
            return Bar()->_visible;
        }
    };

    TEST_F(ChessUiReviewBarTest, TheBarIsHiddenWhileTheGameIsOnOrHasNoMovesAndShownOnAFinishedGame) {
        EXPECT_FALSE(ShownAfterAFrame()) << "not at the start of a game";

        ASSERT_TRUE(PlayAll(Model(), {"f2f3", "e7e5", "g2g4"}));
        EXPECT_FALSE(ShownAfterAFrame()) << "a game in progress";

        ASSERT_TRUE(PlayAll(Model(), {"d8h4"}));
        ASSERT_TRUE(Model().IsGameOver());
        EXPECT_TRUE(ShownAfterAFrame());

        Model().Reset();
        ASSERT_TRUE(Model().Resign(PieceColor::White).has_value());
        EXPECT_FALSE(ShownAfterAFrame()) << "a finished game with no moves has nothing to review";
    }

    TEST_F(ChessUiReviewBarTest, ATakebackOrAResetWithNoFrameBetweenHidesTheBarAtTheNextUpdateAndTheNextDraw) {
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(ShownAfterAFrame());

        ASSERT_EQ(Model().TakeBack(1), 1u);
        _manager->UpdateAll();
        EXPECT_FALSE(Bar()->_visible) << "the update compares";

        ASSERT_TRUE(PlayAll(Model(), {"d8h4"}));
        ASSERT_TRUE(ShownAfterAFrame());
        Model().Reset();
        Draw();
        EXPECT_FALSE(Bar()->_visible) << "so does the draw";
    }

    TEST_F(ChessUiReviewBarTest, TheBarIsHiddenWhileAnEarlierPositionIsViewedOrAPositionIsEditedAndBackAfter) {
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(ShownAfterAFrame());

        ASSERT_TRUE(Model().ShowMoves(2));
        EXPECT_FALSE(ShownAfterAFrame()) << "the result bar is gone while viewing";
        ASSERT_TRUE(Model().ShowMoves(Model().GetMoves().size()));
        EXPECT_TRUE(ShownAfterAFrame());

        Game().BeginEditing(EditStart::Current);
        ASSERT_TRUE(Model().IsDrafting());
        EXPECT_FALSE(ShownAfterAFrame());
    }

    TEST_F(ChessUiReviewBarTest, TheBarIsHiddenWithAPuzzleOnEvenWhenItsGameIsOver) {
        Game().SetPuzzles(MateInTwoSet());
        ASSERT_TRUE(Game().BeginPuzzle(0));
        Settle();
        ASSERT_TRUE(Play(Model(), "a1a7"));
        for (int frame = 0; frame < kFrameLimit && Model().GetMoves().size() < 3; ++frame) {
            FrameOfGameTime();
        }
        ASSERT_EQ(Model().GetMoves().size(), 3u);
        ASSERT_TRUE(Play(Model(), "b2b8"));
        FrameOfGameTime();
        ASSERT_TRUE(Model().IsGameOver());
        ASSERT_TRUE(Game().IsPuzzleOn());

        EXPECT_FALSE(ShownAfterAFrame());
    }

    TEST_F(ChessUiReviewBarTest, TheBarIsHiddenWhileAReviewRunsOrIsDoneAndShownAgainAfterAnAbandon) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<AnalysingEngine>();
        engine->Gate(true);
        CountingAnalysisFactory factory{engine};
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(ShownAfterAFrame());

        ASSERT_TRUE(Analyst().StartReview(Model()));
        EXPECT_FALSE(ShownAfterAFrame()) << "a review runs";
        engine->Open();
        ASSERT_TRUE(WaitForReview());
        EXPECT_FALSE(ShownAfterAFrame()) << "a review is done";

        Analyst().AbandonReview();
        EXPECT_TRUE(ShownAfterAFrame()) << "an abandoned review leaves the button";
    }

    TEST_F(ChessUiReviewBarTest, TheBarIsShownAfterAFailedReviewAsTheRetry) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingAnalysisFactory factory(EngineError{EngineErrorKind::LaunchFailed, "no engine for the review"});
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(Analyst().StartReview(Model()));
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetReviewState(Model()) == ReviewState::Failed; }));

        EXPECT_TRUE(ShownAfterAFrame());
    }

    TEST_F(ChessUiReviewBarTest, AClickOnTheButtonStartsTheReviewAndNeverReachesTheBoard) {
        SKIP_WITHOUT_WORKER_THREADS();
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(ShownAfterAFrame());
        const uint64_t version = Model().GetVersion();
        const size_t moves = Model().GetMoves().size();

        ASSERT_TRUE(ClickBar().Both()) << "the press and the release are the UI's, not the board's";

        EXPECT_EQ(Analyst().GetReviewState(Model()), ReviewState::Running);
        EXPECT_TRUE(Model().IsGameOver()) << "a press on the board would start a new game";
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_EQ(Model().GetMoves().size(), moves);
        EXPECT_TRUE(StringView(Var("status")).starts_with("Reviewing "));
        EXPECT_FALSE(ShownAfterAFrame()) << "the bar goes while the review runs";

        ASSERT_TRUE(WaitForReview());
        EXPECT_FALSE(ShownAfterAFrame());
        EXPECT_TRUE(Model().IsGameOver());
        EXPECT_EQ(Model().GetMoves().size(), moves);
    }

}  // namespace BECore::Chess::Tests
