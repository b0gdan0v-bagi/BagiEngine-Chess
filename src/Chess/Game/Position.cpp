#include "Position.h"

#include <EASTL/algorithm.h>

namespace BECore::Chess {

    namespace {

        constexpr int kBoardSide = 8;
        constexpr int kRankStep = 8;  // index distance between two squares one rank apart

        constexpr int kA1 = 0;
        constexpr int kE1 = 4;
        constexpr int kH1 = 7;
        constexpr int kA8 = 56;
        constexpr int kE8 = 60;
        constexpr int kH8 = 63;

        // Castling moves the rook from the corner next to the king's destination to the square the king crossed.
        constexpr int kKingSideRookDistance = 3;
        constexpr int kQueenSideRookDistance = 4;

        constexpr int kEnPassantRankWhiteToMove = 5;  // rank index of the square behind a pawn that black just pushed two squares
        constexpr int kEnPassantRankBlackToMove = 2;

        constexpr int kMaxPiecesPerSide = 16;  // promotion changes a piece, never adds one

        constexpr size_t kFenMinFields = 4;
        constexpr size_t kFenMaxFields = 6;

        constexpr StringView kPieceLetters = "pnbrqk";  // index = PieceType - 1, lower case; upper case is White

        // Castling rights a move from or onto this square takes away (the king or a rook leaves, or a rook is captured at home).
        constexpr int RightsLostOn(int sq) noexcept {
            switch (sq) {
                case kA1:
                    return kWhiteQueenSide;
                case kE1:
                    return kWhiteKingSide | kWhiteQueenSide;
                case kH1:
                    return kWhiteKingSide;
                case kA8:
                    return kBlackQueenSide;
                case kE8:
                    return kBlackKingSide | kBlackQueenSide;
                case kH8:
                    return kBlackKingSide;
                default:
                    return 0;
            }
        }

        constexpr char PieceLetter(Piece piece) noexcept {
            const char lower = kPieceLetters[static_cast<size_t>(piece.type) - 1];
            return piece.color == PieceColor::White ? static_cast<char>(lower - 'a' + 'A') : lower;
        }

        Optional<Piece> PieceFromLetter(char letter) noexcept {
            const bool isWhite = letter >= 'A' && letter <= 'Z';
            const char lower = isWhite ? static_cast<char>(letter - 'A' + 'a') : letter;
            const size_t found = kPieceLetters.find(lower);
            if (found == StringView::npos) {
                return std::nullopt;
            }
            return Piece{static_cast<PieceType>(found + 1), isWhite ? PieceColor::White : PieceColor::Black};
        }

        bool ParseNumber(StringView text, int& out) noexcept {
            if (text.empty()) {
                return false;
            }
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
            return error == std::errc{} && end == text.data() + text.size() && out >= 0;
        }

        void AppendSquareName(eastl::string& text, int sq) {
            text.push_back(static_cast<char>('a' + sq % kBoardSide));
            text.push_back(static_cast<char>('1' + sq / kBoardSide));
        }

    }  // namespace

    Position Position::Start() {
        auto start = FromFen(kStartFen);
        ASSERT(start.has_value(), "Position::kStartFen must be a valid FEN");
        return *start;
    }

