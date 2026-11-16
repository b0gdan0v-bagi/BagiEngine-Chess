#include "AnalysisText.h"

#include <cmath>

namespace BECore::Chess {

    namespace AnalysisText {

        namespace {

            /// The steepness of the bar: 1 / (1 + exp(-k * centipawns)). Chosen so that a pawn moves the bar visibly and a queen up nearly fills it
            constexpr double kShareSteepness = 0.0055;

            constexpr double kEven = 0.5;

            String Words(StringView text) {
                return String(text.data(), text.size());
            }

        }  // namespace

        String Evaluation(const Optional<AnalysisLine>& lineForWhite, bool on, bool gameOver, bool failed) {
            if (!on) {
                return Words(kEvaluationOff);
            }
            if (failed) {
                return Words(kEvaluationFailed);
            }
            if (gameOver) {
                return Words(kEvaluationGameOver);
            }
            if (!lineForWhite) {
                return Words(kEvaluationPending);
            }
            return ScoreText(lineForWhite->score);
        }

        double Share(const AnalysisScore& scoreForWhite) noexcept {
            if (scoreForWhite.kind == AnalysisScoreKind::Mate) {
                if (scoreForWhite.value == 0) {
                    return kEven;
                }
                return scoreForWhite.value > 0 ? 1.0 : 0.0;
            }
            // The mirrored half is 1 - the computed one, so the symmetry is exact and not a matter of rounding
            const int64_t centipawns = scoreForWhite.value;
            const double magnitude = static_cast<double>(centipawns < 0 ? -centipawns : centipawns);
            const double ahead = 1.0 / (1.0 + std::exp(-kShareSteepness * magnitude));
            return centipawns < 0 ? 1.0 - ahead : ahead;
        }

    }  // namespace AnalysisText

}  // namespace BECore::Chess
