#include "MoveRating.h"

namespace BECore::Chess {

    namespace {

        constexpr int kCentipawnsPerMateMove = 100;

    }  // namespace

    int ScoreToCentipawns(const AnalysisScore& score) noexcept {
        switch (score.kind) {
            case AnalysisScoreKind::Centipawns:
                return score.value;
            case AnalysisScoreKind::Mate: {
                const int moves = score.value < 0 ? -score.value : score.value;
                const int closer = kMateCentipawns - kCentipawnsPerMateMove * moves;
                return score.value < 0 ? -closer : closer;
            }
        }
        return 0;
    }

    eastl::string_view MoveRatingReasonId(MoveRatingReason reason) {
        switch (reason) {
            case MoveRatingReason::Brilliant:
                return "brilliant";
            case MoveRatingReason::NotBest:
                return "not_best";
            case MoveRatingReason::Forced:
                return "forced";
            case MoveRatingReason::Obvious:
                return "obvious";
        }
        return "";
    }

    MoveRating RateMove(const Analysis& before, Move played) {
        if (played != before.Best().move) {
            return {MoveRatingReason::NotBest};
        }
        const AnalysisLine* second = before.Second();
        if (second == nullptr) {
            return {MoveRatingReason::Forced};
        }
        const int gap = ScoreToCentipawns(before.Best().score) - ScoreToCentipawns(second->score);
        return {gap >= kForcedMargin ? MoveRatingReason::Obvious : MoveRatingReason::Brilliant};
    }

}  // namespace BECore::Chess
