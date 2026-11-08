#pragma once

#include <Chess/Game/Move.h>

namespace BECore::Chess {

    /// What an AnalysisScore counts. Centipawns: hundredths of a pawn. Mate: moves to a mate
    CORE_ENUM(AnalysisScoreKind, uint8_t, Centipawns, Mate)

    /**
     * @brief How a position stands, always from the point of view of the side to move (the convention of Search and of UCI).
     *
     * Centipawns: `value` is hundredths of a pawn, positive is good for the side to move. Mate: `value` is the number of moves
     * (not plies) to the mate, positive when the side to move mates, negative when it is mated; 0 is never produced (a mated
     * position has no move to analyse, an engine answers NoMove).
     */
    struct AnalysisScore {
        AnalysisScoreKind kind = AnalysisScoreKind::Centipawns;
        int value = 0;

        [[nodiscard]] bool operator==(const AnalysisScore&) const noexcept = default;
    };

    /// One line of an analysis: the move to play, how the position stands after the best play, and how deep the engine looked (0 when it does not say)
    struct AnalysisLine {
        Move move;
        AnalysisScore score;
        int depth = 0;
    };

    /**
     * @brief The answer to IChessEngine::Analyse(): the lines of the position, best first.
     *
     * One line today (the best move). The list is there so that several lines (MultiPV) can be added without changing the type; a successful
     * analysis is never empty.
     */
    struct Analysis {
        eastl::vector<AnalysisLine> lines;

        /// The first line. Only for an analysis that came back from an engine with a value (it has at least one line)
        [[nodiscard]] const AnalysisLine& Best() const {
            ASSERT(!lines.empty(), "Analysis::Best: no line");
            return lines.front();
        }
    };

    /**
     * @brief A score of Search (centipawns, or kMateScore - ply and its negation, see Evaluation.h) as an AnalysisScore.
     *
     * A mate that falls @p ply plies from the root is `moves = (ply + 1) / 2`: a win lands on an odd ply (our move, then theirs, ...:
     * 1 ply = mate in 1, 3 = 2, 5 = 3) and a loss on an even one (2 plies = mated in 1, 4 = 2, 6 = 3). The sign of @p searchScore is the sign of the value.
     * Anything that is not a mate score is centipawns as it is. A mate at ply 0 (kMateScore itself: the root is mated) is not a score to convert:
     * the root of an analysis has a move, so a search never gives it (an engine answers NoMove first); it is asserted.
     */
    [[nodiscard]] AnalysisScore ScoreFromSearch(int searchScore) noexcept;

    /**
     * @brief @p score (from the side to move's view) as White sees it: unchanged when White is to move, the sign of `value` flipped when Black is.
     *
     * The kind is kept (a mate stays a mate, in the same number of moves). It is the score a display with one fixed side wants (an evaluation bar), where
     * the analysis itself speaks from the side to move. Applying it twice for the same side is the identity.
     */
    [[nodiscard]] AnalysisScore ScoreForWhite(const AnalysisScore& score, PieceColor sideToMove) noexcept;

    /**
     * @brief A stable text of @p score for the log and the control API: "+0.35", "-1.20", "0.00", "#3", "#-2".
     *
     * Centipawns are shown as pawns with two decimals and a sign (none for zero); a centipawn is already a hundredth, so nothing is rounded.
     * A mate is `#` and the signed number of moves.
     */
    [[nodiscard]] eastl::string ScoreText(const AnalysisScore& score);

}  // namespace BECore::Chess
