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
        if (_seedSource) {
            const uint32_t drawn = _seedSource();
            _seed = drawn == 0 ? 1 : drawn;  // 0 is "no variety", never a drawn seed
        }
        BeginGame(Position::Start());
        PlaceHandlesAtStart(_pieces);
    }

    void ChessGameModel::SetSeed(uint32_t seed) noexcept {
        if (seed != _seed) {
            _seed = seed;
            ++_version;
        }
    }

    void ChessGameModel::PlaceHandlesAtStart(PieceTracks& tracks) {
        for (auto& [handle, track] : tracks) {
            track.current = track.start;
            track.piece = track.startPiece;
            track.active = true;
        }
    }

    void ChessGameModel::SetPosition(const Position& position) {
        BeginGame(position);
        DealPieces(_pieces, _position);
    }

    void ChessGameModel::DealPieces(PieceTracks& pieces, const Position& position) {
        for (const PieceColor color : {PieceColor::White, PieceColor::Black}) {
            // The map has no order of its own; the order of the start squares makes the deal the same on every run.
            Vector<PieceTrack*> tracks;
            for (auto& [handle, track] : pieces) {
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
        DropView();
        _position = position;
        _startFen = _position.ToFen();
        _moves.clear();
        _status = EvaluateStatus(_position);
        ClearSelection();
        ClearPremoves();
        ++_version;
    }

    Optional<PieceColor> ChessGameModel::GetWinner() const noexcept {
        if (_status == GameStatus::Checkmate) {
            return Opposite(_position.SideToMove());
        }
        return std::nullopt;
    }

    Vector<Square> ChessGameModel::GetLegalMovesFor(Square from) const {
        Vector<Square> targets;
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
        if (EndViewing() || !s.IsInside() || IsGameOver()) {
            return false;
        }

        const Piece clicked = _position.GetPieceAt(s);

        if (_hasPendingPromotion) {
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
        const PieceColor mover = _position.SideToMove();
        const auto result = ApplyMove(move);
        if (result.has_value() && _premoveColor == mover) {
            // The premoving color moved by hand (a control client played for the person): the queue was made for another position
            ClearPremoves();
        }
        return result;
    }

    std::expected<void, MoveError> ChessGameModel::ApplyMove(Move move) {
        if (IsGameOver()) {
            return std::unexpected(MoveError::GameOver);
        }
        const MoveList legal = MoveGenerator::GenerateLegal(_position);
        if (eastl::find(legal.begin(), legal.end(), move) == legal.end()) {
            return std::unexpected(MoveError::IllegalMove);
        }

        DropView();
        RelocatePieces(_pieces, move);
        _position.DoMove(move);
        _moves.push_back(move);
        ClearSelection();
        _status = EvaluateStatus(_position);
        if (IsGameOver()) {
            ClearPremoves();
        }
        ++_version;
        return {};
    }

    size_t ChessGameModel::TakeBack(size_t plies) {
        const size_t count = eastl::min(plies, _moves.size());
        if (count == 0) {
            return 0;
        }
        DropView();
        for (size_t taken = 0; taken < count; ++taken) {
            _position.UndoMove();
            _moves.pop_back();
        }
        ReplayPieces(_pieces, _moves.size());
        ClearSelection();
        ClearPremoves();
        _status = EvaluateStatus(_position);
        ++_version;
        return count;
    }

    void ChessGameModel::ReplayPieces(PieceTracks& tracks, size_t moveCount) const {
        if (_startFen == Position::kStartFen) {
            PlaceHandlesAtStart(tracks);
        } else if (const auto start = Position::FromFen(_startFen)) {
            DealPieces(tracks, *start);
        } else {
            return;  // the model only stores FENs of its own positions, so this is not reached
        }
        for (size_t index = 0; index < moveCount && index < _moves.size(); ++index) {
            RelocatePieces(tracks, _moves[index]);
        }
    }

    bool ChessGameModel::ShowMoves(size_t count) {
        if (count >= _moves.size()) {
            if (!_viewing) {
                return false;
            }
            DropView();
            return true;
        }
        if (_viewing && count == _viewMoveCount) {
            return false;
        }
        if (!_viewing) {
            ClearSelection();
        }
        _viewing = true;
        _viewMoveCount = count;
        BuildView();
        return true;
    }

    bool ChessGameModel::EndViewing() {
        return ShowMoves(_moves.size());
    }

    void ChessGameModel::BuildView() {
        _viewPosition = _position;
        for (size_t undone = _viewMoveCount; undone < _moves.size(); ++undone) {
            _viewPosition.UndoMove();
        }
        _viewPieces = _pieces;
        ReplayPieces(_viewPieces, _viewMoveCount);
    }

    void ChessGameModel::DropView() {
        _viewing = false;
        _viewMoveCount = 0;
        _viewPieces.clear();
    }

    Square ChessGameModel::GetShownSquareOf(PieceHandle handle) const {
        const PieceTrack* track = FindActive(_viewing ? _viewPieces : _pieces, handle);
        return track ? track->current : kOffBoard;
    }

    Piece ChessGameModel::GetShownPieceOf(PieceHandle handle) const {
        const PieceTrack* track = FindActive(_viewing ? _viewPieces : _pieces, handle);
        return track ? track->piece : Piece{};
    }

    const ChessGameModel::PieceTrack* ChessGameModel::FindActive(const PieceTracks& tracks, PieceHandle handle) {
        const auto it = tracks.find(handle);
        return it != tracks.end() && it->second.active ? &it->second : nullptr;
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
            DealPieces(_pieces, _position);
        }
        if (_viewing) {
            BuildView();
        }
    }

    void ChessGameModel::UnregisterPiece(PieceHandle handle) {
        _pieces.erase(handle);
        _viewPieces.erase(handle);
    }

    Square ChessGameModel::GetSquareOf(PieceHandle handle) const {
        const PieceTrack* track = FindActive(_pieces, handle);
        return track ? track->current : kOffBoard;
    }

    Piece ChessGameModel::GetPieceOf(PieceHandle handle) const {
        const PieceTrack* track = FindActive(_pieces, handle);
        return track ? track->piece : Piece{};
    }

    bool ChessGameModel::IsActive(PieceHandle handle) const {
        return FindActive(_pieces, handle) != nullptr;
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

    PremoveBoard ChessGameModel::MakePremoveBoard() const {
        PremoveBoard board(_position);
        for (const Premove& premove : _premoves) {
            board.Apply(premove);
        }
        return board;
    }

    bool ChessGameModel::OnPremoveClicked(PieceColor premover, Square s) {
        if (EndViewing() || !s.IsInside() || IsGameOver() || premover == _position.SideToMove()) {
            return false;
        }
        if (_premoveColor.has_value() && *_premoveColor != premover) {
            ClearPremoves();
        }

        const PremoveBoard board = MakePremoveBoard();

        // A selection that is not of the premover's pieces is a half-made move of the other side: it is no premove selection
        if (_hasSelected && !board.IsOwnPiece(_selected, premover)) {
            ClearSelection();
        }
        if (_hasSelected && eastl::find(_legalTargets.begin(), _legalTargets.end(), s) != _legalTargets.end()) {
            _premoves.push_back(Premove{_selected, s});
            _premoveColor = premover;
            ClearSelection();
            return true;
        }

        if (board.IsOwnPiece(s, premover)) {
            _selected = s;
            _hasSelected = true;
            _selectedMoves.clear();
            _legalTargets = board.Targets(s, premover);
            return false;
        }

        ClearPremoves();
        ClearSelection();
        return false;
    }

    bool ChessGameModel::PlayPremove() {
        if (_premoves.empty() || _premoveColor != _position.SideToMove() || IsGameOver()) {
            return false;
        }

        const Premove premove = _premoves.front();
        for (const Move move : MoveGenerator::GenerateLegal(_position)) {
            if (move.From() != premove.from || move.To() != premove.to) {
                continue;
            }
            if (move.IsPromotion() && move.PromotionType() != PieceType::Queen) {
                continue;
            }
            _premoves.erase(_premoves.begin());
            if (_premoves.empty()) {
                _premoveColor.reset();
            }
            return ApplyMove(move).has_value();
        }

        ClearPremoves();
        return false;
    }

    void ChessGameModel::ClearPremoves() {
        _premoves.clear();
        _premoveColor.reset();
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

    void ChessGameModel::RelocatePieces(PieceTracks& tracks, Move move) {
        const Square from = move.From();
        const Square to = move.To();

        // Whatever is taken goes first, so the moving handle never finds its target square occupied. An en passant
        // capture takes the pawn that stands beside the capturing one, not the one on the target square.
        if (move.Kind() == MoveKind::EnPassant) {
            Capture(tracks, Square{to.file, from.rank});
        } else {
            Capture(tracks, to);
        }

        PieceTrack* moved = MoveHandle(tracks, from, to);

        if (move.Kind() == MoveKind::Castling) {
            const bool kingSide = to.file > from.file;
            MoveHandle(tracks, Square{static_cast<int8_t>(kingSide ? kKingSideRookFile : kQueenSideRookFile), from.rank},
                       Square{static_cast<int8_t>(kingSide ? kKingSideRookTargetFile : kQueenSideRookTargetFile), from.rank});
        } else if (move.IsPromotion() && moved) {
            moved->piece.type = move.PromotionType();
        }
    }

    void ChessGameModel::Capture(PieceTracks& tracks, Square at) {
        for (auto& [handle, track] : tracks) {
            if (track.active && track.current == at) {
                track.active = false;
                track.current = kOffBoard;
            }
        }
    }

    ChessGameModel::PieceTrack* ChessGameModel::MoveHandle(PieceTracks& tracks, Square from, Square to) {
        for (auto& [handle, track] : tracks) {
            if (track.active && track.current == from) {
                track.current = to;
                return &track;
            }
        }
        return nullptr;
    }

}  // namespace BECore::Chess
