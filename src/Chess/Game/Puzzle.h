#pragma once

#include <Chess/Game/ChessTypes.h>
#include <Chess/Game/Move.h>

namespace BECore::Chess {

    /**
     * @brief One puzzle as the lichess table has it, checked: the position before the setup move and the line of moves.
     *
     * `line[0]` is the program's setup move, then the person, the program, the person, ...; the person moves last, so the line has an even number of moves. Made only by
     * Puzzle::FromFields(), which has replayed the whole line, so every move is legal in its turn.
     */
    class Puzzle {
    public:
        /**
         * @brief Checks the text fields of a row and replays the line from @p startFen.
         *
         * @return the puzzle, or the reason the row is refused: a bad FEN, a move that is no legal move in its turn, fewer than two moves, an odd number of them (the
         * person would not move last), an empty id, a rating that is no whole number.
         */
        [[nodiscard]] static std::expected<Puzzle, String> FromFields(StringView id, StringView startFen, StringView moves, StringView rating, StringView themes);

        String id;
        /// The position before the setup move
        String startFen;
        Vector<Move> line;
        int32_t rating = 0;
        Vector<String> themes;

        /// The person's color: the side to move after the setup move
        [[nodiscard]] PieceColor SolverColor() const noexcept {
            return _solver;
        }

        /// How many moves of the line are the person's
        [[nodiscard]] size_t SolverMoveCount() const noexcept {
            return line.size() / 2;
        }

    private:
        PieceColor _solver = PieceColor::White;
    };

    /**
     * @brief The puzzles of a table, in file order.
     *
     * The table is the TSV `id`, `fen`, `moves`, `rating`, `themes` (a header line first). Pure: the caller hands over the text.
     */
    class PuzzleSet {
    public:
        /**
         * @brief Reads the rows of @p tsv.
         *
         * A row that Puzzle::FromFields() refuses, one with fewer than five columns and one whose id an earlier row has is described in @p problems (when given) and
         * skipped; the others keep their order.
         */
        [[nodiscard]] static PuzzleSet Parse(StringView tsv, Vector<String>* problems = nullptr);

        [[nodiscard]] size_t Size() const noexcept {
            return _puzzles.size();
        }
        [[nodiscard]] bool IsEmpty() const noexcept {
            return _puzzles.empty();
        }

        /// @p index must be below Size()
        [[nodiscard]] const Puzzle& At(size_t index) const;

        /// The index of the puzzle with this id, or nothing
        [[nodiscard]] Optional<size_t> FindIndexById(StringView id) const;

    private:
        Vector<Puzzle> _puzzles;
    };

}  // namespace BECore::Chess
