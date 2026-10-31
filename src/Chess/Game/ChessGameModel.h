#pragma once

#include <Chess/Game/ChessBoard.h>
#include <Chess/Game/ChessTypes.h>

namespace BECore::Chess {

    /**
     * @brief Stateful match wrapper around ChessBoard + MoveGenerator.
     *
     * Owns the active board, whose turn it is, and the currently selected
     * square (if any). The Selected square is the "from" half of a pending
     * move, set by Select() in response to the user clicking a square.
     *
     * Also tracks visual-piece identities through opaque handles so the scene
     * layer can render moves and captures without re-deriving identity from
     * piece type/color. Each handle is bound at registration time to its
     * starting square; Reset() sends every handle back to that square.
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
            return _turn;
        }

        [[nodiscard]] const ChessBoard& GetBoard() const noexcept {
            return _board;
        }
        [[nodiscard]] Piece GetPieceAt(Square s) const noexcept {
            return _board.Get(s);
        }

        [[nodiscard]] bool HasSelection() const noexcept {
            return _hasSelected;
        }
        [[nodiscard]] Square GetSelected() const noexcept {
            return _selected;
        }

        /// Cached legal targets for the currently selected piece. Empty if no selection.
        [[nodiscard]] const eastl::vector<Square>& GetLegalMoves() const noexcept {
            return _legalMoves;
        }

        /// Helper for tests / UI: legal moves for an arbitrary square.
        [[nodiscard]] eastl::vector<Square> GetLegalMovesFor(Square from) const;

        /**
         * @brief Click handler. Drives the two-step selection / move flow:
         *
         *   1. If no selection and `s` holds a piece of the side-to-move → select it.
         *   2. If a selection exists and `s` is a legal target of that selection →
         *      perform the move and switch turns.
         *   3. If a selection exists and `s` holds another own-color piece →
         *      switch selection to it.
         *   4. Otherwise → clear selection.
         *
         * @return true if the board state changed (move executed).
         */
        bool OnSquareClicked(Square s);

        /// Register a visual identity that lives at @p startSquare. Idempotent.
        void RegisterPiece(PieceHandle handle, Square startSquare);
        void UnregisterPiece(PieceHandle handle);

        /// Square currently occupied by @p handle, or {-1,-1} if captured / unknown.
        [[nodiscard]] Square GetSquareOf(PieceHandle handle) const;

        /// True if @p handle is on the board (registered and not captured).
        [[nodiscard]] bool IsActive(PieceHandle handle) const;

    private:
        struct PieceTrack {
            Square start{};
            Square current{-1, -1};
            bool active = false;
        };

        void RecomputeLegalMoves();
        void RelocatePieces(Square from, Square to);

        ChessBoard _board;
        PieceColor _turn = PieceColor::White;
        bool _hasSelected = false;
        Square _selected{};
        eastl::vector<Square> _legalMoves;

        eastl::hash_map<PieceHandle, PieceTrack> _pieces;
    };

}  // namespace BECore::Chess
