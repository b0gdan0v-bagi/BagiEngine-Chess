#include "ChessGameModel.h"

#include <Chess/Game/MoveGenerator.h>

namespace BECore::Chess {

    ChessGameModel::ChessGameModel() {
        Reset();
    }

    void ChessGameModel::Reset() {
        _board.ResetToStart();
        _turn = PieceColor::White;
        _hasSelected = false;
        _selected = Square{};
        _legalMoves.clear();
        for (auto& [handle, track] : _pieces) {
            track.current = track.start;
            track.active = true;
        }
    }

    eastl::vector<Square> ChessGameModel::GetLegalMovesFor(Square from) const {
        return MoveGenerator::Generate(_board, from);
    }

    bool ChessGameModel::OnSquareClicked(Square s) {
        if (!s.IsInside()) {
            return false;
        }

        const Piece clicked = _board.Get(s);

        if (_hasSelected) {
            for (const auto target : _legalMoves) {
                if (target == s) {
                    const Square from = _selected;
                    _board.Move(from, s);
                    RelocatePieces(from, s);
                    _turn = (_turn == PieceColor::White) ? PieceColor::Black : PieceColor::White;
                    _hasSelected = false;
                    _selected = Square{};
                    _legalMoves.clear();
                    return true;
                }
            }
            if (!clicked.IsEmpty() && clicked.color == _turn) {
                _selected = s;
                _hasSelected = true;
                RecomputeLegalMoves();
                return false;
            }
            _hasSelected = false;
            _selected = Square{};
            _legalMoves.clear();
            return false;
        }

        if (!clicked.IsEmpty() && clicked.color == _turn) {
            _selected = s;
            _hasSelected = true;
            RecomputeLegalMoves();
        }
        return false;
    }

    void ChessGameModel::RegisterPiece(PieceHandle handle, Square startSquare) {
        auto& track = _pieces[handle];
        track.start = startSquare;
        track.current = startSquare;
        track.active = true;
    }

    void ChessGameModel::UnregisterPiece(PieceHandle handle) {
        _pieces.erase(handle);
    }

    Square ChessGameModel::GetSquareOf(PieceHandle handle) const {
        const auto it = _pieces.find(handle);
        if (it == _pieces.end() || !it->second.active) {
            return Square{-1, -1};
        }
        return it->second.current;
    }

    bool ChessGameModel::IsActive(PieceHandle handle) const {
        const auto it = _pieces.find(handle);
        return it != _pieces.end() && it->second.active;
    }

    void ChessGameModel::RecomputeLegalMoves() {
        _legalMoves = MoveGenerator::Generate(_board, _selected);
    }

    void ChessGameModel::RelocatePieces(Square from, Square to) {
        // Mark anything sitting on the target square as captured first; only one
        // piece occupies a real square at a time but we iterate generally so
        // future variants (e.g. swap moves) keep working.
        for (auto& [handle, track] : _pieces) {
            if (track.active && track.current == to) {
                track.active = false;
                track.current = Square{-1, -1};
            }
        }
        for (auto& [handle, track] : _pieces) {
            if (track.active && track.current == from) {
                track.current = to;
                break;
            }
        }
    }

}  // namespace BECore::Chess
