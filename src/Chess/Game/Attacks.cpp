#include "Attacks.h"

namespace BECore::Chess {

    namespace {

        struct Direction {
            int dFile;
            int dRank;
        };

        constexpr Array<Direction, 4> kRookDirections{Direction{1, 0}, Direction{-1, 0}, Direction{0, 1}, Direction{0, -1}};
        constexpr Array<Direction, 4> kBishopDirections{Direction{1, 1}, Direction{1, -1}, Direction{-1, 1}, Direction{-1, -1}};
        constexpr Array<Direction, 8> kKnightSteps{Direction{1, 2}, Direction{2, 1}, Direction{-1, 2}, Direction{-2, 1}, Direction{1, -2}, Direction{2, -1}, Direction{-1, -2}, Direction{-2, -1}};
        constexpr Array<Direction, 8> kKingSteps{Direction{1, 0}, Direction{-1, 0}, Direction{0, 1}, Direction{0, -1}, Direction{1, 1}, Direction{1, -1}, Direction{-1, 1}, Direction{-1, -1}};

        constexpr int kBoardSide = 8;
        constexpr size_t kMaxBlockerPatterns = 1u << 12;  // a rook in a corner: 12 ray squares that can hold a blocker

        // One seed per rank for the magic search; any seed works, these only make the build repeatable.
        constexpr Array<uint64_t, kBoardSide> kMagicSeeds{728, 10316, 55013, 32803, 12281, 15100, 16645, 255};
        constexpr uint64_t kZobristSeed = 1070372;

        // A magic whose product leaves fewer than this many bits set in the top byte is too weak to be worth testing.
        constexpr int kMinTopByteBits = 6;
        constexpr int kTopByteShift = 56;

        // xorshift64*: small, fast and good enough for hashing keys and for the magic search.
        class Prng {
        public:
            explicit constexpr Prng(uint64_t seed) noexcept : _state(seed) {}

            constexpr uint64_t Next() noexcept {
                _state ^= _state >> 12;
                _state ^= _state << 25;
                _state ^= _state >> 27;
                return _state * 2685821657736338717ULL;
            }

            // Few set bits: magics with many zeros are far more likely to map blocker patterns without collisions.
            constexpr uint64_t NextSparse() noexcept {
                return Next() & Next() & Next();
            }

        private:
            uint64_t _state;
        };

        template <size_t N>
        Bitboard StepAttacks(int sq, const Array<Direction, N>& steps) {
            Bitboard attacks = 0;
            const int file = sq % kBoardSide;
            const int rank = sq / kBoardSide;
            for (const auto& step : steps) {
                const int targetFile = file + step.dFile;
                const int targetRank = rank + step.dRank;
                if (Square::IsInside(targetFile, targetRank)) {
                    attacks |= SquareBB(targetRank * kBoardSide + targetFile);
                }
            }
            return attacks;
        }

    }  // namespace

    AttackTables::AttackTables() {
        InitLeapers();
        InitSliders(PieceType::Rook, _rookMagics, _rookTable);
        InitSliders(PieceType::Bishop, _bishopMagics, _bishopTable);
        InitLines();
        InitZobrist();
    }

    Bitboard AttackTables::SlidingAttacks(PieceType slider, int sq, Bitboard occupied) noexcept {
        const auto& directions = slider == PieceType::Rook ? kRookDirections : kBishopDirections;
        Bitboard attacks = 0;
        for (const auto& dir : directions) {
            int file = sq % kBoardSide + dir.dFile;
            int rank = sq / kBoardSide + dir.dRank;
            while (Square::IsInside(file, rank)) {
                const Bitboard bit = SquareBB(rank * kBoardSide + file);
                attacks |= bit;
                if ((occupied & bit) != 0) {
                    break;
                }
                file += dir.dFile;
                rank += dir.dRank;
            }
        }
        return attacks;
    }

