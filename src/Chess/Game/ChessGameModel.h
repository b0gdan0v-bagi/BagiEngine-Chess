#pragma once

#include <Chess/Game/ChessTypes.h>
#include <Chess/Game/GameStatus.h>
#include <Chess/Game/Move.h>
#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/Position.h>

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
     * move) and never goes back; selecting a piece does not move it.
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
        [[nodiscard]] const eastl::string& GetStartFen() const noexcept {
            return _startFen;
        }
        /// The moves played since the game began, in order.
        [[nodiscard]] const eastl::vector<Move>& GetMoves() const noexcept {
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
        [[nodiscard]] std::optional<PieceColor> GetWinner() const noexcept;
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
        [[nodiscard]] const eastl::vector<Square>& GetLegalMoves() const noexcept {
            return _legalTargets;
        }

        /// Helper for tests / UI: legal target squares for the piece on an arbitrary square.
        [[nodiscard]] eastl::vector<Square> GetLegalMovesFor(Square from) const;

        /**
         * @brief Click handler. Drives the two-step selection / move flow:
         *
         *   1. If no selection and `s` holds a piece of the side-to-move → select it.
         *   2. If a selection exists and `s` is a legal target of that selection →
         *      perform the move and switch turns. A pawn that reaches the last rank
         *      becomes a queen.
         *   3. If a selection exists and `s` holds another own-color piece →
         *      switch selection to it.
         *   4. Otherwise → clear selection.
         *
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
        struct PieceTrack {
            Square start{};
            Piece startPiece{};
            Square current{-1, -1};
            Piece piece{};
            bool active = false;
        };

        void ClearSelection();
        void RecomputeLegalMoves();
        void RelocatePieces(Move move);
        void Capture(Square at);
        PieceTrack* MoveHandle(Square from, Square to);

        void BeginGame(const Position& position);

        /// Hands the pieces of the current position out to the registered handles; the handles left over go off the board
        void DealPieces();

        Position _position = Position::Start();
        eastl::string _startFen;
        eastl::vector<Move> _moves;
        uint64_t _version = 0;
        GameStatus _status = GameStatus::Ongoing;
        bool _hasSelected = false;
        Square _selected{};
        MoveList _selectedMoves;
        eastl::vector<Square> _legalTargets;

        eastl::hash_map<PieceHandle, PieceTrack> _pieces;
    };

}  // namespace BECore::Chess
