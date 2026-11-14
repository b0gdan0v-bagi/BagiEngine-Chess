#include "MoveNotation.h"

#include <Chess/Game/MoveGenerator.h>

namespace BECore::Chess {

    namespace {

        constexpr char kCaptureMark = 'x';
        constexpr char kCheckMark = '+';
        constexpr char kMateMark = '#';
        constexpr char kPromotionMark = '=';
        constexpr StringView kCastlingKingSide = "O-O";
        constexpr StringView kCastlingQueenSide = "O-O-O";

        char PieceLetter(PieceType type) {
            switch (type) {
                case PieceType::Knight:
                    return 'N';
                case PieceType::Bishop:
                    return 'B';
                case PieceType::Rook:
                    return 'R';
                case PieceType::Queen:
                    return 'Q';
                case PieceType::King:
                    return 'K';
                default:
                    return '\0';
            }
        }

        char FileLetter(Square s) {
            return static_cast<char>('a' + s.file);
        }

        char RankDigit(Square s) {
            return static_cast<char>('1' + s.rank);
        }

        void AppendSquare(eastl::string& text, Square s) {
            text.push_back(FileLetter(s));
            text.push_back(RankDigit(s));
        }

        /// What the piece that moves needs to be told apart from the others of its type that can legally go to the same target
        struct Rivals {
            bool any = false;
            bool sameFile = false;
            bool sameRank = false;
        };

        Rivals FindRivals(const Position& before, Move move, PieceType type) {
            Rivals rivals;
            const Square from = move.From();
            for (const Move other : MoveGenerator::GenerateLegal(before)) {
                if (other.ToIndex() != move.ToIndex() || other.FromIndex() == move.FromIndex()) {
                    continue;
                }
                if (before.GetPieceAt(other.From()).type != type) {
                    continue;
                }
                rivals.any = true;
                rivals.sameFile = rivals.sameFile || other.From().file == from.file;
                rivals.sameRank = rivals.sameRank || other.From().rank == from.rank;
            }
            return rivals;
        }

        char CheckSuffix(const Position& before, Move move) {
            Position after = before;
            after.DoMove(move);
            if (!after.IsInCheck()) {
                return '\0';
            }
            return MoveGenerator::GenerateLegal(after).empty() ? kMateMark : kCheckMark;
        }

    }  // namespace

    eastl::string SanOf(const Position& before, Move move) {
        eastl::string text;
        const Piece piece = before.GetPieceAt(move.From());
        const Square from = move.From();
        const Square to = move.To();

        if (move.Kind() == MoveKind::Castling) {
            const StringView castling = to.file > from.file ? kCastlingKingSide : kCastlingQueenSide;
            text.append(castling.data(), castling.size());
        } else {
            const bool isCapture = move.Kind() == MoveKind::EnPassant || !before.GetPieceAt(to).IsEmpty();
            if (piece.type == PieceType::Pawn) {
                if (isCapture) {
                    text.push_back(FileLetter(from));
                }
            } else {
                text.push_back(PieceLetter(piece.type));
                const Rivals rivals = FindRivals(before, move, piece.type);
                if (rivals.any) {
                    if (!rivals.sameFile) {
                        text.push_back(FileLetter(from));
                    } else if (!rivals.sameRank) {
                        text.push_back(RankDigit(from));
                    } else {
                        AppendSquare(text, from);
                    }
                }
            }
            if (isCapture) {
                text.push_back(kCaptureMark);
            }
            AppendSquare(text, to);
            if (move.IsPromotion()) {
                text.push_back(kPromotionMark);
                text.push_back(PieceLetter(move.PromotionType()));
            }
        }

        if (const char suffix = CheckSuffix(before, move); suffix != '\0') {
            text.push_back(suffix);
        }
        return text;
    }

}  // namespace BECore::Chess
