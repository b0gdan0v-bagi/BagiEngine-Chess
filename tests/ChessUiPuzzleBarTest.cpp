#include <BECore/UI/UIEnvironment.h>
#include <Chess/Components/ChessPuzzleBarComponent.h>
#include <tests/ChessUiHarness.h>
#include <tests/ShippedOpenings.h>

/// The bar of the puzzle mode: the node PuzzleBar (config/nodes/ChessPuzzleBar.xml), shown exactly while a puzzle is on. The scene, the helpers and the fixture ChessUiTest are
/// tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kBarNode = "PuzzleBar";
        constexpr const char* kBarRow = "PuzzleBarRow";

        constexpr const char* kBarButtons[] = {"PuzzleBarRetry", "PuzzleBarNext", "PuzzleBarLeave"};

        /// The controls of the HUD that stand beside the bar's place, and that the bar must never meet (Opening, the place itself, is empty while a puzzle is on)
        constexpr const char* kHudNeighbours[] = {"Turn", "Resign", "Menu", "Reset", "Status"};

        /// White solves: Black's setup h8g8, then Ra1a7, the reply g8h8 and Rb2b8#
        constexpr StringView kMateInTwo = "ladder\t7k/8/8/8/8/8/1R6/R6K b - - 0 1\th8g8 a1a7 g8h8 b2b8\t1200\tmateIn2\n";

        /// Black solves: White's setup a2a3, then Rb8b1#
        constexpr StringView kBlackMate = "backrank\t1r4k1/5ppp/8/8/8/8/P4PPP/6K1 w - - 0 1\ta2a3 b8b1\t800\tmateIn1\n";

        /// From the start of a game, so that the position of the solver's e7e5 is one the book of openings names; the rest of the line is only legal
        constexpr StringView kBookLine = "book\trnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\te2e4 e7e5 g1f3 b8c6\t900\tmateIn1\n";

        PuzzleSet SetOf(std::initializer_list<StringView> rows) {
            String tsv = "id\tfen\tmoves\trating\tthemes\n";
            for (const StringView row : rows) {
                tsv.append(row.data(), row.size());
            }
            Vector<String> problems;
            PuzzleSet set = PuzzleSet::Parse(StringView(tsv.data(), tsv.size()), &problems);
            EXPECT_TRUE(problems.empty());
            return set;
        }

    }  // namespace

    TEST_F(ChessUiTest, ThePuzzleBarPassesTheSizeMatrix) {
        Draw();
        auto canvas = CanvasOf(kBarNode);
        auto hud = Hud();
        ASSERT_TRUE(canvas);
        ASSERT_TRUE(hud);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        hud->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = canvas->GetLayout();
        UICanvasLayout& hudLayout = hud->GetLayout();
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;

        String report;
        size_t reported = 0;
        const auto failures = ValidateOnMatrix(layout, options, ViewContext(), SceneViews());
        if (!failures.empty()) {
            reported++;
            report += DescribeMatrixFailures(failures).c_str();
        }
        // The bar is laid out beside the HUD in the same window, because the two are canvases of their own that nothing keeps apart but the places
        for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
            const UILayoutContext context = ContextOf(matrixCase, true);
            layout.Update(context);
            hudLayout.Update(context);
            String problems = BarProblems(layout, kBarButtons);
            for (const char* button : kBarButtons) {
                const UIElement* own = layout.root->FindByName(PoolString::Intern(button));
                for (const char* neighbour : kHudNeighbours) {
                    const UIElement* other = hudLayout.root->FindByName(PoolString::Intern(neighbour));
                    if (own && other && !own->rect.IsEmpty() && !other->rect.IsEmpty() && Touch(own->rect, other->rect, 0.0f)) {
                        problems += Format("{} meets the HUD's {}\n", button, neighbour);
                    }
                }
            }
            if (!problems.empty() && reported++ < kReportedCases) {
                report += Format("{}:\n{}", matrixCase.label.c_str(), problems.c_str());
            }
        }
        layout.Invalidate();
        hudLayout.Invalidate();
        EXPECT_TRUE(report.empty()) << report.c_str();
    }

    TEST_F(ChessUiTest, ThePuzzleBarIsAHiddenNonModalCanvasWithItsComponentFirstAndNamesOfItsOwn) {
        const auto canvas = CanvasOf(kBarNode);
        ASSERT_TRUE(canvas);
        EXPECT_FALSE(canvas->_visible);
        EXPECT_FALSE(canvas->_modal);
        EXPECT_EQ(canvas->_space, CanvasSpace::SceneView);
        EXPECT_TRUE(canvas->_useSafeArea);

        const SceneNode* node = nullptr;
        for (const auto& each : GetScene().GetRootNode().GetChildren()) {
            if (each->GetName() == PoolString::Intern(kBarNode)) {
                node = each.Get();
            }
        }
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->GetLayer(), 20) << "the layer of the menu and its sections, over the HUD";
        // The component comes before the canvas: the canvas is shown in the frame the puzzle begins
        const auto& components = node->GetComponents();
        const auto behind = node->GetComponent<ChessPuzzleBarComponent>();
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
        const UIElement& row = *canvas->GetRoot().FindByName(PoolString::Intern(kBarRow));
        EXPECT_FALSE(row.blocksInput) << "the row between the buttons is the board's";

        const struct {
            const char* name;
            const char* action;
        } buttons[] = {{"PuzzleBarRetry", "ChessPuzzleRetry"}, {"PuzzleBarNext", "ChessPuzzleNext"}, {"PuzzleBarLeave", "ChessPuzzleLeave"}};
        ASSERT_EQ(std::size(buttons), std::size(kBarButtons));
        for (const auto& [name, action] : buttons) {
            const UIElement* button = canvas->GetRoot().FindByName(PoolString::Intern(name));
            ASSERT_NE(button, nullptr) << name;
            const UIInputClicker* clicker = ClickerOf(*button);
            ASSERT_NE(clicker, nullptr) << name;
            EXPECT_EQ(clicker->action, PoolString::Intern(action)) << name;
        }
    }

    /// The fixture of the cases below: a puzzle is begun in code (the section is the menu's file) and the bar is what is clicked
    class ChessUiPuzzleBarTest : public ChessUiTest {
    protected:
        ChessGame& Game() const {
            ChessGame* game = ChessGame::GetActive();
            EXPECT_NE(game, nullptr);
            return *game;
        }

        IntrusivePtr<UICanvasComponent> Bar() const {
            return CanvasOf(kBarNode);
        }

        /// Gives the game the puzzles and begins the one at @p index; two frames, so the bar has been laid out
        void Begin(PuzzleSet puzzles, size_t index = 0) {
            Game().SetPuzzles(eastl::move(puzzles));
            ASSERT_TRUE(Game().BeginPuzzle(index));
            Settle();
        }

        Taken ClickBar(const char* name) {
            return ClickControl(ElementOf(kBarNode, name));
        }

        bool NothingOfTheMenuIsOpen() const {
            return !MenuList()->_visible && !PuzzleSection()->_visible;
        }
    };

    TEST_F(ChessUiPuzzleBarTest, TheBarIsHiddenOutsideAPuzzleAndShownExactlyWhileOneIsOn) {
        Game().SetPuzzles(SetOf({kMateInTwo}));
        _manager->UpdateAll();
        Draw();
        EXPECT_FALSE(Bar()->_visible) << "not at the start of a game";

        ASSERT_TRUE(Game().BeginPuzzle(0));
        _manager->UpdateAll();
        EXPECT_TRUE(Bar()->_visible);

        ASSERT_TRUE(Game().LeavePuzzle());
        _manager->UpdateAll();
        EXPECT_FALSE(Bar()->_visible) << "a stale bar is closed on the next update";

        ASSERT_TRUE(Game().BeginPuzzle(0));
        _manager->UpdateAll();
        ASSERT_TRUE(Bar()->_visible);
        Game().Reset();
        _manager->UpdateAll();
        EXPECT_FALSE(Bar()->_visible) << "a reset ends the puzzle";

        ASSERT_TRUE(Game().BeginPuzzle(0));
        _manager->UpdateAll();
        ASSERT_TRUE(Bar()->_visible);
        ASSERT_TRUE(FenSetsTheState("4k3/8/8/8/8/8/8/4K2R w - - 0 1"));
        _manager->UpdateAll();
        EXPECT_FALSE(Bar()->_visible) << "a position set from outside ends the puzzle";
    }

    TEST_F(ChessUiPuzzleBarTest, AStaleBarIsClosedByTheNextDrawToo) {
        Begin(SetOf({kMateInTwo}));
        ASSERT_TRUE(Bar()->_visible);

        Game().Reset();
        Draw();
        EXPECT_FALSE(Bar()->_visible);
    }

    TEST_F(ChessUiPuzzleBarTest, RetryBeginsTheSamePuzzleAgainWithNoMistakesAndNoMenuNeedsToBeOpen) {
        Begin(SetOf({kMateInTwo, kBlackMate}));
        ClickMove("b2", "b3");
        FrameOfGameTime();
        ASSERT_EQ(Game().GetPuzzle().GetMistakes(), 1u);
        ASSERT_TRUE(NothingOfTheMenuIsOpen()) << "the section was never opened";

        ASSERT_TRUE(ClickBar("PuzzleBarRetry").Both());

        ASSERT_TRUE(Game().IsPuzzleOn());
        EXPECT_EQ(Game().GetPuzzle().GetIndex(), 0u) << "the same puzzle";
        EXPECT_EQ(Game().GetPuzzle().GetMistakes(), 0u);
        EXPECT_EQ(Model().GetMoves().size(), 1u) << "back at the setup move";
        EXPECT_TRUE(Bar()->_visible);
        EXPECT_TRUE(NothingOfTheMenuIsOpen()) << "a click on the bar opens no menu";
    }

    TEST_F(ChessUiPuzzleBarTest, NextGoesToTheNextPuzzleAndWrapsToTheFirst) {
        Begin(SetOf({kMateInTwo, kBlackMate}));
        for (const size_t expected : {size_t{1}, size_t{0}, size_t{1}}) {
            SCOPED_TRACE(expected);
            ASSERT_TRUE(ClickBar("PuzzleBarNext").Both());
            ASSERT_TRUE(Game().IsPuzzleOn());
            EXPECT_EQ(Game().GetPuzzle().GetIndex(), expected);
            EXPECT_EQ(BlackIsAtTheBottom(), expected == 1u) << "the board follows the puzzle's solver";
            Settle();
            EXPECT_TRUE(Bar()->_visible) << "the bar goes on while a puzzle is on";
            EXPECT_TRUE(NothingOfTheMenuIsOpen());
        }
    }

    TEST_F(ChessUiPuzzleBarTest, LeaveEndsThePuzzleWithAPlainResetHidesTheBarAndKeepsTheOpponentAsPicked) {
        ControlPlays("black");
        const auto choice = [&] {
            const OpponentChoice& picked = Game().GetChoice();
            return Format("{} {} {} {} {}", picked.ModeId(), picked.ColorId(), picked.EngineId(), picked.LevelId(), ControlOpponentString("plays").c_str());
        };
        const String before = choice();
        Begin(SetOf({kMateInTwo}));
        ASSERT_TRUE(Bar()->_visible);

        ASSERT_TRUE(ClickBar("PuzzleBarLeave").Both());

        EXPECT_FALSE(Game().IsPuzzleOn());
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetPosition().ToFen(), Position::Start().ToFen()) << "a new game";
        EXPECT_EQ(choice(), before) << "the opponent is as it was picked";
        Draw();
        EXPECT_FALSE(Bar()->_visible);
        EXPECT_TRUE(NothingOfTheMenuIsOpen());
    }

    // ---- the HUD's Opening line, which the bar stands in the place of -----------------------------------------------------------------

    TEST_F(ChessUiPuzzleBarTest, TheOpeningLineIsEmptyWhileAPuzzleIsOnAndNamesThePositionAgainAfterLeaving) {
        Game().SetOpenings(ShippedOpeningBook());
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        Draw();
        const String named = ShownText(Label("Opening"));
        ASSERT_FALSE(named.empty()) << "the book knows 1.e4 e5";

        Begin(SetOf({kBookLine}));
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(ShownText(Label("Opening")), "") << "after the setup move";

        ClickMove("e7", "e5");
        FrameOfGameTime();
        ASSERT_EQ(Model().GetMoves().size(), 2u);
        Draw();
        EXPECT_EQ(ShownText(Label("Opening")), "") << "also in a position the book names";

        ASSERT_TRUE(ClickBar("PuzzleBarLeave").Both());
        Draw();
        EXPECT_EQ(ShownText(Label("Opening")), "") << "the start of a new game has no name";
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        Draw();
        EXPECT_EQ(ShownText(Label("Opening")), named) << "the name is the game's again";
    }

}  // namespace BECore::Chess::Tests
