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

        void AppendSquare(String& text, Square s) {
            text.push_back(FileLetter(s));
            text.push_back(RankDigit(s));
        }

        /// What the piece that moves needs to be told apart from the others of its type that can legally go to the same target
        struct Rivals {
            bool any = false;
            bool sameFile = false;
            bool sameRank = false;
        };

        Rivals FindRivals(const Position& before, Move move, PieceType type, const MoveList& legal) {
            Rivals rivals;
            const Square from = move.From();
            for (const Move other : legal) {
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

        constexpr StringView kCastlingKingSideZeros = "0-0";
        constexpr StringView kCastlingQueenSideZeros = "0-0-0";
        constexpr StringView kIgnoredSuffixes = "+#!?";
        constexpr int kNotGiven = -1;

        /// What the text of a move says about it, before it is matched against the legal moves
        struct SanPattern {
            PieceType piece = PieceType::Pawn;
            int fromFile = kNotGiven;
            int fromRank = kNotGiven;
            Square to;
            PieceType promotion = PieceType::None;
        };

        bool IsFileChar(char c) {
            return c >= 'a' && c <= 'h';
        }

        bool IsRankChar(char c) {
            return c >= '1' && c <= '8';
        }

        PieceType PieceOfLetter(char letter) {
            switch (letter) {
                case 'N':
                    return PieceType::Knight;
                case 'B':
                    return PieceType::Bishop;
                case 'R':
                    return PieceType::Rook;
                case 'Q':
                    return PieceType::Queen;
                case 'K':
                    return PieceType::King;
                default:
                    return PieceType::None;
            }
        }

        StringView WithoutAnnotations(StringView san) {
            while (!san.empty() && kIgnoredSuffixes.find(san.back()) != StringView::npos) {
                san.remove_suffix(1);
            }
            return san;
        }

        /// The piece, the disambiguation, the target and the promotion of a non-castling text; nothing when it is not made of those parts
        Optional<SanPattern> DecodeSan(StringView san) {
            SanPattern pattern;
            size_t begin = 0;
            size_t end = san.size();

            if (const PieceType piece = PieceOfLetter(san.front()); piece != PieceType::None) {
                pattern.piece = piece;
                begin = 1;
            }
            if (end >= begin + 2 && san[end - 2] == kPromotionMark) {
                pattern.promotion = PieceOfLetter(san[end - 1]);
                if (pattern.promotion == PieceType::None || pattern.promotion == PieceType::King) {
                    return std::nullopt;
                }
                end -= 2;
            }
            if (end < begin + 2 || !IsFileChar(san[end - 2]) || !IsRankChar(san[end - 1])) {
                return std::nullopt;
            }
            pattern.to = Square{static_cast<int8_t>(san[end - 2] - 'a'), static_cast<int8_t>(san[end - 1] - '1')};
            end -= 2;
            if (end > begin && san[end - 1] == kCaptureMark) {
                --end;
            }

            size_t at = begin;
            if (at < end && IsFileChar(san[at])) {
                pattern.fromFile = san[at++] - 'a';
            }
            if (at < end && IsRankChar(san[at])) {
                pattern.fromRank = san[at++] - '1';
            }
            if (at != end || (pattern.piece == PieceType::Pawn && pattern.fromRank != kNotGiven)) {
                return std::nullopt;
            }
            return pattern;
        }

        /// Whether @p move, which is not castling, is what @p pattern names; the caller rejects a text that more than one move fits
        bool Matches(const Position& position, Move move, const SanPattern& pattern) {
            if (move.Kind() == MoveKind::Castling || move.To() != pattern.to || position.GetPieceAt(move.From()).type != pattern.piece) {
                return false;
            }
            if (move.IsPromotion() != (pattern.promotion != PieceType::None) || (move.IsPromotion() && move.PromotionType() != pattern.promotion)) {
                return false;
            }
            const Square from = move.From();
            if ((pattern.fromFile != kNotGiven && from.file != pattern.fromFile) || (pattern.fromRank != kNotGiven && from.rank != pattern.fromRank)) {
                return false;
            }
            // A pawn text without a file is a push: a capture always names the file it leaves
            return pattern.piece != PieceType::Pawn || pattern.fromFile != kNotGiven || from.file == pattern.to.file;
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

    String SanOf(const Position& before, Move move) {
        return SanOf(before, move, MoveGenerator::GenerateLegal(before));
    }

    String SanOf(const Position& before, Move move, const MoveList& legal) {
        String text;
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
                const Rivals rivals = FindRivals(before, move, piece.type, legal);
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

    Optional<Move> ParseSan(const Position& position, StringView san) {
        return ParseSan(position, san, MoveGenerator::GenerateLegal(position));
    }

    Optional<Move> ParseSan(const Position& position, StringView san, const MoveList& legal) {
        san = WithoutAnnotations(san);
        if (san.empty()) {
            return std::nullopt;
        }

        const bool kingSide = san == kCastlingKingSide || san == kCastlingKingSideZeros;
        const bool queenSide = san == kCastlingQueenSide || san == kCastlingQueenSideZeros;
        if (kingSide || queenSide) {
            for (const Move move : legal) {
                if (move.Kind() == MoveKind::Castling && (move.To().file > move.From().file) == kingSide) {
                    return move;
                }
            }
            return std::nullopt;
        }

        const Optional<SanPattern> pattern = DecodeSan(san);
        if (!pattern) {
            return std::nullopt;
        }
        Optional<Move> found;
        for (const Move move : legal) {
            if (!Matches(position, move, *pattern)) {
                continue;
            }
            if (found) {
                return std::nullopt;
            }
            found = move;
        }
        return found;
    }

}  // namespace BECore::Chess
