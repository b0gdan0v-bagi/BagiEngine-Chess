#include <tests/ChessUiHarness.h>

/// The move list: the node MovePanel (config/nodes/ChessMovePanel.xml) and what ChessMovePanelComponent writes into it. The scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        constexpr float kTitleHeight = 56.0f;
        constexpr float kRowHeight = 56.0f;
        constexpr float kNumberWidth = 56.0f;
        constexpr const char* kMovePanelPrefix = "MovePanel";
        constexpr const char* kLongestMoves[] = {"Qh4xe1+", "exd8=Q#", "O-O-O"};  // the longest move of each kind of text the list can hold
        constexpr const char* kLongestNumber = "100.";
        constexpr size_t kMovePanelWordRounds = std::size(kLongestMoves);

        /// Sets the 24 variables of the rows to round @p round: the three longest moves and the longest number go round the 16 cells, so each one stands in a White and in a Black cell
        void PublishMovePanelRound(size_t round) {
            UIVars& vars = CoreManager::GetUIVars();
            for (size_t row = 0; row < kMovePanelRows; ++row) {
                vars.Set(PoolString::Intern(Format("movePanelNumber{}", row).c_str()), kLongestNumber);
                vars.Set(PoolString::Intern(Format("movePanelWhite{}", row).c_str()), kLongestMoves[(round + row) % std::size(kLongestMoves)]);
                vars.Set(PoolString::Intern(Format("movePanelBlack{}", row).c_str()), kLongestMoves[(round + row + 1) % std::size(kLongestMoves)]);
            }
        }

        /// Shows the box and the 8 rows of the canvas (the component shows only what the game has), so the layout holds the panel at its full size
        void ShowEveryMovePanelRow(UICanvasComponent& canvas) {
            EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern("MovePanelBox"), true));
            for (size_t row = 0; row < kMovePanelRows; ++row) {
                EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern(Format("MovePanelRow{}", row).c_str()), true)) << row;
            }
        }

        /// What is wrong with the canvas MovePanel as it is laid out with the box and every row shown, one problem to a line, nothing when it is right: a name without the prefix, a box that is
        /// not the one of Game/MovePanel.h, a box that lies inside the safe area exactly where MovePanelFits says it does not, bands and rows of the wrong height, a word that a cell cuts
        String MovePanelProblems(const UICanvasLayout& layout) {
            String problems;
            const float scale = layout.GetResolved().scale;
            const Rect safe = layout.GetResolved().safeBounds;

            Vector<PoolString> names;
            CollectNames(*layout.root, names);
            for (const PoolString& name : names) {
                if (name.ToStringView().substr(0, StringView(kMovePanelPrefix).size()) != kMovePanelPrefix) {
                    problems += Format("{} does not start with {}\n", name.ToStringView(), kMovePanelPrefix);
                }
            }

            const UIElement* box = layout.root->FindByName(PoolString::Intern("MovePanelBox"));
            if (box == nullptr || box->rect.IsEmpty()) {
                return problems + "MovePanelBox is missing or has an empty rect\n";
            }
            const Rect expected{kMovePanelLeft, kMovePanelTop, kMovePanelRight - kMovePanelLeft, kMovePanelBottom - kMovePanelTop};
            if (!SameRect(box->rect, expected)) {
                problems += Format("MovePanelBox ({:.1f}, {:.1f}, {:.1f}, {:.1f}) is not the box of Game/MovePanel.h\n", box->rect.x, box->rect.y, box->rect.w, box->rect.h);
            }
            const bool fits = MovePanelFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h);
            if (LiesIn(box->rect, safe) != fits) {
                problems += Format("MovePanelBox lies in the safe area ({:.1f}, {:.1f}, {:.1f}, {:.1f}): {}, the rule says {}\n", safe.x, safe.y, safe.w, safe.h, LiesIn(box->rect, safe), fits);
            }

            const auto find = [&](const String& name) -> const UIElement* {
                const UIElement* element = layout.root->FindByName(PoolString::Intern(name.c_str()));
                if (element == nullptr) {
                    problems += Format("{} is missing\n", name.c_str());
                } else if (element->rect.IsEmpty()) {
                    problems += Format("{} has an empty rect\n", name.c_str());
                    return nullptr;
                }
                return element;
            };
            const auto fitsIn = [&](const UIElement& element) {
                const UIContentFit fit = element.CheckContent(element.rect.w, element.rect.h, scale);
                if (fit.hasContent && !fit.fits) {
                    problems += Format("{} cuts '{}': {:.1f} x {:.1f} units at scale {:.3f}\n", element.name.ToStringView(), ShownText(element).c_str(), element.rect.w, element.rect.h, scale);
                }
            };

            if (const UIElement* title = find("MovePanelTitle")) {
                if (std::abs(title->rect.h - kTitleHeight) > kPixelTolerance || !LiesIn(title->rect, box->rect)) {
                    problems += "MovePanelTitle is not the band of 56 units at the top of the box\n";
                }
                fitsIn(*title);
            }
            const UIElement* list = find("MovePanelList");
            float previousBottom = list != nullptr ? list->rect.y : 0.0f;
            for (size_t row = 0; row < kMovePanelRows; ++row) {
                const UIElement* line = find(Format("MovePanelRow{}", row));
                const UIElement* number = find(Format("MovePanelNumber{}", row));
                const UIElement* white = find(Format("MovePanelWhite{}", row));
                const UIElement* black = find(Format("MovePanelBlack{}", row));
                if (line == nullptr || number == nullptr || white == nullptr || black == nullptr) {
                    continue;
                }
                if (std::abs(line->rect.h - kRowHeight) > kPixelTolerance) {
                    problems += Format("{} is {:.1f} units high, not {:.0f}\n", line->name.ToStringView(), line->rect.h, kRowHeight);
                }
                if (std::abs(line->rect.y - previousBottom) > kPixelTolerance || (list != nullptr && !LiesIn(line->rect, list->rect))) {
                    problems += Format("{} does not follow the row before it inside the list\n", line->name.ToStringView());
                }
                previousBottom = line->rect.y + line->rect.h;
                if (std::abs(number->rect.w - kNumberWidth) > kPixelTolerance) {
                    problems += Format("{} is {:.1f} units wide, not {:.0f}\n", number->name.ToStringView(), number->rect.w, kNumberWidth);
                }
                if (std::abs(white->rect.w - black->rect.w) > kPixelTolerance || number->rect.x + number->rect.w > white->rect.x + kPixelTolerance ||
                    white->rect.x + white->rect.w > black->rect.x + kPixelTolerance) {
                    problems += Format("the cells of {} are not number, White, Black side by side, White and Black as wide as each other\n", line->name.ToStringView());
                }
                if (!LiesIn(black->rect, line->rect)) {
                    problems += Format("{} is not inside its row\n", black->name.ToStringView());
                }
                fitsIn(*number);
                for (const char* side : {"White", "Black"}) {
                    if (const UIElement* text = find(Format("MovePanel{}Text{}", side, row))) {
                        fitsIn(*text);
                    }
                }
            }
            if (list != nullptr && !LiesIn(list->rect, box->rect)) {
                problems += "MovePanelList is not inside the box\n";
            }
            return problems;
        }

    }  // namespace

    TEST_F(ChessUiTest, TheMovePanelPassesTheSizeMatrixWithTheLongestMoves) {
        Draw();
        auto canvas = MovePanel();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        ShowEveryMovePanelRow(*canvas);

        UICanvasLayout& layout = canvas->GetLayout();
        const PoolString boxName = PoolString::Intern("MovePanelBox");
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        String report;
        size_t reported = 0;
        for (size_t round = 0; round < kMovePanelWordRounds; ++round) {
            PublishMovePanelRound(round);
            for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
                const UILayoutContext context = ContextOf(matrixCase, true);
                // The box shown whatever the window: its numbers, its words and the rule that says where it fits
                canvas->SetElementVisible(boxName, true);
                layout.Invalidate();
                layout.Update(context);
                String problems = MovePanelProblems(layout);

                // The box as the component shows it, exactly where the rule says: what is on the screen passes the validator
                const Rect safe = layout.GetResolved().safeBounds;
                canvas->SetElementVisible(boxName, MovePanelFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h));
                layout.Invalidate();
                layout.Update(context);
                UILayoutValidatorOptions caseOptions = options;
                caseOptions.pixelDensity = context.pixelDensity;
                const auto issues = layout.Validate(caseOptions);
                if (!issues.empty()) {
                    problems += Format("the validator finds {} issue(s) with the box shown as the rule says\n", issues.size());
                }
                if (!problems.empty() && reported++ < kReportedCases) {
                    report += Format("round {}, {}:\n{}", round, matrixCase.label.c_str(), problems.c_str());
                }
            }
        }
        EXPECT_TRUE(report.empty()) << report.c_str();
        canvas->SetElementVisible(boxName, false);
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, TheMovePanelShowsTheRowsOfTheGameAndMarksTheLastMoveInAWideWindow) {
        Settle();
        const UIElement& box = MovePanelElement("MovePanelBox");
        EXPECT_TRUE(box.active.visible) << "a Wide window with no insets has room for the box";
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelTitle")), "Moves");
        EXPECT_FALSE(MovePanelElement("MovePanelRow0").active.visible) << "no move, no row";

        for (const char* uci : {"e2e4", "e7e5", "g1f3"}) {
            ASSERT_TRUE(Play(Model(), uci)) << uci;
        }
        Draw();
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelNumber0")), "1.");
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelWhiteText0")), "e4");
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelBlackText0")), "e5");
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelNumber1")), "2.");
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelWhiteText1")), "Nf3");
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelBlackText1")), "");
        EXPECT_TRUE(MovePanelElement("MovePanelRow1").active.visible);
        EXPECT_FALSE(MovePanelElement("MovePanelRow2").active.visible);
        EXPECT_TRUE(MovePanelElement("MovePanelWhiteMark1").active.visible) << "the last move is marked";
        EXPECT_FALSE(MovePanelElement("MovePanelWhiteMark0").active.visible);
        EXPECT_FALSE(MovePanelElement("MovePanelBlackMark0").active.visible);
        EXPECT_FALSE(MovePanelElement("MovePanelBlackMark1").active.visible);
    }

    TEST_F(ChessUiTest, TheMovePanelIsHiddenInTheViewsThatHaveNoRoomForIt) {
        ASSERT_TRUE(Play(Model(), "e2e4")) << "a game with a move, so it is the window that hides the box";
        for (const Pair<float, float>& output : {Pair<float, float>{1024.0f, 768.0f}, Pair<float, float>{kNarrowestWidth, kNarrowestPortraitHeight}}) {
            SCOPED_TRACE(Format("output {}x{}", output.first, output.second).c_str());
            DrawAt(output.first, output.second);
            Settle();
            EXPECT_FALSE(MovePanelElement("MovePanelBox").active.visible);
            EXPECT_TRUE(MovePanelElement("MovePanelBox").rect.IsEmpty());
        }
    }

}  // namespace BECore::Chess::Tests
