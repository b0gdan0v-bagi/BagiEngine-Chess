#pragma once

#include <Chess/Game/ChessTypes.h>
#include <bit>

namespace BECore::Chess {

    /**
     * @brief A set of squares, one bit per square.
     *
     * Bit i is the square with Square::Index() == i, so a1 is bit 0, h1 bit 7, a8 bit 56 and h8 bit 63.
     */
    using Bitboard = uint64_t;

    constexpr Bitboard kFileA = 0x0101010101010101ULL;
    constexpr Bitboard kFileH = kFileA << 7;
    constexpr Bitboard kRank1 = 0xFFULL;
    constexpr Bitboard kRank2 = kRank1 << 8;
    constexpr Bitboard kRank3 = kRank1 << 16;
    constexpr Bitboard kRank4 = kRank1 << 24;
    constexpr Bitboard kRank5 = kRank1 << 32;
    constexpr Bitboard kRank6 = kRank1 << 40;
    constexpr Bitboard kRank7 = kRank1 << 48;
    constexpr Bitboard kRank8 = kRank1 << 56;

    /// a1 is a dark square, h1 a light one.
    constexpr Bitboard kLightSquares = 0x55AA55AA55AA55AAULL;
    constexpr Bitboard kDarkSquares = ~kLightSquares;

    [[nodiscard]] constexpr Bitboard SquareBB(int index) noexcept {
        return 1ULL << index;
    }

    [[nodiscard]] constexpr Bitboard SquareBB(Square s) noexcept {
        return SquareBB(s.Index());
    }

    [[nodiscard]] constexpr int PopCount(Bitboard bb) noexcept {
        return std::popcount(bb);
    }

    /// Index of the lowest set bit. The board must not be empty.
    [[nodiscard]] constexpr int Lsb(Bitboard bb) noexcept {
        return std::countr_zero(bb);
    }

    /// Index of the lowest set bit, which is cleared from @p bb. The board must not be empty.
    constexpr int PopLsb(Bitboard& bb) noexcept {
        const int index = Lsb(bb);
        bb &= bb - 1;
        return index;
    }

    [[nodiscard]] constexpr bool MoreThanOne(Bitboard bb) noexcept {
        return (bb & (bb - 1)) != 0;
    }

    // Shifts by one square. The ones that change the file mask out the file the bit would wrap around to.
    [[nodiscard]] constexpr Bitboard ShiftNorth(Bitboard bb) noexcept {
        return bb << 8;
    }
    [[nodiscard]] constexpr Bitboard ShiftSouth(Bitboard bb) noexcept {
        return bb >> 8;
    }
    [[nodiscard]] constexpr Bitboard ShiftEast(Bitboard bb) noexcept {
        return (bb & ~kFileH) << 1;
    }
    [[nodiscard]] constexpr Bitboard ShiftWest(Bitboard bb) noexcept {
        return (bb & ~kFileA) >> 1;
    }
    [[nodiscard]] constexpr Bitboard ShiftNorthEast(Bitboard bb) noexcept {
        return (bb & ~kFileH) << 9;
    }
    [[nodiscard]] constexpr Bitboard ShiftNorthWest(Bitboard bb) noexcept {
        return (bb & ~kFileA) << 7;
    }
    [[nodiscard]] constexpr Bitboard ShiftSouthEast(Bitboard bb) noexcept {
        return (bb & ~kFileH) >> 7;
    }
    [[nodiscard]] constexpr Bitboard ShiftSouthWest(Bitboard bb) noexcept {
        return (bb & ~kFileA) >> 9;
    }

}  // namespace BECore::Chess
