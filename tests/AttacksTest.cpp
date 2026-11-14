#include <Chess/Game/Attacks.h>
#include <Common/Printers.h>
#include <EASTL/hash_set.h>

namespace BECore::Chess::Tests {

    namespace {

        constexpr int kRandomOccupanciesPerSquare = 300;
        constexpr uint64_t kTestSeed = 0x9E3779B97F4A7C15ULL;

        // splitmix64: enough randomness to spread blockers over the board, and repeatable.
        uint64_t NextRandom(uint64_t& state) {
            state += 0x9E3779B97F4A7C15ULL;
            uint64_t z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            return z ^ (z >> 31);
        }

        int Idx(int file, int rank) {
            return Square{static_cast<int8_t>(file), static_cast<int8_t>(rank)}.Index();
        }

        const AttackTables& Tables() {
            return AttackTables::GetInstance();
        }

    }  // namespace

    TEST(AttacksTest, KnightReach) {
        EXPECT_EQ(PopCount(Tables().Knight(Idx(0, 0))), 2);
        EXPECT_EQ(Tables().Knight(Idx(0, 0)), SquareBB(Idx(1, 2)) | SquareBB(Idx(2, 1)));
        EXPECT_EQ(PopCount(Tables().Knight(Idx(3, 3))), 8);
        EXPECT_EQ(PopCount(Tables().Knight(Idx(7, 7))), 2);
    }

    TEST(AttacksTest, KingReach) {
        EXPECT_EQ(PopCount(Tables().King(Idx(0, 0))), 3);
        EXPECT_EQ(PopCount(Tables().King(Idx(4, 0))), 5);
        EXPECT_EQ(PopCount(Tables().King(Idx(4, 4))), 8);
    }

    TEST(AttacksTest, PawnAttacksGoForwardOnly) {
        EXPECT_EQ(Tables().Pawn(PieceColor::White, Idx(4, 3)), SquareBB(Idx(3, 4)) | SquareBB(Idx(5, 4)));
        EXPECT_EQ(Tables().Pawn(PieceColor::Black, Idx(4, 3)), SquareBB(Idx(3, 2)) | SquareBB(Idx(5, 2)));
    }

    TEST(AttacksTest, PawnOnTheEdgeFileAttacksOneSquare) {
        EXPECT_EQ(Tables().Pawn(PieceColor::White, Idx(0, 1)), SquareBB(Idx(1, 2)));
        EXPECT_EQ(Tables().Pawn(PieceColor::White, Idx(7, 1)), SquareBB(Idx(6, 2)));
        EXPECT_EQ(Tables().Pawn(PieceColor::Black, Idx(0, 6)), SquareBB(Idx(1, 5)));
    }

    TEST(AttacksTest, PawnOnTheLastRankAttacksNothing) {
        EXPECT_EQ(Tables().Pawn(PieceColor::White, Idx(4, 7)), 0ULL);
        EXPECT_EQ(Tables().Pawn(PieceColor::Black, Idx(4, 0)), 0ULL);
    }

    TEST(AttacksTest, SlidersOnAnEmptyBoard) {
        for (int sq = 0; sq < kSquareCount; ++sq) {
            EXPECT_EQ(PopCount(Tables().Rook(sq, 0)), 14) << "square " << sq;
        }
        EXPECT_EQ(PopCount(Tables().Bishop(Idx(3, 3), 0)), 13);
        EXPECT_EQ(PopCount(Tables().Bishop(Idx(0, 0), 0)), 7);
        EXPECT_EQ(PopCount(Tables().Queen(Idx(3, 3), 0)), 27);
    }

    TEST(AttacksTest, RookStopsAtTheFirstBlockerAndIncludesIt) {
        const Bitboard blockers = SquareBB(Idx(4, 6)) | SquareBB(Idx(1, 4));
        const Bitboard attacks = Tables().Rook(Idx(4, 4), blockers);
        EXPECT_NE(attacks & SquareBB(Idx(4, 6)), 0ULL) << "the blocker itself is attacked";
        EXPECT_EQ(attacks & SquareBB(Idx(4, 7)), 0ULL) << "nothing behind the blocker";
        EXPECT_NE(attacks & SquareBB(Idx(1, 4)), 0ULL);
        EXPECT_EQ(attacks & SquareBB(Idx(0, 4)), 0ULL);
    }

