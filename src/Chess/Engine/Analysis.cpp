#include "Analysis.h"

#include <Chess/Engine/Evaluation.h>

namespace BECore::Chess {

    namespace {

        constexpr int kCentipawnsPerPawn = 100;

    }  // namespace

    AnalysisScore ScoreFromSearch(int searchScore) noexcept {
        if (!IsMateScore(searchScore)) {
            return {AnalysisScoreKind::Centipawns, searchScore};
        }
        const int plies = kMateScore - (searchScore < 0 ? -searchScore : searchScore);
        ASSERT(plies > 0, "ScoreFromSearch: a mate at the root (ply 0) is a finished game, which no analysis is asked of");
        const int moves = (plies + 1) / 2;
        return {AnalysisScoreKind::Mate, searchScore < 0 ? -moves : moves};
    }

    AnalysisScore ScoreForWhite(const AnalysisScore& score, PieceColor sideToMove) noexcept {
        return {score.kind, sideToMove == PieceColor::White ? score.value : -score.value};
    }

    eastl::string ScoreText(const AnalysisScore& score) {
        switch (score.kind) {
            case AnalysisScoreKind::Mate:
                return Format("#{}", score.value);
            case AnalysisScoreKind::Centipawns: {
                const int64_t magnitude = score.value < 0 ? -static_cast<int64_t>(score.value) : score.value;
                const char* sign = score.value > 0 ? "+" : (score.value < 0 ? "-" : "");
                return Format("{}{}.{:02}", sign, magnitude / kCentipawnsPerPawn, magnitude % kCentipawnsPerPawn);
            }
        }
        return {};
    }

}  // namespace BECore::Chess
