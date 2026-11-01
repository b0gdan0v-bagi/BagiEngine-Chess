#include "ChessGameModel.h"

#include <EASTL/algorithm.h>

namespace BECore::Chess {

    namespace {

        constexpr int kKingSideRookFile = 7;
        constexpr int kQueenSideRookFile = 0;
        constexpr int kKingSideRookTargetFile = 5;
        constexpr int kQueenSideRookTargetFile = 3;

        constexpr Square kOffBoard{-1, -1};

    }  // namespace

    ChessGameModel::ChessGameModel() {
        Reset();
    }

    void ChessGameModel::Reset() {
        _position = Position::Start();
        _status = GameStatus::Ongoing;
        ClearSelection();
        for (auto& [handle, track] : _pieces) {
            track.current = track.start;
            track.piece = track.startPiece;
            track.active = true;
        }
    }

    std::optional<PieceColor> ChessGameModel::GetWinner() const noexcept {
        if (_status == GameStatus::Checkmate) {
            return Opposite(_position.SideToMove());
        }
        return std::nullopt;
    }

    eastl::vector<Square> ChessGameModel::GetLegalMovesFor(Square from) const {
        eastl::vector<Square> targets;
        if (!from.IsInside()) {
            return targets;
        }
        for (const Move move : MoveGenerator::GenerateLegal(_position)) {
            if (move.From() == from && eastl::find(targets.begin(), targets.end(), move.To()) == targets.end()) {
                targets.push_back(move.To());
            }
        }
        return targets;
    }

    bool ChessGameModel::OnSquareClicked(Square s) {
        if (!s.IsInside() || IsGameOver()) {
            return false;
        }

        const Piece clicked = _position.GetPieceAt(s);

        if (_hasSelected) {
            // The moves of a promoting pawn share a target; they are listed queen first, so the first match is the queen.
            for (const Move move : _selectedMoves) {
                if (move.To() == s) {
                    return TryMove(move);
                }
            }
            if (!clicked.IsEmpty() && clicked.color == _position.SideToMove()) {
                _selected = s;
                RecomputeLegalMoves();
                return false;
            }
            ClearSelection();
            return false;
        }

        if (!clicked.IsEmpty() && clicked.color == _position.SideToMove()) {
            _selected = s;
            _hasSelected = true;
            RecomputeLegalMoves();
        }
        return false;
    }

    bool ChessGameModel::TryMove(Move move) {
        if (IsGameOver()) {
            return false;
        }
        const MoveList legal = MoveGenerator::GenerateLegal(_position);
        if (eastl::find(legal.begin(), legal.end(), move) == legal.end()) {
            return false;
        }

        RelocatePieces(move);
        _position.DoMove(move);
        ClearSelection();
        _status = EvaluateStatus(_position);
        return true;
    }

    void ChessGameModel::RegisterPiece(PieceHandle handle, Square startSquare) {
        auto& track = _pieces[handle];
        track.start = startSquare;
        track.startPiece = _position.GetPieceAt(startSquare);
        track.current = startSquare;
        track.piece = track.startPiece;
        track.active = true;
    }

    void ChessGameModel::UnregisterPiece(PieceHandle handle) {
        _pieces.erase(handle);
    }

    Square ChessGameModel::GetSquareOf(PieceHandle handle) const {
        const auto it = _pieces.find(handle);
        if (it == _pieces.end() || !it->second.active) {
            return kOffBoard;
        }
        return it->second.current;
    }

    Piece ChessGameModel::GetPieceOf(PieceHandle handle) const {
        const auto it = _pieces.find(handle);
        if (it == _pieces.end() || !it->second.active) {
            return Piece{};
        }
        return it->second.piece;
    }

    bool ChessGameModel::IsActive(PieceHandle handle) const {
        const auto it = _pieces.find(handle);
        return it != _pieces.end() && it->second.active;
    }

    void ChessGameModel::ClearSelection() {
        _hasSelected = false;
        _selected = Square{};
        _selectedMoves.clear();
        _legalTargets.clear();
    }

    void ChessGameModel::RecomputeLegalMoves() {
        _selectedMoves.clear();
        _legalTargets.clear();
        for (const Move move : MoveGenerator::GenerateLegal(_position)) {
            if (move.From() != _selected) {
                continue;
            }
            _selectedMoves.push_back(move);
            if (eastl::find(_legalTargets.begin(), _legalTargets.end(), move.To()) == _legalTargets.end()) {
                _legalTargets.push_back(move.To());
            }
        }
    }

    void ChessGameModel::RelocatePieces(Move move) {
        const Square from = move.From();
        const Square to = move.To();

        // Whatever is taken goes first, so the moving handle never finds its target square occupied. An en passant
        // capture takes the pawn that stands beside the capturing one, not the one on the target square.
        if (move.Kind() == MoveKind::EnPassant) {
            Capture(Square{to.file, from.rank});
        } else {
            Capture(to);
        }

        PieceTrack* moved = MoveHandle(from, to);

        if (move.Kind() == MoveKind::Castling) {
            const bool kingSide = to.file > from.file;
            MoveHandle(Square{static_cast<int8_t>(kingSide ? kKingSideRookFile : kQueenSideRookFile), from.rank},
                       Square{static_cast<int8_t>(kingSide ? kKingSideRookTargetFile : kQueenSideRookTargetFile), from.rank});
        } else if (move.IsPromotion() && moved) {
            moved->piece.type = move.PromotionType();
        }
    }

    void ChessGameModel::Capture(Square at) {
        for (auto& [handle, track] : _pieces) {
            if (track.active && track.current == at) {
                track.active = false;
                track.current = kOffBoard;
            }
        }
    }

    ChessGameModel::PieceTrack* ChessGameModel::MoveHandle(Square from, Square to) {
        for (auto& [handle, track] : _pieces) {
            if (track.active && track.current == from) {
                track.current = to;
                return &track;
            }
        }
        return nullptr;
    }

}  // namespace BECore::Chess
