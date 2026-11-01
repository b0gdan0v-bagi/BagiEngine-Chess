#include "MoveGenerator.h"

namespace BECore::Chess {

    namespace {

        constexpr int kRankStep = 8;

        // Promotion pieces, strongest first, so the queen promotion is the first of the four moves.
        constexpr eastl::array<PieceType, 4> kPromotionOrder{PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight};

        struct CastlingSpec {
            int right;
            int kingFrom;
            int kingTo;
            Bitboard mustBeEmpty;  ///< Squares between the king and the rook.
        };

        constexpr eastl::array<CastlingSpec, 4> kCastlingSpecs{
            CastlingSpec{kWhiteKingSide, 4, 6, SquareBB(5) | SquareBB(6)},
            CastlingSpec{kWhiteQueenSide, 4, 2, SquareBB(1) | SquareBB(2) | SquareBB(3)},
            CastlingSpec{kBlackKingSide, 60, 62, SquareBB(61) | SquareBB(62)},
            CastlingSpec{kBlackQueenSide, 60, 58, SquareBB(57) | SquareBB(58) | SquareBB(59)},
        };
        constexpr size_t kSpecsPerColor = 2;

        // Adds one move per target square; `fromOffset` is the distance from a target back to its pawn.
        void AddPawnMoves(Bitboard targets, int fromOffset, Bitboard promotionRank, MoveList& out) {
            while (targets != 0) {
                const int to = PopLsb(targets);
                const int from = to - fromOffset;
                if ((SquareBB(to) & promotionRank) != 0) {
                    for (const PieceType piece : kPromotionOrder) {
                        out.push_back(Move(Square::FromIndex(from), Square::FromIndex(to), MoveKind::Promotion, piece));
                    }
                } else {
                    out.push_back(Move(Square::FromIndex(from), Square::FromIndex(to)));
                }
            }
        }

        void GeneratePawnMoves(const Position& position, Bitboard target, MoveList& out) {
            const AttackTables& tables = AttackTables::GetInstance();
            const PieceColor us = position.SideToMove();
            const PieceColor them = Opposite(us);
            const bool white = us == PieceColor::White;

            const Bitboard pawns = position.Pieces(us, PieceType::Pawn);
            const Bitboard empty = ~position.Pieces();
            const Bitboard enemies = position.Pieces(them);

            const int push = white ? kRankStep : -kRankStep;
            const Bitboard doublePushRank = white ? kRank3 : kRank6;
            const Bitboard promotionRank = white ? kRank8 : kRank1;

            const Bitboard single = (white ? ShiftNorth(pawns) : ShiftSouth(pawns)) & empty;
            const Bitboard doubled = (white ? ShiftNorth(single & doublePushRank) : ShiftSouth(single & doublePushRank)) & empty;
            AddPawnMoves(single & target, push, promotionRank, out);
            AddPawnMoves(doubled & target, 2 * push, 0, out);

            // Captures towards the east and the west; the offset is the distance back to the capturing pawn.
            const Bitboard towardsEast = (white ? ShiftNorthEast(pawns) : ShiftSouthEast(pawns)) & enemies & target;
            const Bitboard towardsWest = (white ? ShiftNorthWest(pawns) : ShiftSouthWest(pawns)) & enemies & target;
            AddPawnMoves(towardsEast, push + 1, promotionRank, out);
            AddPawnMoves(towardsWest, push - 1, promotionRank, out);

            // En passant. In check it is only an answer when it takes the pawn that gives the check.
            const int epSquare = position.EnPassantSquare();
            if (epSquare != kNoSquare) {
                const int captured = epSquare - push;
                if (!position.IsInCheck() || (position.Checkers() & SquareBB(captured)) != 0) {
                    Bitboard capturers = tables.Pawn(them, epSquare) & pawns;
                    while (capturers != 0) {
                        out.push_back(Move(Square::FromIndex(PopLsb(capturers)), Square::FromIndex(epSquare), MoveKind::EnPassant));
                    }
                }
            }
        }

        void GeneratePieceMoves(const Position& position, PieceType type, Bitboard target, MoveList& out) {
            const AttackTables& tables = AttackTables::GetInstance();
            const Bitboard occupied = position.Pieces();
            Bitboard pieces = position.Pieces(position.SideToMove(), type);
            while (pieces != 0) {
                const int from = PopLsb(pieces);
                Bitboard reach = 0;
                switch (type) {
                    case PieceType::Knight:
                        reach = tables.Knight(from);
                        break;
                    case PieceType::Bishop:
                        reach = tables.Bishop(from, occupied);
                        break;
                    case PieceType::Rook:
                        reach = tables.Rook(from, occupied);
                        break;
                    case PieceType::Queen:
                        reach = tables.Queen(from, occupied);
                        break;
                    case PieceType::King:
                        reach = tables.King(from);
                        break;
                    default:
                        break;
                }
                reach &= target;
                while (reach != 0) {
                    out.push_back(Move(Square::FromIndex(from), Square::FromIndex(PopLsb(reach))));
                }
            }
        }

        void GenerateCastling(const Position& position, MoveList& out) {
            const size_t first = position.SideToMove() == PieceColor::White ? 0 : kSpecsPerColor;
            for (size_t i = first; i < first + kSpecsPerColor; ++i) {
                const CastlingSpec& spec = kCastlingSpecs[i];
                if ((position.CastlingRights() & spec.right) != 0 && (position.Pieces() & spec.mustBeEmpty) == 0) {
                    out.push_back(Move(Square::FromIndex(spec.kingFrom), Square::FromIndex(spec.kingTo), MoveKind::Castling));
                }
            }
        }

    }  // namespace

    MoveList MoveGenerator::GenerateLegal(const Position& position) {
        const AttackTables& tables = AttackTables::GetInstance();
        const PieceColor us = position.SideToMove();
        const Bitboard checkers = position.Checkers();
        MoveList moves;

        // Squares a non-king piece may move to: anywhere not occupied by its own side, or, in check, only onto the checker
        // or between it and the king. A double check can only be answered by the king.
        if (!MoreThanOne(checkers)) {
            Bitboard target = ~position.Pieces(us);
            if (checkers != 0) {
                target = tables.Between(position.KingSquare(us), Lsb(checkers)) | checkers;
            }
            GeneratePawnMoves(position, target, moves);
            GeneratePieceMoves(position, PieceType::Knight, target, moves);
            GeneratePieceMoves(position, PieceType::Bishop, target, moves);
            GeneratePieceMoves(position, PieceType::Rook, target, moves);
            GeneratePieceMoves(position, PieceType::Queen, target, moves);
        }
        GeneratePieceMoves(position, PieceType::King, ~position.Pieces(us), moves);
        if (checkers == 0) {
            GenerateCastling(position, moves);
        }

        // Drop what leaves the king attacked, keeping the generation order.
        size_t kept = 0;
        for (size_t i = 0; i < moves.size(); ++i) {
            if (position.IsLegal(moves[i])) {
                moves[kept++] = moves[i];
            }
        }
        moves.resize(kept);
        return moves;
    }

    std::optional<Move> MoveGenerator::ParseUci(const Position& position, eastl::string_view text) {
        for (const Move move : GenerateLegal(position)) {
            if (eastl::string_view(move.ToUci().c_str()) == text) {
                return move;
            }
        }
        return std::nullopt;
    }

}  // namespace BECore::Chess