    void AttackTables::InitLeapers() {
        for (int sq = 0; sq < kSquareCount; ++sq) {
            const Bitboard bit = SquareBB(sq);
            _knight[Cell(sq)] = StepAttacks(sq, kKnightSteps);
            _king[Cell(sq)] = StepAttacks(sq, kKingSteps);
            _pawn[static_cast<size_t>(PieceColor::White)][Cell(sq)] = ShiftNorthEast(bit) | ShiftNorthWest(bit);
            _pawn[static_cast<size_t>(PieceColor::Black)][Cell(sq)] = ShiftSouthEast(bit) | ShiftSouthWest(bit);
        }
    }

    void AttackTables::InitSliders(PieceType slider, Array<Magic, kSquareCount>& magics, Vector<Bitboard>& table) {
        Array<Bitboard, kMaxBlockerPatterns> occupancy{};
        Array<Bitboard, kMaxBlockerPatterns> reference{};
        Array<int, kMaxBlockerPatterns> epoch{};

        for (int sq = 0; sq < kSquareCount; ++sq) {
            const int file = sq % kBoardSide;
            const int rank = sq / kBoardSide;

            // The last square of a ray never changes where the ray stops, so it is not part of the mask. The edge the piece itself
            // stands on is kept: its ray squares do matter.
            const Bitboard edges = ((kRank1 | kRank8) & ~(kRank1 << (kBoardSide * rank))) | ((kFileA | kFileH) & ~(kFileA << file));

            Magic& magic = magics[Cell(sq)];
            magic.mask = SlidingAttacks(slider, sq, 0) & ~edges;
            magic.shift = static_cast<unsigned>(kSquareCount - PopCount(magic.mask));
            magic.offset = table.size();

            // Every blocker pattern on the mask (the carry-rippler walks all subsets) and the attack set it produces.
            size_t patterns = 0;
            Bitboard subset = 0;
            do {
                occupancy[patterns] = subset;
                reference[patterns] = SlidingAttacks(slider, sq, subset);
                ++patterns;
                subset = (subset - magic.mask) & magic.mask;
            } while (subset != 0);

            table.resize(magic.offset + patterns);
            epoch.fill(0);

            // Try sparse random numbers until one maps every pattern to its own slot, or to a slot that already holds the
            // same attack set. A slot is stamped with the attempt that wrote it, so the table is never cleared between attempts.
            Prng prng(kMagicSeeds[Cell(rank)]);
            int attempt = 0;
            bool found = false;
            while (!found) {
                do {
                    magic.magic = prng.NextSparse();
                } while (PopCount((magic.mask * magic.magic) >> kTopByteShift) < kMinTopByteBits);

                ++attempt;
                found = true;
                for (size_t i = 0; i < patterns; ++i) {
                    const size_t slot = static_cast<size_t>((occupancy[i] * magic.magic) >> magic.shift);
                    if (epoch[slot] < attempt) {
                        epoch[slot] = attempt;
                        table[magic.offset + slot] = reference[i];
                    } else if (table[magic.offset + slot] != reference[i]) {
                        found = false;
                        break;
                    }
                }
            }
        }
    }

    void AttackTables::InitLines() {
        for (int a = 0; a < kSquareCount; ++a) {
            for (int b = 0; b < kSquareCount; ++b) {
                if (a == b) {
                    continue;
                }
                for (const PieceType slider : {PieceType::Rook, PieceType::Bishop}) {
                    const Bitboard fromA = SlidingAttacks(slider, a, 0);
                    if ((fromA & SquareBB(b)) == 0) {
                        continue;
                    }
                    _line[Cell(a)][Cell(b)] = (fromA & SlidingAttacks(slider, b, 0)) | SquareBB(a) | SquareBB(b);
                    _between[Cell(a)][Cell(b)] = SlidingAttacks(slider, a, SquareBB(b)) & SlidingAttacks(slider, b, SquareBB(a));
                }
            }
        }
    }

    void AttackTables::InitZobrist() {
        Prng prng(kZobristSeed);
        for (auto& perColor : _pieceKeys) {
            for (auto& perType : perColor) {
                for (auto& key : perType) {
                    key = prng.Next();
                }
            }
        }
        for (auto& key : _castlingKeys) {
            key = prng.Next();
        }
        for (auto& key : _enPassantKeys) {
            key = prng.Next();
        }
        _sideKey = prng.Next();
    }

}  // namespace BECore::Chess
