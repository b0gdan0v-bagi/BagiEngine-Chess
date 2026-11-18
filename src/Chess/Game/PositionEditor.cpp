#include "PositionEditor.h"

namespace BECore::Chess {

    namespace {

        struct ToolEntry {
            PieceType type;
            StringView id;
        };

        constexpr Array<ToolEntry, 7> kTools{{
            {PieceType::King, "king"},
            {PieceType::Queen, "queen"},
            {PieceType::Rook, "rook"},
            {PieceType::Bishop, "bishop"},
            {PieceType::Knight, "knight"},
            {PieceType::Pawn, "pawn"},
            {PieceType::None, "erase"},
        }};

        constexpr Array<PieceType, 6> kToolPieces{kTools[0].type, kTools[1].type, kTools[2].type, kTools[3].type, kTools[4].type, kTools[5].type};

        constexpr int kBoardSide = 8;
        constexpr int8_t kWhiteBackRank = 0;
        constexpr int8_t kBlackBackRank = 7;
        constexpr int8_t kKingFile = 4;
        constexpr int8_t kQueenRookFile = 0;
        constexpr int8_t kKingRookFile = 7;
        constexpr StringView kCastlingLetters = "KQkq";  // the order of CastlingRight

        constexpr int FlagBit(CastlingRight right) noexcept {
            switch (right) {
                case CastlingRight::WhiteKingSide:
                    return kWhiteKingSide;
                case CastlingRight::WhiteQueenSide:
                    return kWhiteQueenSide;
                case CastlingRight::BlackKingSide:
                    return kBlackKingSide;
                case CastlingRight::BlackQueenSide:
                    return kBlackQueenSide;
            }
            return 0;
        }

        constexpr PieceColor ColorOf(CastlingRight right) noexcept {
            return right == CastlingRight::WhiteKingSide || right == CastlingRight::WhiteQueenSide ? PieceColor::White : PieceColor::Black;
        }

        constexpr bool IsKingSide(CastlingRight right) noexcept {
            return right == CastlingRight::WhiteKingSide || right == CastlingRight::BlackKingSide;
        }

        constexpr size_t IndexOf(Square square) noexcept {
            return static_cast<size_t>(square.Index());
        }

    }  // namespace

    Span<const PieceType> EditorToolPieces() {
        return kToolPieces;
    }

    StringView EditorToolId(PieceType type) {
        for (const ToolEntry& entry : kTools) {
            if (entry.type == type) {
                return entry.id;
            }
        }
        return {};
    }

    Optional<PieceType> FindEditorTool(StringView id) {
        for (const ToolEntry& entry : kTools) {
            if (entry.id == id) {
                return entry.type;
            }
        }
        return std::nullopt;
    }

    void PositionEditor::Clear() {
        _squares = {};
        _sideToMove = PieceColor::White;
        _castlingFlags = 0;
        _lastRefusal.reset();
        ++_revision;
    }

    void PositionEditor::SetStart() {
        Load(Position::Start());
    }

    void PositionEditor::Load(const Position& position) {
        for (int index = 0; index < kSquareCount; ++index) {
            _squares[static_cast<size_t>(index)] = position.PieceAt(index);
        }
        _sideToMove = position.SideToMove();
        _castlingFlags = position.CastlingRights();
        _lastRefusal.reset();
        ++_revision;
    }

    std::expected<void, FenError> PositionEditor::LoadFen(StringView fen) {
        const auto position = Position::FromFen(fen);
        if (!position) {
            return std::unexpected(position.error());
        }
        Load(*position);
        return {};
    }

    std::expected<void, EditRefusal> PositionEditor::Put(Square square, Piece piece) {
        _lastRefusal.reset();
        if (!square.IsInside()) {
            _lastRefusal = EditRefusal::OffBoard;
            return std::unexpected(EditRefusal::OffBoard);
        }
        if (piece.IsEmpty()) {
            Remove(square);
            return {};
        }
        Piece& cell = _squares[IndexOf(square)];
        if (cell == piece) {
            return {};
        }
        const bool replacesOwn = !cell.IsEmpty() && cell.color == piece.color;
        if (CountPieces(piece.color) - (replacesOwn ? 1 : 0) >= kMaxPiecesPerSide) {
            _lastRefusal = EditRefusal::TooManyPieces;
            return std::unexpected(EditRefusal::TooManyPieces);
        }
        cell = piece;
        DropUnavailableRights();
        ++_revision;
        return {};
    }

