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
     * @brief Stateful match wrapper around Position + MoveGenerator.
     *
     * Owns the position, the selected square (the "from" half of a pending move, set by OnSquareClicked() in response to
     * the user clicking a square), the status of the game and the moves played since it began. Every move goes through
     * TryMove(): it is checked against the legal moves, applied, and the status is worked out again (EvaluateStatus()).
     * Once the status is not Ongoing no move is accepted until Reset() or SetPosition().
     *
     * GetVersion() grows with every change a controller has to see (a new game, a position set from outside, a played
     * move, a takeback) and never goes back; selecting a piece does not move it. A click on the last rank does not move the pawn either:
     * it leaves a promotion pending (HasPendingPromotion()) until the person names the piece (ChoosePromotion()) or drops it
     * (CancelPromotion()); a pending promotion is not a change of the game, so the version stays.
     *
     * It also holds the **premoves**: a queue of moves the person made while the program had the move (OnPremoveClicked()),
     * played one per answer of the program (PlayPremove()). Like a selection, a queue is no change of the game: no call that
     * only touches it moves the version, so a search in flight stays valid (the move PlayPremove() plays moves it, as any
     * move). A queue is dropped by a new game (Reset(), SetPosition()), a takeback, the end of the game and a move of the
     * premoving color that did not come from it, and, in PlayPremove(), whole when its front is not legal in its turn. A
     * premoved promotion plays as a queen. Who may premove is for the owner of the opponent to decide; the model only keeps the queue.
     *
     * Also tracks visual-piece identities through opaque handles so the scene layer can render moves and captures
     * without re-deriving identity from piece type/color. Each handle is bound at registration time to its starting
     * square; Reset() sends every handle back to that square. A handle follows its piece through captures, castling,
     * en passant and promotion (it then reports the promoted type).
     */
    class ChessGameModel {
    public:
        /// Opaque caller-provided identity (e.g. a ChessPieceComponent*).
        using PieceHandle = const void*;

        ChessGameModel();

        /// Restart from the standard opening position; restores all registered
        /// handles to their start squares; clears selection.
        void Reset();

        /**
         * @brief Start a game from @p position instead of the opening one (it becomes the start of the move list).
         *
         * Pass a position that came from Position::FromFen(), so that it carries no history. The registered handles are
         * dealt out again: for each color, in the order of their start squares, to the pieces of that color in the order of
         * the squares; a handle that finds no piece leaves the board. FromFen() allows 16 pieces per color at most, which
         * is how many handles a color has in the scene. The status is worked out for the new position (a position that is
         * already mated, stalemated or drawn starts as a finished game); selection is cleared.
         */
        void SetPosition(const Position& position);

        /// Whose turn it is to move.
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
        /// The moves played since the game began, in order.
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

        /// Helper for tests / UI: legal target squares for the piece on an arbitrary square.
        [[nodiscard]] Vector<Square> GetLegalMovesFor(Square from) const;

        /// Drops the selection and its legal targets and any pending promotion, for an owner that takes over the turn (the computer opponent); the version does not move.
        void ClearSelection();

        /// The queued premoves, the next to play first
        [[nodiscard]] const Vector<Premove>& GetPremoves() const noexcept {
            return _premoves;
        }
        /// The color that premoves (the person's); empty while the queue is empty
        [[nodiscard]] Optional<PieceColor> GetPremoveColor() const noexcept {
            return _premoveColor;
        }

        /**
         * @brief Click handler of a premove, for @p premover while the OTHER side has the move (the program thinks).
         *
         * On the premove board (the position with the queued premoves lifted over it, `PremoveBoard`): no selection and an
         * own piece -> select it (GetSelected() and GetLegalMoves() hold its premove targets); a selection and one of its
         * targets -> the premove is queued and the selection ends; a selection and another own piece -> select that one;
         * **any other click drops the whole queue** (and the selection). The length of the queue has no cap. Nothing here
         * moves the version, and ClearSelection() leaves the queue alone.
         *
         * @return true when a premove was queued; false, and nothing done, when the game is over, @p s is outside the board
         * or @p premover is the side to move (that person plays with OnSquareClicked()).
         */
        bool OnPremoveClicked(PieceColor premover, Square s);

        /**
         * @brief Plays the front premove when the premoving color is on the move.
         *
         * The premove is found among the legal moves by `from` and `to` (the queen for a promotion; an en passant capture is
         * found too) and played through the one place a move is applied, so the version moves as for any move. The rest of the
         * queue stays for the next answer.
         *
         * @return true when a move was played. False when the queue is empty or its color is not on the move yet (the queue
         * stays), and when no legal move matches the front: then **the whole queue is dropped** (a premove that has become
         * illegal cuts the ones behind it), with no error and no change of the version.
         */
        bool PlayPremove();

        /// Drops the queue; the version does not move
        void ClearPremoves();

        /**
         * @brief Takes back the last @p plies moves in one step (all there are when fewer).
         *
         * The position goes back (its history of repetitions with it, so the threefold rule stays right), the status is worked
         * out again (a finished game is on again), the selection and a pending promotion are dropped, and the piece handles
         * stand where a game that played only the kept moves would have them (a captured piece is back, a promoted one is a
         * pawn again). The version grows once, whatever the number of plies. The game goes back at most to its start: the
         * opening position, or the one given to SetPosition() (GetStartFen() does not change).
         *
         * @return how many moves were taken back; 0 (no moves, or @p plies is 0) changes nothing, the version included.
         */
        size_t TakeBack(size_t plies = 1);

        /// A selected pawn was clicked onto its last rank and waits for the piece it becomes (ChoosePromotion()); nothing has moved
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
         * @brief Click handler. Drives the two-step selection / move flow:
         *
         *   1. If no selection and `s` holds a piece of the side-to-move → select it.
         *   2. If a selection exists and `s` is a legal target of that selection →
         *      perform the move and switch turns. A pawn that reaches the last rank
         *      does not move: the promotion is left pending (ChoosePromotion() plays it).
         *   3. If a selection exists and `s` holds another own-color piece →
         *      switch selection to it.
         *   4. Otherwise → clear selection.
         *
         * While a promotion is pending any click on a square only drops it (the selection stays).
         * Clicks are ignored once the game is over.
         *
         * @return true if the board state changed (move executed).
         */
        bool OnSquareClicked(Square s);

        /**
         * @brief Play @p move if it is legal for the side to move.
         *
         * The one place a move is applied; it does not need a selection. Moves come from MoveGenerator (see
         * MoveGenerator::ParseUci() for text input), which is the way to play a promotion to a piece other than a queen.
         *
         * @return nothing when the move has been played; GameOver once the game is finished, IllegalMove for any other move.
         */
        std::expected<void, MoveError> TryMove(Move move);

        /// Register a visual identity that lives at @p startSquare of the opening position. Idempotent. Registering into a game
        /// that has moved on or began from a set position puts the piece where that position has one (see SetPosition()).
        void RegisterPiece(PieceHandle handle, Square startSquare);
        void UnregisterPiece(PieceHandle handle);

        /// Square currently occupied by @p handle, or {-1,-1} if captured / unknown.
        [[nodiscard]] Square GetSquareOf(PieceHandle handle) const;

        /// What @p handle is right now (a promoted pawn reports its new type), or an empty piece if captured / unknown.
        [[nodiscard]] Piece GetPieceOf(PieceHandle handle) const;

        /// True if @p handle is on the board (registered and not captured).
        [[nodiscard]] bool IsActive(PieceHandle handle) const;

    private:
        /// The body of TryMove() and PlayPremove(): checks, applies and works out the status; a game that ends drops the premoves
        std::expected<void, MoveError> ApplyMove(Move move);

        /// The real position with the queued premoves lifted over it
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

        /// Sends every handle back to its start square, as the opening position has it
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
