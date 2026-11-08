#pragma once

#include <Chess/Engine/Evaluation.h>
#include <Chess/Game/Attacks.h>
#include <Chess/Game/Move.h>

namespace BECore::Chess {

    /// What a stored score says about the true value of the position. Exact: the score is the value of the position.
    /// Lower: the value is at least the score (the search cut off: a move was as good as that). Upper: the value is at
    /// most the score (no move reached alpha).
    CORE_ENUM(TableBound, uint8_t, Exact, Lower, Upper)

    /// One slot of the table. An empty slot has a depth of -1.
    struct TableEntry {
        ZobristKey key = 0;
        /// The best move found, or the move that cut off; the null move when there is none. Only a hint for the ordering
        Move move;
        /// The score as ScoreToTable() made it (the distance of a mate counted from this position), not from the root
        int16_t score = 0;
        /// The depth in plies of the search that gave the score; -1 for an empty slot
        int8_t depth = -1;
        TableBound bound = TableBound::Exact;
    };
    static_assert(sizeof(TableEntry) == 16, "an entry is 16 bytes: the memory of a table is entries x 16");

    /// The score of a node at @p ply as the table keeps it: a mate is counted from this node, not from the root, so the
    /// entry is right for the same position found at another ply. Any other score is kept as it is.
    [[nodiscard]] constexpr int ScoreToTable(int score, int ply) noexcept {
        if (IsMateScore(score)) {
            return score > 0 ? score + ply : score - ply;
        }
        return score;
    }

    /// The inverse of ScoreToTable() for a node at @p ply, which may differ from the ply it was stored at.
    [[nodiscard]] constexpr int ScoreFromTable(int score, int ply) noexcept {
        if (IsMateScore(score)) {
            return score > 0 ? score - ply : score + ply;
        }
        return score;
    }

    /**
     * @brief A fixed-size table of search results keyed by the Zobrist key of a position.
     *
     * The slot of a key is the key masked by the size (a power of two), so two keys may share one; the entry holds the
     * whole key and Probe() answers only for an equal one. A new result evicts the old one of another key only when it is
     * as deep or deeper (depth-preferred); a result for the key already in the slot always replaces it. The memory is
     * allocated once in the constructor and never grows. No clock, no randomness, no state beyond the slots: the same
     * stores give the same table on every platform.
     *
     * Not thread safe: one table belongs to one search.
     */
    class TranspositionTable {
    public:
        /// A table of @p entries slots, rounded up to a power of two; at least one
        explicit TranspositionTable(size_t entries);

        /// The entry for @p key, or null when the slot holds another key or nothing. Valid until the next Store() or Clear()
        [[nodiscard]] const TableEntry* Probe(ZobristKey key) const noexcept;

        /**
         * @brief Records a result for @p key.
         *
         * @p score is already in the form of ScoreToTable() and must fit 16 bits; @p depth is brought into 0..127. An entry of
         * another key is kept when it is deeper than @p depth. An entry of the same key is always replaced, but its move is
         * kept when @p move is the null move.
         */
        void Store(ZobristKey key, Move move, int score, int depth, TableBound bound) noexcept;

        /// The number of slots (a power of two)
        [[nodiscard]] size_t Size() const noexcept {
            return _entries.size();
        }

        /// Empties every slot
        void Clear() noexcept;

    private:
        [[nodiscard]] size_t IndexOf(ZobristKey key) const noexcept {
            return static_cast<size_t>(key) & (_entries.size() - 1);
        }

        eastl::vector<TableEntry> _entries;
    };

}  // namespace BECore::Chess
