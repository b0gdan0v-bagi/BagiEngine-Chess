#include <Chess/Components/PieceSprite.h>
#include <Chess/Graphics/GraphicsChoice.h>
#include <Common/Printers.h>
#include <tests/ChessPieceArt.h>

/// The look of the game: pure data and the one table of sprite names, nothing to start
namespace BECore::Chess::Tests {

    namespace {

        /// gtest cannot stream an EASTL string view
        String Text(StringView text) {
            return String(text.data(), text.size());
        }

    }  // namespace

    TEST(GraphicsPresetTest, ClassicIsTheDefault) {
        GraphicsChoice choice;
        EXPECT_TRUE(choice.Get() == GraphicsPreset::Classic);
    }

    TEST(GraphicsPresetTest, IdsRoundTripAndAreExact) {
        for (const GraphicsPreset preset : EnumUtils<GraphicsPreset>::Values()) {
            const auto found = ParsePreset(PresetId(preset));
            ASSERT_TRUE(found.has_value()) << Text(PresetId(preset));
            EXPECT_TRUE(*found == preset);
        }
        EXPECT_EQ(Text(PresetId(GraphicsPreset::Classic)), "classic");
        EXPECT_EQ(Text(PresetId(GraphicsPreset::Beer)), "beer");
        for (const StringView text : {StringView(""), StringView("Classic"), StringView("BEER"), StringView("beer "), StringView("tavern"), StringView("Beer")}) {
            EXPECT_FALSE(ParsePreset(text).has_value()) << Text(text);
        }
    }

    TEST(GraphicsPresetTest, TheTwentyFourSpriteNamesAreDistinctAndStartWithThePrefix) {
        Vector<String> names;
        for (const GraphicsPreset preset : EnumUtils<GraphicsPreset>::Values()) {
            for (const Piece piece : AllPieces()) {
                const String name = String(PieceSpriteId(piece, preset).ToStringView());
                const String expected = Format("{}{}{}", Text(SpritePrefix(preset)), EnumUtils<PieceColor>::ToString(piece.color), EnumUtils<PieceType>::ToString(piece.type));
                EXPECT_EQ(name, expected);
                names.push_back(name);
            }
        }
        ASSERT_EQ(names.size(), 24u);
        for (size_t i = 0; i < names.size(); ++i) {
            for (size_t j = i + 1; j < names.size(); ++j) {
                EXPECT_NE(names[i], names[j]);
            }
        }
        EXPECT_EQ(String(PieceSpriteId(Piece{PieceType::King, PieceColor::White}, GraphicsPreset::Beer).ToStringView()), "WhiteKing");
        EXPECT_EQ(String(PieceSpriteId(Piece{PieceType::Pawn, PieceColor::Black}, GraphicsPreset::Classic).ToStringView()), "ClassicBlackPawn");
    }

    TEST(GraphicsChoiceTest, TheRevisionStartsAtOneAndGrowsOnAChangeOnly) {
        GraphicsChoice choice;
        EXPECT_EQ(choice.GetRevision(), 1u);
        EXPECT_FALSE(choice.Set(GraphicsPreset::Classic));
        EXPECT_EQ(choice.GetRevision(), 1u);
        EXPECT_TRUE(choice.Set(GraphicsPreset::Beer));
        EXPECT_TRUE(choice.Get() == GraphicsPreset::Beer);
        EXPECT_EQ(choice.GetRevision(), 2u);
        EXPECT_FALSE(choice.Set(GraphicsPreset::Beer));
        EXPECT_EQ(choice.GetRevision(), 2u);
        EXPECT_TRUE(choice.Set(GraphicsPreset::Classic));
        EXPECT_EQ(choice.GetRevision(), 3u);
    }

}  // namespace BECore::Chess::Tests
