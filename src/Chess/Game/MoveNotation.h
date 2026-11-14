#pragma once

#include <Chess/Game/Move.h>
#include <Chess/Game/Position.h>

namespace BECore::Chess {

    /**
     * @brief The standard algebraic notation (SAN) of a move, as a person reads a score sheet: "e4", "Nf3", "exd5", "Nbd2", "O-O", "e8=Q+", "Qh4#".
     *
     * English piece letters. A piece gets its file, else its rank, else both when another piece of the same type can
     * legally go to the same target (a pinned piece does not count). A stalemate or a draw has no check suffix.
     *
     * @p move must be a legal move of the side to move in @p before (from MoveGenerator::GenerateLegal() or ParseUci()); for anything else the text is
     * unspecified. @p before is not changed.
     */
    [[nodiscard]] String SanOf(const Position& before, Move move);

}  // namespace BECore::Chess
