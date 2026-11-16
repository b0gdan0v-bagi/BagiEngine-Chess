#include <Chess/Sound/SoundCue.h>
#include <tests/ChessUiHarness.h>

/// The menu section "Sound": the node SoundPanel (config/nodes/ChessSoundPanel.xml). The scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kSoundNode = "SoundPanel";
        constexpr const char* kSoundPlate = "SoundPanelPlate";
        constexpr const char* kLongestVolumeText = "100%";
        constexpr float kSliderPress = 0.3f;  ///< where along the slider's travel the press lands: a step of the slider

        const AudioLevel& Master() {
            return CoreManager::GetAudio().GetSettings()._master;
        }

        String Var(const char* name) {
            const StringView value = CoreManager::GetUIVars().Get(PoolString::Intern(name));
            return String(value.data(), value.size());
        }

        /// Every control of the section, by name
        constexpr const char* kSoundControls[] = {"SoundPanelVolume", "SoundPanelMuteRow", "SoundPanelBack"};

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
        EXPECT_EQ(canvas->GetRoot().FindByName(PoolString::Intern("SoundPanelClose")), nullptr) << "Back is the one button: the dim closes all";
        EXPECT_EQ(ClickerOf(ElementOf(kSoundNode, "SoundPanelDim"))->action, PoolString::Intern("ChessCloseMenu"));
        EXPECT_EQ(canvas->GetRoot().children.front()->name, PoolString::Intern("SoundPanelDim")) << "the first child, so a click on the plate never reaches it";
    }

    TEST_F(ChessUiTest, TheMenuOpensTheSoundSectionAndBackAndTheDimLeaveIt) {
        auto section = CanvasOf(kSoundNode);
        ASSERT_TRUE(section);
        for (const char* way : {"SoundPanelBack", "SoundPanelDim"}) {
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
            EXPECT_EQ(MenuList()->_visible, StringView(way) == "SoundPanelBack") << "Back returns to the list, the dim closes all";
            if (MenuList()->_visible) {
                ASSERT_TRUE(ClickMenuList("MenuClose").Both());
            }
        }
    }

    // ---- the section answers: the levels of the audio manager, game to UIVars to widget -------------------------------------------------

    class ChessUiSoundPanelLevelsTest : public ChessUiTest {
    protected:
        void OpenTheSection() {
            ASSERT_TRUE(ClickButton(kMenuColor));
            ASSERT_TRUE(ClickMenuList("MenuSectionSound").Both());
            ASSERT_TRUE(CanvasOf(kSoundNode)->_visible);
            Draw();
        }

        /// A press on the slider at @p fraction of the travel of its thumb (the rect narrowed by half a thumb on both sides)
        Taken ClickTheSliderAt(float fraction) {
            Draw();
            const UIElement& slider = ElementOf(kSoundNode, "SoundPanelVolume");
            const float half = slider.rect.h * 0.5f;
            const auto at = PixelOf(slider.rect.x + half + fraction * (slider.rect.w - slider.rect.h), slider.rect.y + half);
            return ClickAtPixel(at.first, at.second);
        }

        ScopedAudioLevels _levels;
    };

    TEST_F(ChessUiSoundPanelLevelsTest, ThePanelShowsTheLevelsOfTheManagerAtItsFirstDraw) {
        Draw();
        EXPECT_EQ(Var("soundVolumeText"), "100%");
        EXPECT_EQ(Var("soundVolume"), "1");
        EXPECT_EQ(Var("soundMuted"), "0");
    }

    TEST_F(ChessUiSoundPanelLevelsTest, AClickOnTheSliderSetsTheMasterLevelAndTheWordFollows) {
        ASSERT_NO_FATAL_FAILURE(OpenTheSection());
        const uint64_t before = AudioRequests();
        ASSERT_TRUE(ClickTheSliderAt(kSliderPress).Both());
        EXPECT_FLOAT_EQ(Master()._volume, 0.3f);
        EXPECT_FALSE(Master()._muted);
        Draw();
        EXPECT_EQ(Var("soundVolumeText"), "30%");
        EXPECT_EQ(Var("soundVolume"), "0.3");
        EXPECT_TRUE(AudioRequestsSince(before).empty()) << "the slider emits on every step of a drag and does not click";
    }

    TEST_F(ChessUiSoundPanelLevelsTest, TheSwitchSetsAndClearsTheMuteAndClicks) {
        ASSERT_NO_FATAL_FAILURE(OpenTheSection());
        const uint64_t before = AudioRequests();
        ASSERT_TRUE(ClickControl(ElementOf(kSoundNode, "SoundPanelMuteRow")).Both());
        EXPECT_TRUE(Master()._muted);
        Draw();
        EXPECT_EQ(Var("soundMuted"), "1");
        // The click is recorded whether it is taken before or after the mute applies, so only the request is asserted
        const Vector<AudioPlayRecord> requests = AudioRequestsSince(before);
        ASSERT_EQ(requests.size(), 1u);
        EXPECT_EQ(requests[0].clip, ClipPath(SoundCue::Click));
        EXPECT_EQ(requests[0].bus, AudioBus::Ui);

        ASSERT_TRUE(ClickControl(ElementOf(kSoundNode, "SoundPanelMuteRow")).Both());
        EXPECT_FALSE(Master()._muted);
        Draw();
        EXPECT_EQ(Var("soundMuted"), "0");
    }

    TEST_F(ChessUiSoundPanelLevelsTest, ALevelSetFromOutsideShowsAtTheNextDrawAndAMutedMasterKeepsItsVolume) {
        ASSERT_NO_FATAL_FAILURE(OpenTheSection());
        CoreManager::GetAudio().SetMasterVolume(0.5f);
        Draw();
        EXPECT_EQ(Var("soundVolumeText"), "50%");
        CoreManager::GetAudio().SetMasterMuted(true);
        Draw();
        EXPECT_EQ(Var("soundMuted"), "1");
        EXPECT_EQ(Var("soundVolumeText"), "50%") << "the slider still shows the volume the mute keeps";
        EXPECT_EQ(Var("soundVolume"), "0.5");
    }

    TEST_F(ChessUiSoundPanelLevelsTest, AValueThatDoesNotParseChangesNothing) {
        ASSERT_NO_FATAL_FAILURE(OpenTheSection());
        CoreManager::GetAudio().SetMasterVolume(0.4f);
        const uint64_t revision = CoreManager::GetAudio().GetLevelsRevision();
        for (const char* text : {"abc", "", "nan", "inf"}) {
            PressWith("ChessSetVolume", text);
        }
        for (const char* text : {"yes", "", "2"}) {
            PressWith("ChessSetMute", text);
        }
        EXPECT_EQ(CoreManager::GetAudio().GetLevelsRevision(), revision);
        EXPECT_FLOAT_EQ(Master()._volume, 0.4f);
        EXPECT_FALSE(Master()._muted);
    }

    TEST_F(ChessUiSoundPanelLevelsTest, ASteadyFrameWithThePanelOpenWritesNoVariable) {
        ASSERT_NO_FATAL_FAILURE(OpenTheSection());
        Draw();
        const uint64_t version = CoreManager::GetUIVars().GetVersion();
        for (int frame = 0; frame < 3; ++frame) {
            _manager->UpdateAll();
            Draw();
        }
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version);
    }

    TEST_F(ChessUiSoundPanelLevelsTest, AMovePlayedAtVolumeZeroIsStillAskedForAndTheLevelStays) {
        ASSERT_NO_FATAL_FAILURE(OpenTheSection());
        ASSERT_TRUE(ClickTheSliderAt(0.0f).Both());
        ASSERT_FLOAT_EQ(Master()._volume, 0.0f);
        _manager->UpdateAll();
        const uint64_t before = AudioRequests();
        ASSERT_TRUE(Play(Model(), "e2e4"));
        _manager->UpdateAll();
        const Vector<AudioPlayRecord> requests = AudioRequestsSince(before);
        ASSERT_EQ(requests.size(), 1u) << "the request is made; the gain is the backend's";
        EXPECT_EQ(requests[0].clip, ClipPath(SoundCue::Move));
        EXPECT_FLOAT_EQ(Master()._volume, 0.0f);
    }

}  // namespace BECore::Chess::Tests