    TEST(AttacksTest, MagicLookupMatchesTheRayWalkOnRandomBlockers) {
        uint64_t state = kTestSeed;
        for (int sq = 0; sq < kSquareCount; ++sq) {
            for (int i = 0; i < kRandomOccupanciesPerSquare; ++i) {
                // Mixed densities: sparse, medium and nearly full boards.
                Bitboard occupied = NextRandom(state);
                if (i % 3 == 1) {
                    occupied &= NextRandom(state);
                } else if (i % 3 == 2) {
                    occupied |= NextRandom(state);
                }
                ASSERT_EQ(Tables().Rook(sq, occupied), AttackTables::SlidingAttacks(PieceType::Rook, sq, occupied)) << "rook, square " << sq;
                ASSERT_EQ(Tables().Bishop(sq, occupied), AttackTables::SlidingAttacks(PieceType::Bishop, sq, occupied)) << "bishop, square " << sq;
            }
        }
    }

    TEST(AttacksTest, OccupancyOfTheOriginSquareDoesNotChangeTheResult) {
        const int sq = Idx(3, 3);
        EXPECT_EQ(Tables().Rook(sq, SquareBB(sq)), Tables().Rook(sq, 0));
        EXPECT_EQ(Tables().Bishop(sq, SquareBB(sq)), Tables().Bishop(sq, 0));
    }

    TEST(AttacksTest, BetweenIsTheOpenSegment) {
        EXPECT_EQ(Tables().Between(Idx(4, 0), Idx(4, 3)), SquareBB(Idx(4, 1)) | SquareBB(Idx(4, 2)));
        EXPECT_EQ(Tables().Between(Idx(0, 0), Idx(3, 3)), SquareBB(Idx(1, 1)) | SquareBB(Idx(2, 2)));
        EXPECT_EQ(Tables().Between(Idx(4, 3), Idx(4, 0)), SquareBB(Idx(4, 1)) | SquareBB(Idx(4, 2)));
        EXPECT_EQ(Tables().Between(Idx(4, 0), Idx(4, 1)), 0ULL) << "neighbours have nothing between them";
    }

    TEST(AttacksTest, BetweenOfUnalignedSquaresIsEmpty) {
        EXPECT_EQ(Tables().Between(Idx(0, 0), Idx(1, 2)), 0ULL);
        EXPECT_EQ(Tables().Between(Idx(0, 0), Idx(0, 0)), 0ULL);
    }

    TEST(AttacksTest, LineCoversTheWholeRankFileOrDiagonal) {
        EXPECT_EQ(Tables().Line(Idx(2, 0), Idx(5, 0)), kRank1);
        EXPECT_EQ(Tables().Line(Idx(3, 1), Idx(3, 6)), kFileA << 3);
        EXPECT_EQ(PopCount(Tables().Line(Idx(1, 1), Idx(2, 2))), 8) << "the long diagonal a1-h8";
        EXPECT_EQ(Tables().Line(Idx(0, 0), Idx(1, 2)), 0ULL);
    }

    TEST(AttacksTest, AlignedTellsWhetherThreeSquaresShareALine) {
        EXPECT_TRUE(Tables().Aligned(Idx(4, 0), Idx(4, 3), Idx(4, 7)));
        EXPECT_TRUE(Tables().Aligned(Idx(0, 0), Idx(3, 3), Idx(7, 7)));
        EXPECT_FALSE(Tables().Aligned(Idx(4, 0), Idx(4, 3), Idx(5, 7)));
    }

    TEST(AttacksTest, ZobristKeysAreDistinctAndNonZero) {
        HashSet<ZobristKey> seen;
        size_t count = 0;
        const auto add = [&](ZobristKey key) {
            EXPECT_NE(key, 0ULL);
            seen.insert(key);
            ++count;
        };

        for (const auto color : {PieceColor::White, PieceColor::Black}) {
            for (const auto type : {PieceType::Pawn, PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen, PieceType::King}) {
                for (int sq = 0; sq < kSquareCount; ++sq) {
                    add(Tables().PieceKey(Piece{type, color}, sq));
                }
            }
        }
        for (int rights = 0; rights < 16; ++rights) {
            add(Tables().CastlingKey(rights));
        }
        for (int file = 0; file < 8; ++file) {
            add(Tables().EnPassantKey(file));
        }
        add(Tables().SideKey());

        EXPECT_EQ(seen.size(), count);
    }

}  // namespace BECore::Chess::Tests
