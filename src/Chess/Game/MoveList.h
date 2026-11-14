#pragma once

#include <Chess/Game/Move.h>

namespace BECore::Chess {

    /// One move of the list: its text and where it is in the game. A place with no move (White's in a game that began with Black to move, Black's in
    /// the last row) has an empty text and a ply of -1.
    struct MoveListEntry {
        String san;
        int ply = -1;  ///< The index of the move in ChessGameModel::GetMoves()
    };

    /// One line of the list: the number and the two moves of that number, White's and Black's side by side.
    struct MoveListRow {
        int number = 0;
        MoveListEntry white;
        MoveListEntry black;
    };

    /**
     * @brief The rows of the move list as a score sheet shows them: "1. e4 e5", "2. Nf3 Nc6".
     *
     * The game is replayed from @p startFen (ChessGameModel::GetStartFen()) and the text of each move is SanOf() on the position before it. The first
     * number is the fullmove number of the start position; a game that began with Black to move has a first row with no White move, and the number
     * grows after each Black move. The last row may have no Black move; no moves, no rows.
     *
     * A start FEN that does not parse gives no rows. A move that is not legal in its turn (the model never produces one) ends the list there: the
     * rows before it stay.
     */
    [[nodiscard]] eastl::vector<MoveListRow> BuildMoveList(StringView startFen, const eastl::vector<Move>& moves);

}  // namespace BECore::Chess
