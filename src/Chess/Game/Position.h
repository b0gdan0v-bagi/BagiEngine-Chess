#pragma once

#include <Chess/Game/Attacks.h>
#include <Chess/Game/Bitboard.h>
#include <Chess/Game/ChessTypes.h>
#include <Chess/Game/Move.h>

namespace BECore::Chess {

    /// Why a FEN string was rejected.
    CORE_ENUM(FenError, uint8_t, WrongFieldCount, BadPiecePlacement, BadSideToMove, BadCastling, BadEnPassant, BadClock, BadKingCount, PawnOnBackRank, TooManyPieces, OpponentInCheck)

    /// Bits of Position::CastlingRights().
    constexpr int kWhiteKingSide = 1;
    constexpr int kWhiteQueenSide = 2;
    constexpr int kBlackKingSide = 4;
    constexpr int kBlackQueenSide = 8;
    constexpr int kAllCastlingRights = kWhiteKingSide | kWhiteQueenSide | kBlackKingSide | kBlackQueenSide;

    /// Value of Position::EnPassantSquare() when no en passant capture is available.
    constexpr int kNoSquare = -1;

    /**
     * @brief A chess position: where every piece stands plus everything the rules need besides that.
     *
     * Pieces are kept twice, as one bitboard per piece type and per color (for set operations and attack lookups) and
     * as a 64-cell array (to ask what stands on a square). Next to them: the side to move, castling rights, the en
     * passant square, the halfmove clock and the fullmove number, and a Zobrist key that hashes all of it.
     *
     * DoMove() pushes what it overwrites onto a stack and UndoMove() pops it, so a search or a perft walk can make and
     * take back moves without copying the position. The same stack answers repetition questions.
     *
     * The en passant square is kept only when a pawn of the side to move could capture onto it, so two positions that
     * are the same for the rules always have the same key and the same FEN.
     *
     * Standard chess only (no Chess960). Every position holds exactly one king per side, no pawn on a back rank, at most
     * 16 pieces per side (the 16 it started with) and the side that is not to move is never in check; FromFen() rejects
     * anything else, so the lookups below can rely on it.
     */
    class Position {
    public:
        static constexpr eastl::string_view kStartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

        [[nodiscard]] static Position Start();

        /// Parse a FEN string. The halfmove clock and fullmove number may be left out (they default to 0 and 1).
        [[nodiscard]] static std::expected<Position, FenError> FromFen(eastl::string_view fen);

        [[nodiscard]] eastl::string ToFen() const;

        [[nodiscard]] PieceColor SideToMove() const noexcept {
            return _sideToMove;
        }
        [[nodiscard]] Piece PieceAt(int sq) const noexcept {
            return _board[static_cast<size_t>(sq)];
        }
        /// The piece on @p s, or an empty piece for a square outside the board.
        [[nodiscard]] Piece GetPieceAt(Square s) const noexcept {
            return s.IsInside() ? PieceAt(s.Index()) : Piece{};
        }

        [[nodiscard]] Bitboard Pieces() const noexcept {
            return _byColor[0] | _byColor[1];
        }
        [[nodiscard]] Bitboard Pieces(PieceColor color) const noexcept {
            return _byColor[static_cast<size_t>(color)];
        }
        [[nodiscard]] Bitboard Pieces(PieceType type) const noexcept {
            return _byType[static_cast<size_t>(type)];
        }
        [[nodiscard]] Bitboard Pieces(PieceType first, PieceType second) const noexcept {
            return Pieces(first) | Pieces(second);
        }
        [[nodiscard]] Bitboard Pieces(PieceColor color, PieceType type) const noexcept {
            return Pieces(color) & Pieces(type);
        }
        [[nodiscard]] Bitboard Pieces(PieceColor color, PieceType first, PieceType second) const noexcept {
            return Pieces(color) & Pieces(first, second);
        }

