#include <Chess/Game/Bitboard.h>
#include <Common/Printers.h>

namespace BECore::Chess::Tests {

    static_assert(SquareBB(Square{0, 0}) == 1ULL);
    static_assert(SquareBB(Square{7, 7}) == 1ULL << 63);
    static_assert(kFileA == SquareBB(0) + SquareBB(8) + SquareBB(16) + SquareBB(24) + SquareBB(32) + SquareBB(40) + SquareBB(48) + SquareBB(56));
    static_assert(PopCount(kRank1 | kRank8) == 16);

    TEST(BitboardTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts above are the check";
    }

    TEST(BitboardTest, LsbAndPopLsbWalkTheSetBitsInOrder) {
        Bitboard bb = SquareBB(3) | SquareBB(17) | SquareBB(63);
        EXPECT_EQ(Lsb(bb), 3);
        EXPECT_EQ(PopLsb(bb), 3);
        EXPECT_EQ(PopLsb(bb), 17);
        EXPECT_EQ(PopLsb(bb), 63);
        EXPECT_EQ(bb, 0ULL);
    }

    TEST(BitboardTest, MoreThanOne) {
        EXPECT_FALSE(MoreThanOne(0));
        EXPECT_FALSE(MoreThanOne(SquareBB(12)));
        EXPECT_TRUE(MoreThanOne(SquareBB(12) | SquareBB(40)));
    }

    TEST(BitboardTest, ShiftsDoNotWrapAroundTheBoardEdge) {
        const Bitboard h4 = SquareBB(Square{7, 3});
        const Bitboard a4 = SquareBB(Square{0, 3});
        EXPECT_EQ(ShiftEast(h4), 0ULL);
        EXPECT_EQ(ShiftNorthEast(h4), 0ULL);
        EXPECT_EQ(ShiftSouthEast(h4), 0ULL);
        EXPECT_EQ(ShiftWest(a4), 0ULL);
        EXPECT_EQ(ShiftNorthWest(a4), 0ULL);
        EXPECT_EQ(ShiftSouthWest(a4), 0ULL);
    }

    TEST(BitboardTest, ShiftsMoveOneSquare) {
        const Bitboard d4 = SquareBB(Square{3, 3});
        EXPECT_EQ(ShiftNorth(d4), SquareBB(Square{3, 4}));
        EXPECT_EQ(ShiftSouth(d4), SquareBB(Square{3, 2}));
        EXPECT_EQ(ShiftEast(d4), SquareBB(Square{4, 3}));
        EXPECT_EQ(ShiftWest(d4), SquareBB(Square{2, 3}));
        EXPECT_EQ(ShiftNorthEast(d4), SquareBB(Square{4, 4}));
        EXPECT_EQ(ShiftNorthWest(d4), SquareBB(Square{2, 4}));
        EXPECT_EQ(ShiftSouthEast(d4), SquareBB(Square{4, 2}));
        EXPECT_EQ(ShiftSouthWest(d4), SquareBB(Square{2, 2}));
    }

    TEST(BitboardTest, ShiftsOffTheBoardEmptyTheBoard) {
        EXPECT_EQ(ShiftNorth(kRank8), 0ULL);
        EXPECT_EQ(ShiftSouth(kRank1), 0ULL);
    }

}  // namespace BECore::Chess::Tests
