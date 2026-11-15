#pragma once

#include <Chess/Game/ChessTypes.h>
#include <Chess/Game/GameStatus.h>
#include <Chess/Game/Move.h>
#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/Position.h>
#include <Chess/Game/Premove.h>

namespace BECore::Chess {

    /// Why ChessGameModel::TryMove() refused a move.
    CORE_ENUM(MoveError, uint8_t, GameOver, IllegalMove)

    /**
     * @brief Stateful match wrapper around Position + MoveGenerator: the position, the selection, the status and the moves
     * played since the game began. Every move goes through TryMove(); once the status is not Ongoing no move is accepted
     * until Reset() or SetPosition().
     *
     * GetVersion() grows with every change a controller has to see (a new game, a position set from outside, a played
     * move, a takeback) and never goes back. A selection, a pending promotion (a click on the last rank leaves it pending
     * until ChoosePromotion() or CancelPromotion()) and the premove queue are no change of the game: they leave the
     * version alone, so a search in flight stays valid. The move PlayPremove() plays moves it, as any move.
     *
     * The premove queue holds the moves the person made while the program had the move (OnPremoveClicked()), played one per
     * answer of the program (PlayPremove()). It is dropped by a new game (Reset(), SetPosition()), a takeback, the end of
     * the game and a move of the premoving color that did not come from it, and, in PlayPremove(), whole when its front is
     * not legal in its turn. A premoved promotion plays as a queen. Who may premove is for the owner of the opponent to
     * decide; the model only keeps the queue.
     *
     * Visual pieces are tracked through opaque handles, so the scene can render moves and captures without re-deriving
     * identity from piece type and color. A handle is bound at registration to its starting square and follows its piece
     * through captures, castling, en passant and promotion (it then reports the promoted type); Reset() sends it back.
     */
    class ChessGameModel {
    public:
        /// Opaque caller-provided identity (e.g. a ChessPieceComponent*).
        using PieceHandle = const void*;

        ChessGameModel();

        /// Restart from the standard opening position.
        void Reset();

        /**
         * @brief Start a game from @p position instead of the opening one (it becomes the start of the move list).
         *
         * Pass a position that came from Position::FromFen(), so that it carries no history. The registered handles are
         * dealt out again: for each color, in the order of their start squares, to the pieces of that color in the order of
         * the squares; a handle that finds no piece leaves the board (FromFen() allows 16 pieces per color, as many handles
         * as the scene has). A position already mated, stalemated or drawn starts as a finished game.
         */
        void SetPosition(const Position& position);

        [[nodiscard]] PieceColor GetTurn() const noexcept {
            return _position.SideToMove();
        }

        [[nodiscard]] const Position& GetPosition() const noexcept {
            return _position;
        }
        /// The FEN the game began from: the opening position, or the one given to SetPosition().
        [[nodiscard]] const String& GetStartFen() const noexcept {
            return _startFen;
        }
        [[nodiscard]] const Vector<Move>& GetMoves() const noexcept {
            return _moves;
        }
        [[nodiscard]] uint64_t GetVersion() const noexcept {
            return _version;
        }
        [[nodiscard]] Piece GetPieceAt(Square s) const noexcept {
            return _position.GetPieceAt(s);
        }

        [[nodiscard]] GameStatus GetStatus() const noexcept {
            return _status;
        }
        [[nodiscard]] bool IsGameOver() const noexcept {
            return _status != GameStatus::Ongoing;
        }
        /// The side that won; empty while the game is on and for every draw.
        [[nodiscard]] Optional<PieceColor> GetWinner() const noexcept;
        /// The side to move is in check (checkmate included).
        [[nodiscard]] bool IsInCheck() const noexcept {
            return _position.IsInCheck();
        }
        [[nodiscard]] Square GetKingSquare(PieceColor color) const noexcept {
            return Square::FromIndex(_position.KingSquare(color));
        }

        [[nodiscard]] bool HasSelection() const noexcept {
            return _hasSelected;
        }
        [[nodiscard]] Square GetSelected() const noexcept {
            return _selected;
        }

        /// Squares the selected piece can legally move to (each once, even when a pawn can promote there). Empty if no selection.
        [[nodiscard]] const Vector<Square>& GetLegalMoves() const noexcept {
            return _legalTargets;
        }

        /// Legal target squares of the piece on any square, not only the selected one.
        [[nodiscard]] Vector<Square> GetLegalMovesFor(Square from) const;

        /// Drops the selection and a pending promotion, for an owner that takes over the turn (the computer opponent); the premove queue stays.
        void ClearSelection();

        /// The queued premoves, in the order they play: the front one plays first.
        [[nodiscard]] const Vector<Premove>& GetPremoves() const noexcept {
            return _premoves;
        }
        /// The color that premoves (the side whose moves are queued); empty while the queue is empty.
        [[nodiscard]] Optional<PieceColor> GetPremoveColor() const noexcept {
            return _premoveColor;
        }

        /**
         * @brief Click handler of a premove, for @p premover while the OTHER side has the move (the program thinks).
         *
         * Works on the premove board (the position with the queued premoves lifted over it): an own piece is selected
         * (GetLegalMoves() holds its premove targets), one of its targets queues the premove, **any other click drops the
         * whole queue** and the selection. The queue has no cap.
         *
         * @return true when a premove was queued; false, and nothing done, when the game is over, @p s is outside the board
         * or @p premover is the side to move (that person plays with OnSquareClicked()).
         */
        bool OnPremoveClicked(PieceColor premover, Square s);

