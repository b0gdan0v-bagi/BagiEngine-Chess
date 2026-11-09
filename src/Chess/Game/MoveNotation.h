#pragma once

#include <Chess/Game/Move.h>
#include <Chess/Game/Position.h>

namespace BECore::Chess {

    /**
     * @brief The standard algebraic notation (SAN) of a move, as a person reads a score sheet: "e4", "Nf3", "exd5", "Nbd2", "O-O", "e8=Q+", "Qh4#".
     *
     * English piece letters K Q R B N, nothing for a pawn; a capture has "x" (a pawn names its file, en passant included, with no "e.p."); castling is
     * "O-O" / "O-O-O"; a promotion is "=Q" after the target. A piece gets its file, else its rank, else both when another piece of the same type can
     * legally go to the same target (a pinned piece does not count). The suffix is "+" when the side to move after the move is in check and "#" when it
     * also has no legal move; a stalemate or a draw has no suffix.
     *
     * @p move must be a legal move of the side to move in @p before (from MoveGenerator::GenerateLegal() or ParseUci()); for anything else the text is
     * unspecified. @p before is not changed.
     */
    [[nodiscard]] eastl::string SanOf(const Position& before, Move move);

}  // namespace BECore::Chess