    bool PositionEditor::Remove(Square square) {
        _lastRefusal.reset();
        if (!square.IsInside()) {
            _lastRefusal = EditRefusal::OffBoard;
            return false;
        }
        Piece& cell = _squares[IndexOf(square)];
        if (cell.IsEmpty()) {
            return false;
        }
        cell = {};
        DropUnavailableRights();
        ++_revision;
        return true;
    }

    bool PositionEditor::Apply(Square square) {
        _lastRefusal.reset();
        if (!square.IsInside()) {
            _lastRefusal = EditRefusal::OffBoard;
            return false;
        }
        if (_tool.IsEraser()) {
            return Remove(square);
        }
        const Piece piece{_tool.type, _tool.color};
        if (GetPieceAt(square) == piece) {
            return Remove(square);
        }
        return Put(square, piece).has_value();
    }

    void PositionEditor::SetSideToMove(PieceColor color) {
        if (_sideToMove != color) {
            _sideToMove = color;
            ++_revision;
        }
    }

    void PositionEditor::SetCastling(CastlingRight right, bool enabled) {
        if (enabled && !CanCastle(right)) {
            return;
        }
        const int bit = FlagBit(right);
        const int flags = enabled ? (_castlingFlags | bit) : (_castlingFlags & ~bit);
        if (flags != _castlingFlags) {
            _castlingFlags = flags;
            ++_revision;
        }
    }

    bool PositionEditor::GetCastling(CastlingRight right) const noexcept {
        return (_castlingFlags & FlagBit(right)) != 0;
    }

    bool PositionEditor::CanCastle(CastlingRight right) const noexcept {
        const PieceColor color = ColorOf(right);
        const int8_t rank = color == PieceColor::White ? kWhiteBackRank : kBlackBackRank;
        const int8_t rookFile = IsKingSide(right) ? kKingRookFile : kQueenRookFile;
        return GetPieceAt(Square{kKingFile, rank}) == Piece{PieceType::King, color} && GetPieceAt(Square{rookFile, rank}) == Piece{PieceType::Rook, color};
    }

    void PositionEditor::SetTool(EditorTool tool) {
        if (!(_tool == tool)) {
            _tool = tool;
            ++_revision;
        }
    }

    int PositionEditor::CountPieces(PieceColor color) const noexcept {
        int count = 0;
        for (const Piece& piece : _squares) {
            if (!piece.IsEmpty() && piece.color == color) {
                ++count;
            }
        }
        return count;
    }

    String PositionEditor::ToFen() const {
        String fen;
        for (int rank = kBoardSide - 1; rank >= 0; --rank) {
            int empty = 0;
            for (int file = 0; file < kBoardSide; ++file) {
                const Piece piece = _squares[static_cast<size_t>(rank * kBoardSide + file)];
                if (piece.IsEmpty()) {
                    ++empty;
                    continue;
                }
                if (empty > 0) {
                    fen.push_back(static_cast<char>('0' + empty));
                    empty = 0;
                }
                fen.push_back(FenPieceLetter(piece));
            }
            if (empty > 0) {
                fen.push_back(static_cast<char>('0' + empty));
            }
            if (rank > 0) {
                fen.push_back('/');
            }
        }
        fen.push_back(' ');
        fen.push_back(_sideToMove == PieceColor::White ? 'w' : 'b');
        fen.push_back(' ');
        if (_castlingFlags == 0) {
            fen.push_back('-');
        } else {
            for (const CastlingRight right : EnumUtils<CastlingRight>::Values()) {
                if (GetCastling(right)) {
                    fen.push_back(kCastlingLetters[static_cast<size_t>(right)]);
                }
            }
        }
        fen.append(" - 0 1");
        return fen;
    }

    std::expected<Position, FenError> PositionEditor::Build() const {
        return Position::FromFen(ToFen());
    }

    void PositionEditor::DropUnavailableRights() noexcept {
        for (const CastlingRight right : EnumUtils<CastlingRight>::Values()) {
            if (GetCastling(right) && !CanCastle(right)) {
                _castlingFlags &= ~FlagBit(right);
            }
        }
    }

}  // namespace BECore::Chess
