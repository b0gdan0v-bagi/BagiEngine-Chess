#pragma once

#include <Chess/Game/Move.h>
#include <Chess/Game/MoveGenerator.h>
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

    /// The same text for a caller that has the legal moves of @p before already (MoveGenerator::GenerateLegal(before)), so they are not generated again
    [[nodiscard]] String SanOf(const Position& before, Move move, const MoveList& legal);

    /**
     * @brief The inverse of SanOf(): the one legal move of the side to move in @p position that @p san names, or nothing.
     *
     * Reads what a score sheet or an opening table writes: "e4", "Nf3", "exd5" (also en passant), "Nbd2", "R1e2", "Qh4xe1", "O-O", "O-O-O", "e8=Q",
     * "bxa8=N+". A trailing "+", "#", "!" or "?" is ignored and "0-0" / "0-0-0" is castling. Nothing for no legal match, an ambiguous text, a
     * promotion letter on a move that does not promote, garbage and the empty text: it reads data, so it never asserts.
     */
    [[nodiscard]] Optional<Move> ParseSan(const Position& position, StringView san);

    /// The same for a caller that has the legal moves of @p position already (MoveGenerator::GenerateLegal(position)), so they are not generated again
    [[nodiscard]] Optional<Move> ParseSan(const Position& position, StringView san, const MoveList& legal);

}  // namespace BECore::Chess
