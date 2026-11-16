#include <BECore/UI/UIProgressBar.h>
#include <BECore/UI/UIToggleComponent.h>
#include <tests/ChessUiHarness.h>

/// The analysis controls of the move panel: the Hint button and the evaluation cell of the band MovePanelBandAnalysis in the Wide view and their twins in MovePanelBandButtons in the Tall
/// view (config/nodes/ChessMovePanel.xml). The scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        // The longest word of each kind the cell can hold, and a bar for each of them (the whole range of the share)
        constexpr const char* kEvaluationWords[] = {"Eval off", "...", "-", "No analysis", "+300.00", "-300.00", "#-100"};
        constexpr const char* kShares[] = {"0", "0.5", "1", "0.25", "0.75", "0", "1"};
        static_assert(std::size(kEvaluationWords) == std::size(kShares));

        constexpr float kAnalysisControlHeight = 56.0f;  // a touch target at the smallest fit scale of the matrix
        constexpr float kEvaluationBarHeight = 12.0f;
        constexpr float kHintWidth = 96.0f;

        /// The names of the controls of one place: the lowest band in the Wide view, the end of the band of the buttons in the Tall view
        struct AnalysisNames {
            const char* band;
            const char* hint;
            const char* eval;
            const char* text;
            const char* bar;
        };

        constexpr AnalysisNames kWideNames{"MovePanelBandAnalysis", "MovePanelHint", "MovePanelEval", "MovePanelEvalText", "MovePanelEvalBar"};
        constexpr AnalysisNames kTallNames{"MovePanelBandButtons", "MovePanelTallHint", "MovePanelTallEval", "MovePanelTallEvalText", "MovePanelTallEvalBar"};

        /// Whether the box of the panel lies in the safe area of the view a laid out canvas has
        bool BoxFitsIn(const UICanvasLayout& layout) {
            const Rect safe = layout.GetResolved().safeBounds;
            return MovePanelFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h, LaidOutForTall(layout) ? MovePanelView::Tall : MovePanelView::Wide);
        }

        /// What is wrong with the controls of the canvas MovePanel as it is laid out with the box shown: the pair of the view in its band, 56 high, apart, inside the box and clear of the bands
        /// above; the pair of the other view with no room; the words and the bar cut by nothing, the text above the bar
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
            if (box == nullptr || buttons == nullptr || takeBack == nullptr || band == nullptr || hint == nullptr || eval == nullptr || text == nullptr || bar == nullptr) {
                return problems;
            }

            for (const char* name : {hidden.hint, hidden.eval, hidden.text, hidden.bar}) {
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

            for (const UIElement* control : {hint, eval}) {
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
            EXPECT_EQ(clicker->action, PoolString::Intern("ChessHint")) << hint.name.CStr();
            EXPECT_TRUE(clicker->disabled) << hint.name.CStr() << ": greyed until the component says a hint can be asked";
            EXPECT_EQ(TextOf(hint)->GetDisplayText(hint), "Hint");

            const UIToggleComponent* toggle = eval.GetComponent<UIToggleComponent>();
            ASSERT_NE(toggle, nullptr) << eval.name.CStr();
            EXPECT_EQ(toggle->bind, PoolString::Intern("analysisEvaluationOn")) << eval.name.CStr();
            EXPECT_EQ(toggle->action, PoolString::Intern("ChessSetEvaluation")) << eval.name.CStr();
            ASSERT_NE(ClickerOf(eval), nullptr) << eval.name.CStr();
            EXPECT_TRUE(ClickerOf(eval)->action.Empty()) << "the toggle decides what a click emits";

            const UIProgressBar* progress = bar.Cast<UIProgressBar>();
            ASSERT_NE(progress, nullptr) << bar.name.CStr();
            EXPECT_EQ(progress->bind, PoolString::Intern("analysisBar")) << bar.name.CStr();
            EXPECT_FLOAT_EQ(progress->minValue, 0.0f);
            EXPECT_FLOAT_EQ(progress->maxValue, 1.0f);
            EXPECT_FALSE(bar.IsInteractive()) << "a press on the bar goes to the cell under it";
        }

    }  // namespace

    TEST_F(ChessUiTest, TheAnalysisControlsPassTheSizeMatrixWithTheLongestWords) {
        Draw();
        auto canvas = MovePanel();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update

        UICanvasLayout& layout = canvas->GetLayout();
        const PoolString boxName = PoolString::Intern("MovePanelBox");
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        UIVars& vars = CoreManager::GetUIVars();
        String report;
        size_t reported = 0;
        size_t laidOut = 0;
        for (size_t round = 0; round < std::size(kEvaluationWords); ++round) {
            vars.Set(PoolString::Intern("analysisText"), kEvaluationWords[round]);
            vars.Set(PoolString::Intern("analysisBar"), kShares[round]);
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

        CoreManager::GetUIVars().Set(PoolString::Intern("analysisText"), "+0.35");
        CoreManager::GetUIVars().Set(PoolString::Intern("analysisBar"), "0.25");
        Draw();
        EXPECT_EQ(ShownText(MovePanelElement(kWideNames.text)), "+0.35");
        EXPECT_EQ(ShownText(MovePanelElement(kTallNames.text)), "+0.35");
        EXPECT_FLOAT_EQ(MovePanelElement(kWideNames.bar).Cast<UIProgressBar>()->GetFraction(), 0.25f);
        EXPECT_FLOAT_EQ(MovePanelElement(kTallNames.bar).Cast<UIProgressBar>()->GetFraction(), 0.25f);
    }

    TEST_F(ChessUiTest, TheWidePairIsShownOnlyInTheWideViewAndTheTallTwinsOnlyInTheTallView) {
        Settle();
        ASSERT_TRUE(MovePanelElement("MovePanelBox").active.visible);
        for (const char* name : {kWideNames.band, kWideNames.hint, kWideNames.eval}) {
            EXPECT_TRUE(MovePanelElement(name).active.visible) << name << " in the Wide view";
        }
        for (const char* name : {kTallNames.hint, kTallNames.eval}) {
            EXPECT_FALSE(MovePanelElement(name).active.visible) << name << " in the Wide view";
            EXPECT_TRUE(MovePanelElement(name).rect.IsEmpty()) << name << " takes no room in the Wide view";
        }

        ResizeWithoutInsets(kNarrowestWidth, kNarrowestTallHeight);
        ASSERT_EQ(GetScene().GetActiveView().name, PoolString::Intern(kTallViewName));
        ASSERT_TRUE(MovePanelElement("MovePanelBox").active.visible);
        EXPECT_FALSE(MovePanelElement(kWideNames.band).active.visible) << "the band is hidden in the Tall view";
        for (const char* name : {kWideNames.band, kWideNames.hint, kWideNames.eval}) {
            EXPECT_TRUE(MovePanelElement(name).rect.IsEmpty()) << name << " takes no room in the Tall view";
        }
        for (const char* name : {kTallNames.hint, kTallNames.eval}) {
            EXPECT_TRUE(MovePanelElement(name).active.visible) << name << " in the Tall view";
        }
    }

}  // namespace BECore::Chess::Tests
