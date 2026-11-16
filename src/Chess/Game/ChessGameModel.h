#pragma once

#include <BECore/Utils/MoveOnlyFunction.h>
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
     * move, a takeback, a changed seed) and never goes back. A selection, a pending promotion (a click on the last rank leaves it pending
     * until ChoosePromotion() or CancelPromotion()) and the premove queue are no change of the game: they leave the
     * version alone, so a search in flight stays valid. The move PlayPremove() plays moves it, as any move.
     *
     * The premove queue holds the moves the person made while the program had the move (OnPremoveClicked()), played one per
     * answer of the program (PlayPremove()). It is dropped by a new game (Reset(), SetPosition()), a takeback, the end of
     * the game and a move of the premoving color that did not come from it, and, in PlayPremove(), whole when its front is
     * not legal in its turn. A premoved promotion plays as a queen. Who may premove is for the owner of the opponent to
     * decide; the model only keeps the queue.
     *
     * The seed varies what an engine picks among equal options (0 = no variety, the engines behave as they always did). A bare
     * model has seed 0 and keeps it over Reset(); a model with a seed source (SetSeedSource()) draws a new non-zero seed in
     * Reset(), inside the version step of the new game. SetPosition() keeps the seed: it is no new draw, so a position set from
     * outside stays reproducible. SetSeed() moves the version only when the value changes, so a search in flight is dropped.
     *
     * Viewing (ShowMoves()) shows an earlier position of the game on the board while the game stays where it is: a second set
     * of the handles and a copy of the position, read through the GetShown...() calls. It is no change of the game either.
     *
     * Visual pieces are tracked through opaque handles, so the scene can render moves and captures without re-deriving
     * identity from piece type and color. A handle is bound at registration to its starting square and follows its piece
     * through captures, castling, en passant and promotion (it then reports the promoted type); Reset() sends it back.
     */
    class ChessGameModel {
    public:
        /// Opaque caller-provided identity (e.g. a ChessPieceComponent*).
        using PieceHandle = const void*;

        /// Where Reset() gets a new seed from; returns any value (a 0 becomes 1).
        using SeedSource = MoveOnlyFunction<uint32_t()>;

        ChessGameModel();

        /// Restart from the standard opening position; with a seed source, also with a new seed (one version step for both).
        void Reset();

        /// Empty (the default) keeps the seed over Reset().
        void SetSeedSource(SeedSource source) {
            _seedSource = std::move(source);
        }
        [[nodiscard]] uint32_t GetSeed() const noexcept {
            return _seed;
        }
        /// The version grows only when @p seed differs from the current one.
        void SetSeed(uint32_t seed) noexcept;

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

        /**
         * @brief Shows the position after the first @p count moves for viewing only; @p count of GetMoves().size() or more is the game's own position, which ends viewing.
         *
         * The game does not change: GetPosition(), the moves, the status, the version and GetSquareOf() stay, GetShownPosition(), GetShownSquareOf() and GetShownPieceOf()
         * give the earlier position. The version stays because a view is like a selection: a search in flight remains valid. Starting to view drops the selection and a pending
         * promotion (they belong to the game's position); the premove queue stays. Every change of the game (a move, Reset(), SetPosition(), TakeBack()) ends viewing, so a new
         * move is never hidden, and so does a click on the board (OnSquareClicked(), OnPremoveClicked()), which then does nothing else.
         *
         * @return whether what is shown changed
         */
        bool ShowMoves(size_t count);

        /// Shows the game's own position again: ShowMoves(GetMoves().size())
        bool EndViewing();

        [[nodiscard]] bool IsViewing() const noexcept {
            return _viewing;
        }
        /// The moves played up to the shown position: GetMoves().size() unless viewing
        [[nodiscard]] size_t GetShownMoveCount() const noexcept {
            return _viewing ? _viewMoveCount : _moves.size();
        }
        /// The position the board shows: the viewed one while viewing, else GetPosition()
        [[nodiscard]] const Position& GetShownPosition() const noexcept {
            return _viewing ? _viewPosition : _position;
        }
        /// GetSquareOf() in the shown position
        [[nodiscard]] Square GetShownSquareOf(PieceHandle handle) const;
        /// GetPieceOf() in the shown position (a pawn that promotes later is still a pawn there)
        [[nodiscard]] Piece GetShownPieceOf(PieceHandle handle) const;

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

        using PieceTracks = HashMap<PieceHandle, PieceTrack>;

        void RecomputeLegalMoves();
        static void RelocatePieces(PieceTracks& tracks, Move move);
        static void Capture(PieceTracks& tracks, Square at);
        static PieceTrack* MoveHandle(PieceTracks& tracks, Square from, Square to);
        static const PieceTrack* FindActive(const PieceTracks& tracks, PieceHandle handle);

        void BeginGame(const Position& position);

        static void PlaceHandlesAtStart(PieceTracks& tracks);

        /// Hands the pieces of @p position out to the handles of @p pieces; the handles left over go off the board
        static void DealPieces(PieceTracks& pieces, const Position& position);

        /// Puts the handles of @p tracks where the game has them after its first @p moveCount moves: dealt to the position it began from, then moved by each of those moves
        void ReplayPieces(PieceTracks& tracks, size_t moveCount) const;

        /// Builds the shown position and its handles for _viewMoveCount
        void BuildView();

        /// Ends viewing without a word: the game is about to change
        void DropView();

        Position _position = Position::Start();
        String _startFen;
        Vector<Move> _moves;
        uint64_t _version = 0;
        uint32_t _seed = 0;
        SeedSource _seedSource;
        GameStatus _status = GameStatus::Ongoing;
        bool _hasSelected = false;
        Square _selected{};
        MoveList _selectedMoves;
        Vector<Square> _legalTargets;
        bool _hasPendingPromotion = false;
        Square _promotionTarget{};
        Vector<Premove> _premoves;
        Optional<PieceColor> _premoveColor;

        PieceTracks _pieces;

        bool _viewing = false;
        size_t _viewMoveCount = 0;
        Position _viewPosition = Position::Start();
        PieceTracks _viewPieces;
    };

}  // namespace BECore::Chess
