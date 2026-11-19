#include <Chess/Components/ChessPuzzleSectionComponent.h>
#include <tests/ChessUiHarness.h>

/// The puzzle mode on the screen: what the HUD says while a puzzle is on (StatusText's puzzle words through ChessHudComponent), and the menu section "Puzzles". The puzzle's own rules are
/// PuzzleSessionTest's and ChessGamePuzzleTest's; the scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        /// White solves: Black's setup h8g8, then Ra1a7, the reply g8h8 and Rb2b8#
        constexpr StringView kMateInTwo = "ladder\t7k/8/8/8/8/8/1R6/R6K b - - 0 1\th8g8 a1a7 g8h8 b2b8\t1200\tmateIn2\n";

        /// Black solves: White's setup a2a3, then Rb8b1#
        constexpr StringView kBlackMate = "backrank\t1r4k1/5ppp/8/8/8/8/P4PPP/6K1 w - - 0 1\ta2a3 b8b1\t800\tmateIn1\n";

        constexpr int kFrameLimit = 40;  ///< frames of a quarter second: far more than the reply needs

        constexpr const char* kSectionNode = "PuzzleSection";
        constexpr const char* kSectionPlate = "PuzzleSectionPlate";

        /// Four digits in the number, the rating and the count of mistakes: the widths the lines are held to
        constexpr size_t kWorstCount = 9999;
        constexpr int32_t kWorstRating = 3999;

        /// Every word the section can show, from the code that makes them; the lines lay out independently of one another, so a round takes the word i of each list (the shorter wrap round)
        struct SectionWords {
            Vector<String> number;
            Vector<String> rating;
            Vector<String> state;
            Vector<String> next;

            [[nodiscard]] size_t Rounds() const {
                return std::max({number.size(), rating.size(), state.size(), next.size()});
            }

            void Publish(size_t round) const {
                UIVars& vars = CoreManager::GetUIVars();
                vars.Set("puzzleNumber"_intern, StringView(number[round % number.size()]));
                vars.Set("puzzleRating"_intern, StringView(rating[round % rating.size()]));
                vars.Set("puzzleState"_intern, StringView(state[round % state.size()]));
                vars.Set("puzzleNextWord"_intern, StringView(next[round % next.size()]));
            }
        };

        SectionWords AllSectionWords() {
            SectionWords words;
            words.number = {String(), StatusText::PuzzleNumber(kWorstCount - 1, kWorstCount)};
            words.rating = {String(), StatusText::PuzzleRating(kWorstRating)};
            for (const PuzzlePhase phase : EnumUtils<PuzzlePhase>::Values()) {
                for (const bool wrong : {false, true}) {
                    for (const size_t mistakes : {size_t{0}, size_t{1}, kWorstCount}) {
                        words.state.push_back(StatusText::PuzzleProgress(phase, wrong, mistakes));
                    }
                }
            }
            words.state.push_back(String());
            words.state.push_back(StatusText::NoPuzzles());
            words.next = {StatusText::PuzzleNextWord(false), StatusText::PuzzleNextWord(true)};
            return words;
        }

        /// The clickers of the three actions, live or grey (what the component writes, written here because the matrix lays the canvas out without its frame)
        void GreyTheButtons(UICanvasComponent& canvas, bool grey) {
            for (const char* name : {"PuzzleSectionNext", "PuzzleSectionRetry", "PuzzleSectionLeave"}) {
                UIElement* button = canvas.GetRoot().FindByName(PoolString::Intern(name));
                ASSERT_NE(button, nullptr) << name;
                button->GetComponent<UIInputClicker>()->disabled = grey;
            }
        }

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

    // ---- the section: its canvas ----------------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, ThePuzzleSectionPassesTheSizeMatrixWithEveryWord) {
        Draw();
        auto canvas = CanvasOf(kSectionNode);
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update

        UICanvasLayout& layout = canvas->GetLayout();
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        const SectionWords words = AllSectionWords();
        String report;
        size_t reported = 0;
        for (const bool grey : {false, true}) {
            GreyTheButtons(*canvas, grey);
            for (size_t round = 0; round < words.Rounds(); ++round) {
                words.Publish(round);
                const auto failures = ValidateOnMatrix(layout, options, ViewContext(), SceneViews());
                if (!failures.empty() && reported++ < kReportedCases) {
                    report += Format("round {}{}: {}", round, grey ? " grey" : "", DescribeMatrixFailures(failures).c_str());
                }
                // The validator does not see a Clip label's cut, nor a plate too low for what it holds
                OnEveryCaseOfTheMatrix(
                    layout,
                    [&](const String& label, const UICanvasLayout& laidOut) {
                        const String problems = PanelProblems(laidOut, kSectionPlate);
                        if (!problems.empty() && reported++ < kReportedCases) {
                            report += Format("round {}{}, {}:\n{}", round, grey ? " grey" : "", label.c_str(), problems.c_str());
                        }
                    },
                    true);
            }
        }
        EXPECT_TRUE(report.empty()) << report.c_str();
    }

    TEST_F(ChessUiTest, NoWordOfThePuzzleSectionIsCutInTheNarrowestWindowOfEachViewAndEveryButtonIsATouchTarget) {
        Draw();
        auto canvas = CanvasOf(kSectionNode);
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = canvas->GetLayout();
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        const SectionWords words = AllSectionWords();

        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            for (size_t round = 0; round < words.Rounds(); ++round) {
                words.Publish(round);
                LayOutInTheNarrowestWindow(layout, shape);
                const float scale = layout.GetResolved().scale;
                const String problems = PanelProblems(layout, kSectionPlate);
                EXPECT_TRUE(problems.empty()) << problems.c_str();
                for (const char* name : {"PuzzleSectionNext", "PuzzleSectionRetry", "PuzzleSectionLeave", "PuzzleSectionBack"}) {
                    const UIElement& button = ElementOf(kSectionNode, name);
                    EXPECT_NEAR(button.rect.h, kSegmentHeight, kPixelTolerance) << name;
                    EXPECT_GE(std::min(button.rect.w, button.rect.h) * scale, minTouch - kPixelTolerance) << name << " is a touch target on the smallest window";
                    const UIContentFit fit = button.CheckContent(button.rect.w - 2.0f * kButtonAir, button.rect.h, scale);
                    EXPECT_TRUE(fit.hasContent && fit.fits) << name << " '" << ShownText(button).c_str() << "' in " << button.rect.w << " units";
                }
            }
        }
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, ThePuzzleSectionIsAHiddenModalCanvasOfTheMenuWithItsComponentFirstAndNamesOfItsOwn) {
        const auto canvas = CanvasOf(kSectionNode);
        ASSERT_TRUE(canvas);
        EXPECT_FALSE(canvas->_visible);
        EXPECT_TRUE(canvas->_modal);
        EXPECT_EQ(canvas->_space, CanvasSpace::SceneView);
        EXPECT_TRUE(canvas->_useSafeArea);

        const SceneNode* node = nullptr;
        for (const auto& each : GetScene().GetRootNode().GetChildren()) {
            if (each->GetName() == PoolString::Intern(kSectionNode)) {
                node = each.Get();
            }
        }
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->GetLayer(), 20) << "the layer of the menu and its sections, over the HUD";
        // The component comes before the canvas: its words and greyed looks are written in the frame the section is shown
        const auto& components = node->GetComponents();
        const auto behind = node->GetComponent<ChessPuzzleSectionComponent>();
        ASSERT_TRUE(behind);
        ASSERT_EQ(components.size(), 2u);
        EXPECT_EQ(components[0].Get(), static_cast<IComponent*>(behind.Get()));
        EXPECT_EQ(components[1].Get(), static_cast<IComponent*>(canvas.Get()));

        Vector<PoolString> names;
        CollectNames(canvas->GetRoot(), names);
        ASSERT_FALSE(names.empty());
        for (size_t i = 0; i < names.size(); ++i) {
            EXPECT_TRUE(names[i].ToStringView().starts_with(kSectionNode)) << names[i].CStr() << ": elements are found by name in every canvas";
            EXPECT_EQ(eastl::count(names.begin(), names.end(), names[i]), 1) << names[i].CStr() << " is twice in the section";
        }
        for (const auto& other : GetScene().GetRootNode().GetChildren()) {
            const auto otherCanvas = other->GetComponent<UICanvasComponent>();
            if (!otherCanvas || other->GetName() == PoolString::Intern(kSectionNode)) {
                continue;
            }
            for (const PoolString& name : names) {
                EXPECT_EQ(otherCanvas->GetRoot().FindByName(name), nullptr) << name.CStr() << " is also in " << other->GetName().CStr();
            }
        }
        const struct {
            const char* name;
            const char* action;
        } buttons[] = {{"PuzzleSectionNext", "ChessPuzzleNext"},
                       {"PuzzleSectionRetry", "ChessPuzzleRetry"},
                       {"PuzzleSectionLeave", "ChessPuzzleLeave"},
                       {"PuzzleSectionBack", "ChessOpenMenu"},
                       {"PuzzleSectionDim", "ChessCloseMenu"}};
        for (const auto& [name, action] : buttons) {
            EXPECT_EQ(ClickerOf(ElementOf(kSectionNode, name))->action, PoolString::Intern(action)) << name;
        }
        EXPECT_EQ(canvas->GetRoot().children.front()->name, "PuzzleSectionDim"_intern) << "the first child, so a click on the plate never reaches it";
    }

    // ---- the section: clicks through the real scene ---------------------------------------------------------------------------------

    TEST_F(ChessUiTest, TheListOfTheMenuOpensThePuzzleSectionAndBackAndEscapeLeaveIt) {
        ASSERT_TRUE(ClickButton(kMenuColor));
        const UIElement& button = MenuButton("MenuSectionPuzzles");
        EXPECT_EQ(ShownText(button), "Puzzles");
        EXPECT_EQ(ClickerOf(button)->value, "puzzles"_intern);

        ASSERT_TRUE(ClickMenuList("MenuSectionPuzzles").Both());
        EXPECT_TRUE(PuzzleSection()->_visible);
        EXPECT_FALSE(MenuList()->_visible) << "the section replaces the list";
        ASSERT_TRUE(ClickPuzzleSection("PuzzleSectionBack").Both());
        EXPECT_FALSE(PuzzleSection()->_visible);
        EXPECT_TRUE(MenuList()->_visible) << "Back returns to the list";

        ASSERT_TRUE(ClickMenuList("MenuSectionPuzzles").Both());
        SDLEvents::KeyDownEvent::Emit(SDLK_ESCAPE, SDL_KMOD_NONE, false);
        EXPECT_FALSE(PuzzleSection()->_visible);
        EXPECT_FALSE(MenuList()->_visible);
    }

    TEST_F(ChessUiTest, StartBeginsTheFirstPuzzleClosesTheMenuAndTurnsTheBoardForABlackSolver) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kBlackMate, kMateInTwo}));
        ASSERT_TRUE(OpenThePuzzleSection());
        Draw();
        EXPECT_FALSE(ClickerOf(ElementOf(kSectionNode, "PuzzleSectionNext"))->disabled);
        EXPECT_TRUE(ClickerOf(ElementOf(kSectionNode, "PuzzleSectionRetry"))->disabled) << "no puzzle to retry";
        EXPECT_TRUE(ClickerOf(ElementOf(kSectionNode, "PuzzleSectionLeave"))->disabled) << "no puzzle to leave";
        EXPECT_EQ(Var("puzzleNextWord"), "Start");

        ASSERT_TRUE(ClickPuzzleSection("PuzzleSectionNext").Both());
        ASSERT_TRUE(game.IsPuzzleOn());
        EXPECT_EQ(game.GetPuzzle().GetIndex(), 0u);
        EXPECT_FALSE(PuzzleSection()->_visible);
        EXPECT_FALSE(MenuList()->_visible) << "the board is in front";
        EXPECT_TRUE(BlackIsAtTheBottom()) << "Black solves the first puzzle";
        EXPECT_EQ(Var("turn"), "Your move");

        ASSERT_TRUE(OpenThePuzzleSection());
        Draw();
        EXPECT_FALSE(ClickerOf(ElementOf(kSectionNode, "PuzzleSectionRetry"))->disabled);
        EXPECT_FALSE(ClickerOf(ElementOf(kSectionNode, "PuzzleSectionLeave"))->disabled);
        EXPECT_EQ(Var("puzzleNextWord"), "Next");
    }

    TEST_F(ChessUiTest, NextGoesToTheNextPuzzleAndWrapsToTheFirst) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kBlackMate, kMateInTwo}));
        for (const size_t expected : {size_t{0}, size_t{1}, size_t{0}}) {
            SCOPED_TRACE(expected);
            ASSERT_TRUE(OpenThePuzzleSection());
            ASSERT_TRUE(ClickPuzzleSection("PuzzleSectionNext").Both());
            ASSERT_TRUE(game.IsPuzzleOn());
            EXPECT_EQ(game.GetPuzzle().GetIndex(), expected);
            EXPECT_EQ(BlackIsAtTheBottom(), expected == 0u) << "the board follows the puzzle's solver";
        }
    }

    TEST_F(ChessUiTest, RetryBeginsTheSamePuzzleAgainWithNoMistakes) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo, kBlackMate}));
        ASSERT_TRUE(game.BeginPuzzle(0));
        Settle();
        ClickMove("b2", "b3");
        FrameOfGameTime();
        ASSERT_EQ(game.GetPuzzle().GetMistakes(), 1u);
        EXPECT_EQ(Var("puzzleState"), "Wrong move, 1 mistake");

        ASSERT_TRUE(OpenThePuzzleSection());
        ASSERT_TRUE(ClickPuzzleSection("PuzzleSectionRetry").Both());
        ASSERT_TRUE(game.IsPuzzleOn());
        EXPECT_EQ(game.GetPuzzle().GetIndex(), 0u) << "the same puzzle";
        EXPECT_EQ(game.GetPuzzle().GetMistakes(), 0u);
        EXPECT_EQ(Model().GetMoves().size(), 1u) << "back at the setup move";
        EXPECT_FALSE(PuzzleSection()->_visible);
        EXPECT_EQ(Var("puzzleState"), "Your move");
    }

    TEST_F(ChessUiTest, LeaveEndsThePuzzleWithAPlainResetAndTheOpponentAsPicked) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo}));
        ControlPlays("black");
        const auto choice = [&] {
            const OpponentChoice& picked = game.GetChoice();
            return Format("{} {} {} {} {}", picked.ModeId(), picked.ColorId(), picked.EngineId(), picked.LevelId(), ControlOpponentString("plays").c_str());
        };
        const String before = choice();
        ASSERT_TRUE(game.BeginPuzzle(0));
        Settle();

        ASSERT_TRUE(OpenThePuzzleSection());
        ASSERT_TRUE(ClickPuzzleSection("PuzzleSectionLeave").Both());
        EXPECT_FALSE(game.IsPuzzleOn());
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetPosition().ToFen(), Position::Start().ToFen()) << "a new game";
        EXPECT_EQ(choice(), before) << "the opponent is as it was picked";
        EXPECT_FALSE(PuzzleSection()->_visible);
        EXPECT_EQ(Var("turn"), "White to move");
    }

    TEST_F(ChessUiTest, TheThreeActionsAreAnsweredWithNoMenuOpen) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo, kBlackMate}));
        ASSERT_FALSE(MenuList()->_visible);
        ASSERT_FALSE(PuzzleSection()->_visible);

        Press("ChessPuzzleNext");
        ASSERT_TRUE(game.IsPuzzleOn()) << "no puzzle was on: the first begins";
        EXPECT_EQ(game.GetPuzzle().GetIndex(), 0u);
        Press("ChessPuzzleNext");
        EXPECT_EQ(game.GetPuzzle().GetIndex(), 1u);
        Press("ChessPuzzleRetry");
        ASSERT_TRUE(game.IsPuzzleOn());
        EXPECT_EQ(game.GetPuzzle().GetIndex(), 1u);
        Press("ChessPuzzleLeave");
        EXPECT_FALSE(game.IsPuzzleOn());

        const uint64_t version = Model().GetVersion();
        Press("ChessPuzzleRetry");
        Press("ChessPuzzleLeave");
        EXPECT_EQ(Model().GetVersion(), version) << "with no puzzle on there is nothing to retry or leave";
    }

    TEST_F(ChessUiTest, AnActionOfTheSectionClosesAnOpenMenuOnlyWhenItWorked) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(PuzzleSet());
        Press("ChessOpenMenu");
        ASSERT_TRUE(MenuList()->_visible);
        Press("ChessPuzzleNext");
        EXPECT_TRUE(MenuList()->_visible) << "an empty set begins nothing, and the menu stays";

        game.SetPuzzles(SetOf({kMateInTwo}));
        Press("ChessPuzzleNext");
        EXPECT_TRUE(game.IsPuzzleOn());
        EXPECT_FALSE(MenuList()->_visible);
    }

    TEST_F(ChessUiTest, WithNoPuzzlesStartIsGreyAndTheStateLineSaysSo) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(PuzzleSet());
        ASSERT_TRUE(OpenThePuzzleSection());
        Draw();
        EXPECT_TRUE(ClickerOf(ElementOf(kSectionNode, "PuzzleSectionNext"))->disabled);
        EXPECT_EQ(Var("puzzleState"), "No puzzles");
        EXPECT_EQ(Var("puzzleNumber"), "");
        EXPECT_EQ(Var("puzzleRating"), "");

        const uint64_t version = Model().GetVersion();
        ClickPuzzleSection("PuzzleSectionNext");
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_FALSE(game.IsPuzzleOn());
        EXPECT_TRUE(PuzzleSection()->_visible) << "nothing was begun, so the menu stays";

        game.SetPuzzles(SetOf({kMateInTwo}));
        Draw();
        EXPECT_FALSE(ClickerOf(ElementOf(kSectionNode, "PuzzleSectionNext"))->disabled) << "a set that arrives lights it";
        EXPECT_EQ(Var("puzzleState"), "");
    }

    TEST_F(ChessUiTest, APuzzleEndedByAResetOrAPositionGreysRetryAndLeaveAtTheNextDrawWithNoFrameBetween) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo}));
        ASSERT_TRUE(game.BeginPuzzle(0));
        ASSERT_TRUE(OpenThePuzzleSection());
        const auto greyed = [&](const char* name) {
            Draw();
            return ClickerOf(ElementOf(kSectionNode, name))->disabled;
        };
        ASSERT_FALSE(greyed("PuzzleSectionRetry"));
        ASSERT_FALSE(greyed("PuzzleSectionLeave"));

        Model().Reset();
        EXPECT_TRUE(greyed("PuzzleSectionRetry"));
        EXPECT_TRUE(greyed("PuzzleSectionLeave"));
        EXPECT_EQ(Var("puzzleState"), "");
        EXPECT_EQ(Var("puzzleNextWord"), "Start");

        ASSERT_TRUE(game.BeginPuzzle(0));
        ASSERT_FALSE(greyed("PuzzleSectionRetry"));
        ASSERT_TRUE(FenSetsTheState("4k3/8/8/8/8/8/8/4K2R w - - 0 1"));
        EXPECT_TRUE(greyed("PuzzleSectionRetry"));
        EXPECT_TRUE(greyed("PuzzleSectionLeave"));
    }

    TEST_F(ChessUiTest, TheLinesFollowTheSessionAndASteadyFrameWritesNoVariable) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo, kBlackMate}));
        ASSERT_TRUE(game.BeginPuzzle(0));
        ASSERT_TRUE(OpenThePuzzleSection());
        Draw();
        EXPECT_EQ(Var("puzzleNumber"), "Puzzle 1 of 2");
        EXPECT_EQ(Var("puzzleRating"), "Rating 1200");
        EXPECT_EQ(Var("puzzleState"), "Your move");

        // The section is modal: the moves are made through the model, which is what a click on the board ends in
        ASSERT_TRUE(Play(Model(), "b2b3"));
        FrameOfGameTime();
        EXPECT_EQ(Var("puzzleState"), "Wrong move, 1 mistake");

        ASSERT_TRUE(Play(Model(), "a1a7"));
        FrameOfGameTime();
        EXPECT_EQ(Var("puzzleState"), "Replying, 1 mistake");
        for (int frame = 0; frame < kFrameLimit && Model().GetMoves().size() < 3; ++frame) {
            FrameOfGameTime();
        }
        ASSERT_EQ(Model().GetMoves().size(), 3u);
        EXPECT_EQ(Var("puzzleState"), "Your move, 1 mistake");

        ASSERT_TRUE(Play(Model(), "b2b8"));
        FrameOfGameTime();
        EXPECT_EQ(Var("puzzleState"), "Solved, 1 mistake");

        Draw();
        const uint64_t version = CoreManager::GetUIVars().GetVersion();
        for (int frame = 0; frame < 3; ++frame) {
            _manager->UpdateAll();
            Draw();
        }
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version);
    }

    // ---- the HUD while a puzzle is on -------------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, AStartedPuzzleSaysYourMoveAndNothingUnderIt) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo}));
        ASSERT_TRUE(game.BeginPuzzle(0));
        Settle();

        EXPECT_EQ(Var("turn"), "Your move");
        EXPECT_EQ(Var("status"), "");
    }

    TEST_F(ChessUiTest, ARightMoveSaysTheReplyIsDueThenYourMoveAgainAfterTheReply) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo}));
        ASSERT_TRUE(game.BeginPuzzle(0));
        Settle();

        ClickMove("a1", "a7");
        FrameOfGameTime();
        ASSERT_EQ(Model().GetMoves().size(), 2u);
        EXPECT_EQ(Var("turn"), "Replying");
        EXPECT_EQ(Var("status"), "");

        for (int frame = 0; frame < kFrameLimit && Model().GetMoves().size() < 3; ++frame) {
            FrameOfGameTime();
        }
        ASSERT_EQ(Model().GetMoves().size(), 3u) << "the reply was played";
        EXPECT_EQ(Var("turn"), "Your move");
    }

    TEST_F(ChessUiTest, AWrongMoveSaysSoUntilTheNextMoveAndTheBoardIsTheOneBeforeIt) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo}));
        ASSERT_TRUE(game.BeginPuzzle(0));
        Settle();

        ClickMove("b2", "b3");
        FrameOfGameTime();
        ASSERT_EQ(Model().GetMoves().size(), 1u) << "taken back";
        EXPECT_EQ(Var("status"), "Wrong move");
        EXPECT_EQ(Var("turn"), "Your move");
        EXPECT_EQ(game.GetPuzzle().GetMistakes(), 1u);

        ClickMove("a1", "a7");
        FrameOfGameTime();
        EXPECT_EQ(Var("status"), "") << "the next move of the person takes the word away";
    }

    TEST_F(ChessUiTest, TheSolvingMoveSaysSolvedAndTheGamesOwnCheckmateStandsUnderIt) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo}));
        ASSERT_TRUE(game.BeginPuzzle(0));
        Settle();
        ClickMove("a1", "a7");
        for (int frame = 0; frame < kFrameLimit && Model().GetMoves().size() < 3; ++frame) {
            FrameOfGameTime();
        }
        ASSERT_EQ(Model().GetMoves().size(), 3u);

        ClickMove("b2", "b8");
        FrameOfGameTime();

        ASSERT_TRUE(Model().IsGameOver());
        EXPECT_EQ(Var("turn"), "Solved");
        EXPECT_EQ(Var("status"), "Checkmate");
    }

    TEST_F(ChessUiTest, AViewOverAPuzzleShowsTheViewsLineAndTheTurnStaysThePuzzles) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo}));
        ASSERT_TRUE(game.BeginPuzzle(0));
        Settle();

        ASSERT_TRUE(Model().ShowMoves(0));
        EXPECT_EQ(Var("status"), "At the start - tap board");
        EXPECT_EQ(Var("turn"), "Your move");

        ASSERT_TRUE(Model().EndViewing());
        EXPECT_EQ(Var("status"), "");
    }

    TEST_F(ChessUiTest, TheEndOfAPuzzleGivesTheGamesOwnWordsAtOnce) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(SetOf({kMateInTwo}));
        ASSERT_TRUE(game.BeginPuzzle(0));
        Settle();
        ASSERT_EQ(Var("turn"), "Your move");

        game.Reset();
        EXPECT_EQ(Var("turn"), "White to move");

        ASSERT_TRUE(game.BeginPuzzle(0));
        ASSERT_EQ(Var("turn"), "Your move");
        ASSERT_TRUE(game.LeavePuzzle());
        EXPECT_EQ(Var("turn"), "White to move");
    }

}  // namespace BECore::Chess::Tests
