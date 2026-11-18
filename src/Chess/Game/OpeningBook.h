#pragma once

#include <Chess/Game/Attacks.h>
#include <Chess/Game/Move.h>

namespace BECore::Chess {

    /// An opening as the table names it: the ECO code ("B90") and the full name ("Sicilian Defense: Najdorf Variation")
    struct OpeningEntry {
        String eco;
        String name;
    };

    /**
     * @brief The openings of a table, found by the Zobrist key of the position they end in, so a transposition is the same opening.
     *
     * The table is the TSV of the lichess `chess-openings` data (`eco`, `name`, `pgn`, a header line first). Pure: the caller hands over the text.
     */
    class OpeningBook {
    public:
        /**
         * @brief Reads the rows of @p tsv and replays each `pgn` from the start position.
         *
         * A line with fewer than three columns, or a `pgn` that does not replay, is described in @p problems (when given) and skipped. Of two rows that end in the
         * same position the first one is kept. Rows that share their first plies with the row before them replay only the plies that differ.
         */
        [[nodiscard]] static OpeningBook Parse(StringView tsv, Vector<String>* problems = nullptr);

        /// The opening that ends in the position with @p key, or null
        [[nodiscard]] const OpeningEntry* Find(ZobristKey key) const;

        [[nodiscard]] size_t Size() const noexcept {
            return _entries.size();
        }
        [[nodiscard]] bool IsEmpty() const noexcept {
            return _entries.empty();
        }

    private:
        HashMap<ZobristKey, OpeningEntry> _entries;
    };

    /**
     * @brief The opening of a game: the entry of the last position among the start position and the first @p count moves that the book knows.
     *
     * So the name stays after the game has left the book. The moves are legal moves of the game that began at @p startFen (nothing is generated here); a
     * @p count above the number of moves means all of them; an invalid FEN gives nothing.
     */
    [[nodiscard]] Optional<OpeningEntry> OpeningOf(const OpeningBook& book, StringView startFen, const Vector<Move>& moves, size_t count);

    /**
     * @brief The name as the HUD shows it in at most @p maxChars characters (UTF-8 code points).
     *
     * The whole name when it fits, else the family (the text before the first ':'), else the text before the first ',', else the longest run of whole words
     * that fits (a single word longer than the limit is cut at the limit).
     */
    [[nodiscard]] String OpeningDisplayName(StringView name, size_t maxChars);

}  // namespace BECore::Chess
