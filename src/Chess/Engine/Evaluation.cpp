#include "Evaluation.h"

namespace BECore::Chess {

    namespace {

        using PieceSquareTable = eastl::array<int, kSquareCount>;

        /// Black reads a table at the square flipped over the horizontal middle: a1 <-> a8
        constexpr int kMirrorMask = 56;

        // The tables are written rank 1 first, a1 first in a row, so the index is the square index of Bitboard.h (what is
        // written at the top is White's back rank). Values are the simplified evaluation function of the chess programming
        // wiki, in centipawns, from White's point of view.
        constexpr PieceSquareTable kPawnTable{
            0,  0,  0,   0,   0,   0,   0,  0,   // rank 1
            5,  10, 10,  -20, -20, 10,  10, 5,   // rank 2
            5,  -5, -10, 0,   0,   -10, -5, 5,   // rank 3
            0,  0,  0,   20,  20,  0,   0,  0,   // rank 4
            5,  5,  10,  25,  25,  10,  5,  5,   // rank 5
            10, 10, 20,  30,  30,  20,  10, 10,  // rank 6
            50, 50, 50,  50,  50,  50,  50, 50,  // rank 7
            0,  0,  0,   0,   0,   0,   0,  0,   // rank 8
        };

        constexpr PieceSquareTable kKnightTable{
            -50, -40, -30, -30, -30, -30, -40, -50,  // rank 1
            -40, -20, 0,   5,   5,   0,   -20, -40,  // rank 2
            -30, 5,   10,  15,  15,  10,  5,   -30,  // rank 3
            -30, 0,   15,  20,  20,  15,  0,   -30,  // rank 4
            -30, 5,   15,  20,  20,  15,  5,   -30,  // rank 5
            -30, 0,   10,  15,  15,  10,  0,   -30,  // rank 6
            -40, -20, 0,   0,   0,   0,   -20, -40,  // rank 7
            -50, -40, -30, -30, -30, -30, -40, -50,  // rank 8
        };

        constexpr PieceSquareTable kBishopTable{
            -20, -10, -10, -10, -10, -10, -10, -20,  // rank 1
            -10, 5,   0,   0,   0,   0,   5,   -10,  // rank 2
            -10, 10,  10,  10,  10,  10,  10,  -10,  // rank 3
            -10, 0,   10,  10,  10,  10,  0,   -10,  // rank 4
            -10, 5,   5,   10,  10,  5,   5,   -10,  // rank 5
            -10, 0,   5,   10,  10,  5,   0,   -10,  // rank 6
            -10, 0,   0,   0,   0,   0,   0,   -10,  // rank 7
            -20, -10, -10, -10, -10, -10, -10, -20,  // rank 8
        };

        constexpr PieceSquareTable kRookTable{
            0,  0,  0,  5,  5,  0,  0,  0,   // rank 1
            -5, 0,  0,  0,  0,  0,  0,  -5,  // rank 2
            -5, 0,  0,  0,  0,  0,  0,  -5,  // rank 3
            -5, 0,  0,  0,  0,  0,  0,  -5,  // rank 4
            -5, 0,  0,  0,  0,  0,  0,  -5,  // rank 5
            -5, 0,  0,  0,  0,  0,  0,  -5,  // rank 6
            5,  10, 10, 10, 10, 10, 10, 5,   // rank 7
            0,  0,  0,  0,  0,  0,  0,  0,   // rank 8
        };

        constexpr PieceSquareTable kQueenTable{
            -20, -10, -10, -5, -5, -10, -10, -20,  // rank 1
            -10, 0,   5,   0,  0,  0,   0,   -10,  // rank 2
            -10, 5,   5,   5,  5,  5,   0,   -10,  // rank 3
            0,   0,   5,   5,  5,  5,   0,   -5,   // rank 4
            -5,  0,   5,   5,  5,  5,   0,   -5,   // rank 5
            -10, 0,   5,   5,  5,  5,   0,   -10,  // rank 6
            -10, 0,   0,   0,  0,  0,   0,   -10,  // rank 7
            -20, -10, -10, -5, -5, -10, -10, -20,  // rank 8
        };

