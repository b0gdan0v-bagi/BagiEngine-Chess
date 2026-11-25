#include <BECore/UI/UIEnvironment.h>
#include <tests/ChessUiHarness.h>

/// The menu section "Graphics": the node GraphicsPanel (config/nodes/ChessGraphicsPanel.xml) and the look it picks. The scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kGraphicsNode = "GraphicsPanel";
        constexpr const char* kGraphicsPlate = "GraphicsPanelPlate";
        constexpr const char* kWhiteKingNode = "WhiteKing_e1";  ///< the node's name is the same in every preset; its sprite is what follows the look

        /// Every control of the section, by name
        constexpr const char* kGraphicsControls[] = {"GraphicsPanelClassic", "GraphicsPanelBeer", "GraphicsPanelBack"};

    }  // namespace

    TEST_F(ChessUiTest, TheGraphicsPanelPassesTheSizeMatrix) {
        auto canvas = CanvasOf(kGraphicsNode);
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);
        const auto failures = ValidateWithWords(canvas->GetLayout());
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        const String problems = PanelProblemsOnMatrix(canvas->GetLayout(), kReportedCases, kGraphicsPlate);
        EXPECT_TRUE(problems.empty()) << problems.c_str();
    }

    TEST_F(ChessUiTest, NoWordOfTheGraphicsPanelIsCutInTheNarrowestWindowAndEveryControlIsATouchTarget) {
        auto canvas = CanvasOf(kGraphicsNode);
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = canvas->GetLayout();
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;

        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            LayOutInTheNarrowestWindow(layout, shape);
            const String problems = PanelProblems(layout, kGraphicsPlate);
            EXPECT_TRUE(problems.empty()) << problems.c_str();

            for (const char* name : kGraphicsControls) {
                const UIElement& control = ElementOf(kGraphicsNode, name);
                EXPECT_GE(control.rect.h, kSegmentHeight - kPixelTolerance) << name;
                EXPECT_GE(std::min(control.rect.w, control.rect.h) * layout.GetResolved().scale, minTouch - kPixelTolerance) << name << " is a touch target on the smallest window";
                const UIContentFit fit = control.CheckContent(control.rect.w, control.rect.h, layout.GetResolved().scale);
                EXPECT_TRUE(fit.hasContent && fit.fits) << name << " '" << ShownText(control).c_str() << "' in " << control.rect.w << " units";
            }
        }
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, TheGraphicsPanelIsAHiddenModalCanvasOfTheMenuWithNamesOfItsOwn) {
        auto canvas = CanvasOf(kGraphicsNode);
        ASSERT_TRUE(canvas);
        EXPECT_FALSE(canvas->_visible);
        EXPECT_TRUE(canvas->_modal);
        EXPECT_EQ(canvas->_space, CanvasSpace::SceneView);
        EXPECT_TRUE(canvas->_useSafeArea);

        Vector<PoolString> names;
        CollectNames(canvas->GetRoot(), names);
        ASSERT_FALSE(names.empty());
        for (const PoolString& name : names) {
            EXPECT_TRUE(name.ToStringView().starts_with(kGraphicsNode)) << name.CStr() << ": elements are found by name in every canvas";
        }
        for (const char* other : {"Hud", "BoardCoordinates", "Material", "MovePanel", "MenuPanel", "OpponentPanel", "MovesSection", "SoundPanel", "PromotionPanel"}) {
            const auto otherCanvas = CanvasOf(other);
            ASSERT_TRUE(otherCanvas) << other;
            for (const PoolString& name : names) {
                EXPECT_EQ(otherCanvas->GetRoot().FindByName(name), nullptr) << name.CStr() << " is also in " << other;
            }
        }
        EXPECT_EQ(ClickerOf(ElementOf(kGraphicsNode, "GraphicsPanelBack"))->action, "ChessOpenMenu"_intern);
        EXPECT_EQ(ClickerOf(ElementOf(kGraphicsNode, "GraphicsPanelDim"))->action, "ChessCloseMenu"_intern);
        EXPECT_EQ(canvas->GetRoot().children.front()->name, "GraphicsPanelDim"_intern) << "the first child, so a click on the plate never reaches it";
    }

    TEST_F(ChessUiTest, TheMenuOpensTheGraphicsSectionAndBackAndTheDimLeaveIt) {
        auto section = CanvasOf(kGraphicsNode);
        ASSERT_TRUE(section);
        for (const char* way : {"GraphicsPanelBack", "GraphicsPanelDim"}) {
            SCOPED_TRACE(way);
            ASSERT_TRUE(ClickButton(kMenuColor));
            ASSERT_TRUE(MenuList()->_visible);
            ASSERT_TRUE(ClickMenuList("MenuSectionGraphics").Both());
            EXPECT_TRUE(section->_visible);
            EXPECT_FALSE(MenuList()->_visible);

            const bool dim = StringView(way) == "GraphicsPanelDim";
            const auto corner = DimCorner(kGraphicsNode, way);
            const bool clicked = dim ? ClickAtPixel(corner.first, corner.second).Both() : ClickControl(ElementOf(kGraphicsNode, way)).Both();
            ASSERT_TRUE(clicked);
            EXPECT_FALSE(section->_visible);
            EXPECT_EQ(MenuList()->_visible, StringView(way) == "GraphicsPanelBack") << "Back returns to the list, the dim closes all";
            if (MenuList()->_visible) {
                ASSERT_TRUE(ClickMenuList("MenuClose").Both());
            }
        }
    }

    // ---- the section answers: the choice of the game, game to UIVars to widget, and the theme --------------------------------------------

    class ChessUiGraphicsPanelPickTest : public ChessUiTest {
    protected:
        void OpenTheSection() {
            ASSERT_TRUE(ClickButton(kMenuColor));
            ASSERT_TRUE(ClickMenuList("MenuSectionGraphics").Both());
            ASSERT_TRUE(CanvasOf(kGraphicsNode)->_visible);
            Draw();
        }

        PoolString ShownSpriteOfTheKing() const {
            const auto sprite = PieceSprite(kWhiteKingNode);
            EXPECT_TRUE(sprite);
            return sprite ? sprite->_spriteId : PoolString();
        }
    };

    TEST_F(ChessUiGraphicsPanelPickTest, TheFirstFrameAppliesTheThemeOfTheDefaultPresetAndShowsItInTheGroup) {
        ASSERT_TRUE(CoreManager::GetUIThemes().SetTheme("slate"_intern));
        _manager->UpdateAll();
        Draw();
        EXPECT_EQ(CoreManager::GetUIThemes().GetTheme(), "classic"_intern) << "the theme follows the choice, not what was set before the first frame";
        EXPECT_EQ(Var("graphicsPreset"), "classic");
    }

    TEST_F(ChessUiGraphicsPanelPickTest, AClickOnBeerTurnsThePiecesAndTheBoardAndAClickOnClassicTurnsThemBack) {
        ASSERT_NO_FATAL_FAILURE(OpenTheSection());
        ASSERT_TRUE(ElementOf(kGraphicsNode, "GraphicsPanelClassic").selected);
        EXPECT_FALSE(ElementOf(kGraphicsNode, "GraphicsPanelBeer").selected);
        EXPECT_EQ(ShownSpriteOfTheKing(), "ClassicWhiteKing"_intern);

        ASSERT_TRUE(ClickControl(ElementOf(kGraphicsNode, "GraphicsPanelBeer")).Both());
        _manager->UpdateAll();
        Draw();
        EXPECT_EQ(Graphics().Get(), GraphicsPreset::Beer);
        EXPECT_EQ(ShownSpriteOfTheKing(), "WhiteKing"_intern);
        EXPECT_EQ(CoreManager::GetUIThemes().GetTheme(), "tavern"_intern);
        EXPECT_EQ(Var("graphicsPreset"), "beer");
        EXPECT_TRUE(ElementOf(kGraphicsNode, "GraphicsPanelBeer").selected) << "the segment follows the variable";
        EXPECT_FALSE(ElementOf(kGraphicsNode, "GraphicsPanelClassic").selected);

        ASSERT_TRUE(ClickControl(ElementOf(kGraphicsNode, "GraphicsPanelClassic")).Both());
        _manager->UpdateAll();
        Draw();
        EXPECT_EQ(ShownSpriteOfTheKing(), "ClassicWhiteKing"_intern);
        EXPECT_EQ(CoreManager::GetUIThemes().GetTheme(), "classic"_intern);
        EXPECT_TRUE(ElementOf(kGraphicsNode, "GraphicsPanelClassic").selected);
    }

    TEST_F(ChessUiGraphicsPanelPickTest, AChoiceMadeInCodeRecoloursTheBoardToo) {
        Graphics().Set(GraphicsPreset::Beer);
        _manager->UpdateAll();
        Draw();
        EXPECT_EQ(CoreManager::GetUIThemes().GetTheme(), "tavern"_intern) << "a pick by the control API or a restored save has no click, and the theme follows";
        EXPECT_EQ(Var("graphicsPreset"), "beer");
    }

    TEST_F(ChessUiGraphicsPanelPickTest, AnIdInNoTableChangesNothing) {
        _manager->UpdateAll();
        const uint64_t revision = Graphics().GetRevision();
        for (const char* text : {"", "Beer", "pastel", "classic "}) {
            PressWith("ChessSetGraphics", text);
        }
        EXPECT_EQ(Graphics().GetRevision(), revision);
        EXPECT_EQ(Graphics().Get(), GraphicsPreset::Classic);
    }

    TEST_F(ChessUiGraphicsPanelPickTest, ASteadyFrameWithThePanelOpenWritesNoVariable) {
        ASSERT_NO_FATAL_FAILURE(OpenTheSection());
        Draw();
        const uint64_t version = CoreManager::GetUIVars().GetVersion();
        for (int frame = 0; frame < 3; ++frame) {
            _manager->UpdateAll();
            Draw();
        }
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version);
    }

}  // namespace BECore::Chess::Tests