        /**
         * @brief Plays the front premove when the premoving color is on the move.
         *
         * The premove is found among the legal moves by `from` and `to` (the queen for a promotion) and played through
         * ApplyMove(). The rest of the queue stays for the next answer.
         *
         * @return true when a move was played. False when the queue is empty or its color is not on the move yet (the queue
         * stays), and when no legal move matches the front: then **the whole queue is dropped** (a premove that has become
         * illegal cuts the ones behind it), with no error and no change of the version.
         */
        bool PlayPremove();

        /// Drops the queue
        void ClearPremoves();

        /**
         * @brief Takes back the last @p plies moves in one step (all there are when fewer).
         *
         * The position goes back (its history of repetitions with it, so the threefold rule stays right), a finished game is
         * on again, the selection, a pending promotion and the premove queue are dropped, and the handles stand where a game
         * that played only the kept moves would have them (a captured piece is back, a promoted one is a pawn again). The
         * version grows once, whatever the number of plies. The game goes back at most to GetStartFen().
         *
         * @return how many moves were taken back; 0 (no moves, or @p plies is 0) changes nothing, the version included.
         */
        size_t TakeBack(size_t plies = 1);

        /// A pawn was clicked onto its last rank and waits for the piece it becomes (ChoosePromotion()); nothing has moved
        [[nodiscard]] bool HasPendingPromotion() const noexcept {
            return _hasPendingPromotion;
        }
        /// The square the pending promotion goes to; meaningful only while HasPendingPromotion()
        [[nodiscard]] Square GetPromotionTarget() const noexcept {
            return _promotionTarget;
        }

        /**
         * @brief Plays the pending promotion as @p piece (Queen, Rook, Bishop or Knight).
         *
         * @return nothing when the move has been played; GameOver once the game is finished, IllegalMove when no promotion is
         * pending or @p piece is not one of the four (then nothing changes).
         */
        std::expected<void, MoveError> ChoosePromotion(PieceType piece);

        /// Drops the pending promotion and keeps the selection; the version does not move.
        void CancelPromotion();

        /**
         * @brief Click handler of the two-step select / move flow: a piece of the side to move is selected, a legal target of
         * the selection plays the move, another own piece switches the selection, anything else clears it. A pawn that
         * reaches the last rank does not move: the promotion is left pending. While one is pending any click only drops it
         * (the selection stays). Clicks are ignored once the game is over.
         *
         * @return true if the board state changed (move executed).
         */
        bool OnSquareClicked(Square s);

        /**
         * @brief Play @p move if it is legal for the side to move.
         *
         * Needs no selection. Moves come from MoveGenerator (MoveGenerator::ParseUci() for text input), which is the way to
         * play a promotion to a piece other than a queen.
         *
         * @return nothing when the move has been played; GameOver once the game is finished, IllegalMove for any other move.
         */
        std::expected<void, MoveError> TryMove(Move move);

        /// Idempotent. Registering into a game that has moved on or began from a set position puts the piece where that
        /// position has one (see SetPosition()).
        void RegisterPiece(PieceHandle handle, Square startSquare);
        void UnregisterPiece(PieceHandle handle);

        /// {-1,-1} if captured / unknown.
        [[nodiscard]] Square GetSquareOf(PieceHandle handle) const;

        /// A promoted pawn reports its new type; an empty piece if captured / unknown.
        [[nodiscard]] Piece GetPieceOf(PieceHandle handle) const;

        /// Whether the piece is on the board: the handle is known and the piece was not captured.
        [[nodiscard]] bool IsActive(PieceHandle handle) const;

    private:
        /// The body of TryMove() and PlayPremove(): checks, applies and works out the status; a game that ends drops the premoves
        std::expected<void, MoveError> ApplyMove(Move move);

        [[nodiscard]] PremoveBoard MakePremoveBoard() const;

        struct PieceTrack {
            Square start{};
            Piece startPiece{};
            Square current{-1, -1};
            Piece piece{};
            bool active = false;
        };

        void RecomputeLegalMoves();
        void RelocatePieces(Move move);
        void Capture(Square at);
        PieceTrack* MoveHandle(Square from, Square to);

        void BeginGame(const Position& position);

        void PlaceHandlesAtStart();

        /// Hands the pieces of @p position out to the registered handles; the handles left over go off the board
        void DealPieces(const Position& position);

        /// Puts the handles where the game has them: dealt to the position it began from, then moved by each move played
        void ReplayPieces();

        Position _position = Position::Start();
        String _startFen;
        Vector<Move> _moves;
        uint64_t _version = 0;
        GameStatus _status = GameStatus::Ongoing;
        bool _hasSelected = false;
        Square _selected{};
        MoveList _selectedMoves;
        Vector<Square> _legalTargets;
        bool _hasPendingPromotion = false;
        Square _promotionTarget{};
        Vector<Premove> _premoves;
        Optional<PieceColor> _premoveColor;

        HashMap<PieceHandle, PieceTrack> _pieces;
    };

}  // namespace BECore::Chess
