#pragma once

#include <Chess/Game/Bitboard.h>

namespace BECore::Chess {

    using ZobristKey = uint64_t;

    /**
     * @brief Precomputed attack sets and hashing keys, built once on first use.
     *
     * Leapers (knight, king, pawn) are plain lookups. Rooks and bishops use magic bitboards: the blockers on a
     * piece's rays are masked out of the occupancy, multiplied by a per-square magic number and shifted to an index
     * into a table that holds the attack set for exactly that blocker pattern. The magics are searched for with a
     * seeded generator when the tables are built; a candidate is accepted only if it maps every blocker pattern to the
     * attack set the slow SlidingAttacks() gives for it.
     *
     * Squares are Square::Index() values (a1 = 0 ... h8 = 63). Immutable after construction, so it is safe to read
     * from any thread.
     */
    class AttackTables : public SingletonAtomic<AttackTables> {
        friend class SingletonAtomic<AttackTables>;

    public:
        [[nodiscard]] Bitboard Knight(int sq) const noexcept {
            return _knight[Cell(sq)];
        }
        [[nodiscard]] Bitboard King(int sq) const noexcept {
            return _king[Cell(sq)];
        }
        /// Squares a pawn of @p color standing on @p sq attacks (not where it pushes).
        [[nodiscard]] Bitboard Pawn(PieceColor color, int sq) const noexcept {
            return _pawn[static_cast<size_t>(color)][Cell(sq)];
        }

        [[nodiscard]] Bitboard Rook(int sq, Bitboard occupied) const noexcept {
            return _rookTable[_rookMagics[Cell(sq)].Slot(occupied)];
        }
        [[nodiscard]] Bitboard Bishop(int sq, Bitboard occupied) const noexcept {
            return _bishopTable[_bishopMagics[Cell(sq)].Slot(occupied)];
        }
        [[nodiscard]] Bitboard Queen(int sq, Bitboard occupied) const noexcept {
            return Rook(sq, occupied) | Bishop(sq, occupied);
        }

        /// Squares strictly between @p a and @p b when they share a rank, file or diagonal; empty otherwise.
        [[nodiscard]] Bitboard Between(int a, int b) const noexcept {
            return _between[Cell(a)][Cell(b)];
        }
        /// The whole rank, file or diagonal through @p a and @p b (both included); empty when they are not aligned.
        [[nodiscard]] Bitboard Line(int a, int b) const noexcept {
            return _line[Cell(a)][Cell(b)];
        }
        [[nodiscard]] bool Aligned(int a, int b, int c) const noexcept {
            return (Line(a, b) & SquareBB(c)) != 0;
        }

        [[nodiscard]] ZobristKey PieceKey(Piece piece, int sq) const noexcept {
            return _pieceKeys[static_cast<size_t>(piece.color)][static_cast<size_t>(piece.type)][Cell(sq)];
        }
        [[nodiscard]] ZobristKey CastlingKey(int rights) const noexcept {
            return _castlingKeys[Cell(rights)];
        }
        [[nodiscard]] ZobristKey EnPassantKey(int file) const noexcept {
            return _enPassantKeys[Cell(file)];
        }
        [[nodiscard]] ZobristKey SideKey() const noexcept {
            return _sideKey;
        }

        /**
         * @brief Reference (slow) attack set of a rook or bishop on @p sq with the given blockers, found by walking the rays.
         *
         * A blocker square is included in the result. Used to build and to test the magic tables.
         */
        [[nodiscard]] static Bitboard SlidingAttacks(PieceType slider, int sq, Bitboard occupied) noexcept;

    private:
        static constexpr size_t kCastlingStates = 16;
        static constexpr size_t kFiles = 8;

        struct Magic {
            Bitboard mask = 0;
            Bitboard magic = 0;
            size_t offset = 0;  ///< First entry of this square in the shared attack table.
            unsigned shift = 0;

            [[nodiscard]] size_t Slot(Bitboard occupied) const noexcept {
                return offset + static_cast<size_t>(((occupied & mask) * magic) >> shift);
            }
        };

        AttackTables();

        void InitLeapers();
        void InitSliders(PieceType slider, eastl::array<Magic, kSquareCount>& magics, eastl::vector<Bitboard>& table);
        void InitLines();
        void InitZobrist();

        static constexpr size_t Cell(int index) noexcept {
            return static_cast<size_t>(index);
        }

        eastl::array<Bitboard, kSquareCount> _knight{};
        eastl::array<Bitboard, kSquareCount> _king{};
        eastl::array<eastl::array<Bitboard, kSquareCount>, 2> _pawn{};

        eastl::array<Magic, kSquareCount> _rookMagics{};
        eastl::array<Magic, kSquareCount> _bishopMagics{};
        eastl::vector<Bitboard> _rookTable;
        eastl::vector<Bitboard> _bishopTable;

        eastl::array<eastl::array<Bitboard, kSquareCount>, kSquareCount> _between{};
        eastl::array<eastl::array<Bitboard, kSquareCount>, kSquareCount> _line{};

        eastl::array<eastl::array<eastl::array<ZobristKey, kSquareCount>, 7>, 2> _pieceKeys{};
        eastl::array<ZobristKey, kCastlingStates> _castlingKeys{};
        eastl::array<ZobristKey, kFiles> _enPassantKeys{};
        ZobristKey _sideKey = 0;
    };

}  // namespace BECore::Chess
