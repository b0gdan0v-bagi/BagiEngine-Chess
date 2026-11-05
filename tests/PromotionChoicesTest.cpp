#include <Chess/Game/PromotionChoices.h>
#include <Common/Printers.h>

/// The pieces a pawn can become: pure data, nothing to start
namespace BECore::Chess::Tests {

    namespace {

        /// gtest cannot stream an EASTL string view
        std::string Text(eastl::string_view text) {
            return std::string(text.data(), text.size());
        }

    }  // namespace

    TEST(PromotionChoicesTest, HasTheFourPiecesQueenFirst) {
        const auto pieces = PromotionPieces();
        ASSERT_EQ(pieces.size(), 4u);
        EXPECT_TRUE(pieces[0] == PieceType::Queen);
        EXPECT_TRUE(pieces[1] == PieceType::Rook);
        EXPECT_TRUE(pieces[2] == PieceType::Bishop);
        EXPECT_TRUE(pieces[3] == PieceType::Knight);
    }

    TEST(PromotionChoicesTest, IdsAndLabelsArePinned) {
        EXPECT_EQ(Text(PromotionId(PieceType::Queen)), "queen");
        EXPECT_EQ(Text(PromotionId(PieceType::Rook)), "rook");
        EXPECT_EQ(Text(PromotionId(PieceType::Bishop)), "bishop");
        EXPECT_EQ(Text(PromotionId(PieceType::Knight)), "knight");
        EXPECT_EQ(Text(PromotionLabel(PieceType::Queen)), "Queen");
        EXPECT_EQ(Text(PromotionLabel(PieceType::Rook)), "Rook");
        EXPECT_EQ(Text(PromotionLabel(PieceType::Bishop)), "Bishop");
        EXPECT_EQ(Text(PromotionLabel(PieceType::Knight)), "Knight");
    }

    TEST(PromotionChoicesTest, IdsRoundTrip) {
        for (const PieceType piece : PromotionPieces()) {
            const auto found = FindPromotion(PromotionId(piece));
            ASSERT_TRUE(found.has_value()) << Text(PromotionId(piece));
            EXPECT_TRUE(*found == piece);
        }
    }

    TEST(PromotionChoicesTest, IdsAreLowerCaseAndUniqueAndLabelsAreWords) {
        for (const PieceType piece : PromotionPieces()) {
            const eastl::string_view id = PromotionId(piece);
            const eastl::string_view label = PromotionLabel(piece);
            ASSERT_FALSE(id.empty());
            ASSERT_FALSE(label.empty());
            for (const char c : id) {
                EXPECT_TRUE(c >= 'a' && c <= 'z') << Text(id);
            }
            EXPECT_TRUE(label[0] >= 'A' && label[0] <= 'Z') << Text(label);
            for (const PieceType other : PromotionPieces()) {
                if (other != piece) {
                    EXPECT_NE(PromotionId(other), id);
                    EXPECT_NE(PromotionLabel(other), label);
                }
            }
        }
    }

    TEST(PromotionChoicesTest, MatchingIsExact) {
        for (const char* text : {"", "Queen", "QUEEN", "quee", "queen ", " queen", "pawn", "king", "none", "n", "q", "Rook"}) {
            EXPECT_FALSE(FindPromotion(text).has_value()) << "\"" << text << "\"";
        }
    }

    TEST(PromotionChoicesTest, OtherPiecesHaveNoNameAndNoWord) {
        for (const PieceType piece : {PieceType::None, PieceType::Pawn, PieceType::King}) {
            EXPECT_TRUE(PromotionId(piece).empty());
            EXPECT_TRUE(PromotionLabel(piece).empty());
        }
    }

}  // namespace BECore::Chess::Tests
