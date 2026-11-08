#include "TranspositionTable.h"

namespace BECore::Chess {

    namespace {

        constexpr int kMaxStoredDepth = 127;

        size_t RoundUpToPowerOfTwo(size_t value) noexcept {
            size_t result = 1;
            while (result < value) {
                result <<= 1;
            }
            return result;
        }

    }  // namespace

    TranspositionTable::TranspositionTable(size_t entries) : _entries(RoundUpToPowerOfTwo(entries)) {}

    const TableEntry* TranspositionTable::Probe(ZobristKey key) const noexcept {
        const TableEntry& entry = _entries[IndexOf(key)];
        return (entry.depth >= 0 && entry.key == key) ? &entry : nullptr;
    }

    void TranspositionTable::Store(ZobristKey key, Move move, int score, int depth, TableBound bound) noexcept {
        ASSERT(score >= INT16_MIN && score <= INT16_MAX, "TranspositionTable: the score does not fit 16 bits");
        const int storedDepth = depth < 0 ? 0 : (depth > kMaxStoredDepth ? kMaxStoredDepth : depth);

        TableEntry& slot = _entries[IndexOf(key)];
        const bool sameKey = slot.depth >= 0 && slot.key == key;
        if (!sameKey && slot.depth > storedDepth) {
            return;
        }

        const Move keptMove = (sameKey && move.IsNull()) ? slot.move : move;
        slot.key = key;
        slot.move = keptMove;
        slot.score = static_cast<int16_t>(score);
        slot.depth = static_cast<int8_t>(storedDepth);
        slot.bound = bound;
    }

    void TranspositionTable::Clear() noexcept {
        for (TableEntry& entry : _entries) {
            entry = TableEntry{};
        }
    }

}  // namespace BECore::Chess
