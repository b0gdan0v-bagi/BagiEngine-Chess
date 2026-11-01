#include <Chess/Game/Move.h>
#include <Common/Printers.h>

namespace BECore::Chess::Tests {

    static_assert(sizeof(Move) == 2);
    static_assert(Move{}.IsNull());
    static_assert(!Move{Square{4, 1}, Square{4, 3}}.IsNull());

    TEST(MoveTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts above are the check";
    }

    TEST(MoveTest, PacksAndUnpacksEveryField) {
        const Move move{Square{4, 6}, Square{3, 7}, MoveKind::Promotion, PieceType::Rook};
        EXPECT_EQ(move.From(), (Square{4, 6}));
        EXPECT_EQ(move.To(), (Square{3, 7}));
        EXPECT_TRUE(move.Kind() == MoveKind::Promotion);
        EXPECT_TRUE(move.PromotionType() == PieceType::Rook);
        EXPECT_TRUE(move.IsPromotion());
    }

    TEST(MoveTest, EverySquarePairSurvivesPacking) {
        for (int from = 0; from < kSquareCount; ++from) {
            for (int to = 0; to < kSquareCount; ++to) {
                const Move move{Square::FromIndex(from), Square::FromIndex(to)};
                ASSERT_EQ(move.FromIndex(), from);
                ASSERT_EQ(move.ToIndex(), to);
            }
        }
    }

    TEST(MoveTest, KindsAndPromotionPiecesDoNotMix) {
        for (const auto kind : {MoveKind::Normal, MoveKind::Promotion, MoveKind::EnPassant, MoveKind::Castling}) {
            for (const auto piece : {PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen}) {
                const Move move{Square{0, 6}, Square{0, 7}, kind, piece};
                EXPECT_TRUE(move.Kind() == kind);
                EXPECT_TRUE(move.PromotionType() == piece);
                EXPECT_EQ(move.FromIndex(), (Square{0, 6}.Index()));
                EXPECT_EQ(move.ToIndex(), (Square{0, 7}.Index()));
            }
        }
    }

    TEST(MoveTest, UciForQuietMoveCaptureAndCastling) {
        EXPECT_EQ((Move(Square{4, 1}, Square{4, 3}).ToUci()), "e2e4");
        EXPECT_EQ((Move(Square{6, 7}, Square{5, 5}).ToUci()), "g8f6");
        EXPECT_EQ((Move(Square{4, 0}, Square{6, 0}, MoveKind::Castling).ToUci()), "e1g1");
    }

    TEST(MoveTest, UciForPromotionHasThePieceLetter) {
        EXPECT_EQ((Move(Square{4, 6}, Square{4, 7}, MoveKind::Promotion, PieceType::Queen).ToUci()), "e7e8q");
        EXPECT_EQ((Move(Square{4, 6}, Square{4, 7}, MoveKind::Promotion, PieceType::Knight).ToUci()), "e7e8n");
        EXPECT_EQ((Move(Square{0, 1}, Square{1, 0}, MoveKind::Promotion, PieceType::Rook).ToUci()), "a2b1r");
        EXPECT_EQ((Move(Square{0, 1}, Square{1, 0}, MoveKind::Promotion, PieceType::Bishop).ToUci()), "a2b1b");
    }

    TEST(MoveTest, NullMoveIsNullInUci) {
        EXPECT_EQ(Move{}.ToUci(), "0000");
    }

}  // namespace BECore::Chess::Tests
