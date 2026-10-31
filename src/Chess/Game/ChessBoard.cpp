#include "ChessBoard.h"

namespace BECore::Chess {

    namespace {
        constexpr eastl::array<PieceType, 8> kBackRank{PieceType::Rook, PieceType::Knight, PieceType::Bishop, PieceType::Queen, PieceType::King, PieceType::Bishop, PieceType::Knight, PieceType::Rook};
    }

    void ChessBoard::Clear() noexcept {
        _cells.fill(Piece{});
    }

    void ChessBoard::ResetToStart() noexcept {
        Clear();
        for (int file = 0; file < 8; ++file) {
            Set(Square{static_cast<int8_t>(file), 0}, Piece{kBackRank[static_cast<size_t>(file)], PieceColor::White});
            Set(Square{static_cast<int8_t>(file), 1}, Piece{PieceType::Pawn, PieceColor::White});
            Set(Square{static_cast<int8_t>(file), 6}, Piece{PieceType::Pawn, PieceColor::Black});
            Set(Square{static_cast<int8_t>(file), 7}, Piece{kBackRank[static_cast<size_t>(file)], PieceColor::Black});
        }
    }

    Piece ChessBoard::Get(Square s) const noexcept {
        if (!s.IsInside()) {
            return Piece{};
        }
        return _cells[static_cast<size_t>(s.Index())];
    }

    Piece ChessBoard::Get(int file, int rank) const noexcept {
        return Get(Square{static_cast<int8_t>(file), static_cast<int8_t>(rank)});
    }

    void ChessBoard::Set(Square s, Piece p) noexcept {
        if (!s.IsInside()) {
            return;
        }
        _cells[static_cast<size_t>(s.Index())] = p;
    }

    void ChessBoard::Move(Square from, Square to) noexcept {
        if (!from.IsInside() || !to.IsInside()) {
            return;
        }
        const Piece moving = _cells[static_cast<size_t>(from.Index())];
        _cells[static_cast<size_t>(from.Index())] = Piece{};
        _cells[static_cast<size_t>(to.Index())] = moving;
    }

}  // namespace BECore::Chess
