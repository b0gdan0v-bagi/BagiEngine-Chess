#include <tests/ChessUiHarness.h>

/// The menu section "Sound": the node SoundPanel (config/nodes/ChessSoundPanel.xml). The scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kSoundNode = "SoundPanel";
        constexpr const char* kSoundPlate = "SoundPanelPlate";
        constexpr const char* kLongestVolumeText = "100%";

        /// Every control of the section, by name
        constexpr const char* kSoundControls[] = {"SoundPanelVolume", "SoundPanelMuteRow", "SoundPanelBack", "SoundPanelClose"};

    }  // namespace

    TEST_F(ChessUiTest, TheSoundPanelPassesTheSizeMatrix) {
        auto canvas = CanvasOf(kSoundNode);
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);
        CoreManager::GetUIVars().Set("soundVolumeText"_intern, kLongestVolumeText);
        const auto failures = ValidateWithWords(canvas->GetLayout());
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        const String problems = PanelProblemsOnMatrix(canvas->GetLayout(), kReportedCases, kSoundPlate);
        EXPECT_TRUE(problems.empty()) << problems.c_str();
    }

    TEST_F(ChessUiTest, NoWordOfTheSoundPanelIsCutInTheNarrowestWindowAndEveryControlIsATouchTarget) {
        auto canvas = CanvasOf(kSoundNode);
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = canvas->GetLayout();
        CoreManager::GetUIVars().Set("soundVolumeText"_intern, kLongestVolumeText);
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;

        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            LayOutInTheNarrowestWindow(layout, shape);
            const String problems = PanelProblems(layout, kSoundPlate);
            EXPECT_TRUE(problems.empty()) << problems.c_str();

            for (const char* name : kSoundControls) {
                const UIElement& control = ElementOf(kSoundNode, name);
                EXPECT_GE(control.rect.h, kSegmentHeight - kPixelTolerance) << name;
                EXPECT_GE(std::min(control.rect.w, control.rect.h) * layout.GetResolved().scale, minTouch - kPixelTolerance) << name << " is a touch target on the smallest window";
            }
            const UIElement& value = ElementOf(kSoundNode, "SoundPanelVolumeValue");
            const UIContentFit fit = value.CheckContent(value.rect.w, value.rect.h, layout.GetResolved().scale);
            EXPECT_TRUE(fit.hasContent && fit.fits) << "'" << ShownText(value).c_str() << "' in " << value.rect.w << " units";
            EXPECT_GT(ElementOf(kSoundNode, "SoundPanelVolume").rect.w, 0.0f) << "the slider keeps room beside its caption and its number";
        }
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, TheSoundPanelIsAHiddenModalCanvasOfTheMenuWithNamesOfItsOwn) {
        auto canvas = CanvasOf(kSoundNode);
        ASSERT_TRUE(canvas);
        EXPECT_FALSE(canvas->_visible);
        EXPECT_TRUE(canvas->_modal);
        EXPECT_EQ(canvas->_space, CanvasSpace::SceneView);
        EXPECT_TRUE(canvas->_useSafeArea);

        Vector<PoolString> names;
        CollectNames(canvas->GetRoot(), names);
        ASSERT_FALSE(names.empty());
        for (const PoolString& name : names) {
            EXPECT_TRUE(name.ToStringView().starts_with(kSoundNode)) << name.CStr() << ": elements are found by name in every canvas";
        }
        for (const char* other : {"Hud", "BoardCoordinates", "Material", "MovePanel", "MenuPanel", "OpponentPanel", "MovesSection", "PromotionPanel"}) {
            const auto otherCanvas = CanvasOf(other);
            ASSERT_TRUE(otherCanvas) << other;
            for (const PoolString& name : names) {
                EXPECT_EQ(otherCanvas->GetRoot().FindByName(name), nullptr) << name.CStr() << " is also in " << other;
            }
        }
        EXPECT_EQ(ClickerOf(ElementOf(kSoundNode, "SoundPanelBack"))->action, PoolString::Intern("ChessOpenMenu"));
        EXPECT_EQ(ClickerOf(ElementOf(kSoundNode, "SoundPanelClose"))->action, PoolString::Intern("ChessCloseMenu"));
        EXPECT_EQ(ClickerOf(ElementOf(kSoundNode, "SoundPanelDim"))->action, PoolString::Intern("ChessCloseMenu"));
        EXPECT_EQ(canvas->GetRoot().children.front()->name, PoolString::Intern("SoundPanelDim")) << "the first child, so a click on the plate never reaches it";
    }

    TEST_F(ChessUiTest, TheMenuOpensTheSoundSectionAndBackAndCloseAndTheDimLeaveIt) {
        auto section = CanvasOf(kSoundNode);
        ASSERT_TRUE(section);
        for (const char* way : {"SoundPanelBack", "SoundPanelClose", "SoundPanelDim"}) {
            SCOPED_TRACE(way);
            ASSERT_TRUE(ClickButton(kMenuColor));
            ASSERT_TRUE(MenuList()->_visible);
            ASSERT_TRUE(ClickMenuList("MenuSectionSound").Both());
            EXPECT_TRUE(section->_visible);
            EXPECT_FALSE(MenuList()->_visible);
            EXPECT_FALSE(Panel()->_visible);

            // The dim is clicked in its corner, far from the plate
            const bool dim = StringView(way) == "SoundPanelDim";
            const auto corner = DimCorner(kSoundNode, way);
            const bool clicked = dim ? ClickAtPixel(corner.first, corner.second).Both() : ClickControl(ElementOf(kSoundNode, way)).Both();
            ASSERT_TRUE(clicked);
            EXPECT_FALSE(section->_visible);
            EXPECT_EQ(MenuList()->_visible, StringView(way) == "SoundPanelBack") << "Back returns to the list, the others close all";
            if (MenuList()->_visible) {
                ASSERT_TRUE(ClickMenuList("MenuClose").Both());
            }
        }
    }

}  // namespace BECore::Chess::Tests
