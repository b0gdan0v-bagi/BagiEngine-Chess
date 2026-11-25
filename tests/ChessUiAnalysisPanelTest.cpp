#include <BECore/UI/UIEnvironment.h>
#include <BECore/UI/UIProgressBar.h>
#include <BECore/UI/UISwitchComponent.h>
#include <BECore/UI/UIToggleComponent.h>
#include <Chess/Engine/AnalysisText.h>
#include <tests/ChessUiHarness.h>

/// The analysis controls of the move panel: the Hint button, the evaluation cell and the ratings switch of the band MovePanelBandAnalysis in the Wide view and their twins in MovePanelBandButtons in
/// the Tall view (config/nodes/ChessMovePanel.xml). The scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        // The longest word of each kind the cell can hold, and a bar for each of them (the whole range of the share)
        constexpr const char* kEvaluationWords[] = {"Eval off", "...", "-", "No analysis", "+300.00", "-300.00", "#-100"};
        constexpr const char* kShares[] = {"0", "0.5", "1", "0.25", "0.75", "0", "1"};
        static_assert(std::size(kEvaluationWords) == std::size(kShares));

        constexpr float kAnalysisControlHeight = 56.0f;  // a touch target at the smallest fit scale of the matrix
        constexpr float kEvaluationBarHeight = 12.0f;
        constexpr float kHintWidth = 96.0f;
        constexpr float kRatingsWidth = 56.0f;  // the touch target's width: the cell is no wider than it needs

        /// The names of the controls of one place: the lowest band in the Wide view, the end of the band of the buttons in the Tall view
        struct AnalysisNames {
            const char* band;
            const char* hint;
            const char* eval;
            const char* text;
            const char* bar;
            const char* ratings;
            const char* ratingsText;
            const char* ratingsSwitch;
        };

        constexpr AnalysisNames kWideNames{"MovePanelBandAnalysis", "MovePanelHint",    "MovePanelEval",        "MovePanelEvalText",
                                           "MovePanelEvalBar",      "MovePanelRatings", "MovePanelRatingsText", "MovePanelRatingsSwitch"};
        constexpr AnalysisNames kTallNames{"MovePanelBandButtons", "MovePanelTallHint",    "MovePanelTallEval",        "MovePanelTallEvalText",
                                           "MovePanelTallEvalBar", "MovePanelTallRatings", "MovePanelTallRatingsText", "MovePanelTallRatingsSwitch"};

        /// Whether the box of the panel lies in the safe area of the view a laid out canvas has
        bool BoxFitsIn(const UICanvasLayout& layout) {
            const Rect safe = layout.GetResolved().safeBounds;
            return MovePanelFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h, LaidOutForTall(layout) ? MovePanelView::Tall : MovePanelView::Wide);
        }

        /// What is wrong with the controls of the canvas MovePanel as it is laid out with the box shown: the three of the view in its band, 56 high, apart, inside the box and clear of the bands
        /// above; the three of the other view with no room; the words and the bar cut by nothing, the text above the bar, the ratings cell a touch target that holds its word and its switch
        String AnalysisProblems(const UICanvasLayout& layout) {
            String problems;
            const bool tall = LaidOutForTall(layout);
            const float scale = layout.GetResolved().scale;
            const AnalysisNames& shown = tall ? kTallNames : kWideNames;
            const AnalysisNames& hidden = tall ? kWideNames : kTallNames;

            const auto find = [&](const char* name) -> const UIElement* {
                const UIElement* element = layout.root->FindByName(PoolString::Intern(name));
                if (element == nullptr) {
                    problems += Format("{} is missing\n", name);
                }
                return element;
            };
            const UIElement* box = find("MovePanelBox");
            const UIElement* buttons = find("MovePanelBandButtons");
            const UIElement* takeBack = find("MovePanelTakeBack");
            const UIElement* band = find(shown.band);
            const UIElement* hint = find(shown.hint);
            const UIElement* eval = find(shown.eval);
            const UIElement* text = find(shown.text);
            const UIElement* bar = find(shown.bar);
            const UIElement* ratings = find(shown.ratings);
            const UIElement* ratingsText = find(shown.ratingsText);
            const UIElement* ratingsSwitch = find(shown.ratingsSwitch);
            if (box == nullptr || buttons == nullptr || takeBack == nullptr || band == nullptr || hint == nullptr || eval == nullptr || text == nullptr || bar == nullptr || ratings == nullptr ||
                ratingsText == nullptr || ratingsSwitch == nullptr) {
                return problems;
            }

            for (const char* name : {hidden.hint, hidden.eval, hidden.text, hidden.bar, hidden.ratings, hidden.ratingsText, hidden.ratingsSwitch}) {
                const UIElement* other = find(name);
                if (other != nullptr && !other->rect.IsEmpty()) {
                    problems += Format("{} is laid out in the {} view, which has its twin\n", name, tall ? "Tall" : "Wide");
                }
            }
            if (!tall) {
                if (std::abs(band->rect.h - kAnalysisControlHeight) > kPixelTolerance) {
                    problems += "MovePanelBandAnalysis is not a band of 56 units\n";
                }
                if (band->rect.y < buttons->rect.y + buttons->rect.h - kPixelTolerance) {
                    problems += "MovePanelBandAnalysis is not under the band of the buttons\n";
                }
            } else {
                const UIElement* analysisBand = find(kWideNames.band);
                if (analysisBand != nullptr && !analysisBand->rect.IsEmpty()) {
                    problems += "MovePanelBandAnalysis is not hidden in the Tall view, which has no room for it\n";
                }
                if (takeBack->rect.x + takeBack->rect.w > hint->rect.x + kPixelTolerance) {
                    problems += "MovePanelTallHint is not after Take back\n";
                }
            }
            if (!LiesIn(band->rect, box->rect)) {
                problems += Format("{} is not inside the box\n", shown.band);
            }

            for (const UIElement* control : {hint, eval, ratings}) {
                if (std::abs(control->rect.h - kAnalysisControlHeight) > kPixelTolerance) {
                    problems += Format("{} is {:.1f} units high, not {:.0f}\n", control->name.ToStringView(), control->rect.h, kAnalysisControlHeight);
                }
                if (!LiesIn(control->rect, band->rect)) {
                    problems += Format("{} is not inside {}\n", control->name.ToStringView(), shown.band);
                }
            }
            if (std::abs(hint->rect.w - kHintWidth) > kPixelTolerance) {
                problems += Format("{} is {:.1f} units wide, not {:.0f}\n", shown.hint, hint->rect.w, kHintWidth);
            }
            if (hint->rect.x + hint->rect.w > eval->rect.x + kPixelTolerance) {
                problems += "the evaluation cell is not after the Hint button\n";
            }
            if (eval->rect.x + eval->rect.w > ratings->rect.x + kPixelTolerance) {
                problems += "the ratings cell is not after the evaluation cell\n";
            }
            if (ratings->rect.w < kRatingsWidth - kPixelTolerance) {
                problems += Format("{} is {:.1f} units wide, under the touch target of {:.0f}\n", shown.ratings, ratings->rect.w, kRatingsWidth);
            }
            const UIContentFit ratingsFit = ratingsText->CheckContent(ratingsText->rect.w, ratingsText->rect.h, scale);
            if (ratingsFit.hasContent && !ratingsFit.fits) {
                problems += Format("{} cuts '{}': {:.1f} x {:.1f} units at scale {:.3f}\n", shown.ratingsText, ShownText(*ratingsText).c_str(), ratingsText->rect.w, ratingsText->rect.h, scale);
            }
            if (!LiesIn(ratingsText->rect, ratings->rect) || !LiesIn(ratingsSwitch->rect, ratings->rect)) {
                problems += Format("{} or {} is not inside the ratings cell\n", shown.ratingsText, shown.ratingsSwitch);
            }
            if (ratingsText->rect.y + ratingsText->rect.h > ratingsSwitch->rect.y + kPixelTolerance) {
                problems += Format("{} lies on {}\n", shown.ratingsText, shown.ratingsSwitch);
            }
            const UIContentFit hintFit = hint->CheckContent(hint->rect.w - 2.0f * kButtonAir, hint->rect.h, scale);
            if (hintFit.hasContent && !hintFit.fits) {
                problems += Format("{} cuts '{}': {:.1f} units wide at scale {:.3f}\n", shown.hint, ShownText(*hint).c_str(), hint->rect.w, scale);
            }

            const UIContentFit textFit = text->CheckContent(text->rect.w, text->rect.h, scale);
            if (textFit.hasContent && !textFit.fits) {
                problems += Format("{} cuts '{}': {:.1f} x {:.1f} units at scale {:.3f}\n", shown.text, ShownText(*text).c_str(), text->rect.w, text->rect.h, scale);
            }
            if (!LiesIn(text->rect, eval->rect) || !LiesIn(bar->rect, eval->rect)) {
                problems += Format("{} or {} is not inside the evaluation cell\n", shown.text, shown.bar);
            }
            if (std::abs(bar->rect.h - kEvaluationBarHeight) > kPixelTolerance) {
                problems += Format("{} is {:.1f} units high, not {:.0f}\n", shown.bar, bar->rect.h, kEvaluationBarHeight);
            }
            if (text->rect.y + text->rect.h > bar->rect.y + kPixelTolerance) {
                problems += Format("{} lies on {}: the word would be over the light fill\n", shown.text, shown.bar);
            }
            return problems;
        }

        void ExpectAnalysisControls(const UIElement& hint, const UIElement& eval, const UIElement& bar) {
            const UIInputClicker* clicker = ClickerOf(hint);
            ASSERT_NE(clicker, nullptr) << hint.name.CStr();
            EXPECT_EQ(clicker->action, "ChessHint"_intern) << hint.name.CStr();
            EXPECT_FALSE(clicker->disabled) << hint.name.CStr() << ": two people at the start, the person is to move";
            EXPECT_EQ(TextOf(hint)->GetDisplayText(hint), "Hint");

            const UIToggleComponent* toggle = eval.GetComponent<UIToggleComponent>();
            ASSERT_NE(toggle, nullptr) << eval.name.CStr();
            EXPECT_EQ(toggle->bind, "analysisEvaluationOn"_intern) << eval.name.CStr();
            EXPECT_EQ(toggle->action, "ChessSetEvaluation"_intern) << eval.name.CStr();
            ASSERT_NE(ClickerOf(eval), nullptr) << eval.name.CStr();
            EXPECT_TRUE(ClickerOf(eval)->action.Empty()) << "the toggle decides what a click emits";

            const UIProgressBar* progress = bar.Cast<UIProgressBar>();
            ASSERT_NE(progress, nullptr) << bar.name.CStr();
            EXPECT_EQ(progress->bind, "analysisBar"_intern) << bar.name.CStr();
            EXPECT_FLOAT_EQ(progress->minValue, 0.0f);
            EXPECT_FLOAT_EQ(progress->maxValue, 1.0f);
            EXPECT_FALSE(bar.IsInteractive()) << "a press on the bar goes to the cell under it";
        }

        void ExpectRatingsCell(const UIElement& cell) {
            const UIToggleComponent* toggle = cell.GetComponent<UIToggleComponent>();
            ASSERT_NE(toggle, nullptr) << cell.name.CStr();
            EXPECT_EQ(toggle->bind, "analysisRatingsOn"_intern) << cell.name.CStr();
            EXPECT_EQ(toggle->action, "ChessSetRatings"_intern) << cell.name.CStr();
            ASSERT_NE(ClickerOf(cell), nullptr) << cell.name.CStr();
            EXPECT_TRUE(ClickerOf(cell)->action.Empty()) << "the toggle decides what a click emits";
        }

    }  // namespace

    TEST_F(ChessUiTest, TheAnalysisControlsPassTheSizeMatrixWithTheLongestWords) {
        Draw();
        auto canvas = MovePanel();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update

        UICanvasLayout& layout = canvas->GetLayout();
        const PoolString boxName = "MovePanelBox"_intern;
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        UIVars& vars = CoreManager::GetUIVars();
        String report;
        size_t reported = 0;
        size_t laidOut = 0;
        for (size_t round = 0; round < std::size(kEvaluationWords); ++round) {
            vars.Set("analysisText"_intern, kEvaluationWords[round]);
            vars.Set("analysisBar"_intern, kShares[round]);
            for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
                const UILayoutContext context = ContextOf(matrixCase, true);
                canvas->SetElementVisible(boxName, true);
                layout.Invalidate();
                layout.Update(context);
                String problems;
                if (BoxFitsIn(layout)) {
                    problems = AnalysisProblems(layout);
                    ++laidOut;
                } else {
                    canvas->SetElementVisible(boxName, false);
                    layout.Invalidate();
                    layout.Update(context);
                }
                UILayoutValidatorOptions caseOptions = options;
                caseOptions.pixelDensity = context.pixelDensity;
                const auto issues = layout.Validate(caseOptions);
                if (!issues.empty()) {
                    problems += Format("the validator finds {} issue(s) with the box shown as the rule says\n", issues.size());
                }
                if (!problems.empty() && reported++ < kReportedCases) {
                    report += Format("word '{}', {}:\n{}", kEvaluationWords[round], matrixCase.label.c_str(), problems.c_str());
                }
            }
        }
        EXPECT_TRUE(report.empty()) << report.c_str();
        EXPECT_GT(laidOut, 0u) << "the matrix has windows that hold the box";
        canvas->SetElementVisible(boxName, false);
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, TheAnalysisControlsOfBothViewsBindTheSameVariablesAndActions) {
        Settle();
        ExpectAnalysisControls(MovePanelElement(kWideNames.hint), MovePanelElement(kWideNames.eval), MovePanelElement(kWideNames.bar));
        ExpectAnalysisControls(MovePanelElement(kTallNames.hint), MovePanelElement(kTallNames.eval), MovePanelElement(kTallNames.bar));
        ExpectRatingsCell(MovePanelElement(kWideNames.ratings));
        ExpectRatingsCell(MovePanelElement(kTallNames.ratings));
        EXPECT_EQ(ShownText(MovePanelElement(kWideNames.ratingsText)), "!!");
        EXPECT_EQ(ShownText(MovePanelElement(kTallNames.ratingsText)), "!!");
        EXPECT_NE(MovePanelElement(kWideNames.ratingsSwitch).GetComponent<UISwitchComponent>(), nullptr) << "the state is the thumb's side, not a colour alone";
        EXPECT_NE(MovePanelElement(kTallNames.ratingsSwitch).GetComponent<UISwitchComponent>(), nullptr);

        CoreManager::GetUIVars().Set("analysisText"_intern, "+0.35");
        CoreManager::GetUIVars().Set("analysisBar"_intern, "0.25");
        Draw();
        EXPECT_EQ(ShownText(MovePanelElement(kWideNames.text)), "+0.35");
        EXPECT_EQ(ShownText(MovePanelElement(kTallNames.text)), "+0.35");
        EXPECT_FLOAT_EQ(MovePanelElement(kWideNames.bar).Cast<UIProgressBar>()->GetFraction(), 0.25f);
        EXPECT_FLOAT_EQ(MovePanelElement(kTallNames.bar).Cast<UIProgressBar>()->GetFraction(), 0.25f);
    }

    TEST_F(ChessUiTest, TheWidePairIsShownOnlyInTheWideViewAndTheTallTwinsOnlyInTheTallView) {
        Settle();
        ASSERT_TRUE(MovePanelElement("MovePanelBox").active.visible);
        for (const char* name : {kWideNames.band, kWideNames.hint, kWideNames.eval, kWideNames.ratings}) {
            EXPECT_TRUE(MovePanelElement(name).active.visible) << name << " in the Wide view";
        }
        for (const char* name : {kTallNames.hint, kTallNames.eval, kTallNames.ratings}) {
            EXPECT_FALSE(MovePanelElement(name).active.visible) << name << " in the Wide view";
            EXPECT_TRUE(MovePanelElement(name).rect.IsEmpty()) << name << " takes no room in the Wide view";
        }

        ResizeWithoutInsets(kNarrowestWidth, kNarrowestTallHeight);
        ASSERT_EQ(GetScene().GetActiveView().name, PoolString::Intern(kTallViewName));
        ASSERT_TRUE(MovePanelElement("MovePanelBox").active.visible);
        EXPECT_FALSE(MovePanelElement(kWideNames.band).active.visible) << "the band is hidden in the Tall view";
        for (const char* name : {kWideNames.band, kWideNames.hint, kWideNames.eval, kWideNames.ratings}) {
            EXPECT_TRUE(MovePanelElement(name).rect.IsEmpty()) << name << " takes no room in the Tall view";
        }
        for (const char* name : {kTallNames.hint, kTallNames.eval, kTallNames.ratings}) {
            EXPECT_TRUE(MovePanelElement(name).active.visible) << name << " in the Tall view";
        }
    }

    namespace {

        constexpr AnalysisScore kScore35{AnalysisScoreKind::Centipawns, 35};

        /// An analysis engine that answers +0.35 for the side to move whatever the position, and the factory that hands it out
        struct ScriptedAnalysis {
            explicit ScriptedAnalysis(Optional<Move> move = std::nullopt) : engine(New<AnalysingEngine>(kScore35, move)), factory({engine}) {}

            decltype(New<AnalysingEngine>(kScore35)) engine;
            CountingAnalysisFactory factory;
        };

        /// Quiet frames in which a request that wrongly got through would have reached the engine
        constexpr int kQuietFrames = 20;

        /// White solves: Black's setup h8g8, then Ra1a7, the reply g8h8 and Rb2b8#
        constexpr StringView kMateInTwoRow = "ladder\t7k/8/8/8/8/8/1R6/R6K b - - 0 1\th8g8 a1a7 g8h8 b2b8\t1200\tmateIn2\n";

        PuzzleSet MateInTwoSet() {
            String tsv = "id\tfen\tmoves\trating\tthemes\n";
            tsv.append(kMateInTwoRow.data(), kMateInTwoRow.size());
            return PuzzleSet::Parse(StringView(tsv.data(), tsv.size()));
        }

    }  // namespace

    TEST_F(ChessUiTest, TheHintButtonIsGreyOnlyWhereAHintCannotBeAsked) {
        SKIP_WITHOUT_WORKER_THREADS();
        Settle();
        const UIElement& button = MovePanelElement("MovePanelHint");
        const UIElement& tallButton = MovePanelElement("MovePanelTallHint");
        EXPECT_FALSE(ClickerOf(button)->disabled) << "two people: the person is to move";
        EXPECT_FALSE(ClickerOf(tallButton)->disabled);

        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ControlPlays("white");
        Settle();
        EXPECT_TRUE(ClickerOf(button)->disabled) << "the program's turn";
        EXPECT_TRUE(ClickerOf(tallButton)->disabled);

        ControlPlays("black");
        Settle();
        EXPECT_FALSE(ClickerOf(button)->disabled) << "the person's turn against the program";
    }

    TEST_F(ChessUiTest, TheHintButtonOfAPuzzleFollowsTheSolverWhateverTheOpponentIs) {
        ChessGame& game = *ChessGame::GetActive();
        game.SetPuzzles(MateInTwoSet());
        const UIElement& button = MovePanelElement("MovePanelHint");

        ControlPlays("white");  // the program is picked for the solver's color
        ASSERT_TRUE(game.BeginPuzzle(0));
        Settle();
        EXPECT_FALSE(ClickerOf(button)->disabled) << "the solver's turn";

        ASSERT_TRUE(PlayAll(Model(), {"a1a7"}));
        Settle();
        ASSERT_TRUE(game.IsPuzzleOn());
        EXPECT_TRUE(ClickerOf(button)->disabled) << "the program's reply is due, though the opponent seat is White";

        ASSERT_TRUE(game.LeavePuzzle());
        ControlPlays(nullptr);  // two people
        ASSERT_TRUE(game.BeginPuzzle(0));
        ASSERT_TRUE(PlayAll(Model(), {"a1a7"}));
        Settle();
        ASSERT_TRUE(game.IsPuzzleOn());
        EXPECT_TRUE(ClickerOf(button)->disabled) << "two people: the reply is still the program's";
    }

    TEST_F(ChessUiTest, TheHintButtonIsGreyInAFinishedGame) {
        ASSERT_TRUE(PlayAll(Model(), {"f2f3", "e7e5", "g2g4", "d8h4"}));
        ASSERT_TRUE(Model().IsGameOver());
        Settle();
        EXPECT_TRUE(ClickerOf(MovePanelElement("MovePanelHint"))->disabled);
    }

    TEST_F(ChessUiTest, AClickOnTheHintButtonAsksForTheHintAndTheBoardShowsIt) {
        ScriptedAnalysis analysis(Mv("e2e4"));
        Analyst().SetEngineFactory(analysis.factory.AsFactory());
        Settle();
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelHint")).Both());
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetHint(Model()).has_value(); }));
        EXPECT_EQ(Analyst().GetHint(Model())->move, Mv("e2e4"));
        EXPECT_FALSE(Analyst().IsEvaluationOn()) << "a hint does not switch the evaluation on";

        Draw();
        const auto [from, to] = HintFills();
        ASSERT_EQ(from.size(), 1u);
        ASSERT_EQ(to.size(), 1u);
        EXPECT_TRUE(SameRect(from[0]->rect, SquareRect("e2")));
        EXPECT_TRUE(SameRect(to[0]->rect, SquareRect("e4")));

        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelHint")).Both());
        _manager->UpdateAll();
        EXPECT_EQ(analysis.factory.Calls(), 1) << "a second press while the hint stands asks for nothing";
    }

    TEST_F(ChessUiTest, AMoveDropsTheHintAndTheButtonAsksAgain) {
        ScriptedAnalysis analysis;
        Analyst().SetEngineFactory(analysis.factory.AsFactory());
        ASSERT_TRUE(ShowTheHint());
        ASSERT_TRUE(PlayAll(Model(), {"e2e4"}));
        Draw();
        EXPECT_TRUE(HintFills().first.empty());
        EXPECT_FALSE(Analyst().GetHint(Model()).has_value());

        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelHint")).Both());
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetHint(Model()).has_value(); }));
        EXPECT_TRUE(Analyst().IsHintRequested(Model()));
    }

    TEST_F(ChessUiTest, AForcedHintActionChangesNothingWhereTheButtonIsGrey) {
        SKIP_WITHOUT_WORKER_THREADS();
        ScriptedAnalysis analysis;
        Analyst().SetEngineFactory(analysis.factory.AsFactory());
        CountingFactory opponentFactory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(opponentFactory.AsFactory());
        _manager->UpdateAll();
        ControlPlays("white");
        Settle();
        ASSERT_TRUE(ClickerOf(MovePanelElement("MovePanelHint"))->disabled);

        Press("ChessHint");
        for (int frame = 0; frame < kQuietFrames; ++frame) {
            _manager->UpdateAll();
        }
        EXPECT_FALSE(Analyst().IsHintRequested(Model())) << "the program's turn";
        EXPECT_EQ(analysis.factory.Calls(), 0);
    }

    TEST_F(ChessUiTest, AForcedHintActionChangesNothingInAFinishedGame) {
        ScriptedAnalysis analysis;
        Analyst().SetEngineFactory(analysis.factory.AsFactory());
        ASSERT_TRUE(PlayAll(Model(), {"f2f3", "e7e5", "g2g4", "d8h4"}));
        Settle();
        Press("ChessHint");
        for (int frame = 0; frame < kQuietFrames; ++frame) {
            _manager->UpdateAll();
        }
        EXPECT_FALSE(Analyst().IsHintRequested(Model()));
        EXPECT_EQ(analysis.factory.Calls(), 0);
    }

    TEST_F(ChessUiTest, TheHintIsAskedInTheGamesPositionAndEndsAViewOfAnEarlierOne) {
        ScriptedAnalysis analysis;
        Analyst().SetEngineFactory(analysis.factory.AsFactory());
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5", "g1f3"}));
        ASSERT_TRUE(Model().ShowMoves(1));
        Settle();
        ASSERT_TRUE(Model().IsViewing());

        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelHint")).Both());
        EXPECT_FALSE(Model().IsViewing()) << "the hint is for the game's position";
        EXPECT_EQ(Model().GetMoves().size(), 3u);
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetHint(Model()).has_value(); }));
        EXPECT_EQ(analysis.engine->LastRequest().moves.size(), 3u) << "the request is the whole game, not the viewed prefix";
    }

    TEST_F(ChessUiTest, TheEvaluationCellSwitchesTheEvaluationOnAndOffAndSaysTheLineInWhitesView) {
        ScriptedAnalysis analysis;
        Analyst().SetEngineFactory(analysis.factory.AsFactory());
        Settle();
        EXPECT_EQ(Var("analysisText"), "Eval off");
        EXPECT_EQ(Var("analysisEvaluationOn"), "false");
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelEvalText")), "Eval off");

        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelEval")).Both());
        EXPECT_TRUE(Analyst().IsEvaluationOn());
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetEvaluation(Model()).has_value(); }));
        EXPECT_EQ(Var("analysisText"), "+0.35");
        EXPECT_EQ(Var("analysisEvaluationOn"), "true");
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelEvalText")), "+0.35");
        const float bar = MovePanelElement("MovePanelEvalBar").Cast<UIProgressBar>()->GetFraction();
        EXPECT_GT(bar, 0.5f);
        EXPECT_LT(bar, 1.0f);

        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelEval")).Both());
        EXPECT_FALSE(Analyst().IsEvaluationOn());
        EXPECT_EQ(Var("analysisText"), "Eval off");
        EXPECT_FLOAT_EQ(MovePanelElement("MovePanelEvalBar").Cast<UIProgressBar>()->GetFraction(), 0.0f);
    }

    TEST_F(ChessUiTest, TheEvaluationFollowsTheMovesAndTurnsToWhitesView) {
        ScriptedAnalysis analysis;
        Analyst().SetEngineFactory(analysis.factory.AsFactory());
        Settle();
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelEval")).Both());
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetEvaluation(Model()).has_value(); }));
        ASSERT_EQ(Var("analysisText"), "+0.35");

        ASSERT_TRUE(PlayAll(Model(), {"e2e4"}));
        EXPECT_EQ(Var("analysisText"), "...") << "no answer for the new position yet";
        EXPECT_FLOAT_EQ(MovePanelElement("MovePanelEvalBar").Cast<UIProgressBar>()->GetFraction(), 0.5f);
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetEvaluation(Model()).has_value(); }));
        EXPECT_EQ(Var("analysisText"), "-0.35") << "the double says +0.35 for Black, who is to move";
        EXPECT_LT(MovePanelElement("MovePanelEvalBar").Cast<UIProgressBar>()->GetFraction(), 0.5f);
    }

    TEST_F(ChessUiTest, AFailedAnalysisSaysSoAndAFinishedGameSaysNothingToAnalyse) {
        CountingAnalysisFactory factory(EngineError{EngineErrorKind::LaunchFailed, "no engine"});
        Analyst().SetEngineFactory(factory.AsFactory());
        Settle();
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelEval")).Both());
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetError().has_value(); }));
        EXPECT_EQ(Var("analysisText"), "No analysis");

        ASSERT_TRUE(PlayAll(Model(), {"f2f3", "e7e5", "g2g4", "d8h4"}));
        ASSERT_TRUE(Model().IsGameOver());
        _manager->UpdateAll();
        EXPECT_EQ(Var("analysisText"), "-");
    }

    TEST_F(ChessUiTest, AnythingButTrueOrFalseDoesNotSwitchTheEvaluation) {
        ScriptedAnalysis analysis;
        Analyst().SetEngineFactory(analysis.factory.AsFactory());
        Settle();
        PressWith("ChessSetEvaluation", "maybe");
        PressWith("ChessSetEvaluation", "");
        _manager->UpdateAll();
        EXPECT_FALSE(Analyst().IsEvaluationOn());
        PressWith("ChessSetEvaluation", "true");
        EXPECT_TRUE(Analyst().IsEvaluationOn());
        PressWith("ChessSetEvaluation", "false");
        EXPECT_FALSE(Analyst().IsEvaluationOn());
    }

    TEST_F(ChessUiTest, TheTallTwinsAnswerTheSameActions) {
        ScriptedAnalysis analysis;
        Analyst().SetEngineFactory(analysis.factory.AsFactory());
        ResizeWithoutInsets(kNarrowestWidth, kNarrowestTallHeight);
        ASSERT_EQ(GetScene().GetActiveView().name, PoolString::Intern(kTallViewName));
        ASSERT_TRUE(MovePanelElement("MovePanelBox").active.visible);

        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelTallEval")).Both());
        EXPECT_TRUE(Analyst().IsEvaluationOn());
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetEvaluation(Model()).has_value(); }));
        Draw();
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelTallEvalText")), "+0.35") << "the twin binds the same variables";
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelTallHint")).Both());
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetHint(Model()).has_value(); }));
        EXPECT_TRUE(Analyst().IsHintRequested(Model()));
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelTallEval")).Both());
        EXPECT_FALSE(Analyst().IsEvaluationOn());
    }

    TEST_F(ChessUiTest, TheRatingsCellSwitchesTheRatingsOnAndOffAndTouchesNothingElse) {
        ScriptedAnalysis analysis;
        Analyst().SetEngineFactory(analysis.factory.AsFactory());
        Settle();
        EXPECT_FALSE(Analyst().AreRatingsOn()) << "off at the start of a game";
        EXPECT_EQ(Var("analysisRatingsOn"), "false");

        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelRatings")).Both());
        EXPECT_TRUE(Analyst().AreRatingsOn());
        EXPECT_EQ(Var("analysisRatingsOn"), "true");
        EXPECT_FALSE(Analyst().IsEvaluationOn());
        EXPECT_FALSE(Analyst().IsHintRequested(Model()));

        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelEval")).Both());
        EXPECT_TRUE(Analyst().IsEvaluationOn());
        EXPECT_TRUE(Analyst().AreRatingsOn()) << "the evaluation cell leaves the ratings alone";
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelEval")).Both());
        EXPECT_FALSE(Analyst().IsEvaluationOn());

        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelRatings")).Both());
        EXPECT_FALSE(Analyst().AreRatingsOn());
        EXPECT_EQ(Var("analysisRatingsOn"), "false");
        EXPECT_FALSE(Analyst().IsHintRequested(Model()));
    }

    TEST_F(ChessUiTest, AnythingButTrueOrFalseDoesNotSwitchTheRatings) {
        Settle();
        PressWith("ChessSetRatings", "maybe");
        PressWith("ChessSetRatings", "");
        _manager->UpdateAll();
        EXPECT_FALSE(Analyst().AreRatingsOn());
        PressWith("ChessSetRatings", "true");
        EXPECT_TRUE(Analyst().AreRatingsOn());
        PressWith("ChessSetRatings", "false");
        EXPECT_FALSE(Analyst().AreRatingsOn());
    }

    TEST_F(ChessUiTest, TheTallRatingsTwinAnswersTheSameAction) {
        ResizeWithoutInsets(kNarrowestWidth, kNarrowestTallHeight);
        ASSERT_EQ(GetScene().GetActiveView().name, PoolString::Intern(kTallViewName));
        ASSERT_TRUE(MovePanelElement("MovePanelBox").active.visible);

        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelTallRatings")).Both());
        EXPECT_TRUE(Analyst().AreRatingsOn());
        EXPECT_EQ(Var("analysisRatingsOn"), "true");
        EXPECT_FALSE(Analyst().IsEvaluationOn());
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelTallRatings")).Both());
        EXPECT_FALSE(Analyst().AreRatingsOn());
    }

    TEST_F(ChessUiTest, TheEvaluationAsksTheEngineThePersonPicked) {
        ScriptedAnalysis analysis;
        Analyst().SetEngineFactory(analysis.factory.AsFactory());
        Settle();
        ASSERT_TRUE(ClickControl(MovePanelElement("MovePanelEval")).Both());
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetEvaluation(Model()).has_value(); }));
        ASSERT_EQ(analysis.factory.Calls(), 1);
        const StringView picked = OpponentEngineId(Opponent().GetEngine());
        EXPECT_EQ(analysis.factory.Engines().front(), String(picked.data(), picked.size()));
    }

    // ---- A held review: the band shows the evaluation of the viewed position ----

    namespace {

        /// What the band says for the review's entry @p entry: the words of AnalysisText over its score in White's view, and the bar of it
        struct ReviewedWords {
            String text;
            String bar;
        };

        ReviewedWords WordsOf(const ReviewedPosition& entry) {
            const AnalysisLine line{entry.best, ScoreForWhite(entry.score, entry.sideToMove), kReviewDepth};
            return {AnalysisText::Evaluation(line, true, false, false), Format("{:.3f}", AnalysisText::Share(line.score))};
        }

    }  // namespace

    TEST_F(ChessUiTest, AViewedPositionShowsTheReviewsEvaluationOfItInWhitesView) {
        SKIP_WITHOUT_WORKER_THREADS();
        ReviewOnTheOwnEngine();
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(Analyst().StartReview(Model()).has_value());
        ASSERT_TRUE(WaitForReview());
        ASSERT_FALSE(Analyst().IsEvaluationOn());

        for (const size_t shown : {0u, 2u, 3u}) {
            ASSERT_TRUE(Model().ShowMoves(shown));
            const Optional<ReviewedPosition> entry = Analyst().GetReviewedPosition(Model(), shown);
            ASSERT_TRUE(entry.has_value()) << shown;
            const ReviewedWords expected = WordsOf(*entry);

            EXPECT_EQ(Var("analysisText"), expected.text) << shown;
            EXPECT_EQ(Var("analysisBar"), expected.bar) << shown;
            EXPECT_EQ(ShownText(MovePanelElement("MovePanelEvalText")), expected.text) << shown;
            EXPECT_EQ(Var("analysisEvaluationOn"), "false") << "the toggles are the analyst's, whatever the view shows";
            EXPECT_EQ(Var("analysisRatingsOn"), "false");
        }
        EXPECT_EQ(Var("analysisText"), "#-1") << "after g4 Black mates in one: White's view";
        EXPECT_EQ(Var("analysisBar"), "0.000");

        Analyst().SetEvaluation(true);
        EXPECT_EQ(Var("analysisEvaluationOn"), "true");
        EXPECT_EQ(Var("analysisText"), "#-1") << "the evaluation's own switch does not change what a held review says";
    }

    TEST_F(ChessUiTest, BackToTheGameTheWordsAreTheGamesOwnAgain) {
        SKIP_WITHOUT_WORKER_THREADS();
        ReviewOnTheOwnEngine();
        ASSERT_TRUE(PlayFoolsMate());
        Analyst().SetEvaluation(true);
        ASSERT_TRUE(Analyst().StartReview(Model()).has_value());
        ASSERT_TRUE(WaitForReview());
        ASSERT_TRUE(Model().ShowMoves(2));
        ASSERT_NE(Var("analysisText"), "-");

        ASSERT_TRUE(Model().EndViewing());

        EXPECT_EQ(Var("analysisText"), "-") << "the game is finished: nothing to analyse";
        EXPECT_EQ(Var("analysisBar"), "0.500");
    }

    TEST_F(ChessUiTest, WithNoReviewOrACancelledOneAViewedPositionKeepsTheGamesWords) {
        SKIP_WITHOUT_WORKER_THREADS();
        ReviewOnTheOwnEngine();
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(Model().ShowMoves(2));
        EXPECT_EQ(Var("analysisText"), "Eval off") << "no review held";

        ASSERT_TRUE(Model().EndViewing());
        ASSERT_TRUE(Analyst().StartReview(Model()).has_value());
        ASSERT_TRUE(WaitForReview());
        ASSERT_TRUE(Model().ShowMoves(2));
        ASSERT_NE(Var("analysisText"), "Eval off");

        Analyst().AbandonReview();
        EXPECT_EQ(Var("analysisText"), "Eval off") << "the review is gone, the band is the game's again at the next draw";
        EXPECT_EQ(Var("analysisBar"), "0.000");
    }

    TEST_F(ChessUiTest, AnUnfinishedGamesViewKeepsTheGamesEvaluation) {
        ScriptedAnalysis analysis;
        Analyst().SetEngineFactory(analysis.factory.AsFactory());
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5", "g1f3"}));
        Analyst().SetEvaluation(true);
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetEvaluation(Model()).has_value(); }));
        const String game = Var("analysisText");
        ASSERT_NE(game, "...");

        ASSERT_TRUE(Model().ShowMoves(1));

        EXPECT_EQ(Var("analysisText"), game) << "no review: the evaluation is of the game's position, as before";
    }

    TEST_F(ChessUiTest, APositionTheRunningReviewHasNotReachedReadsPendingWithAnEvenBar) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<AnalysingEngine>();
        engine->Gate(true);
        CountingAnalysisFactory factory{engine};
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(Analyst().StartReview(Model()).has_value());
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        ASSERT_TRUE(Model().ShowMoves(2));
        ASSERT_FALSE(Analyst().GetReviewedPosition(Model(), 2).has_value());

        EXPECT_EQ(Var("analysisText"), "...");
        EXPECT_EQ(Var("analysisBar"), "0.500");
        engine->Open();
        ASSERT_TRUE(WaitForReview());
        EXPECT_NE(Var("analysisText"), "...") << "the position came";
    }

    TEST_F(ChessUiTest, AViewedPositionOfAFailedReviewWithNoEntryReadsNoAnalysis) {
        CountingAnalysisFactory factory(EngineError{EngineErrorKind::LaunchFailed, "no engine"});
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(Analyst().StartReview(Model()).has_value());
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetReviewState(Model()) == ReviewState::Failed; }));
        ASSERT_TRUE(Model().ShowMoves(2));
        ASSERT_FALSE(Analyst().GetReviewedPosition(Model(), 2).has_value());

        EXPECT_EQ(Var("analysisText"), "No analysis");
        EXPECT_EQ(Var("analysisBar"), "0.500");
    }

    TEST_F(ChessUiTest, AStepOfTheViewWritesTheWordsAndASteadyFrameWhileViewingWritesNoVariable) {
        SKIP_WITHOUT_WORKER_THREADS();
        ReviewOnTheOwnEngine();
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(Analyst().StartReview(Model()).has_value());
        ASSERT_TRUE(WaitForReview());
        ASSERT_TRUE(Model().ShowMoves(1));
        const String first = Var("analysisText");
        const uint64_t version = CoreManager::GetUIVars().GetVersion();
        for (int frame = 0; frame < 3; ++frame) {
            _manager->UpdateAll();
            Draw();
        }
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version) << "a steady frame while viewing writes nothing";

        ASSERT_TRUE(Model().ShowMoves(3));
        Draw();

        EXPECT_GT(CoreManager::GetUIVars().GetVersion(), version) << "a step of the view republishes: it moves no version and no revision";
        EXPECT_NE(Var("analysisText"), first);
    }

}  // namespace BECore::Chess::Tests
