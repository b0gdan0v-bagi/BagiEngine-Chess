#include "ChessGameModel.h"

#include <Chess/Game/PromotionChoices.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

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
        BeginGame(Position::Start());
        PlaceHandlesAtStart();
    }

    void ChessGameModel::PlaceHandlesAtStart() {
        for (auto& [handle, track] : _pieces) {
            track.current = track.start;
            track.piece = track.startPiece;
            track.active = true;
        }
    }

    void ChessGameModel::SetPosition(const Position& position) {
        BeginGame(position);
        DealPieces(_position);
    }

    void ChessGameModel::DealPieces(const Position& position) {
        for (const PieceColor color : {PieceColor::White, PieceColor::Black}) {
            // The map has no order of its own; the order of the start squares makes the deal the same on every run.
            eastl::vector<PieceTrack*> tracks;
            for (auto& [handle, track] : _pieces) {
                if (track.startPiece.color == color) {
                    tracks.push_back(&track);
                }
            }
            eastl::sort(tracks.begin(), tracks.end(), [](const PieceTrack* left, const PieceTrack* right) { return left->start.Index() < right->start.Index(); });

            size_t next = 0;
            for (int sq = 0; sq < kSquareCount && next < tracks.size(); ++sq) {
                const Piece piece = position.PieceAt(sq);
                if (piece.IsEmpty() || piece.color != color) {
                    continue;
                }
                PieceTrack& track = *tracks[next++];
                track.current = Square::FromIndex(sq);
                track.piece = piece;
                track.active = true;
            }
            for (; next < tracks.size(); ++next) {
                tracks[next]->current = kOffBoard;
                tracks[next]->active = false;
            }
        }
    }

    void ChessGameModel::BeginGame(const Position& position) {
        _position = position;
        _startFen = _position.ToFen();
        _moves.clear();
        _status = EvaluateStatus(_position);
        ClearSelection();
        ++_version;
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

        if (_hasPendingPromotion) {
            // The person clicked the board instead of naming a piece: the choice is dropped, the pawn stays selected.
            CancelPromotion();
            return false;
        }

        if (_hasSelected) {
            for (const Move move : _selectedMoves) {
                if (move.To() == s) {
                    if (move.IsPromotion()) {
                        // The moves of a promoting pawn share a target; the person names the piece (ChoosePromotion()).
                        _hasPendingPromotion = true;
                        _promotionTarget = s;
                        return false;
                    }
                    return TryMove(move).has_value();
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

    std::expected<void, MoveError> ChessGameModel::TryMove(Move move) {
        if (IsGameOver()) {
            return std::unexpected(MoveError::GameOver);
        }
        const MoveList legal = MoveGenerator::GenerateLegal(_position);
        if (eastl::find(legal.begin(), legal.end(), move) == legal.end()) {
            return std::unexpected(MoveError::IllegalMove);
        }

        RelocatePieces(move);
        _position.DoMove(move);
        _moves.push_back(move);
        ClearSelection();
        _status = EvaluateStatus(_position);
        ++_version;
        return {};
    }

    size_t ChessGameModel::TakeBack(size_t plies) {
        const size_t count = eastl::min(plies, _moves.size());
        if (count == 0) {
            return 0;
        }
        for (size_t taken = 0; taken < count; ++taken) {
            _position.UndoMove();
            _moves.pop_back();
        }
        ReplayPieces();
        ClearSelection();
        _status = EvaluateStatus(_position);
        ++_version;
        return count;
    }

    void ChessGameModel::ReplayPieces() {
        if (_startFen == Position::kStartFen) {
            PlaceHandlesAtStart();
        } else if (const auto start = Position::FromFen(_startFen)) {
            DealPieces(*start);
        } else {
            return;  // the model only stores FENs of its own positions, so this is not reached
        }
        for (const Move move : _moves) {
            RelocatePieces(move);
        }
    }

    void ChessGameModel::RegisterPiece(PieceHandle handle, Square startSquare) {
        auto& track = _pieces[handle];
        track.start = startSquare;
        track.startPiece = Position::Start().GetPieceAt(startSquare);  // the scene is laid out for the opening, whatever the game has reached
        track.current = startSquare;
        track.piece = track.startPiece;
        track.active = true;

        // The scene registers its pieces in its first frame, which a client that acts at once (the control API on the manual
        // clock) can precede: a game that has moved on, or began from another position, is dealt again with this piece in it.
        if (!_moves.empty() || _startFen != Position::kStartFen) {
            DealPieces(_position);
        }
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

    std::expected<void, MoveError> ChessGameModel::ChoosePromotion(PieceType piece) {
        if (IsGameOver()) {
            return std::unexpected(MoveError::GameOver);
        }
        if (!_hasPendingPromotion || PromotionId(piece).empty()) {
            return std::unexpected(MoveError::IllegalMove);
        }
        for (const Move move : _selectedMoves) {
            if (move.To() == _promotionTarget && move.IsPromotion() && move.PromotionType() == piece) {
                return TryMove(move);
            }
        }
        return std::unexpected(MoveError::IllegalMove);
    }

    void ChessGameModel::CancelPromotion() {
        _hasPendingPromotion = false;
        _promotionTarget = Square{};
    }

    void ChessGameModel::ClearSelection() {
        CancelPromotion();
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