        [[nodiscard]] int KingSquare(PieceColor color) const noexcept {
            return Lsb(Pieces(color, PieceType::King));
        }

        [[nodiscard]] int CastlingRights() const noexcept {
            return _castlingRights;
        }
        /// Square a pawn of the side to move would land on by capturing en passant, or kNoSquare.
        [[nodiscard]] int EnPassantSquare() const noexcept {
            return _enPassantSquare;
        }
        /// Plies since the last capture or pawn move.
        [[nodiscard]] int HalfmoveClock() const noexcept {
            return _halfmoveClock;
        }
        [[nodiscard]] int FullmoveNumber() const noexcept {
            return _fullmoveNumber;
        }
        [[nodiscard]] ZobristKey Key() const noexcept {
            return _key;
        }

        /// Enemy pieces that give check to the side to move.
        [[nodiscard]] Bitboard Checkers() const noexcept {
            return _checkers;
        }
        [[nodiscard]] bool IsInCheck() const noexcept {
            return _checkers != 0;
        }
        /// Pieces of the side to move that stand alone between its king and an enemy slider.
        [[nodiscard]] Bitboard Pinned() const noexcept {
            return _pinned;
        }

        /// Pieces of both colors that attack @p sq, with the blockers given by @p occupied.
        [[nodiscard]] Bitboard AttackersTo(int sq, Bitboard occupied) const noexcept;
        [[nodiscard]] bool IsSquareAttacked(int sq, PieceColor by) const noexcept {
            return (AttackersTo(sq, Pieces()) & Pieces(by)) != 0;
        }

        /**
         * @brief Whether a pseudo-legal move of the side to move leaves its own king safe.
         *
         * "Pseudo-legal" is what MoveGenerator produces: the piece moves the way it may, castling has its squares empty
         * and its rights, and while in check the move is one of the evasions (king move, capture of the single checker
         * or a block). Pins, king safety, the castling path and the en passant discovered check are decided here. Moves
         * that did not come from the generator should go through MoveGenerator::GenerateLegal() instead.
         */
        [[nodiscard]] bool IsLegal(Move move) const noexcept;

        /// Play a legal move; UndoMove() takes it back.
        void DoMove(Move move);
        /// Take back the last DoMove(). Does nothing if there is none.
        void UndoMove();
        [[nodiscard]] bool CanUndo() const noexcept {
            return !_history.empty();
        }

        /// How often the current position has occurred since the last capture or pawn move, itself included.
        [[nodiscard]] int RepetitionCount() const noexcept;

        /// Neither side can ever deliver mate: king against king, a lone minor piece, or bishops that all stand on one color.
        [[nodiscard]] bool IsInsufficientMaterial() const noexcept;

    private:
        /// What DoMove() overwrites, so UndoMove() can put it back.
        struct UndoInfo {
            Move move;
            Piece captured;
            ZobristKey key = 0;
            Bitboard checkers = 0;
            Bitboard pinned = 0;
            int castlingRights = 0;
            int enPassantSquare = kNoSquare;
            int halfmoveClock = 0;
        };

        Position() = default;

        void PutPiece(Piece piece, int sq) noexcept;
        void RemovePiece(int sq) noexcept;
        void MovePiece(int from, int to) noexcept;
        void UpdateCheckInfo() noexcept;

        eastl::array<Piece, kSquareCount> _board{};
        eastl::array<Bitboard, 7> _byType{};  ///< Indexed by PieceType; the None slot stays empty.
        eastl::array<Bitboard, 2> _byColor{};

        PieceColor _sideToMove = PieceColor::White;
        int _castlingRights = 0;
        int _enPassantSquare = kNoSquare;
        int _halfmoveClock = 0;
        int _fullmoveNumber = 1;
        ZobristKey _key = 0;
        Bitboard _checkers = 0;
        Bitboard _pinned = 0;

        eastl::vector<UndoInfo> _history;
    };

}  // namespace BECore::Chess
