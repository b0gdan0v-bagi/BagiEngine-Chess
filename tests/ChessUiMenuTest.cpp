#include <tests/ChessUiHarness.h>

/// The list of the menu: its layout, its table, navigation by real clicks and broken copies of it. The scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    // ---- the list of the menu: the layout ------------------------------------------------------------------------------------------
    // The list is the node MenuPanel (a modal canvas, hidden at the start), built like the promotion panel: the same checks as for the opponent panel, with the plate MenuPlate. The buttons
    // are the sections of the table (Game/MenuSections.h) and Close, so the checks walk the table and a new section is covered without an edit here

    namespace {

        constexpr const char* kMenuPlate = "MenuPlate";
        constexpr const char* kMenuClose = "MenuClose";
        constexpr const char* kSectionButtonPrefix = "MenuSection";

        /// The name of the button of a section in the scene: its id with a capital, MenuSectionOpponent (the word on the button is the row's label, "Resign / Draw" for MenuSectionGame)
        String MenuSectionButton(const MenuSection& section) {
            String id(section.id);
            id[0] = static_cast<char>(id[0] - 'a' + 'A');
            return Format("{}{}", kSectionButtonPrefix, id);
        }

        /// The buttons of the list in the order of the scene: one per row of the table, then Close
        Vector<String> MenuButtons() {
            Vector<String> names;
            for (const MenuSection& section : MenuSections()) {
                names.push_back(MenuSectionButton(section));
            }
            names.push_back(kMenuClose);
            return names;
        }

        /// The height of MenuPlate by the comment of the scene: 16 + 44 (title) + 56 per section + 56 (Close) + 12 per gap + 16, with one gap more than the stack has
        float MenuPlateHeight() {
            const float sections = static_cast<float>(MenuSections().size());
            return 16.0f + 44.0f + 56.0f * sections + 56.0f + 12.0f * (sections + 2.0f) + 16.0f;
        }

    }  // namespace

    TEST_F(ChessUiTest, TheMenuPanelIsHiddenAtTheStartAndModal) {
        auto panel = MenuList();
        ASSERT_TRUE(panel) << "the shipped scene has the node MenuPanel";
        EXPECT_FALSE(panel->_visible);
        EXPECT_TRUE(panel->_modal);
        Draw();
        EXPECT_TRUE(RectCalls(kPlateColor).empty());
    }

    TEST_F(ChessUiTest, TheMenuPanelPassesTheSizeMatrix) {
        auto panel = MenuList();
        ASSERT_TRUE(panel);
        panel->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        const auto failures = ValidateWithWords(panel->GetLayout());
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(ChessUiTest, NoWordOfTheMenuPanelIsCutOnAnyCaseOfTheMatrix) {
        auto panel = MenuList();
        ASSERT_TRUE(panel);
        panel->UpdateLayout(1280.0f, 720.0f);
        const String problems = PanelProblemsOnMatrix(panel->GetLayout(), kReportedCases, kMenuPlate);
        EXPECT_TRUE(problems.empty()) << problems.c_str();
    }

    TEST_F(ChessUiTest, NoWordOfTheMenuPanelIsCutInTheNarrowestWindowAndEveryButtonIsATouchTarget) {
        auto panel = MenuList();
        ASSERT_TRUE(panel);
        panel->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = panel->GetLayout();
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        const Vector<String> buttons = MenuButtons();
        ASSERT_EQ(buttons.size(), MenuSections().size() + 1) << "a button per section and Close";

        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            LayOutInTheNarrowestWindow(layout, shape);

            const float scale = shape.NarrowestScale();
            EXPECT_NEAR(layout.GetResolved().scale, scale, 0.01f);
            const String problems = PanelProblems(layout, kMenuPlate);
            EXPECT_TRUE(problems.empty()) << problems.c_str();
            EXPECT_NEAR(ElementOf("MenuPanel", kMenuPlate).rect.w, std::min(640.0f, 0.94f * layout.root->rect.w), kPixelTolerance)
                << "94% of the safe area up to maxWidth: 640 in the default view, about 555 in the narrowest Portrait window";

            for (const String& name : buttons) {
                const UIElement& button = MenuButton(name.c_str());
                EXPECT_NEAR(button.rect.h, kSegmentHeight, kPixelTolerance) << name.c_str();
                EXPECT_GE(std::min(button.rect.w, button.rect.h) * layout.GetResolved().scale, minTouch - kPixelTolerance) << name.c_str() << " is a touch target on the smallest window";
                const UIContentFit fit = button.CheckContent(button.rect.w - 2.0f * kButtonAir, button.rect.h, layout.GetResolved().scale);
                EXPECT_TRUE(fit.hasContent && fit.fits) << name.c_str() << " '" << ShownText(button).c_str() << "' in " << button.rect.w << " units";
            }
        }
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, TheMenuPlateIsInsideTheViewCenteredAndLaidOutAsTheCommentOfTheSceneSays) {
        ASSERT_TRUE(ClickButton(kMenuColor));
        ASSERT_TRUE(MenuList()->_visible);

        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}, Pair<float, float>{1024.0f, 768.0f}, Pair<float, float>{800.0f, 800.0f},
                                 Pair<float, float>{2560.0f, 1080.0f}, Pair<float, float>{480.0f, 853.0f}, Pair<float, float>{768.0f, 1024.0f}}) {
            SCOPED_TRACE(Format("{}x{}", size.first, size.second).c_str());
            DrawAt(size.first, size.second);
            const UIElement& plate = ElementOf("MenuPanel", kMenuPlate);
            ASSERT_FALSE(plate.rect.IsEmpty()) << "the plate is shown";
            EXPECT_GE(plate.rect.x, -kPixelTolerance);
            EXPECT_GE(plate.rect.y, -kPixelTolerance);
            EXPECT_LE(plate.rect.x + plate.rect.w, ViewWidth() + kPixelTolerance);
            EXPECT_LE(plate.rect.y + plate.rect.h, ViewHeight() + kPixelTolerance);
            EXPECT_NEAR(plate.rect.x + plate.rect.w * 0.5f, ViewWidth() * 0.5f, 0.5f) << "the plate is centered in the view";
            EXPECT_LE(plate.rect.w, 640.0f + kPixelTolerance);
            EXPECT_NEAR(plate.rect.h, MenuPlateHeight(), kPixelTolerance) << "16 + 44 + 56 per section + 56 + 12 per gap + 16: 428 with four sections";

            // The title, then the sections in the order of the table, then Close: one column, 12 units apart, 16 inside the plate
            const UIElement* above = &ElementOf("MenuPanel", "MenuTitle");
            EXPECT_NEAR(above->rect.y, plate.rect.y + 16.0f, kPixelTolerance);
            for (const String& name : MenuButtons()) {
                const UIElement& button = MenuButton(name.c_str());
                EXPECT_NEAR(button.rect.y, above->rect.y + above->rect.h + 12.0f, kPixelTolerance) << name.c_str();
                EXPECT_NEAR(button.rect.x, plate.rect.x + 16.0f, kPixelTolerance) << name.c_str();
                EXPECT_NEAR(button.rect.w, plate.rect.w - 32.0f, kPixelTolerance) << name.c_str() << ": stretched across the plate";
                above = &button;
            }
            EXPECT_LE(above->rect.y + above->rect.h, plate.rect.y + plate.rect.h - 16.0f + kPixelTolerance) << "Close keeps the padding of the plate";
        }
    }

    // ---- the list of the menu: the table and the scene agree -------------------------------------------------------------------------------

    TEST_F(ChessUiTest, EveryRowOfTheTableHasItsButtonAndItsCanvasAndEveryButtonHasARow) {
        size_t rows = 0;
        for (const MenuSection& section : MenuSections()) {
            ++rows;
            SCOPED_TRACE(String(section.id).c_str());
            const String name = MenuSectionButton(section);
            const UIElement& button = MenuButton(name.c_str());
            EXPECT_EQ(ShownText(button), String(section.label)) << "the word on the button is the one of the table";
            EXPECT_EQ(ClickerOf(button)->action, PoolString::Intern("ChessOpenSection"));
            EXPECT_EQ(ClickerOf(button)->value, PoolString::Intern(section.id)) << "the id of the table is the value";

            const auto canvas = CanvasOf(String(section.node).c_str());
            ASSERT_TRUE(canvas) << "the node of the row has a canvas";
            EXPECT_FALSE(canvas->_visible) << "a section is hidden at the start";
            EXPECT_TRUE(canvas->_modal);
        }
        EXPECT_EQ(rows, MenuSections().size());

        // The other way: a button called MenuSection<something> that has no row is a button without a feature
        size_t buttons = 0;
        for (const UIElement* element : AllElements(MenuList()->GetRoot())) {
            const StringView name = element->name.ToStringView();
            if (!name.starts_with(kSectionButtonPrefix)) {
                continue;
            }
            ++buttons;
            bool found = false;
            for (const MenuSection& section : MenuSections()) {
                found = found || MenuSectionButton(section) == String(name);
            }
            EXPECT_TRUE(found) << String(name).c_str() << " has no row in MenuSections()";
        }
        EXPECT_EQ(buttons, MenuSections().size()) << "one button per row";
    }

    TEST_F(ChessUiTest, TheCloseAndBackButtonsEmitTheActionsOfTheMenu) {
        EXPECT_EQ(ClickerOf(MenuButton(kMenuClose))->action, PoolString::Intern("ChessCloseMenu"));
        EXPECT_EQ(ShownText(MenuButton(kMenuClose)), "Close");
        EXPECT_EQ(ClickerOf(PanelButton("OpponentBack"))->action, PoolString::Intern("ChessOpenMenu"));
        EXPECT_EQ(ShownText(PanelButton("OpponentBack")), "Back");
        EXPECT_EQ(Panel()->GetRoot().FindByName(PoolString::Intern("OpponentClose")), nullptr) << "Back is the one button of the section: its dim closes all";
        EXPECT_EQ(ClickerOf(PanelElement("Dim"))->action, PoolString::Intern("ChessCloseMenu"));
        EXPECT_EQ(ClickerOf(*HudElement("Menu"))->action, PoolString::Intern("ChessOpenMenu"));
    }

    TEST_F(ChessUiTest, TheNamesOfTheMenuPanelAreItsOwnAndTheHudMenuButtonIsNotInIt) {
        Vector<PoolString> names;
        CollectNames(MenuList()->GetRoot(), names);
        ASSERT_FALSE(names.empty());
        for (size_t i = 0; i < names.size(); ++i) {
            EXPECT_TRUE(names[i].ToStringView().starts_with("Menu")) << names[i].CStr() << ": elements are found by name in every canvas, so the list keeps the prefix";
            EXPECT_EQ(eastl::count(names.begin(), names.end(), names[i]), 1) << names[i].CStr() << " is twice in the list";
        }
        EXPECT_EQ(MenuList()->GetRoot().FindByName(PoolString::Intern("Menu")), nullptr) << "the name Menu is the HUD's alone";
        EXPECT_NE(HudElement("Menu"), nullptr);
        for (const char* node : {"Hud", "BoardCoordinates", "Material", "OpponentPanel", "PromotionPanel"}) {
            const auto canvas = CanvasOf(node);
            ASSERT_TRUE(canvas) << node;
            for (const PoolString& name : names) {
                EXPECT_EQ(canvas->GetRoot().FindByName(name), nullptr) << name.CStr() << " is also in " << node;
            }
        }
    }

    // ---- the list of the menu: navigation by real clicks -----------------------------------------------------------------------------------

    TEST_F(ChessUiTest, TheListIsShownOnlyAfterMenuAndEveryWayOutOfTheSectionIsBackOrTheDim) {
        ActionLog log;
        EXPECT_FALSE(MenuList()->_visible);
        EXPECT_FALSE(Panel()->_visible);
        ASSERT_TRUE(ClickButton(kMenuColor));
        EXPECT_TRUE(MenuList()->_visible);
        EXPECT_FALSE(Panel()->_visible) << "the list does not open a section by itself";

        ASSERT_TRUE(ClickMenuList("MenuSectionOpponent").Both());
        EXPECT_FALSE(MenuList()->_visible);
        EXPECT_TRUE(Panel()->_visible);
        const auto dim = DimCorner("OpponentPanel", "Dim");
        ASSERT_TRUE(ClickAtPixel(dim.first, dim.second).Both());
        EXPECT_FALSE(MenuList()->_visible);
        EXPECT_FALSE(Panel()->_visible);

        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("OpponentBack").Both());
        EXPECT_TRUE(MenuList()->_visible);
        EXPECT_FALSE(Panel()->_visible);
        ASSERT_TRUE(ClickMenuList("MenuClose").Both());
        EXPECT_FALSE(MenuList()->_visible);
        EXPECT_FALSE(Panel()->_visible);
        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessCloseMenu"));
    }

    TEST_F(ChessUiTest, WhileTheListIsShownThePlateTakesAPressAndTheDimClosesItAndNothingUnderTheDimSeesOne) {
        ASSERT_TRUE(ClickButton(kMenuColor));
        ASSERT_TRUE(MenuList()->_visible);
        Draw();
        ASSERT_TRUE(Play(Model(), "e2e4"));  // something for Reset to undo
        const uint64_t afterMove = Model().GetVersion();
        Draw();
        ActionLog log;

        const auto title = ScreenCenter(ElementOf("MenuPanel", "MenuTitle"));
        EXPECT_TRUE(ClickAtPixel(title.first, title.second).Both());
        EXPECT_EQ(log.Total(), 0u) << "the title is no button, and the press did not reach the dim";
        EXPECT_TRUE(MenuList()->_visible);

        // The dim over the board (a1, h8), over the Reset button and over the Menu button of the HUD: each press closes the list and nothing else gets it
        const UIElement& plate = ElementOf("MenuPanel", "MenuPlate");
        const Pair<float, float> points[] = {SquareCenter("a1"), SquareCenter("h8"), ScreenCenter(*HudElement("Reset")), ScreenCenter(*HudElement("Menu"))};
        for (const auto& point : points) {
            SCOPED_TRACE(Format("{},{}", point.first, point.second).c_str());
            ASSERT_FALSE(Covers(plate, point)) << "a point of the dim, not of the plate";
            const size_t before = log.Total();
            EXPECT_TRUE(ClickAtPixel(point.first, point.second).Both());
            EXPECT_EQ(log.Total(), before + 1) << "one action: the dim's";
            EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessCloseMenu"));
            EXPECT_FALSE(MenuList()->_visible);
            EXPECT_FALSE(Panel()->_visible);
            EXPECT_FALSE(Model().HasSelection()) << "no click reached the board";
            EXPECT_EQ(Model().GetVersion(), afterMove) << "Reset under the dim was not pressed";

            ASSERT_TRUE(ClickButton(kMenuColor)) << "the Menu button works again once the list is closed";
            ASSERT_TRUE(MenuList()->_visible);
            Draw();
        }
    }

    TEST_F(ChessUiTest, AClickOnTheDimOfTheListOrOfTheOpponentPanelClosesBothAndNeverAClickOnThePlate) {
        ActionLog log;
        ASSERT_TRUE(ClickButton(kMenuColor));
        ASSERT_TRUE(MenuList()->_visible);
        const auto menuTitle = ScreenCenter(ElementOf("MenuPanel", "MenuTitle"));
        EXPECT_TRUE(ClickAtPixel(menuTitle.first, menuTitle.second).Both());
        EXPECT_TRUE(MenuList()->_visible) << "a click on the plate closes nothing";

        const size_t before = log.Total();
        const auto menuDim = DimCorner("MenuPanel", "MenuDim");
        ASSERT_FALSE(Covers(ElementOf("MenuPanel", "MenuPlate"), menuDim));
        EXPECT_TRUE(ClickAtPixel(menuDim.first, menuDim.second).Both());
        EXPECT_EQ(log.Total(), before + 1);
        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessCloseMenu"));
        EXPECT_FALSE(MenuList()->_visible);
        EXPECT_FALSE(Panel()->_visible);

        const auto e2 = SquareCenter("e2");
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(e2.first, e2.second, true))) << "the board takes clicks again";
        EXPECT_TRUE(Model().HasSelection());
        Release(e2.first, e2.second);

        ASSERT_TRUE(OpenThePanel());
        const auto title = ScreenCenter(PanelElement("Title"));
        const size_t opened = log.Total();
        EXPECT_TRUE(ClickAtPixel(title.first, title.second).Both());
        EXPECT_EQ(log.Total(), opened);
        EXPECT_TRUE(Panel()->_visible) << "a click on the plate closes nothing";

        const auto panelDim = DimCorner("OpponentPanel", "Dim");
        ASSERT_FALSE(Covers(PanelElement("Plate"), panelDim));
        EXPECT_TRUE(ClickAtPixel(panelDim.first, panelDim.second).Both());
        EXPECT_EQ(log.Total(), opened + 1);
        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessCloseMenu"));
        EXPECT_FALSE(Panel()->_visible);
        EXPECT_FALSE(MenuList()->_visible) << "the list is not shown behind it";
    }

    TEST_F(ChessUiTest, AnUnknownSectionChangesNothingAndAKnownOneIsTheTablesWord) {
        ASSERT_TRUE(ClickButton(kMenuColor));
        ASSERT_TRUE(MenuList()->_visible);
        for (const char* id : {"puzzles", "", "Opponent", "OPPONENT"}) {
            SCOPED_TRACE(id);
            PressWith("ChessOpenSection", id);
            EXPECT_TRUE(MenuList()->_visible) << "the list stays";
            EXPECT_FALSE(Panel()->_visible);
        }
        Press("ChessOpenSection");  // no value at all
        EXPECT_TRUE(MenuList()->_visible);
        EXPECT_FALSE(Panel()->_visible);

        PressWith("ChessOpenSection", "opponent");
        EXPECT_FALSE(MenuList()->_visible);
        EXPECT_TRUE(Panel()->_visible);
        PressWith("ChessOpenSection", "puzzles");
        EXPECT_TRUE(Panel()->_visible) << "an unknown id does not close the shown section either";
    }

    TEST_F(ChessUiTest, ALevelPickedInTheOpponentPanelSurvivesBackAndASecondOpponent) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("LevelMax").Both());
        const auto picked = Opponent().GetLevel();
        EXPECT_EQ(picked, *FindOpponentLevel("max"));

        ASSERT_TRUE(ClickPanel("OpponentBack").Both());
        ASSERT_TRUE(MenuList()->_visible);
        ASSERT_TRUE(ClickMenuList("MenuSectionOpponent").Both());
        ASSERT_TRUE(Panel()->_visible);
        Draw();
        EXPECT_EQ(Opponent().GetLevel(), picked);
        EXPECT_EQ(Var("opponentLevel"), "max");
        EXPECT_TRUE(PanelButton("LevelMax").selected) << "the panel shows the pick again";
    }

    // ---- the list of the menu: broken copies, so that the checks can fail ---------------------------------------------------------------------

    TEST_F(ChessUiTest, AMenuButtonUnderTheTouchSizeIsCaught) {
        String xml = ShippedElementsXml("MenuPanel");
        ASSERT_FALSE(xml.empty());
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(name="MenuClose" height="56")", R"(name="MenuClose" height="40")"));
        CanvasCopy copy(xml);
        EXPECT_TRUE(HasIssue(ValidateWithWords(copy.layout), UILayoutIssueKind::TouchTargetTooSmall));
        EXPECT_NE(PanelProblemsOnMatrix(copy.layout, kReportedCases, kMenuPlate).find("MenuClose is 40.0 units high"), String::npos);

        String section = ShippedElementsXml("MenuPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(section, R"(name="MenuSectionOpponent" height="56")", R"(name="MenuSectionOpponent" height="40")"));
        CanvasCopy low(section);
        EXPECT_NE(PanelProblemsOnMatrix(low.layout, kReportedCases, kMenuPlate).find("MenuSectionOpponent is 40.0 units high"), String::npos);

        CanvasCopy shipped(ShippedElementsXml("MenuPanel"));
        EXPECT_TRUE(PanelProblemsOnMatrix(shipped.layout, kReportedCases, kMenuPlate).empty()) << "the same checks pass on the shipped panel";
        EXPECT_FALSE(HasIssue(ValidateWithWords(shipped.layout), UILayoutIssueKind::TouchTargetTooSmall));
    }

    TEST_F(ChessUiTest, AMenuPlateTooLowForItsButtonsIsCaught) {
        // A fixed-height plate does not grow: with a section button more than the height was written for, Close sticks out of it
        String xml = ShippedElementsXml("MenuPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(height="428")", R"(height="150")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        const String problems = PanelProblems(copy.layout, kMenuPlate);
        EXPECT_NE(problems.find("MenuClose sticks out of the plate"), String::npos) << problems.c_str();
    }

    TEST_F(ChessUiTest, AMenuWordTooLongForThePlateIsCaught) {
        String xml = ShippedElementsXml("MenuPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(text="Opponent")", R"(text="The opponent of this game and everything about the engine that plays it")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        const String problems = PanelProblems(copy.layout, kMenuPlate);
        EXPECT_NE(problems.find("MenuSectionOpponent cuts 'The opponent"), String::npos) << problems.c_str();
    }

}  // namespace BECore::Chess::Tests
