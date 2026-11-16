#include <Chess/Components/ChessMovePanelComponent.h>
#include <Chess/Game/MoveList.h>
#include <tests/ChessUiHarness.h>

/// The move list: the node MovePanel (config/nodes/ChessMovePanel.xml) and what ChessMovePanelComponent writes into it. The scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        constexpr float kBandHeight = 56.0f;
        constexpr const char* kPanelBands[] = {"MovePanelBandTopPlayer", "MovePanelBandBottomPlayer", "MovePanelBandButtons"};  // in the order of the box, top down
        constexpr float kRowHeight = 56.0f;
        constexpr float kNumberWidth = 56.0f;
        constexpr const char* kMovePanelPrefix = "MovePanel";
        constexpr const char* kLongestMoves[] = {"Qh4xe1+", "exd8=Q#", "O-O-O"};  // the longest move of each kind of text the list can hold
        constexpr const char* kLongestNumber = "100.";
        constexpr size_t kMovePanelWordRounds = std::size(kLongestMoves);
        constexpr const char* kSides[] = {"White", "Black"};
        const Color kMovePanelPlateColor{32, 44, 50, 255};   // the plate of the box (ChessMovePanel.xml)
        const Color kMovePanelMarkColor{245, 205, 50, 150};  // the gold of the board's last-move squares, with the alpha of the mark
        constexpr size_t kLongGameRows = 20;
        constexpr const char* kBlackToMoveFen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
        constexpr const char* kPawnOnTheSeventh = "k7/4P3/8/8/8/8/8/7K w - - 0 1";  // e7e8q is a check along the eighth rank

        /// Sets the 24 variables of the rows to round @p round: the three longest moves and the longest number go round the 16 cells, so each one stands in a White and in a Black cell
        void PublishMovePanelRound(size_t round) {
            UIVars& vars = CoreManager::GetUIVars();
            for (size_t row = 0; row < kMovePanelRows; ++row) {
                vars.Set(PoolString::Intern(Format("movePanelNumber{}", row).c_str()), kLongestNumber);
                vars.Set(PoolString::Intern(Format("movePanelWhite{}", row).c_str()), kLongestMoves[(round + row) % std::size(kLongestMoves)]);
                vars.Set(PoolString::Intern(Format("movePanelBlack{}", row).c_str()), kLongestMoves[(round + row + 1) % std::size(kLongestMoves)]);
            }
        }

        /// Shows the box, the 8 rows and their marks (the component shows only what the game has), so the layout holds the panel at its full size
        void ShowEveryMovePanelRow(UICanvasComponent& canvas) {
            EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern("MovePanelBox"), true));
            for (size_t row = 0; row < kMovePanelRows; ++row) {
                EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern(Format("MovePanelRow{}", row).c_str()), true)) << row;
                for (const char* side : kSides) {
                    EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern(Format("MovePanel{}Mark{}", side, row).c_str()), true)) << side << row;
                }
            }
        }

        /// What is wrong with the canvas MovePanel as it is laid out with the box and every row shown, one problem to a line, nothing when it is right: a name without the prefix, a box that is
        /// not the one of Game/MovePanel.h, a box that lies inside the safe area exactly where MovePanelFits says it does not, bands and rows of the wrong height or place (the bands of the players
        /// are where the panel set of Material is drawn: kPanelTopBand, kPanelBottomBand), a word that a cell cuts
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

            const UIElement* list = find("MovePanelList");
            const UIElement* bands[std::size(kPanelBands)] = {};
            for (size_t band = 0; band < std::size(kPanelBands); ++band) {
                bands[band] = find(kPanelBands[band]);
                if (bands[band] != nullptr && (std::abs(bands[band]->rect.h - kBandHeight) > kPixelTolerance || !LiesIn(bands[band]->rect, box->rect))) {
                    problems += Format("{} is not a band of {:.0f} units inside the box\n", kPanelBands[band], kBandHeight);
                }
            }
            if (bands[0] != nullptr && (!LiesIn(bands[0]->rect, kPanelTopBand) || std::abs(bands[0]->rect.y - kPanelTopBand.y) > kPixelTolerance)) {
                problems += "MovePanelBandTopPlayer is not the top band, where the panel set of Material draws the upper group\n";
            }
            if (bands[1] != nullptr && (!LiesIn(bands[1]->rect, kPanelBottomBand) || std::abs(bands[1]->rect.y - kPanelBottomBand.y) > kPixelTolerance)) {
                problems += "MovePanelBandBottomPlayer is not the band where the panel set of Material draws the lower group\n";
            }
            if (list != nullptr && bands[0] != nullptr && bands[1] != nullptr && bands[2] != nullptr &&
                !(bands[0]->rect.y + bands[0]->rect.h <= list->rect.y && list->rect.y + list->rect.h <= bands[1]->rect.y && bands[1]->rect.y + bands[1]->rect.h <= bands[2]->rect.y)) {
                problems += "the box is not the top band, the list, the band of the other player and the band of the buttons, top down and apart\n";
            }
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
                for (const char* side : kSides) {
                    if (const UIElement* text = find(Format("MovePanel{}Text{}", side, row))) {
                        fitsIn(*text);
                    }
                    const UIElement* cell = StringView(side) == "White" ? white : black;
                    if (const UIElement* mark = find(Format("MovePanel{}Mark{}", side, row))) {
                        if (!SameRect(mark->rect, cell->rect)) {
                            problems += Format("{} does not fill the cell {} of the move it marks\n", mark->name.ToStringView(), cell->name.ToStringView());
                        }
                    }
                }
            }
            if (list != nullptr && !LiesIn(list->rect, box->rect)) {
                problems += "MovePanelList is not inside the box\n";
            }
            return problems;
        }

        /// Lays the canvas out in @p context the way the component leaves it: the box shown exactly where MovePanelFits says, every row shown; whether the box is shown
        bool LayOutAsTheComponentShowsIt(UICanvasComponent& canvas, const UILayoutContext& context) {
            const PoolString boxName = PoolString::Intern("MovePanelBox");
            UICanvasLayout& layout = canvas.GetLayout();
            canvas.SetElementVisible(boxName, true);
            layout.Invalidate();
            layout.Update(context);
            const Rect safe = layout.GetResolved().safeBounds;
            const bool fits = MovePanelFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h);
            canvas.SetElementVisible(boxName, fits);
            layout.Invalidate();
            layout.Update(context);
            return fits;
        }

        /// A game of @p plies plies that none of the end rules stops: the legal move with the index `ply * 5 + 1` modulo their count, at every ply
        bool PlayAQuietGame(ChessGameModel& model, size_t plies) {
            for (size_t ply = 0; ply < plies; ++ply) {
                const MoveList legal = MoveGenerator::GenerateLegal(model.GetPosition());
                if (legal.empty() || model.IsGameOver() || !model.TryMove(legal[(ply * 5 + 1) % legal.size()]).has_value()) {
                    return false;
                }
            }
            return !model.IsGameOver();
        }

        bool PlayAll(ChessGameModel& model, std::initializer_list<const char*> moves) {
            for (const char* uci : moves) {
                if (!Play(model, uci)) {
                    return false;
                }
            }
            return true;
        }

        /// The elements of the shipped canvas with the box and the rows shown, the text a copy of a test starts from
        String ShownMovePanelXml() {
            String xml = ShippedElementsXml("MovePanel");
            const String hidden = " visible=\"false\"";
            for (size_t at = xml.find(hidden); at != String::npos; at = xml.find(hidden)) {
                xml.erase(at, hidden.size());
            }
            return xml;
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

    TEST_F(ChessUiTest, TheMovePanelIsOnItsOwnLayerOverThePiecesAndUnderTheHud) {
        const auto& nodes = GetScene().GetRootNode().GetChildren();
        const auto indexOf = [&](const char* name) -> int {
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (nodes[i]->GetName() == PoolString::Intern(name)) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        };
        for (const char* name : {"Board", "BoardCoordinates", "Material", "MovePanel", "Result", "Hud", "MenuPanel", "OpponentPanel", "PromotionPanel"}) {
            ASSERT_GE(indexOf(name), 0) << "the scene has the node " << name;
        }
        const auto layerOf = [&](const char* name) { return nodes[static_cast<size_t>(indexOf(name))]->GetLayer(); };
        EXPECT_EQ(layerOf("MovePanel"), 4);
        EXPECT_LT(layerOf("MovePanel"), layerOf("Material")) << "under the material, which draws the players' rows over the bands of the box";
        EXPECT_GT(indexOf("MovePanel"), indexOf("Material")) << "written after the material, before the HUD";
        EXPECT_LT(indexOf("MovePanel"), indexOf("Hud"));
        EXPECT_GT(layerOf("MovePanel"), layerOf("Board"));
        EXPECT_GT(layerOf("MovePanel"), layerOf("Result"));
        EXPECT_LT(layerOf("MovePanel"), layerOf("Hud"));
        EXPECT_LT(layerOf("MovePanel"), layerOf("MenuPanel"));
        EXPECT_LT(layerOf("MovePanel"), layerOf("OpponentPanel"));
        EXPECT_LT(layerOf("MovePanel"), layerOf("PromotionPanel"));

        // The component comes before the canvas: it gives the variables their words before the canvas draws in the same frame
        const auto& components = nodes[static_cast<size_t>(indexOf("MovePanel"))]->GetComponents();
        const auto words = nodes[static_cast<size_t>(indexOf("MovePanel"))]->GetComponent<ChessMovePanelComponent>();
        const auto canvas = MovePanel();
        ASSERT_TRUE(words);
        ASSERT_TRUE(canvas);
        ASSERT_EQ(components.size(), 2u);
        EXPECT_EQ(components[0].Get(), static_cast<IComponent*>(words.Get()));
        EXPECT_EQ(components[1].Get(), static_cast<IComponent*>(canvas.Get()));

        EXPECT_EQ(canvas->_space, CanvasSpace::SceneView);
        EXPECT_FALSE(canvas->_useSafeArea) << "fixed scene units like the board; the rule of the panel reads the safe area itself";
        EXPECT_FALSE(canvas->_modal);
        EXPECT_TRUE(canvas->_visible);
    }

    TEST_F(ChessUiTest, TheMovePanelIsDrawnOverThePiecesAndTheResultBarAndUnderTheHudAndThePanels) {
        const auto king = GiveAtlasCell("WhiteKing_e1");
        StartFrom(kBlackMatesOnA1);
        ASSERT_TRUE(Play(Model(), "a8a1"));
        ASSERT_TRUE(Model().IsGameOver());
        _manager->UpdateAll();
        Settle();
        ASSERT_TRUE(OpenThePanel());
        Draw();

        const auto& calls = _renderer->calls;
        long lastSquare = -1;
        long firstPiece = -1;
        long dim = -1;
        long plateOfTheList = -1;
        long lastMark = -1;
        long resetButton = -1;
        long plateOfThePanel = -1;
        long firstChip = -1;
        for (size_t i = 0; i < calls.size(); ++i) {
            const DrawCall& call = calls[i];
            const long at = static_cast<long>(i);
            if (call.kind == DrawKind::Rect && (SameColor(call.color, kLightSquareColor) || SameColor(call.color, kDarkSquareColor))) {
                lastSquare = at;
            } else if (call.kind == DrawKind::Texture && call.texture == king.Get()) {
                firstPiece = firstPiece < 0 ? at : firstPiece;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kBoardDimColor)) {
                dim = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kMovePanelPlateColor)) {
                plateOfTheList = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kMovePanelMarkColor)) {
                lastMark = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kResetColor)) {
                resetButton = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kPlateColor)) {
                plateOfThePanel = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kMaterialChipColor)) {
                firstChip = firstChip < 0 ? at : firstChip;
            }
        }
        ASSERT_GE(lastSquare, 0);
        ASSERT_GE(firstPiece, 0) << "the king is drawn";
        ASSERT_GE(dim, 0) << "the game is over: the board is dimmed";
        ASSERT_GE(plateOfTheList, 0) << "the box is drawn";
        ASSERT_EQ(MaterialPlace(), "panel");
        ASSERT_GE(firstChip, 0) << "most of the pieces are gone: the material is drawn in the bands of the box";
        EXPECT_LT(plateOfTheList, firstChip) << "the chips of the material are drawn on the plate, not under it";
        ASSERT_GE(lastMark, 0) << "the move of the mate is marked";
        ASSERT_GE(resetButton, 0);
        ASSERT_GE(plateOfThePanel, 0) << "the opponent panel is open";
        EXPECT_LT(lastSquare, plateOfTheList) << "over the squares";
        EXPECT_LT(firstPiece, plateOfTheList) << "over the pieces";
        EXPECT_LT(dim, plateOfTheList) << "and over the dim of the finished game";
        EXPECT_LT(plateOfTheList, lastMark) << "the mark is on the plate";
        EXPECT_LT(lastMark, resetButton) << "under the HUD";
        EXPECT_LT(lastMark, plateOfThePanel) << "and under the panels";
    }

    TEST_F(ChessUiTest, TheMovePanelTakesNoClickAndTheBoardAndResetStillGetTheirOwn) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        Settle();
        const auto canvas = MovePanel();
        ASSERT_TRUE(canvas);
        ASSERT_TRUE(MovePanelElement("MovePanelBox").active.visible);
        for (const UIElement* element : AllElements(canvas->GetRoot())) {
            EXPECT_FALSE(element->blocksInput) << element->name.CStr();
            EXPECT_FALSE(element->IsInteractive()) << element->name.CStr();
        }
        // a hit test of the canvas finds nothing at the middle of any element, and a press on the box, a band and the rows is not taken by the UI
        for (const UIElement* element : AllElements(canvas->GetRoot())) {
            const Rect& rect = element->rect;
            EXPECT_EQ(canvas->GetLayout().HitTest(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f), nullptr) << element->name.CStr();
        }
        for (const char* name : {"MovePanelBox", "MovePanelBandTopPlayer", "MovePanelRow0", "MovePanelWhite0", "MovePanelBlack0"}) {
            const auto [x, y] = ScreenCenter(MovePanelElement(name));
            EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true))) << name;
            EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false))) << name;
        }

        // the board right next to the box takes a press, and Reset above it takes a click
        EXPECT_FALSE(Model().HasSelection());
        Click("h2");
        ASSERT_TRUE(Model().HasSelection()) << "the file h of the board is the one next to the box";
        EXPECT_EQ(Model().GetSelected(), Sq("h2"));
        ASSERT_TRUE(ClickButton(kResetColor)) << "Reset is at the right end above the box";
        Settle();
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(ShownMovePanelRows(), "");
    }

    TEST_F(ChessUiTest, TheShownMovePanelLiesInTheSafeAreaAndClearOfTheHudAndTheBoardOnTheSizeMatrix) {
        Draw();
        auto panel = MovePanel();
        auto hud = Hud();
        ASSERT_TRUE(panel);
        ASSERT_TRUE(hud);
        panel->UpdateLayout(1280.0f, 720.0f);
        hud->UpdateLayout(1280.0f, 720.0f);
        ShowEveryMovePanelRow(*panel);
        PublishMovePanelRound(0);

        size_t shown = 0;
        size_t hidden = 0;
        String report;
        for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
            const UILayoutContext context = ContextOf(matrixCase, true);
            const bool isShown = LayOutAsTheComponentShowsIt(*panel, context);
            hud->GetLayout().Update(context);
            const UICanvasLayout& layout = panel->GetLayout();
            const UIElement* box = layout.root->FindByName(PoolString::Intern("MovePanelBox"));
            ASSERT_NE(box, nullptr);
            if (!isShown) {
                ++hidden;
                EXPECT_TRUE(box->rect.IsEmpty()) << matrixCase.label.c_str() << ": a hidden box takes no room";
                continue;
            }
            ++shown;
            if (!LaidOutForWide(layout)) {
                report += Format("{}: the box is shown in a view that is not Wide\n", matrixCase.label.c_str());
            }
            if (!LiesIn(box->rect, layout.GetResolved().safeBounds)) {
                report += Format("{}: the box is not inside the safe area\n", matrixCase.label.c_str());
            }
            if (box->rect.x < kBoardLeft + kBoardUnits) {
                report += Format("{}: the box reaches the board\n", matrixCase.label.c_str());
            }
            // The insets move the HUD, not the box: a notch pushes the top row (Menu, Reset) down onto the top band, the home bar of the side insets pushes Status up onto the band of the
            // buttons. The 8 rows are clear of the HUD in every case
            const UIElement* list = layout.root->FindByName(PoolString::Intern("MovePanelList"));
            ASSERT_NE(list, nullptr);
            String touching;
            for (const UIElement* control : AllElements(*hud->GetLayout().root)) {
                if (TextOf(*control) == nullptr || control->rect.IsEmpty()) {
                    continue;
                }
                if (Touch(list->rect, control->rect, kMinGap)) {
                    report += Format("{}: the rows and {} are closer than {:.0f} units\n", matrixCase.label.c_str(), control->name.ToStringView(), kMinGap);
                }
                if (Touch(box->rect, control->rect, kMinGap)) {
                    touching += Format(" {}", control->name.ToStringView());
                }
            }
            const bool notch = matrixCase.label.find("notch") != String::npos;
            const bool sideInsets = matrixCase.label.find("side insets") != String::npos;
            const char* expected = notch ? " Menu Reset" : sideInsets ? " Status" : "";
            if (touching != expected) {
                report += Format("{}: the HUD controls closer to the box than {:.0f} units are '{}', not '{}'\n", matrixCase.label.c_str(), kMinGap, touching.c_str(), expected);
            }
            // Per band: a notch reaches the top band only, the home bar of side insets the band of the buttons only; the band of the other player's material is clear in every window
            const char* expectedPerBand[] = {notch ? " Menu Reset" : "", "", sideInsets ? " Status" : ""};
            for (size_t band = 0; band < std::size(kPanelBands); ++band) {
                const UIElement* element = layout.root->FindByName(PoolString::Intern(kPanelBands[band]));
                ASSERT_NE(element, nullptr);
                String near;
                for (const UIElement* control : AllElements(*hud->GetLayout().root)) {
                    if (TextOf(*control) != nullptr && !control->rect.IsEmpty() && Touch(element->rect, control->rect, kMinGap)) {
                        near += Format(" {}", control->name.ToStringView());
                    }
                }
                if (near != expectedPerBand[band]) {
                    report +=
                        Format("{}: the HUD controls closer to {} than {:.0f} units are '{}', not '{}'\n", matrixCase.label.c_str(), kPanelBands[band], kMinGap, near.c_str(), expectedPerBand[band]);
                }
            }
        }
        panel->SetElementVisible(PoolString::Intern("MovePanelBox"), false);
        panel->GetLayout().Invalidate();
        hud->GetLayout().Invalidate();
        EXPECT_TRUE(report.empty()) << report.c_str();
        EXPECT_GT(shown, 0u);
        EXPECT_GT(hidden, 0u) << "the default and the Portrait view never show it";
    }

    TEST_F(ChessUiTest, TheMovePanelIsShownOnlyInTheWideViewAndOnlyWhereTheSideInsetsLeaveItsBox) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4"}));
        Settle();
        ASSERT_TRUE(MovePanelElement("MovePanelBox").active.visible) << "a Wide window with no insets";

        // 700 x 480 is the narrowest Wide window with side insets: they take more of each side of the view than the 40 units the box keeps from the edge
        ResizeWithSideInsets(kWideNarrowestWidth, kNarrowestHeight);
        EXPECT_FLOAT_EQ(GetScene().GetViewWidth(), kWideViewWidth);
        const Rect cut = MovePanel()->GetLayout().GetResolved().safeBounds;
        EXPECT_NEAR(cut.x, kViewShapes[2].NarrowestSafeLeft(), 0.2f);
        EXPECT_GT(cut.x, kWideViewWidth - kMovePanelRight);
        EXPECT_FALSE(MovePanelElement("MovePanelBox").active.visible);
        EXPECT_TRUE(MovePanelElement("MovePanelBox").rect.IsEmpty());
        EXPECT_EQ(MovePanelWord(0, "White"), "e4") << "the words are not taken away, only the box";

        // 3:2 and 16:10 at 480 px keep the box: the letterbox of the view is as wide as the inset, or the inset is less than the 40 units
        for (const Pair<float, float>& output : {Pair<float, float>{720.0f, 480.0f}, Pair<float, float>{768.0f, 480.0f}}) {
            SCOPED_TRACE(Format("output {}x{}", output.first, output.second).c_str());
            ResizeWithSideInsets(output.first, output.second);
            EXPECT_FLOAT_EQ(GetScene().GetViewWidth(), kWideViewWidth);
            const UIElement& box = MovePanelElement("MovePanelBox");
            EXPECT_TRUE(box.active.visible);
            EXPECT_TRUE(LiesIn(box.rect, MovePanel()->GetLayout().GetResolved().safeBounds));
        }

        ResizeWithSideInsets(kWideNarrowestWidth, kNarrowestHeight);
        ASSERT_FALSE(MovePanelElement("MovePanelBox").active.visible);
        ASSERT_TRUE(PlayAll(Model(), {"e7e5"}));
        Settle();
        EXPECT_FALSE(MovePanelElement("MovePanelBox").active.visible) << "it stays hidden when the game changes";
        ResizeWithoutInsets(1280.0f, 720.0f);
        EXPECT_TRUE(MovePanelElement("MovePanelBox").active.visible) << "and comes back when the window has room";
        EXPECT_EQ(ShownMovePanelRows(), "1. e4 [e5]");
    }

    TEST_F(ChessUiTest, TheMovePanelIsHiddenUntilTheCanvasHasBeenLaidOutOnce) {
        // Before the first layout the safe area is not known and the box waits: nothing flashes. The first Draw() of a window that switches the view lays the canvas out in its second frame
        ASSERT_TRUE(MovePanel());
        EXPECT_FALSE(MovePanel()->GetLayout().HasLayout());
        EXPECT_FALSE(MovePanelElement("MovePanelBox").style.visible);
        Settle();
        EXPECT_TRUE(MovePanel()->GetLayout().HasLayout());
        EXPECT_TRUE(MovePanelElement("MovePanelBox").active.visible) << "a Wide window has room once the layout is known";
    }

    TEST_F(ChessUiTest, TheMovePanelStartsWithNoRow) {
        Settle();
        EXPECT_EQ(ShownMovePanelRows(), "");
        for (size_t row = 0; row < kMovePanelRows; ++row) {
            EXPECT_FALSE(MovePanelElement(Format("MovePanelRow{}", row).c_str()).active.visible) << row;
            for (const char* side : kSides) {
                EXPECT_FALSE(MovePanelElement(Format("MovePanel{}Mark{}", side, row).c_str()).active.visible) << side << row;
            }
        }
    }

    TEST_F(ChessUiTest, TheMovePanelShowsTheRowsOfTheGameAndMarksTheLastMoveOnly) {
        Settle();
        ASSERT_TRUE(PlayAll(Model(), {"e2e4"}));
        EXPECT_EQ(ShownMovePanelRows(), "1. [e4]") << "a row with White's move only";
        ASSERT_TRUE(PlayAll(Model(), {"e7e5"}));
        EXPECT_EQ(ShownMovePanelRows(), "1. e4 [e5]") << "after a move of Black the mark is on Black's cell";
        ASSERT_TRUE(PlayAll(Model(), {"g1f3"}));
        EXPECT_EQ(ShownMovePanelRows(), "1. e4 e5 | 2. [Nf3]");
        EXPECT_FALSE(MovePanelElement("MovePanelWhiteMark0").active.visible);
        EXPECT_FALSE(MovePanelElement("MovePanelBlackMark0").active.visible);
        EXPECT_FALSE(MovePanelElement("MovePanelBlackMark1").active.visible);
    }

    TEST_F(ChessUiTest, TheMovePanelShowsACaptureACastlingAPromotionACheckAndAMate) {
        Settle();
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "d7d5", "e4d5"}));
        EXPECT_EQ(MovePanelWord(1, "White"), "exd5");

        Model().Reset();
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6", "e1g1"}));
        EXPECT_EQ(ShownMovePanelRows(), "1. e4 e5 | 2. Nf3 Nc6 | 3. Bc4 Nf6 | 4. [O-O]");

        Model().SetPosition(PositionFromFen(kPawnOnTheSeventh));
        ASSERT_TRUE(PlayAll(Model(), {"e7e8q"}));
        EXPECT_EQ(MovePanelWord(0, "White"), "e8=Q+") << "a promotion that gives check";

        Model().Reset();
        ASSERT_TRUE(PlayAll(Model(), {"f2f3", "e7e5", "g2g4", "d8h4"}));
        ASSERT_TRUE(Model().IsGameOver());
        EXPECT_EQ(ShownMovePanelRows(), "1. f3 e5 | 2. g4 [Qh4#]");
    }

    TEST_F(ChessUiTest, AGameOfTwentyRowsShowsTheLastEightOfThem) {
        Settle();
        ASSERT_TRUE(PlayAQuietGame(Model(), 2 * kLongGameRows));
        const Vector<MoveListRow> list = BuildMoveList(Model().GetStartFen(), Model().GetMoves());
        ASSERT_EQ(list.size(), kLongGameRows);
        Draw();
        for (size_t row = 0; row < kMovePanelRows; ++row) {
            const MoveListRow& expected = list[kLongGameRows - kMovePanelRows + row];
            ASSERT_FALSE(expected.white.san.empty() || expected.black.san.empty());
            EXPECT_TRUE(MovePanelElement(Format("MovePanelRow{}", row).c_str()).active.visible) << row;
            EXPECT_EQ(ShownText(MovePanelElement(Format("MovePanelNumber{}", row).c_str())), Format("{}.", 13 + row)) << "the numbers are those of the game, not of the window";
            EXPECT_EQ(MovePanelWord(row, "White"), expected.white.san) << row;
            EXPECT_EQ(MovePanelWord(row, "Black"), expected.black.san) << row;
        }
        EXPECT_TRUE(MovePanelElement("MovePanelBlackMark7").active.visible) << "the last move of the game";
        EXPECT_FALSE(MovePanelElement("MovePanelWhiteMark7").active.visible);
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelNumber0")), "13.");
        EXPECT_EQ(ShownText(MovePanelElement("MovePanelNumber7")), "20.");
    }

    TEST_F(ChessUiTest, AResetASetStateAndATakebackUpdateTheRowsAndTheMarkInTheSameFrame) {
        Settle();
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5", "g1f3"}));
        ASSERT_EQ(ShownMovePanelRows(), "1. e4 e5 | 2. [Nf3]");

        EXPECT_EQ(ChessGame::GetActiveModel()->TakeBack(1), 1u);
        EXPECT_EQ(ShownMovePanelRows(), "1. e4 [e5]");
        EXPECT_EQ(ChessGame::GetActiveModel()->TakeBack(2), 2u);
        EXPECT_EQ(ShownMovePanelRows(), "");

        ASSERT_TRUE(PlayAll(Model(), {"d2d4", "d7d5"}));
        ASSERT_EQ(ShownMovePanelRows(), "1. d4 [d5]");
        Model().Reset();
        EXPECT_EQ(ShownMovePanelRows(), "");
        EXPECT_FALSE(MovePanelElement("MovePanelBlackMark0").active.visible);

        ASSERT_TRUE(PlayAll(Model(), {"e2e4"}));
        ASSERT_TRUE(FenSetsTheState(kBlackToMoveFen)) << "set_state of the control API: the list restarts from that position";
        EXPECT_EQ(ShownMovePanelRows(), "");
        ASSERT_TRUE(PlayAll(Model(), {"e7e5"}));
        EXPECT_EQ(ShownMovePanelRows(), "1. ... [e5]") << "a game that begins with Black to move has no White move in its first row";
        EXPECT_FALSE(MovePanelElement("MovePanelWhiteMark0").active.visible) << "the dots are never the current move";
    }

    TEST_F(ChessUiTest, AMoveOfTheProgramIsSeenByTheMovePanelInTheSameFrameItIsPlayed) {
        StartFrom(kBlackCanTakeAPawn);
        Settle();
        ASSERT_EQ(ShownMovePanelRows(), "");
        const auto take = MoveGenerator::ParseUci(Model().GetPosition(), "d5e4");
        ASSERT_TRUE(take.has_value());
        const auto engine = New<GatedEngine>(true, *take);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 1; })) << "the program took the pawn";
        EXPECT_EQ(ShownMovePanelRows(), "2. ... [dxe4]");
    }

    TEST_F(ChessUiTest, WithNoGameActiveTheMovePanelHasNoRowShown) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        ASSERT_EQ(ShownMovePanelRows(), "1. e4 [e5]");

        GameManager::GetInstance().Shutdown();
        ASSERT_EQ(ChessGame::GetActive(), nullptr);
        Draw();
        Draw();
        EXPECT_EQ(ShownMovePanelRows(), "");
        EXPECT_EQ(MovePanelWord(0, "White"), "");
        EXPECT_EQ(MovePanelWord(0, "Black"), "");
        EXPECT_FALSE(MovePanelElement("MovePanelBlackMark0").active.visible);
    }

    TEST_F(ChessUiTest, ABrokenCopyOfTheMovePanelCanvasIsCaughtByTheChecks) {
        Draw();
        const String shipped = ShownMovePanelXml();
        ASSERT_FALSE(shipped.empty());
        const auto problemsOf = [&](const String& xml) {
            CanvasCopy copy(xml, false);
            String report;
            size_t reported = 0;
            PublishMovePanelRound(0);
            OnEveryCaseOfTheMatrix(
                copy.layout,
                [&](const String& label, const UICanvasLayout& laidOut) {
                    const String problems = MovePanelProblems(laidOut);
                    if (!problems.empty() && reported++ < kReportedCases) {
                        report += Format("{}:\n{}", label.c_str(), problems.c_str());
                    }
                },
                true);
            return report;
        };

        EXPECT_EQ(problemsOf(shipped), "") << "the same checks pass on the shipped canvas";

        String noMark = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(noMark, R"(name="MovePanelBlackMark3")", R"(name="MovePanelBlackMarkX3")"));
        EXPECT_NE(problemsOf(noMark).find("MovePanelBlackMark3 is missing"), String::npos) << "a row without its mark";

        String unprefixed = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(unprefixed, R"(name="MovePanelBandButtons")", R"(name="BandButtons")"));
        EXPECT_NE(problemsOf(unprefixed).find("BandButtons does not start with MovePanel"), String::npos) << "elements are found by name in every canvas";

        String swapped = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(swapped, R"(name="MovePanelBandBottomPlayer")", R"(name="MovePanelBandSwap")"));
        ASSERT_NO_FATAL_FAILURE(Replace(swapped, R"(name="MovePanelBandButtons")", R"(name="MovePanelBandBottomPlayer")"));
        ASSERT_NO_FATAL_FAILURE(Replace(swapped, R"(name="MovePanelBandSwap")", R"(name="MovePanelBandButtons")"));
        EXPECT_NE(problemsOf(swapped).find("MovePanelBandBottomPlayer is not the band where the panel set of Material draws"), String::npos) << "the bands under the list in the other order";

        String bigFont = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(bigFont, R"(name="MovePanelWhiteText0" anchor="Stretch" fontSize="24")", R"(name="MovePanelWhiteText0" anchor="Stretch" fontSize="90")"));
        EXPECT_NE(problemsOf(bigFont).find("MovePanelWhiteText0 cuts"), String::npos) << "a font of 90 does not fit the cell";

        String lowRow = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(lowRow, R"(name="MovePanelRow0" height="56")", R"(name="MovePanelRow0" height="40")"));
        EXPECT_NE(problemsOf(lowRow).find("MovePanelRow0 is 40.0 units high"), String::npos);

        String moved = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(moved, R"(name="MovePanelBox" x="760" y="80")", R"(name="MovePanelBox" x="700" y="80")"));
        EXPECT_NE(problemsOf(moved).find("is not the box of Game/MovePanel.h"), String::npos);
    }

}  // namespace BECore::Chess::Tests
