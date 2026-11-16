#pragma once

#include <Chess/Engine/Analysis.h>

namespace BECore::Chess {

    /// The words and the bar of the evaluation cell of the move panel. Pure, in Engine/ because the score types live here and Game/ must not know them
    namespace AnalysisText {

        inline constexpr StringView kEvaluationOff = "Eval off";
        inline constexpr StringView kEvaluationPending = "...";
        inline constexpr StringView kEvaluationGameOver = "-";
        inline constexpr StringView kEvaluationFailed = "No analysis";

        /// The longest word Evaluation() can make for a score an engine gives (a search never scores beyond kMateScore - kMaxMatePly, a mate is at most kMaxMatePly / 2 moves),
        /// so the cell and its font are sized by it. A score beyond that (an engine's own) may be longer: the cell clips it
        inline constexpr size_t kMaxEvaluationLength = 11;

        /**
         * @brief The word of the evaluation cell. The rules in order: off, failed, game over, the line, else pending.
         *
         * @param lineForWhite the best line of the game's position with its score from White's view (ScoreForWhite); empty when there is no answer yet
         * @param on whether the evaluation is switched on
         * @param gameOver whether the game is finished (nothing to analyse)
         * @param failed whether the search of this position failed
         */
        [[nodiscard]] String Evaluation(const Optional<AnalysisLine>& lineForWhite, bool on, bool gameOver, bool failed);

        /**
         * @brief The share of White in the bar, 0..1, from a score in White's view.
         *
         * 0.5 at 0 centipawns, a sigmoid of the centipawns (strictly rising until it saturates in a double), `Share(-x) == 1 - Share(x)` exactly, a mate for White 1 and against White 0.
         * Never outside 0..1, for any int.
         */
        [[nodiscard]] double Share(const AnalysisScore& scoreForWhite) noexcept;

    }  // namespace AnalysisText

}  // namespace BECore::Chess
