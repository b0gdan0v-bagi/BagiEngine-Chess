#include <Chess/Components/ChessReviewSectionComponent.h>
#include <Chess/Components/MoveRows.h>
#include <Chess/Engine/OpponentText.h>
#include <Chess/Engine/ReviewText.h>
#include <tests/ChessUiHarness.h>

/// The review of a finished game on the screen: the menu section "Review" (ChessReviewSectionComponent over the analyst's GameReviewer). The review's own rules are GameReviewerTest's and
/// PositionAnalystTest's, the marks in the move list are ChessUiMovePanelTest's; the scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kSectionNode = "ReviewSection";
        constexpr const char* kSectionPlate = "ReviewSectionPlate";
        constexpr const char* kStart = "ReviewSectionStart";
        constexpr const char* kCancel = "ReviewSectionCancel";

        /// Four digits in both counts: the widths the line is held to
        constexpr size_t kWorstCount = 9999;

        /// White solves: Black's setup h8g8, then Ra1a7, the reply g8h8 and Rb2b8#
        constexpr StringView kMateInTwo = "ladder\t7k/8/8/8/8/8/1R6/R6K b - - 0 1\th8g8 a1a7 g8h8 b2b8\t1200\tmateIn2\n";

        constexpr int kFrameLimit = 40;  ///< frames of a quarter second: far more than a puzzle's reply needs

        /// Every sentence of the line the section can show, from the code that makes them, each once
        Vector<String> AllStatusWords() {
            Vector<String> words{String()};
            const ReviewProgress worst{kWorstCount, kWorstCount};
            for (const ReviewState state : EnumUtils<ReviewState>::Values()) {
                for (const Optional<ReviewRefusal> refusal : {Optional<ReviewRefusal>(), Optional<ReviewRefusal>(ReviewRefusal::NotFinished), Optional<ReviewRefusal>(ReviewRefusal::NoMoves)}) {
                    for (const bool puzzleOn : {false, true}) {
                        for (const ReviewProgress progress : {ReviewProgress{}, worst}) {
                            String word = ReviewText::Status(state, progress, refusal, puzzleOn);
                            if (eastl::find(words.begin(), words.end(), word) == words.end()) {
                                words.push_back(std::move(word));
                            }
                        }
                    }
                }
            }
            return words;
        }

        /// The clickers of the two actions, live or grey (what the component writes, written here because the matrix lays the canvas out without its frame)
        void GreyTheButtons(UICanvasComponent& canvas, bool grey) {
            for (const char* name : {kStart, kCancel}) {
                UIElement* button = canvas.GetRoot().FindByName(PoolString::Intern(name));
                ASSERT_NE(button, nullptr) << name;
                button->GetComponent<UIInputClicker>()->disabled = grey;
            }
        }

        PuzzleSet MateInTwoSet() {
            String tsv = "id\tfen\tmoves\trating\tthemes\n";
            tsv.append(kMateInTwo.data(), kMateInTwo.size());
            Vector<String> problems;
            PuzzleSet set = PuzzleSet::Parse(StringView(tsv.data(), tsv.size()), &problems);
            EXPECT_TRUE(problems.empty());
            return set;
        }

    }  // namespace

    // ---- the section: its canvas ----------------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, TheReviewSectionPassesTheSizeMatrixWithEveryWord) {
        Draw();
        auto canvas = CanvasOf(kSectionNode);
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update

        UICanvasLayout& layout = canvas->GetLayout();
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        const Vector<String> words = AllStatusWords();
        for (const String& word : words) {
            EXPECT_LE(word.size(), ReviewText::kMaxStatusLength) << word.c_str();
        }
        String report;
        size_t reported = 0;
        for (const bool grey : {false, true}) {
            GreyTheButtons(*canvas, grey);
            for (size_t round = 0; round < words.size(); ++round) {
                CoreManager::GetUIVars().Set("reviewStatus"_intern, StringView(words[round]));
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

    TEST_F(ChessUiTest, NoWordOfTheReviewSectionIsCutInTheNarrowestWindowOfEachViewAndEveryButtonIsATouchTarget) {
        Draw();
        auto canvas = CanvasOf(kSectionNode);
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = canvas->GetLayout();
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        const Vector<String> words = AllStatusWords();

        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            for (const String& word : words) {
                CoreManager::GetUIVars().Set("reviewStatus"_intern, StringView(word));
                LayOutInTheNarrowestWindow(layout, shape);
                const float scale = layout.GetResolved().scale;
                const String problems = PanelProblems(layout, kSectionPlate);
                EXPECT_TRUE(problems.empty()) << problems.c_str();
                for (const char* name : {kStart, kCancel, "ReviewSectionBack"}) {
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

    TEST_F(ChessUiTest, TheReviewSectionIsAHiddenModalCanvasOfTheMenuWithItsComponentFirstAndNamesOfItsOwn) {
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
        const auto behind = node->GetComponent<ChessReviewSectionComponent>();
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
        } buttons[] = {{kStart, "ChessReviewStart"}, {kCancel, "ChessReviewCancel"}, {"ReviewSectionBack", "ChessOpenMenu"}, {"ReviewSectionDim", "ChessCloseMenu"}};
        for (const auto& [name, action] : buttons) {
            EXPECT_EQ(ClickerOf(ElementOf(kSectionNode, name))->action, PoolString::Intern(action)) << name;
        }
        EXPECT_EQ(canvas->GetRoot().children.front()->name, "ReviewSectionDim"_intern) << "the first child, so a click on the plate never reaches it";
        EXPECT_FALSE(ClickerOf(ElementOf(kSectionNode, kStart))->disabled) << "the XML starts with Start live";
        EXPECT_TRUE(ClickerOf(ElementOf(kSectionNode, kCancel))->disabled) << "and with Cancel grey: the game starts with no review";
    }

    // ---- the section: clicks through the real scene ---------------------------------------------------------------------------------

    TEST_F(ChessUiTest, TheListOfTheMenuOpensTheReviewSectionAndBackAndEscapeLeaveIt) {
        ASSERT_TRUE(ClickButton(kMenuColor));
        const UIElement& button = MenuButton("MenuSectionReview");
        EXPECT_EQ(ShownText(button), "Review");
        EXPECT_EQ(ClickerOf(button)->value, "review"_intern);

        ASSERT_TRUE(ClickMenuList("MenuSectionReview").Both());
        EXPECT_TRUE(ReviewSection()->_visible);
        EXPECT_FALSE(MenuList()->_visible) << "the section replaces the list";
        ASSERT_TRUE(ClickReviewSection("ReviewSectionBack").Both());
        EXPECT_FALSE(ReviewSection()->_visible);
        EXPECT_TRUE(MenuList()->_visible) << "Back returns to the list";

        ASSERT_TRUE(ClickMenuList("MenuSectionReview").Both());
        SDLEvents::KeyDownEvent::Emit(SDLK_ESCAPE, SDL_KMOD_NONE, false);
        EXPECT_FALSE(ReviewSection()->_visible);
        EXPECT_FALSE(MenuList()->_visible);
    }

    TEST_F(ChessUiTest, StartIsGreyWhileTheGameIsNotOverHasNoMovesOrAPuzzleIsOnAndLiveOnAFinishedGame) {
        ASSERT_TRUE(OpenTheReviewSection());
        const auto greyed = [&](const char* name) {
            Draw();
            return ClickerOf(ElementOf(kSectionNode, name))->disabled;
        };
        EXPECT_TRUE(greyed(kStart));
        EXPECT_TRUE(greyed(kCancel)) << "no review to cancel";
        EXPECT_EQ(Var("reviewStatus"), "The game is not over");

        ASSERT_TRUE(Model().Resign(PieceColor::White).has_value());
        ASSERT_TRUE(Model().IsGameOver());
        EXPECT_TRUE(greyed(kStart)) << "a game with no moves has nothing to review";
        EXPECT_EQ(Var("reviewStatus"), "No moves to review");

        Model().Reset();
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(MateInTwoSet());
        ASSERT_TRUE(game.BeginPuzzle(0));
        EXPECT_TRUE(greyed(kStart));
        EXPECT_EQ(Var("reviewStatus"), "Not during a puzzle");

        ASSERT_TRUE(game.LeavePuzzle());
        ASSERT_TRUE(PlayFoolsMate());
        EXPECT_FALSE(greyed(kStart));
        EXPECT_EQ(Var("reviewStatus"), "Not started");
    }

    TEST_F(ChessUiTest, APuzzleThatIsSolvedStillRefusesTheReviewOfItsFinishedGame) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(MateInTwoSet());
        ASSERT_TRUE(game.BeginPuzzle(0));
        Settle();
        ASSERT_TRUE(Play(Model(), "a1a7"));
        for (int frame = 0; frame < kFrameLimit && Model().GetMoves().size() < 3; ++frame) {
            FrameOfGameTime();
        }
        ASSERT_EQ(Model().GetMoves().size(), 3u);
        ASSERT_TRUE(Play(Model(), "b2b8"));
        FrameOfGameTime();
        ASSERT_TRUE(Model().IsGameOver());
        ASSERT_TRUE(game.IsPuzzleOn());

        ASSERT_TRUE(OpenTheReviewSection());
        Draw();
        EXPECT_TRUE(ClickerOf(ElementOf(kSectionNode, kStart))->disabled);
        EXPECT_EQ(Var("reviewStatus"), "Not during a puzzle");
        Press("ChessReviewStart");
        EXPECT_EQ(Analyst().GetReviewState(Model()), ReviewState::Idle) << "the action asks the rule again";
    }

    TEST_F(ChessUiTest, StartRunsTheReviewAndTheStatusLineFollowsItWhileTheMenuStaysOpen) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<AnalysingEngine>();
        engine->Gate(true);
        CountingAnalysisFactory factory{engine};
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(OpenTheReviewSection());
        const auto greyed = [&](const char* name) {
            Draw();
            return ClickerOf(ElementOf(kSectionNode, name))->disabled;
        };
        ASSERT_FALSE(greyed(kStart));
        EXPECT_EQ(Var("reviewStatus"), "Not started");

        ASSERT_TRUE(ClickReviewSection(kStart).Both());
        EXPECT_TRUE(ReviewSection()->_visible) << "the person watches the progress here";
        ASSERT_EQ(Analyst().GetReviewState(Model()), ReviewState::Running);
        EXPECT_EQ(Var("reviewStatus"), "Reviewing 0 of 4") << "the engine is held at its gate; the position after the mate is not searched";
        EXPECT_TRUE(greyed(kStart)) << "a running review is not started again";
        EXPECT_FALSE(greyed(kCancel));

        engine->Open();
        bool sawProgress = false;
        ASSERT_TRUE(FramesUntil([&] {
            const String line = Var("reviewStatus");
            sawProgress = sawProgress || StringView(line).starts_with("Reviewing ");
            return Analyst().GetReviewState(Model()) == ReviewState::Done;
        }));
        EXPECT_TRUE(sawProgress);
        EXPECT_EQ(Var("reviewStatus"), "Review done");
        EXPECT_TRUE(greyed(kStart)) << "a done review is not started again";
        EXPECT_FALSE(greyed(kCancel));
    }

    TEST_F(ChessUiTest, CancelDropsARunningReviewAndStartIsLiveAgain) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<AnalysingEngine>();
        engine->Gate(true);
        CountingAnalysisFactory factory{engine};
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(OpenTheReviewSection());
        ASSERT_TRUE(ClickReviewSection(kStart).Both());
        ASSERT_EQ(Analyst().GetReviewState(Model()), ReviewState::Running);

        ASSERT_TRUE(ClickReviewSection(kCancel).Both());
        EXPECT_EQ(Analyst().GetReviewState(Model()), ReviewState::Idle);
        EXPECT_EQ(Var("reviewStatus"), "Not started");
        EXPECT_TRUE(ClickerOf(ElementOf(kSectionNode, kCancel))->disabled);
        EXPECT_FALSE(ClickerOf(ElementOf(kSectionNode, kStart))->disabled);
        EXPECT_TRUE(ReviewSection()->_visible);

        engine->Open();
        ASSERT_TRUE(ClickReviewSection(kStart).Both());
        ASSERT_TRUE(WaitForReview());
    }

    TEST_F(ChessUiTest, CancelDropsADoneReviewWithItsMarks) {
        SKIP_WITHOUT_WORKER_THREADS();
        ReviewOnTheOwnEngine();
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(OpenTheReviewSection());
        ASSERT_TRUE(ClickReviewSection(kStart).Both());
        ASSERT_TRUE(WaitForReview());
        ASSERT_EQ(Var("reviewStatus"), "Review done");
        ASSERT_EQ(CurrentMovePanelRows(&Model(), &Analyst())[1].white.text, "g4??");

        ASSERT_TRUE(ClickReviewSection(kCancel).Both());

        EXPECT_EQ(Analyst().GetReviewState(Model()), ReviewState::Idle);
        EXPECT_EQ(CurrentMovePanelRows(&Model(), &Analyst())[1].white.text, "g4") << "the marks go with the review";
        EXPECT_EQ(Var("reviewStatus"), "Not started");
        EXPECT_TRUE(ClickerOf(ElementOf(kSectionNode, kCancel))->disabled);
        EXPECT_FALSE(ClickerOf(ElementOf(kSectionNode, kStart))->disabled);
    }

    TEST_F(ChessUiTest, TheTwoActionsAreAnsweredWithNoMenuOpen) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<AnalysingEngine>();
        engine->Gate(true);
        CountingAnalysisFactory factory{engine};
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_FALSE(MenuList()->_visible);
        ASSERT_FALSE(ReviewSection()->_visible);

        Press("ChessReviewStart");
        EXPECT_EQ(Analyst().GetReviewState(Model()), ReviewState::Running) << "the action is answered with no menu open";
        Press("ChessReviewCancel");
        EXPECT_EQ(Analyst().GetReviewState(Model()), ReviewState::Idle);
        engine->Open();
    }

    TEST_F(ChessUiTest, StartOnAGameThatIsNotOverDoesNothing) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        const uint64_t revision = Analyst().GetReviewRevision();
        Press("ChessReviewStart");
        Press("ChessReviewCancel");
        EXPECT_EQ(Analyst().GetReviewState(Model()), ReviewState::Idle);
        EXPECT_EQ(Analyst().GetReviewRevision(), revision);
    }

    TEST_F(ChessUiTest, ATakebackOrAResetUnderADoneReviewTakesItAwayAtTheNextDrawAndAReplayedGameStartsOver) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<AnalysingEngine>();
        CountingAnalysisFactory factory{engine};
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(OpenTheReviewSection());
        ASSERT_TRUE(ClickReviewSection(kStart).Both());
        ASSERT_TRUE(WaitForReview());
        ASSERT_EQ(Var("reviewStatus"), "Review done");

        ASSERT_EQ(Model().TakeBack(1), 1u);
        EXPECT_EQ(Var("reviewStatus"), "The game is not over") << "read at the next draw, with no frame between";
        EXPECT_TRUE(ClickerOf(ElementOf(kSectionNode, kStart))->disabled);
        EXPECT_TRUE(ClickerOf(ElementOf(kSectionNode, kCancel))->disabled);

        FrameOfGameTime(0.0);  // the analyst's Update() drops the review for good; before it the same game played again would still be the held one
        ASSERT_TRUE(Play(Model(), "d8h4"));
        EXPECT_EQ(Var("reviewStatus"), "Not started");
        EXPECT_FALSE(ClickerOf(ElementOf(kSectionNode, kStart))->disabled);
        EXPECT_TRUE(ClickerOf(ElementOf(kSectionNode, kCancel))->disabled);

        ASSERT_TRUE(ClickReviewSection(kStart).Both());
        ASSERT_TRUE(WaitForReview());
        ASSERT_EQ(Var("reviewStatus"), "Review done");
        Model().Reset();
        EXPECT_EQ(Var("reviewStatus"), "The game is not over");
        EXPECT_TRUE(ClickerOf(ElementOf(kSectionNode, kCancel))->disabled);
    }

    TEST_F(ChessUiTest, AFailedReviewSaysSoAndStartIsLiveAgain) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingAnalysisFactory factory(EngineError{EngineErrorKind::LaunchFailed, "no engine for the review"});
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(OpenTheReviewSection());
        ASSERT_TRUE(ClickReviewSection(kStart).Both());
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetReviewState(Model()) == ReviewState::Failed; }));

        EXPECT_EQ(Var("reviewStatus"), "Review failed");
        EXPECT_FALSE(ClickerOf(ElementOf(kSectionNode, kStart))->disabled) << "a failed review starts again";
        EXPECT_FALSE(ClickerOf(ElementOf(kSectionNode, kCancel))->disabled);

        ASSERT_TRUE(ClickReviewSection(kStart).Both());
        ASSERT_TRUE(FramesUntil([&] { return factory.Calls() == 2; })) << "the start asked for an engine again";
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetReviewState(Model()) == ReviewState::Failed; }));
        EXPECT_EQ(Var("reviewStatus"), "Review failed");
    }

    TEST_F(ChessUiTest, TheLineFollowsTheReviewAndASteadyFrameWritesNoVariable) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<AnalysingEngine>();
        engine->Gate(true);
        CountingAnalysisFactory factory{engine};
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(OpenTheReviewSection());
        ASSERT_TRUE(ClickReviewSection(kStart).Both());
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));

        const auto steady = [&](const char* when) {
            Draw();
            const uint64_t version = CoreManager::GetUIVars().GetVersion();
            for (int frame = 0; frame < 3; ++frame) {
                _manager->UpdateAll();
                Draw();
            }
            EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version) << when;
        };
        steady("a review held at its gate");

        engine->Open();
        ASSERT_TRUE(WaitForReview());
        steady("a done review");
    }

    // ---- the HUD while a review runs ------------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, WhileAReviewRunsTheStatusLineSaysHowFarItIsAndTheTurnLineStaysTheGames) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<AnalysingEngine>();
        engine->Gate(true);
        CountingAnalysisFactory factory{engine};
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_EQ(Var("status"), "Checkmate");

        Press("ChessReviewStart");
        ASSERT_EQ(Analyst().GetReviewState(Model()), ReviewState::Running);
        EXPECT_EQ(Var("status"), "Reviewing 0/4");
        EXPECT_EQ(Var("turn"), StatusText::Turn(Model(), false)) << "the Turn line still says who won";

        engine->Open();
        ASSERT_TRUE(WaitForReview());
        EXPECT_EQ(Var("status"), "Checkmate") << "a done review leaves nothing of its words";
    }

    TEST_F(ChessUiTest, EveryStepOfTheReviewReachesTheStatusLineAndACancelledOneLeavesNothing) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<AnalysingEngine>();
        CountingAnalysisFactory factory{engine};
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayFoolsMate());

        Press("ChessReviewStart");
        ASSERT_TRUE(FramesUntil([&] {
            const ReviewState state = Analyst().GetReviewState(Model());
            const String status = Var("status");
            if (state == ReviewState::Running) {
                EXPECT_EQ(status, ReviewText::Hud(Analyst().GetReviewProgress(Model())));
            }
            return state == ReviewState::Done;
        }));

        Press("ChessReviewCancel");
        EXPECT_EQ(Var("status"), "Checkmate");

        const auto gated = New<AnalysingEngine>();
        gated->Gate(true);
        CountingAnalysisFactory gatedFactory{gated};
        Analyst().SetEngineFactory(gatedFactory.AsFactory());
        Press("ChessReviewStart");
        ASSERT_EQ(Var("status"), "Reviewing 0/4");
        Press("ChessReviewCancel");
        EXPECT_EQ(Var("status"), "Checkmate");
        gated->Open();
    }

    TEST_F(ChessUiTest, AViewOverAReviewShowsTheViewsLineAndTheReviewComesBackWhenTheViewEnds) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<AnalysingEngine>();
        engine->Gate(true);
        CountingAnalysisFactory factory{engine};
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayFoolsMate());
        Press("ChessReviewStart");
        ASSERT_EQ(Var("status"), "Reviewing 0/4");

        ASSERT_TRUE(Model().ShowMoves(2));
        EXPECT_EQ(Var("status"), StatusText::Viewing(Model()));
        ASSERT_TRUE(Model().EndViewing());
        EXPECT_EQ(Var("status"), "Reviewing 0/4");
        engine->Open();
    }

    TEST_F(ChessUiTest, AStandingErrorOfTheOpponentIsShownOverARunningReview) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<AnalysingEngine>();
        engine->Gate(true);
        CountingAnalysisFactory factory{engine};
        Analyst().SetEngineFactory(factory.AsFactory());
        CountingFactory opponents(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        Opponent().SetEngineFactory(opponents.AsFactory());
        ASSERT_TRUE(PlayFoolsMate());
        Press("ChessReviewStart");
        ASSERT_EQ(Var("status"), "Reviewing 0/4");

        ASSERT_FALSE(Opponent().SetSide(PieceColor::Black).has_value()) << "no engine can be made, the pick is refused";
        ASSERT_TRUE(Opponent().GetError().has_value());
        EXPECT_EQ(Var("status"), OpponentText::Error(*Opponent().GetError()));
        engine->Open();
    }

}  // namespace BECore::Chess::Tests
