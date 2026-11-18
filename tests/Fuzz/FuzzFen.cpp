#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/Position.h>
#include <tests/Fuzz/ChessFuzzBoard.h>
#include <tests/Fuzz/ChessFuzzTargets.h>

namespace BECore::Chess::Fuzzing {

    namespace {

        using BoardScan::kBoardSide;

        /// The most legal moves any position has
        constexpr size_t kMostLegalMoves = 218;
        constexpr int kMostPiecesPerSide = 16;
        constexpr int kBackRankLow = 0;
        constexpr int kBackRankHigh = kBoardSide - 1;
        constexpr int kPieceLetterCount = 6;
        constexpr char kPieceLetters[kPieceLetterCount + 1] = "pnbrqk";

        /// The square of the en passant target that a side to move can have: behind the pawn that has just pushed two squares
        constexpr int kEnPassantRankWhiteToMove = 5;
        constexpr int kEnPassantRankBlackToMove = 2;

        struct CastlingRight {
            int bit;
            PieceColor color;
            int kingSquare;
            int rookSquare;
        };

        constexpr Array<CastlingRight, 4> kCastlingRights{{
            {kWhiteKingSide, PieceColor::White, 4, 7},
            {kWhiteQueenSide, PieceColor::White, 4, 0},
            {kBlackKingSide, PieceColor::Black, 60, 63},
            {kBlackQueenSide, PieceColor::Black, 60, 56},
        }};

        StringView ViewOf(const uint8_t* data, size_t size) {
            return StringView(reinterpret_cast<const char*>(data), size);
        }

        StringView ViewOf(const String& text) {
            return StringView(text.data(), text.size());
        }

        /// The first field of a FEN, written from the cells
        String PlacementOf(const Position& position) {
            String placement;
            for (int rank = kBoardSide - 1; rank >= 0; --rank) {
                int empty = 0;
                for (int file = 0; file < kBoardSide; ++file) {
                    const Piece piece = position.PieceAt(rank * kBoardSide + file);
                    if (piece.IsEmpty()) {
                        ++empty;
                        continue;
                    }
                    if (empty > 0) {
                        placement.push_back(static_cast<char>('0' + empty));
                        empty = 0;
                    }
                    const char lower = kPieceLetters[static_cast<size_t>(piece.type) - 1];
                    placement.push_back(piece.color == PieceColor::White ? static_cast<char>(lower - 'a' + 'A') : lower);
                }
                if (empty > 0) {
                    placement.push_back(static_cast<char>('0' + empty));
                }
                if (rank > 0) {
                    placement.push_back('/');
                }
            }
            return placement;
        }

        /// What the class comment of Position promises about the pieces: one king per side, no pawn on a back rank, at most 16 pieces per side, and the two ways of reading the board agree
        void CheckPieces(const Position& position) {
            Array<int, 2> pieces{};
            Array<int, 2> kings{};
            for (int square = 0; square < kBoardSide * kBoardSide; ++square) {
                const Piece piece = position.PieceAt(square);
                const Bitboard bit = Bitboard{1} << square;
                if (piece.IsEmpty()) {
                    CHESS_FUZZ_CHECK((position.Pieces() & bit) == 0);
                    continue;
                }
                CHESS_FUZZ_CHECK((position.Pieces(piece.color) & bit) != 0);
                CHESS_FUZZ_CHECK((position.Pieces(piece.type) & bit) != 0);
                ++pieces[static_cast<size_t>(piece.color)];
                if (piece.type == PieceType::King) {
                    ++kings[static_cast<size_t>(piece.color)];
                }
                if (piece.type == PieceType::Pawn) {
                    CHESS_FUZZ_CHECK(square / kBoardSide != kBackRankLow && square / kBoardSide != kBackRankHigh);
                }
            }
            for (const size_t color : {size_t{0}, size_t{1}}) {
                CHESS_FUZZ_CHECK(kings[color] == 1);
                CHESS_FUZZ_CHECK(pieces[color] <= kMostPiecesPerSide);
            }
            CHESS_FUZZ_CHECK(PopCount(position.Pieces()) == pieces[0] + pieces[1]);
        }

