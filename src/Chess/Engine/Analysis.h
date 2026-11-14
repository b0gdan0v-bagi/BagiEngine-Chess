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

    /// The most lines a SearchRequest may ask an analysis for (SearchRequest::lines); more is a BadRequest
    inline constexpr int kMaxAnalysisLines = 8;

    /**
     * @brief The answer to IChessEngine::Analyse(): the lines of the position, best first.
     *
     * Every line is a different legal move of the position with its own score, **all scores from the side to move** (line 2 is "the second best move
     * and how the position stands if it is played"). Line 1 is the answer a request for one line gets at a fixed depth (the same move; for OwnEngine also the same score
     * and depth; under a move time the searches are not repeatable, so neither are their lines). There are as many lines as SearchRequest::lines asks for, but fewer when the position has fewer legal
     * moves (a position with one legal move has one line, whatever was asked) or when the engine sent fewer: that is not an error. A successful analysis is never empty. The scores of the lines are
     * not forced to fall: an engine that searches each line on its own (OwnEngine) may give line 2 a score a little above line 1's.
     */
    struct Analysis {
        eastl::vector<AnalysisLine> lines;

        /// The first line. Only for an analysis that came back from an engine with a value (it has at least one line)
        [[nodiscard]] const AnalysisLine& Best() const {
            ASSERT(!lines.empty(), "Analysis::Best: no line");
            return lines.front();
        }

        [[nodiscard]] size_t LineCount() const noexcept {
            return lines.size();
        }

        /// The second line, or null when there is only one: a consumer that compares line 1 with line 2 does not index blindly
        [[nodiscard]] const AnalysisLine* Second() const noexcept {
            return lines.size() >= 2 ? &lines[1] : nullptr;
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
    [[nodiscard]] String ScoreText(const AnalysisScore& score);

}  // namespace BECore::Chess
