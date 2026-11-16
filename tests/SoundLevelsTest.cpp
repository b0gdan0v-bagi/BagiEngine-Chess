#include <Chess/Sound/SoundLevels.h>
#include <gtest/gtest.h>
#include <tests/ShippedScene.h>

/// The pure rules of the Sound section: the texts the slider and the switch emit and the texts the widgets show
namespace BECore::Chess::Tests {

    namespace {

        String Text(StringView text) {
            return String(text.data(), text.size());
        }

    }  // namespace

    TEST(SoundLevelsTest, AVolumeIsAFiniteNumberClampedToTheRangeOfTheSlider) {
        ASSERT_TRUE(ParseVolume("0.3").has_value());
        EXPECT_FLOAT_EQ(*ParseVolume("0.3"), 0.3f);
        EXPECT_FLOAT_EQ(*ParseVolume("1"), 1.0f);
        EXPECT_FLOAT_EQ(*ParseVolume("0"), 0.0f);
        EXPECT_FLOAT_EQ(*ParseVolume("-1"), 0.0f);
        EXPECT_FLOAT_EQ(*ParseVolume("2"), 1.0f);
    }

    TEST(SoundLevelsTest, WhatIsNoNumberIsNoVolume) {
        for (const char* text : {"", "abc", "nan", "inf", "-inf", "0.3x", " 0.3", "0.3 ", "--1"}) {
            EXPECT_FALSE(ParseVolume(text).has_value()) << "\"" << text << "\"";
        }
    }

    TEST(SoundLevelsTest, TheMuteIsOneOrZeroAndNothingElse) {
        ASSERT_TRUE(ParseMuted("1").has_value());
        EXPECT_TRUE(*ParseMuted("1"));
        ASSERT_TRUE(ParseMuted("0").has_value());
        EXPECT_FALSE(*ParseMuted("0"));
        for (const char* text : {"", "yes", "true", "2", "01", "-1"}) {
            EXPECT_FALSE(ParseMuted(text).has_value()) << "\"" << text << "\"";
        }
    }

    TEST(SoundLevelsTest, ThePercentIsRoundedToAWholeNumber) {
        EXPECT_EQ(VolumePercentText(0.7f), "70%");
        EXPECT_EQ(VolumePercentText(0.0f), "0%");
        EXPECT_EQ(VolumePercentText(1.0f), "100%");
        EXPECT_EQ(VolumePercentText(0.05f), "5%");
        EXPECT_EQ(VolumePercentText(0.333f), "33%");
    }

    TEST(SoundLevelsTest, TheVariableTextIsShortAndExact) {
        EXPECT_EQ(VolumeVarText(0.3f), "0.3");
        EXPECT_EQ(VolumeVarText(0.0f), "0");
        EXPECT_EQ(VolumeVarText(1.0f), "1");
        EXPECT_EQ(VolumeVarText(0.1f + 0.2f).find("0.30000001"), String::npos);
    }

    TEST(SoundLevelsTest, TheStepIsTheStepOfTheSliderInTheShippedNode) {
        pugi::xml_document document;
        ASSERT_TRUE(LoadShippedSceneDocument(document));
        const pugi::xml_node slider = document.select_node("//node[@name='SoundPanel']//component[@type='UISliderComponent']").node();
        ASSERT_TRUE(slider);
        const StringView step = slider.attribute("step").value();
        ASSERT_FALSE(step.empty());
        const auto parsed = ParseVolume(step);
        ASSERT_TRUE(parsed.has_value()) << Text(step).c_str();
        EXPECT_FLOAT_EQ(*parsed, kVolumeStep);
    }

}  // namespace BECore::Chess::Tests