        /// The check, the castling rights, the en passant square and the clocks
        void CheckState(const Position& position) {
            const PieceColor us = position.SideToMove();
            const PieceColor them = Opposite(us);
            const int ourKing = BoardScan::KingOf(position, us);
            const int theirKing = BoardScan::KingOf(position, them);
            CHESS_FUZZ_CHECK(ourKing >= 0 && theirKing >= 0);

            // The side that just moved did not leave its king attacked
            CHESS_FUZZ_CHECK(BoardScan::AttackersOf(position, theirKing, us) == 0);
            CHESS_FUZZ_CHECK(!position.IsSquareAttacked(theirKing, us));

            const uint64_t checkers = BoardScan::AttackersOf(position, ourKing, them);
            CHESS_FUZZ_CHECK(position.Checkers() == checkers);
            CHESS_FUZZ_CHECK(position.IsInCheck() == (checkers != 0));
            CHESS_FUZZ_CHECK(position.IsSquareAttacked(ourKing, them) == (checkers != 0));

            for (const CastlingRight& right : kCastlingRights) {
                if ((position.CastlingRights() & right.bit) == 0) {
                    continue;
                }
                const Piece king = position.PieceAt(right.kingSquare);
                const Piece rook = position.PieceAt(right.rookSquare);
                CHESS_FUZZ_CHECK(king.type == PieceType::King && king.color == right.color);
                CHESS_FUZZ_CHECK(rook.type == PieceType::Rook && rook.color == right.color);
            }
            CHESS_FUZZ_CHECK((position.CastlingRights() & ~kAllCastlingRights) == 0);

            const int enPassant = position.EnPassantSquare();
            if (enPassant != kNoSquare) {
                const int file = enPassant % kBoardSide;
                const int rank = enPassant / kBoardSide;
                const bool whiteToMove = us == PieceColor::White;
                CHESS_FUZZ_CHECK(rank == (whiteToMove ? kEnPassantRankWhiteToMove : kEnPassantRankBlackToMove));
                CHESS_FUZZ_CHECK(position.PieceAt(enPassant).IsEmpty());
                // The pawn that pushed stands in front of the square, and a pawn of ours can take it: the square is kept only then
                const int pushedRank = whiteToMove ? rank - 1 : rank + 1;
                CHESS_FUZZ_CHECK(BoardScan::Holds(position, file, pushedRank, them, PieceType::Pawn));
                const bool capturable = BoardScan::Holds(position, file - 1, pushedRank, us, PieceType::Pawn) || BoardScan::Holds(position, file + 1, pushedRank, us, PieceType::Pawn);
                CHESS_FUZZ_CHECK(capturable);
            }
            CHESS_FUZZ_CHECK(position.HalfmoveClock() >= 0);
            CHESS_FUZZ_CHECK(position.FullmoveNumber() >= 1);
        }

        /// ToFen writes the position back so that reading it gives the same position
        void CheckFenRoundTrip(const Position& position) {
            const String fen = position.ToFen();
            const String placement = PlacementOf(position);
            CHESS_FUZZ_CHECK(fen.size() > placement.size() && fen.substr(0, placement.size()) == placement && fen[placement.size()] == ' ');

            const auto again = Position::FromFen(ViewOf(fen));
            CHESS_FUZZ_CHECK(again.has_value());
            CHESS_FUZZ_CHECK(again->Key() == position.Key());
            CHESS_FUZZ_CHECK(again->ToFen() == fen);
        }

        /// One ply: what the generator gives is legal, spelled in a way that reads back, undone exactly, and leaves a position that is a position
        void CheckMoves(const Position& position) {
            const PieceColor us = position.SideToMove();
            const MoveList moves = MoveGenerator::GenerateLegal(position);
            CHESS_FUZZ_CHECK(moves.size() <= kMostLegalMoves);

            const String fen = position.ToFen();
            Position walker = position;
            for (const Move move : moves) {
                CHESS_FUZZ_CHECK(position.IsLegal(move));
                const Piece mover = position.PieceAt(move.FromIndex());
                CHESS_FUZZ_CHECK(!mover.IsEmpty() && mover.color == us);

                const String spelling = move.ToUci();
                const Optional<Move> parsed = MoveGenerator::ParseUci(position, ViewOf(spelling));
                CHESS_FUZZ_CHECK(parsed.has_value() && *parsed == move);

                walker.DoMove(move);
                CHESS_FUZZ_CHECK(walker.SideToMove() == Opposite(us));
                const int moverKing = BoardScan::KingOf(walker, us);
                CHESS_FUZZ_CHECK(moverKing >= 0 && BoardScan::AttackersOf(walker, moverKing, Opposite(us)) == 0);
                const String after = walker.ToFen();
                const auto reparsed = Position::FromFen(ViewOf(after));
                CHESS_FUZZ_CHECK(reparsed.has_value());
                CHESS_FUZZ_CHECK(reparsed->Key() == walker.Key());
                CHESS_FUZZ_CHECK(reparsed->ToFen() == after);

                walker.UndoMove();
                CHESS_FUZZ_CHECK(walker.Key() == position.Key());
                CHESS_FUZZ_CHECK(walker.ToFen() == fen);
            }
        }

    }  // namespace

    void FuzzFen(const uint8_t* data, size_t size) {
        const auto parsed = Position::FromFen(ViewOf(data, size));
        if (!parsed) {
            CHESS_FUZZ_CHECK(!EnumUtils<FenError>::ToString(parsed.error()).empty());
            return;
        }
        CheckPieces(*parsed);
        CheckState(*parsed);
        CheckFenRoundTrip(*parsed);
        CheckMoves(*parsed);
    }

    bool FenIsAccepted(const uint8_t* data, size_t size) {
        return Position::FromFen(ViewOf(data, size)).has_value();
    }

}  // namespace BECore::Chess::Fuzzing
