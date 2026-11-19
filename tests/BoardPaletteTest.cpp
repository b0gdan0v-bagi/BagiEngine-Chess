#include <Chess/Components/BoardPalette.h>
#include <Common/Printers.h>
#include <Common/UIThemeTestSupport.h>
#include <cmath>
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
            static constexpr auto tavern = "tavern"_intern;
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

        // What "readable" means, in 0..255 channels. The numbers are the least that `classic` meets, not what a better board would want: raising one fails the look the board always had.
        // The distance (Euclid over the three channels, 441 at most) between a mark over a square and the bare square; classic's weakest is the capture over a dark square, 38
        constexpr float kMinMarkDistance = 30.0f;
        // The same between two marks, each over the bare square; classic's weakest is the selection against a legal target on a dark square, 28
        constexpr float kMinMarkPairDistance = 25.0f;
        // The contrast ratio (WCAG, 1 to 21) of a coordinate label against its own cell; classic's brown pair is 2.29. A label is a letter or a digit, so the 4.5 of a body of text is not asked
        constexpr float kMinLabelContrast = 2.0f;

        struct Mark {
            const char* token;
            Color color;
        };

        /// The nine marks of the board, in the order OnDraw() draws them
        Vector<Mark> Marks(const BoardPalette& palette) {
            return {{"boardLastMove", palette.LastMove()}, {"boardCheck", palette.Check()},         {"boardMate", palette.Mate()},
                    {"boardHintFrom", palette.HintFrom()}, {"boardHintTo", palette.HintTo()},       {"boardPremove", palette.Premove()},
                    {"boardSelected", palette.Selected()}, {"boardLegalMove", palette.LegalMove()}, {"boardLegalCapture", palette.LegalCapture()}};
        }

        /// @p mark drawn over the opaque @p square with its alpha (`src over dst`)
        Color Composite(const Color& mark, const Color& square) {
            const float alpha = static_cast<float>(mark.a) / 255.0f;
            const auto mix = [alpha](uint8_t source, uint8_t destination) {
                return static_cast<uint8_t>(std::lround(static_cast<float>(source) * alpha + static_cast<float>(destination) * (1.0f - alpha)));
            };
            return {mix(mark.r, square.r), mix(mark.g, square.g), mix(mark.b, square.b), 255};
        }

        float Distance(const Color& first, const Color& second) {
            const float red = static_cast<float>(first.r) - static_cast<float>(second.r);
            const float green = static_cast<float>(first.g) - static_cast<float>(second.g);
            const float blue = static_cast<float>(first.b) - static_cast<float>(second.b);
            return std::sqrt(red * red + green * green + blue * blue);
        }

        float Luminance(const Color& color) {
            const auto linear = [](uint8_t channel) {
                const float value = static_cast<float>(channel) / 255.0f;
                return value <= 0.03928f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
            };
            return 0.2126f * linear(color.r) + 0.7152f * linear(color.g) + 0.0722f * linear(color.b);
        }

        float ContrastRatio(const Color& first, const Color& second) {
            const float lighter = std::max(Luminance(first), Luminance(second));
            const float darker = std::min(Luminance(first), Luminance(second));
            return (lighter + 0.05f) / (darker + 0.05f);
        }

        /// What cannot be seen or told apart on the board of @p palette, one problem to a line, nothing when it is readable: each mark over each square against the bare square, every two
        /// marks against each other over each square, and a coordinate label (the dark square's color on a light cell, the light square's on a dark one) against its cell
        String ReadabilityProblems(const BoardPalette& palette) {
            String problems;
            const Vector<Mark> marks = Marks(palette);
            const struct {
                const char* name;
                Color color;
            } squares[] = {{"light", palette.LightSquare()}, {"dark", palette.DarkSquare()}};
            for (const auto& square : squares) {
                for (const Mark& mark : marks) {
                    const float distance = Distance(Composite(mark.color, square.color), square.color);
                    if (distance < kMinMarkDistance) {
                        problems += Format("{} over the {} square differs from it by {:.1f}, under {:.0f}\n", mark.token, square.name, distance, kMinMarkDistance);
                    }
                }
                for (size_t first = 0; first < marks.size(); ++first) {
                    for (size_t second = first + 1; second < marks.size(); ++second) {
                        const float distance = Distance(Composite(marks[first].color, square.color), Composite(marks[second].color, square.color));
                        if (distance < kMinMarkPairDistance) {
                            problems += Format("{} and {} over the {} square differ by {:.1f}, under {:.0f}\n", marks[first].token, marks[second].token, square.name, distance, kMinMarkPairDistance);
                        }
                    }
                }
            }
            const float onLight = ContrastRatio(palette.DarkSquare(), palette.LightSquare());
            const float onDark = ContrastRatio(palette.LightSquare(), palette.DarkSquare());
            if (onLight < kMinLabelContrast) {
                problems += Format("a label on a light cell has the contrast {:.2f}, under {:.1f}\n", onLight, kMinLabelContrast);
            }
            if (onDark < kMinLabelContrast) {
                problems += Format("a label on a dark cell has the contrast {:.2f}, under {:.1f}\n", onDark, kMinLabelContrast);
            }
            return problems;
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

    TEST_F(BoardPaletteTest, TheShippedFileHasClassicSlateAndTavernAndTavernIsTheDefault) {
        EXPECT_EQ(_themes.GetDefaultTheme(), Themes::tavern);
        ASSERT_EQ(_themes.GetThemes().size(), 3u);
        EXPECT_TRUE(_themes.HasTheme(Themes::classic));
        EXPECT_TRUE(_themes.HasTheme(Themes::slate));
        EXPECT_TRUE(_themes.HasTheme(Themes::tavern));
        EXPECT_EQ(_themes.GetTheme(), Themes::tavern);
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
        ASSERT_TRUE(_themes.SetTheme(Themes::classic));
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
        ASSERT_TRUE(_themes.SetTheme(Themes::classic));
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

    // ---- readable on every theme ---------------------------------------------------------------------------------------------------

    TEST_F(BoardPaletteTest, EveryMarkIsSeenAndToldApartAndEveryLabelReadsOnEveryTheme) {
        size_t visited = 0;
        ForEachTheme(_themes, [&](PoolString theme) {
            ++visited;
            BoardPalette palette;
            palette.Refresh(_themes);
            const String problems = ReadabilityProblems(palette);
            EXPECT_TRUE(problems.empty()) << theme.ToStringView().data() << ":\n" << problems.c_str();
        });
        EXPECT_EQ(visited, 3u) << "classic, slate and tavern";
    }

    TEST_F(BoardPaletteTest, ABrokenCopyOfClassicIsCaughtByTheReadabilityChecks) {
        // The copy is the shipped classic with one token changed: each case must make the checks complain about exactly what was broken
        const auto problemsWith = [](const char* token, const Color& value) {
            UIThemes themes;
            EXPECT_TRUE(LoadShippedThemes(themes));
            EXPECT_TRUE(themes.SetTheme(Themes::classic));
            EXPECT_TRUE(themes.Set(Themes::classic, token, value));
            BoardPalette palette;
            palette.Refresh(themes);
            return ReadabilityProblems(palette);
        };

        EXPECT_TRUE(problemsWith("boardLastMove", kClassicLastMove).empty()) << "the copy with nothing broken passes";

        const String invisible = problemsWith("boardLastMove", Color{245, 205, 50, 0});
        EXPECT_NE(invisible.find("boardLastMove over the light square"), String::npos) << "an alpha of 0 shows nothing: " << invisible.c_str();
        EXPECT_NE(invisible.find("boardLastMove over the dark square"), String::npos) << invisible.c_str();

        const String asTheSquare = problemsWith("boardSelected", kClassicLight);
        EXPECT_NE(asTheSquare.find("boardSelected over the light square"), String::npos) << "a mark in the color of its square: " << asTheSquare.c_str();
        EXPECT_EQ(asTheSquare.find("boardSelected over the dark square"), String::npos) << "it still shows on the dark one";

        const String twins = problemsWith("boardHintTo", kClassicHintFrom);
        EXPECT_NE(twins.find("boardHintFrom and boardHintTo over the light square"), String::npos) << "two marks that look the same: " << twins.c_str();

        const String sameSquares = problemsWith("boardDark", kClassicLight);
        EXPECT_NE(sameSquares.find("a label on a light cell"), String::npos) << "a label in the color of its cell: " << sameSquares.c_str();
        EXPECT_NE(sameSquares.find("a label on a dark cell"), String::npos) << sameSquares.c_str();
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