    std::expected<Position, FenError> Position::FromFen(StringView fen) {
        eastl::array<StringView, kFenMaxFields> fields{};
        size_t fieldCount = 0;
        size_t cursor = 0;
        while (cursor < fen.size()) {
            if (fen[cursor] == ' ') {
                ++cursor;
                continue;
            }
            size_t end = cursor;
            while (end < fen.size() && fen[end] != ' ') {
                ++end;
            }
            if (fieldCount == kFenMaxFields) {
                return std::unexpected(FenError::WrongFieldCount);
            }
            fields[fieldCount++] = fen.substr(cursor, end - cursor);
            cursor = end;
        }
        if (fieldCount < kFenMinFields) {
            return std::unexpected(FenError::WrongFieldCount);
        }

        Position pos;

        // 1. Piece placement, rank 8 first.
        int file = 0;
        int rank = kBoardSide - 1;
        for (const char c : fields[0]) {
            if (c == '/') {
                if (file != kBoardSide || rank == 0) {
                    return std::unexpected(FenError::BadPiecePlacement);
                }
                --rank;
                file = 0;
            } else if (c >= '1' && c <= '8') {
                file += c - '0';
                if (file > kBoardSide) {
                    return std::unexpected(FenError::BadPiecePlacement);
                }
            } else {
                const auto piece = PieceFromLetter(c);
                if (!piece || file >= kBoardSide) {
                    return std::unexpected(FenError::BadPiecePlacement);
                }
                pos.PutPiece(*piece, rank * kBoardSide + file);
                ++file;
            }
        }
        if (rank != 0 || file != kBoardSide) {
            return std::unexpected(FenError::BadPiecePlacement);
        }
        for (const auto color : {PieceColor::White, PieceColor::Black}) {
            if (PopCount(pos.Pieces(color, PieceType::King)) != 1) {
                return std::unexpected(FenError::BadKingCount);
            }
        }
        if ((pos.Pieces(PieceType::Pawn) & (kRank1 | kRank8)) != 0) {
            return std::unexpected(FenError::PawnOnBackRank);
        }
        for (const auto color : {PieceColor::White, PieceColor::Black}) {
            if (PopCount(pos.Pieces(color)) > kMaxPiecesPerSide) {
                return std::unexpected(FenError::TooManyPieces);
            }
        }

        // 2. Side to move.
        if (fields[1] == "w") {
            pos._sideToMove = PieceColor::White;
        } else if (fields[1] == "b") {
            pos._sideToMove = PieceColor::Black;
        } else {
            return std::unexpected(FenError::BadSideToMove);
        }
        const PieceColor us = pos._sideToMove;
        const PieceColor them = Opposite(us);

        // 3. Castling rights; each one needs its king and rook on their home squares.
        if (fields[2] != "-") {
            for (const char c : fields[2]) {
                int right = 0;
                int kingSquare = kE1;
                int rookSquare = kH1;
                Piece king{PieceType::King, PieceColor::White};
                Piece rook{PieceType::Rook, PieceColor::White};
                switch (c) {
                    case 'K':
                        right = kWhiteKingSide;
                        break;
                    case 'Q':
                        right = kWhiteQueenSide;
                        rookSquare = kA1;
                        break;
                    case 'k':
                        right = kBlackKingSide;
                        kingSquare = kE8;
                        rookSquare = kH8;
                        king.color = rook.color = PieceColor::Black;
                        break;
                    case 'q':
                        right = kBlackQueenSide;
                        kingSquare = kE8;
                        rookSquare = kA8;
                        king.color = rook.color = PieceColor::Black;
                        break;
                    default:
                        return std::unexpected(FenError::BadCastling);
                }
                const Piece onKing = pos.PieceAt(kingSquare);
                const Piece onRook = pos.PieceAt(rookSquare);
                const bool placed = onKing.type == king.type && onKing.color == king.color && onRook.type == rook.type && onRook.color == rook.color;
                if ((pos._castlingRights & right) != 0 || !placed) {
                    return std::unexpected(FenError::BadCastling);
                }
                pos._castlingRights |= right;
            }
        }

        // 4. En passant square: behind a pawn that has just advanced two squares, and only kept when it can be captured.
        if (fields[3] != "-") {
            if (fields[3].size() != 2 || fields[3][0] < 'a' || fields[3][0] > 'h') {
                return std::unexpected(FenError::BadEnPassant);
            }
            const int epFile = fields[3][0] - 'a';
            const int epRank = fields[3][1] - '1';
            const int expectedRank = us == PieceColor::White ? kEnPassantRankWhiteToMove : kEnPassantRankBlackToMove;
            if (epRank != expectedRank) {
                return std::unexpected(FenError::BadEnPassant);
            }
            const int epSquare = epRank * kBoardSide + epFile;
            const int pawnSquare = epSquare + (us == PieceColor::White ? -kRankStep : kRankStep);
            const Piece pawn = pos.PieceAt(pawnSquare);
            if (!pos.PieceAt(epSquare).IsEmpty() || pawn.type != PieceType::Pawn || pawn.color != them) {
                return std::unexpected(FenError::BadEnPassant);
            }
            if ((AttackTables::GetInstance().Pawn(them, epSquare) & pos.Pieces(us, PieceType::Pawn)) != 0) {
                pos._enPassantSquare = epSquare;
            }
        }

        // 5. and 6. Clocks.
        if (fieldCount > 4 && !ParseNumber(fields[4], pos._halfmoveClock)) {
            return std::unexpected(FenError::BadClock);
        }
        if (fieldCount > 5 && (!ParseNumber(fields[5], pos._fullmoveNumber) || pos._fullmoveNumber < 1)) {
            return std::unexpected(FenError::BadClock);
        }

        // The side that just moved cannot have left its king attacked.
        if ((pos.AttackersTo(pos.KingSquare(them), pos.Pieces()) & pos.Pieces(us)) != 0) {
            return std::unexpected(FenError::OpponentInCheck);
        }

        const AttackTables& tables = AttackTables::GetInstance();
        pos._key ^= tables.CastlingKey(pos._castlingRights);
        if (pos._enPassantSquare != kNoSquare) {
            pos._key ^= tables.EnPassantKey(pos._enPassantSquare % kBoardSide);
        }
        if (us == PieceColor::Black) {
            pos._key ^= tables.SideKey();
        }
        pos.UpdateCheckInfo();
        return pos;
    }

