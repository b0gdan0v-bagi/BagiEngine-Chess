#include "MoveGenerator.h"

namespace BECore::Chess {

    namespace {

        struct Offset {
            int dFile;
            int dRank;
        };

        constexpr eastl::array<Offset, 4> kRookDirs{Offset{1, 0}, Offset{-1, 0}, Offset{0, 1}, Offset{0, -1}};

        constexpr eastl::array<Offset, 4> kBishopDirs{Offset{1, 1}, Offset{1, -1}, Offset{-1, 1}, Offset{-1, -1}};

        constexpr eastl::array<Offset, 8> kQueenDirs{Offset{1, 0}, Offset{-1, 0}, Offset{0, 1}, Offset{0, -1}, Offset{1, 1}, Offset{1, -1}, Offset{-1, 1}, Offset{-1, -1}};

        constexpr eastl::array<Offset, 8> kKingDirs{Offset{1, 0}, Offset{-1, 0}, Offset{0, 1}, Offset{0, -1}, Offset{1, 1}, Offset{1, -1}, Offset{-1, 1}, Offset{-1, -1}};

        constexpr eastl::array<Offset, 8> kKnightDirs{Offset{1, 2}, Offset{2, 1}, Offset{-1, 2}, Offset{-2, 1}, Offset{1, -2}, Offset{2, -1}, Offset{-1, -2}, Offset{-2, -1}};

        // Walk in a single direction until we step off-board or hit a piece.
        // Empty squares are added; an enemy piece is added then we stop;
        // a friendly piece stops without being added.
        void RaySlide(const ChessBoard& board, Square from, Offset dir, PieceColor mover, eastl::vector<Square>& out) {
            int file = from.file + dir.dFile;
            int rank = from.rank + dir.dRank;
            while (Square::IsInside(file, rank)) {
                const Square target{static_cast<int8_t>(file), static_cast<int8_t>(rank)};
                const Piece occupant = board.Get(target);
                if (occupant.IsEmpty()) {
                    out.push_back(target);
                } else {
                    if (occupant.color != mover) {
                        out.push_back(target);
                    }
                    return;
                }
                file += dir.dFile;
                rank += dir.dRank;
            }
        }

        void TryStep(const ChessBoard& board, Square from, Offset dir, PieceColor mover, eastl::vector<Square>& out) {
            const int file = from.file + dir.dFile;
            const int rank = from.rank + dir.dRank;
            if (!Square::IsInside(file, rank)) {
                return;
            }
            const Square target{static_cast<int8_t>(file), static_cast<int8_t>(rank)};
            const Piece occupant = board.Get(target);
            if (occupant.IsEmpty() || occupant.color != mover) {
                out.push_back(target);
            }
        }

        void GeneratePawn(const ChessBoard& board, Square from, PieceColor mover, eastl::vector<Square>& out) {
            const int dir = (mover == PieceColor::White) ? 1 : -1;
            const int startRank = (mover == PieceColor::White) ? 1 : 6;

            // 1 forward
            const int oneRank = from.rank + dir;
            if (Square::IsInside(from.file, oneRank)) {
                const Square one{from.file, static_cast<int8_t>(oneRank)};
                if (board.Get(one).IsEmpty()) {
                    out.push_back(one);
                    // 2 forward (only from start rank, only if both squares empty)
                    if (from.rank == startRank) {
                        const Square two{from.file, static_cast<int8_t>(oneRank + dir)};
                        if (board.Get(two).IsEmpty()) {
                            out.push_back(two);
                        }
                    }
                }
            }

            // Diagonal captures
            for (int df : {-1, 1}) {
                const int file = from.file + df;
                const int rank = from.rank + dir;
                if (!Square::IsInside(file, rank)) {
                    continue;
                }
                const Square target{static_cast<int8_t>(file), static_cast<int8_t>(rank)};
                const Piece occupant = board.Get(target);
                if (!occupant.IsEmpty() && occupant.color != mover) {
                    out.push_back(target);
                }
            }
        }

    }  // namespace

    eastl::vector<Square> MoveGenerator::Generate(const ChessBoard& board, Square from) {
        eastl::vector<Square> moves;
        if (!from.IsInside()) {
            return moves;
        }
        const Piece piece = board.Get(from);
        if (piece.IsEmpty()) {
            return moves;
        }

        switch (piece.type) {
            case PieceType::Pawn:
                GeneratePawn(board, from, piece.color, moves);
                break;
            case PieceType::Knight:
                for (auto d : kKnightDirs) {
                    TryStep(board, from, d, piece.color, moves);
                }
                break;
            case PieceType::Bishop:
                for (auto d : kBishopDirs) {
                    RaySlide(board, from, d, piece.color, moves);
                }
                break;
            case PieceType::Rook:
                for (auto d : kRookDirs) {
                    RaySlide(board, from, d, piece.color, moves);
                }
                break;
            case PieceType::Queen:
                for (auto d : kQueenDirs) {
                    RaySlide(board, from, d, piece.color, moves);
                }
                break;
            case PieceType::King:
                for (auto d : kKingDirs) {
                    TryStep(board, from, d, piece.color, moves);
                }
                break;
            case PieceType::None:
                break;
        }
        return moves;
    }

}  // namespace BECore::Chess
