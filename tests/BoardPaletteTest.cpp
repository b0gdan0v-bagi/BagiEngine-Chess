#include <Chess/Components/BoardPalette.h>
#include <Common/Printers.h>
#include <Common/UIThemeTestSupport.h>
#include <gtest/gtest.h>
#include <tests/ShippedScene.h>

/**
 * The board colors are tokens of the shipped `config/UIThemes.xml`, read through BoardPalette. The file is read from the source tree (a game's content is not mounted in the test
 * environment) into a table of the test's own, so no test depends on the global one.
 */
namespace BECore::Chess::Tests {

    using namespace BECore::Tests;

    namespace {

        struct Themes {
            static constexpr auto classic = "classic"_intern;
            static constexpr auto slate = "slate"_intern;
        };

        // The look of the board before it had themes: `classic` must stay exactly this
        const Color kClassicLight{240, 217, 181, 255};
        const Color kClassicDark{181, 136, 99, 255};
        const Color kClassicSelected{120, 200, 120, 200};
        const Color kClassicLegalMove{120, 200, 120, 120};
        const Color kClassicLegalCapture{220, 90, 90, 160};
        const Color kClassicCheck{230, 60, 60, 210};
        const Color kClassicMate{150, 20, 20, 240};
        const Color kClassicHintFrom{60, 130, 230, 110};
        const Color kClassicHintTo{60, 130, 230, 200};
        const Color kClassicLastMove{245, 205, 50, 140};
        const Color kClassicPremove{150, 90, 200, 150};

        void ExpectSame(const Color& actual, const Color& expected, const char* what) {
            EXPECT_TRUE(ColorsEqual(actual, expected)) << what << ": " << static_cast<int>(actual.r) << "," << static_cast<int>(actual.g) << "," << static_cast<int>(actual.b) << ","
                                                       << static_cast<int>(actual.a);
        }

        /// Every color of the palette as one list, to compare two palettes or to find which differ
        Vector<Color> Colors(const BoardPalette& palette) {
            return {palette.LightSquare(), palette.DarkSquare(), palette.Selected(), palette.LegalMove(), palette.LegalCapture(), palette.Check(),
                    palette.Mate(),        palette.HintFrom(),   palette.HintTo(),   palette.LastMove(),  palette.Premove()};
        }

    }  // namespace

    class BoardPaletteTest : public ::testing::Test {
    protected:
        void SetUp() override {
            ASSERT_TRUE(LoadShippedThemes(_themes));
        }

        UIThemes _themes;
    };

    TEST_F(BoardPaletteTest, TheShippedFileHasClassicAndSlateAndClassicIsTheDefault) {
        EXPECT_EQ(_themes.GetDefaultTheme(), Themes::classic);
        ASSERT_EQ(_themes.GetThemes().size(), 2u);
        EXPECT_TRUE(_themes.HasTheme(Themes::classic));
        EXPECT_TRUE(_themes.HasTheme(Themes::slate));
        EXPECT_EQ(_themes.GetTheme(), Themes::classic);
    }

    TEST_F(BoardPaletteTest, EveryThemeDefinesEveryTokenAndNoTokenOfThePaletteIsMissing) {
        ExpectThemesComplete(_themes);
        ForEachTheme(_themes, [&](PoolString theme) {
            BoardPalette palette;
            palette.Refresh(_themes);
            EXPECT_TRUE(_themes.GetMissing().empty()) << theme.ToStringView().data();
        });
        EXPECT_EQ(_themes.GetTokens().size(), 11u) << "the eleven board tokens, nothing else in the file";
    }

    TEST_F(BoardPaletteTest, ClassicIsTheColorsTheBoardAlwaysHad) {
        BoardPalette palette;
        palette.Refresh(_themes);
        ExpectSame(palette.LightSquare(), kClassicLight, "light square");
        ExpectSame(palette.DarkSquare(), kClassicDark, "dark square");
        ExpectSame(palette.Selected(), kClassicSelected, "selected");
        ExpectSame(palette.LegalMove(), kClassicLegalMove, "legal move");
        ExpectSame(palette.LegalCapture(), kClassicLegalCapture, "legal capture");
        ExpectSame(palette.Check(), kClassicCheck, "check");
        ExpectSame(palette.Mate(), kClassicMate, "mate");
        ExpectSame(palette.HintFrom(), kClassicHintFrom, "hint from");
        ExpectSame(palette.HintTo(), kClassicHintTo, "hint to");
        ExpectSame(palette.LastMove(), kClassicLastMove, "last move");
        ExpectSame(palette.Premove(), kClassicPremove, "premove");
    }

    TEST_F(BoardPaletteTest, ASwitchOfTheThemeChangesThePaletteAtTheNextRefresh) {
        BoardPalette palette;
        palette.Refresh(_themes);
        const Vector<Color> classic = Colors(palette);

        ASSERT_TRUE(_themes.SetTheme(Themes::slate));
        EXPECT_TRUE(ColorsEqual(palette.LightSquare(), classic[0])) << "nothing changes before the next Refresh";
        palette.Refresh(_themes);
        const Vector<Color> slate = Colors(palette);
        ASSERT_EQ(slate.size(), classic.size());
        for (size_t index = 0; index < slate.size(); ++index) {
            EXPECT_FALSE(ColorsEqual(slate[index], classic[index])) << "color " << index << " is the same in both themes";
        }

        ASSERT_TRUE(_themes.SetTheme(Themes::classic));
        palette.Refresh(_themes);
        ExpectSame(palette.LightSquare(), kClassicLight, "light square after the way back");
    }

    TEST_F(BoardPaletteTest, AnUnchangedVersionResolvesNothingAgain) {
        BoardPalette palette;
        EXPECT_EQ(palette.GetResolveCount(), 0u);
        palette.Refresh(_themes);
        EXPECT_EQ(palette.GetResolveCount(), 1u);
        for (int frame = 0; frame < 10; ++frame) {
            palette.Refresh(_themes);
        }
        EXPECT_EQ(palette.GetResolveCount(), 1u);

        ASSERT_TRUE(_themes.SetTheme(Themes::slate));
        palette.Refresh(_themes);
        EXPECT_EQ(palette.GetResolveCount(), 2u);
    }

    TEST_F(BoardPaletteTest, ATokenNoThemeDefinesDrawsMagentaAndIsRecorded) {
        UIThemes bare;
        bare.Set(Themes::classic, "boardLight", kClassicLight);
        BoardPalette palette;
        palette.Refresh(bare);
        ExpectSame(palette.LightSquare(), kClassicLight, "the one token that is defined");
        ExpectSame(palette.DarkSquare(), kMissingTokenColor, "a token no theme defines");
        EXPECT_EQ(bare.GetMissing().size(), 10u);
    }

}  // namespace BECore::Chess::Tests
