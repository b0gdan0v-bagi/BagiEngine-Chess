#include "Premove.h"

namespace BECore::Chess {

    namespace {

        constexpr int kWhitePawnStartRank = 1;
        constexpr int kBlackPawnStartRank = 6;
        constexpr int kWhiteLastRank = 7;
        constexpr int kBlackLastRank = 0;

        constexpr size_t Index(PieceColor color) {
            return static_cast<size_t>(color);
        }

        constexpr size_t Index(Square square) {
            return static_cast<size_t>(square.Index());
        }

        Bitboard PawnTargets(PieceColor color, Square from, Bitboard own) {
            const bool white = color == PieceColor::White;
            const auto forward = [white](Bitboard bb) { return white ? ShiftNorth(bb) : ShiftSouth(bb); };

            Bitboard targets = AttackTables::GetInstance().Pawn(color, from.Index());
            const Bitboard one = forward(SquareBB(from));
            if ((one & own) == 0) {
                targets |= one;
                if (from.rank == (white ? kWhitePawnStartRank : kBlackPawnStartRank)) {
                    targets |= forward(one) & ~own;
                }
            }
            return targets;
        }

    }  // namespace

    PremoveBoard::PremoveBoard(const Position& position) {
        for (int index = 0; index < kSquareCount; ++index) {
            _cells[static_cast<size_t>(index)] = position.PieceAt(index);
        }
        _byColor[Index(PieceColor::White)] = position.Pieces(PieceColor::White);
        _byColor[Index(PieceColor::Black)] = position.Pieces(PieceColor::Black);
    }

    Piece PremoveBoard::GetPieceAt(Square square) const noexcept {
        return square.IsInside() ? _cells[Index(square)] : Piece{};
    }

    bool PremoveBoard::IsOwnPiece(Square square, PieceColor color) const noexcept {
        return square.IsInside() && (_byColor[Index(color)] & SquareBB(square)) != 0;
    }

    eastl::vector<Square> PremoveBoard::Targets(Square from, PieceColor color) const {
        eastl::vector<Square> result;
        const Piece piece = GetPieceAt(from);
        if (piece.IsEmpty() || piece.color != color) {
            return result;
        }

        const AttackTables& tables = AttackTables::GetInstance();
        const Bitboard own = _byColor[Index(color)];
        const int index = from.Index();

        Bitboard targets = 0;
        switch (piece.type) {
            case PieceType::Pawn:
                targets = PawnTargets(color, from, own);
                break;
            case PieceType::Knight:
                targets = tables.Knight(index);
                break;
            case PieceType::Bishop:
                targets = tables.Bishop(index, own);
                break;
            case PieceType::Rook:
                targets = tables.Rook(index, own);
                break;
            case PieceType::Queen:
                targets = tables.Queen(index, own);
                break;
            case PieceType::King:
                targets = tables.King(index);
                break;
            case PieceType::None:
                break;
        }

        targets &= ~own;
        while (targets != 0) {
            result.push_back(Square::FromIndex(PopLsb(targets)));
        }
        return result;
    }

    void PremoveBoard::Apply(const Premove& premove) {
        if (!premove.from.IsInside() || !premove.to.IsInside()) {
            return;
        }
        Piece piece = _cells[Index(premove.from)];
        if (piece.IsEmpty()) {
            return;
        }

        const int lastRank = piece.color == PieceColor::White ? kWhiteLastRank : kBlackLastRank;
        if (piece.type == PieceType::Pawn && premove.to.rank == lastRank) {
            piece.type = PieceType::Queen;
        }

        _byColor[Index(Opposite(piece.color))] &= ~SquareBB(premove.to);
        _byColor[Index(piece.color)] &= ~SquareBB(premove.from);
        _byColor[Index(piece.color)] |= SquareBB(premove.to);
        _cells[Index(premove.from)] = Piece{};
        _cells[Index(premove.to)] = piece;
    }

}  // namespace BECore::Chess
