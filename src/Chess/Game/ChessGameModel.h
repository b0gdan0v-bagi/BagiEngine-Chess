#pragma once

#include <Chess/Game/ChessTypes.h>
#include <Chess/Game/GameStatus.h>
#include <Chess/Game/Move.h>
#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/Position.h>

namespace BECore::Chess {

    /**
     * @brief Stateful match wrapper around Position + MoveGenerator.
     *
     * Owns the position, the selected square (the "from" half of a pending move, set by OnSquareClicked() in response to
     * the user clicking a square) and the status of the game. Every move goes through TryMove(): it is checked against the
     * legal moves, applied, and the status is worked out again (EvaluateStatus()). Once the status is not Ongoing no move
     * is accepted until Reset().
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

        /// Whose turn it is to move.
        [[nodiscard]] PieceColor GetTurn() const noexcept {
            return _position.SideToMove();
        }

        [[nodiscard]] const Position& GetPosition() const noexcept {
            return _position;
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
         * @return true if the move was legal and has been played.
         */
        bool TryMove(Move move);

        /// Register a visual identity that lives at @p startSquare. Idempotent.
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

        Position _position = Position::Start();
        GameStatus _status = GameStatus::Ongoing;
        bool _hasSelected = false;
        Square _selected{};
        MoveList _selectedMoves;
        eastl::vector<Square> _legalTargets;

        eastl::hash_map<PieceHandle, PieceTrack> _pieces;
    };

}  // namespace BECore::Chess