    eastl::string Position::ToFen() const {
        eastl::string fen;
        for (int rank = kBoardSide - 1; rank >= 0; --rank) {
            int empty = 0;
            for (int file = 0; file < kBoardSide; ++file) {
                const Piece piece = PieceAt(rank * kBoardSide + file);
                if (piece.IsEmpty()) {
                    ++empty;
                    continue;
                }
                if (empty > 0) {
                    fen.push_back(static_cast<char>('0' + empty));
                    empty = 0;
                }
                fen.push_back(PieceLetter(piece));
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
        if (_castlingRights == 0) {
            fen.push_back('-');
        } else {
            if ((_castlingRights & kWhiteKingSide) != 0) {
                fen.push_back('K');
            }
            if ((_castlingRights & kWhiteQueenSide) != 0) {
                fen.push_back('Q');
            }
            if ((_castlingRights & kBlackKingSide) != 0) {
                fen.push_back('k');
            }
            if ((_castlingRights & kBlackQueenSide) != 0) {
                fen.push_back('q');
            }
        }
        fen.push_back(' ');
        if (_enPassantSquare == kNoSquare) {
            fen.push_back('-');
        } else {
            AppendSquareName(fen, _enPassantSquare);
        }
        fen.append(Format(" {} {}", _halfmoveClock, _fullmoveNumber));
        return fen;
    }

    Bitboard Position::AttackersTo(int sq, Bitboard occupied) const noexcept {
        const AttackTables& tables = AttackTables::GetInstance();
        return (tables.Pawn(PieceColor::Black, sq) & Pieces(PieceColor::White, PieceType::Pawn)) | (tables.Pawn(PieceColor::White, sq) & Pieces(PieceColor::Black, PieceType::Pawn)) |
               (tables.Knight(sq) & Pieces(PieceType::Knight)) | (tables.King(sq) & Pieces(PieceType::King)) | (tables.Rook(sq, occupied) & Pieces(PieceType::Rook, PieceType::Queen)) |
               (tables.Bishop(sq, occupied) & Pieces(PieceType::Bishop, PieceType::Queen));
    }

    bool Position::IsLegal(Move move) const noexcept {
        const AttackTables& tables = AttackTables::GetInstance();
        const PieceColor us = _sideToMove;
        const PieceColor them = Opposite(us);
        const int from = move.FromIndex();
        const int to = move.ToIndex();
        const int king = KingSquare(us);

        switch (move.Kind()) {
            case MoveKind::EnPassant: {
                // Both pawns leave their rank at once, which can open a rank or a diagonal onto the king.
                const int captured = to + (us == PieceColor::White ? -kRankStep : kRankStep);
                const Bitboard occupied = (Pieces() ^ SquareBB(from) ^ SquareBB(captured)) | SquareBB(to);
                return (tables.Rook(king, occupied) & Pieces(them, PieceType::Rook, PieceType::Queen)) == 0 && (tables.Bishop(king, occupied) & Pieces(them, PieceType::Bishop, PieceType::Queen)) == 0;
            }
            case MoveKind::Castling: {
                // The king may not start in, cross or land on an attacked square.
                const int step = to > from ? 1 : -1;
                for (int sq = from;; sq += step) {
                    if (IsSquareAttacked(sq, them)) {
                        return false;
                    }
                    if (sq == to) {
                        return true;
                    }
                }
            }
            case MoveKind::Normal:
            case MoveKind::Promotion:
                break;
        }

        if (king == from) {
            // The king itself must not be counted as a blocker for the slider that attacks the square behind it.
            return (AttackersTo(to, Pieces() ^ SquareBB(from)) & Pieces(them)) == 0;
        }
        // A pinned piece may only slide along the line to its king.
        return (_pinned & SquareBB(from)) == 0 || tables.Aligned(from, to, king);
    }

    void Position::DoMove(Move move) {
        ASSERT(!move.IsNull(), "Position::DoMove: null move");
        const AttackTables& tables = AttackTables::GetInstance();
        const PieceColor us = _sideToMove;
        const PieceColor them = Opposite(us);
        const int from = move.FromIndex();
        const int to = move.ToIndex();
        const Piece moving = PieceAt(from);
        const MoveKind kind = move.Kind();

        const int captureSquare = kind == MoveKind::EnPassant ? to + (us == PieceColor::White ? -kRankStep : kRankStep) : to;
        const Piece captured = kind == MoveKind::Castling ? Piece{} : PieceAt(captureSquare);

        _history.push_back(UndoInfo{move, captured, _key, _checkers, _pinned, _castlingRights, _enPassantSquare, _halfmoveClock});

        _key ^= tables.SideKey();
        if (_enPassantSquare != kNoSquare) {
            _key ^= tables.EnPassantKey(_enPassantSquare % kBoardSide);
            _enPassantSquare = kNoSquare;
        }
        ++_halfmoveClock;

        if (!captured.IsEmpty()) {
            RemovePiece(captureSquare);
            _halfmoveClock = 0;
        }

        if (kind == MoveKind::Castling) {
            const bool kingSide = to > from;
            MovePiece(from, to);
            MovePiece(kingSide ? from + kKingSideRookDistance : from - kQueenSideRookDistance, kingSide ? from + 1 : from - 1);
        } else if (kind == MoveKind::Promotion) {
            RemovePiece(from);
            PutPiece(Piece{move.PromotionType(), us}, to);
        } else {
            MovePiece(from, to);
        }

        if (moving.type == PieceType::Pawn) {
            _halfmoveClock = 0;
            if (to - from == 2 * kRankStep || from - to == 2 * kRankStep) {
                const int skipped = (from + to) / 2;
                if ((tables.Pawn(us, skipped) & Pieces(them, PieceType::Pawn)) != 0) {
                    _enPassantSquare = skipped;
                    _key ^= tables.EnPassantKey(skipped % kBoardSide);
                }
            }
        }

        const int oldRights = _castlingRights;
        _castlingRights &= ~(RightsLostOn(from) | RightsLostOn(to));
        if (_castlingRights != oldRights) {
            _key ^= tables.CastlingKey(oldRights) ^ tables.CastlingKey(_castlingRights);
        }

        if (us == PieceColor::Black) {
            ++_fullmoveNumber;
        }
        _sideToMove = them;
        UpdateCheckInfo();
    }

    void Position::UndoMove() {
        if (_history.empty()) {
            return;
        }
        const UndoInfo info = _history.back();
        _history.pop_back();

        _sideToMove = Opposite(_sideToMove);
        const PieceColor us = _sideToMove;
        const Move move = info.move;
        const int from = move.FromIndex();
        const int to = move.ToIndex();

        if (move.Kind() == MoveKind::Castling) {
            const bool kingSide = to > from;
            MovePiece(to, from);
            MovePiece(kingSide ? from + 1 : from - 1, kingSide ? from + kKingSideRookDistance : from - kQueenSideRookDistance);
        } else if (move.Kind() == MoveKind::Promotion) {
            RemovePiece(to);
            PutPiece(Piece{PieceType::Pawn, us}, from);
        } else {
            MovePiece(to, from);
        }

        if (!info.captured.IsEmpty()) {
            const int captureSquare = move.Kind() == MoveKind::EnPassant ? to + (us == PieceColor::White ? -kRankStep : kRankStep) : to;
            PutPiece(info.captured, captureSquare);
        }

        // The piece helpers above hashed as they went; the saved key is the exact value from before the move.
        _key = info.key;
        _checkers = info.checkers;
        _pinned = info.pinned;
        _castlingRights = info.castlingRights;
        _enPassantSquare = info.enPassantSquare;
        _halfmoveClock = info.halfmoveClock;
        if (us == PieceColor::Black) {
            --_fullmoveNumber;
        }
    }

    int Position::RepetitionCount() const noexcept {
        int count = 1;
        const size_t reach = eastl::min(static_cast<size_t>(_halfmoveClock), _history.size());
        // Only positions with the same side to move can repeat: every second ply back.
        for (size_t back = 2; back <= reach; back += 2) {
            if (_history[_history.size() - back].key == _key) {
                ++count;
            }
        }
        return count;
    }

    bool Position::IsInsufficientMaterial() const noexcept {
        if ((Pieces(PieceType::Pawn) | Pieces(PieceType::Rook, PieceType::Queen)) != 0) {
            return false;
        }
        const Bitboard knights = Pieces(PieceType::Knight);
        const Bitboard bishops = Pieces(PieceType::Bishop);
        if (knights == 0 && bishops == 0) {
            return true;
        }
        if (knights == 0) {
            return (bishops & kLightSquares) == 0 || (bishops & kDarkSquares) == 0;
        }
        return bishops == 0 && !MoreThanOne(knights);
    }

    void Position::PutPiece(Piece piece, int sq) noexcept {
        const Bitboard bit = SquareBB(sq);
        _board[static_cast<size_t>(sq)] = piece;
        _byType[static_cast<size_t>(piece.type)] |= bit;
        _byColor[static_cast<size_t>(piece.color)] |= bit;
        _key ^= AttackTables::GetInstance().PieceKey(piece, sq);
    }

    void Position::RemovePiece(int sq) noexcept {
        const Piece piece = _board[static_cast<size_t>(sq)];
        const Bitboard bit = SquareBB(sq);
        _byType[static_cast<size_t>(piece.type)] &= ~bit;
        _byColor[static_cast<size_t>(piece.color)] &= ~bit;
        _board[static_cast<size_t>(sq)] = Piece{};
        _key ^= AttackTables::GetInstance().PieceKey(piece, sq);
    }

    void Position::MovePiece(int from, int to) noexcept {
        const Piece piece = _board[static_cast<size_t>(from)];
        RemovePiece(from);
        PutPiece(piece, to);
    }

    void Position::UpdateCheckInfo() noexcept {
        const AttackTables& tables = AttackTables::GetInstance();
        const PieceColor us = _sideToMove;
        const PieceColor them = Opposite(us);
        const int king = KingSquare(us);
        const Bitboard occupied = Pieces();

        _checkers = AttackersTo(king, occupied) & Pieces(them);

        // An enemy slider with exactly one piece between it and the king pins that piece when it is ours.
        _pinned = 0;
        Bitboard snipers = (tables.Rook(king, 0) & Pieces(them, PieceType::Rook, PieceType::Queen)) | (tables.Bishop(king, 0) & Pieces(them, PieceType::Bishop, PieceType::Queen));
        while (snipers != 0) {
            const Bitboard between = tables.Between(king, PopLsb(snipers)) & occupied;
            if (between != 0 && !MoreThanOne(between)) {
                _pinned |= between & Pieces(us);
            }
        }
    }

}  // namespace BECore::Chess