        constexpr PieceSquareTable kKingMiddlegameTable{
            20,  30,  10,  0,   0,   10,  30,  20,   // rank 1
            20,  20,  0,   0,   0,   0,   20,  20,   // rank 2
            -10, -20, -20, -20, -20, -20, -20, -10,  // rank 3
            -20, -30, -30, -40, -40, -30, -30, -20,  // rank 4
            -30, -40, -40, -50, -50, -40, -40, -30,  // rank 5
            -30, -40, -40, -50, -50, -40, -40, -30,  // rank 6
            -30, -40, -40, -50, -50, -40, -40, -30,  // rank 7
            -30, -40, -40, -50, -50, -40, -40, -30,  // rank 8
        };

        constexpr PieceSquareTable kKingEndgameTable{
            -50, -30, -30, -30, -30, -30, -30, -50,  // rank 1
            -30, -30, 0,   0,   0,   0,   -30, -30,  // rank 2
            -30, -10, 20,  30,  30,  20,  -10, -30,  // rank 3
            -30, -10, 30,  40,  40,  30,  -10, -30,  // rank 4
            -30, -10, 30,  40,  40,  30,  -10, -30,  // rank 5
            -30, -10, 20,  30,  30,  20,  -10, -30,  // rank 6
            -30, -20, -10, 0,   0,   -10, -20, -30,  // rank 7
            -50, -40, -30, -20, -20, -30, -40, -50,  // rank 8
        };

        static_assert(kPawnTable.size() == kSquareCount);

        const PieceSquareTable& TableOf(PieceType type, bool endgame) noexcept {
            switch (type) {
                case PieceType::Pawn:
                    return kPawnTable;
                case PieceType::Knight:
                    return kKnightTable;
                case PieceType::Bishop:
                    return kBishopTable;
                case PieceType::Rook:
                    return kRookTable;
                case PieceType::Queen:
                    return kQueenTable;
                case PieceType::King:
                case PieceType::None:
                    break;
            }
            return endgame ? kKingEndgameTable : kKingMiddlegameTable;
        }

        /// The pieces of @p color as White would see them: material and tables, in centipawns
        int ScoreOf(const Position& position, PieceColor color, bool endgame) noexcept {
            int score = 0;
            for (const PieceType type : {PieceType::Pawn, PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen, PieceType::King}) {
                const PieceSquareTable& table = TableOf(type, endgame);
                const int value = type == PieceType::King ? 0 : PieceValue(type);
                Bitboard pieces = position.Pieces(color, type);
                while (pieces != 0) {
                    const int square = PopLsb(pieces);
                    const int index = color == PieceColor::White ? square : square ^ kMirrorMask;
                    score += value + table[static_cast<size_t>(index)];
                }
            }
            return score;
        }

        int NonPawnMaterial(const Position& position) noexcept {
            return PopCount(position.Pieces(PieceType::Knight)) * kKnightValue + PopCount(position.Pieces(PieceType::Bishop)) * kBishopValue + PopCount(position.Pieces(PieceType::Rook)) * kRookValue +
                   PopCount(position.Pieces(PieceType::Queen)) * kQueenValue;
        }

    }  // namespace

    int PieceValue(PieceType type) noexcept {
        switch (type) {
            case PieceType::Pawn:
                return kPawnValue;
            case PieceType::Knight:
                return kKnightValue;
            case PieceType::Bishop:
                return kBishopValue;
            case PieceType::Rook:
                return kRookValue;
            case PieceType::Queen:
                return kQueenValue;
            case PieceType::King:
                return kKingValue;
            case PieceType::None:
                break;
        }
        return 0;
    }

    bool IsEndgame(const Position& position) noexcept {
        return NonPawnMaterial(position) <= kEndgameMaterialLimit;
    }

    int Evaluate(const Position& position) noexcept {
        const bool endgame = IsEndgame(position);
        const int white = ScoreOf(position, PieceColor::White, endgame);
        const int black = ScoreOf(position, PieceColor::Black, endgame);
        const int fromWhite = white - black;
        return position.SideToMove() == PieceColor::White ? fromWhite : -fromWhite;
    }

}  // namespace BECore::Chess
