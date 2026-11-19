#include "MoveReview.h"

#include <Chess/Game/MoveGenerator.h>

namespace BECore::Chess {

    namespace {

        int Clamped(int centipawns) noexcept {
            return eastl::clamp(centipawns, -kReviewClamp, kReviewClamp);
        }

        MoveQuality QualityOfLoss(int loss) noexcept {
            if (loss < kInaccuracyLoss) {
                return MoveQuality::Good;
            }
            if (loss < kMistakeLoss) {
                return MoveQuality::Inaccuracy;
            }
            return loss < kBlunderLoss ? MoveQuality::Mistake : MoveQuality::Blunder;
        }

    }  // namespace

    StringView MoveQualityId(MoveQuality quality) {
        switch (quality) {
            case MoveQuality::Best:
                return "best";
            case MoveQuality::Good:
                return "good";
            case MoveQuality::Inaccuracy:
                return "inaccuracy";
            case MoveQuality::Mistake:
                return "mistake";
            case MoveQuality::Blunder:
                return "blunder";
        }
        return "";
    }

    Optional<int> TerminalCentipawns(const Position& position) {
        if (!MoveGenerator::GenerateLegal(position).empty()) {
            return std::nullopt;
        }
        return position.IsInCheck() ? -kMateCentipawns : 0;
    }

    MoveReview ReviewMove(int bestCentipawns, Move bestMove, Move played, int afterCentipawns) {
        if (played == bestMove) {
            return {0, MoveQuality::Best};
        }
        const int loss = eastl::max(0, Clamped(bestCentipawns) - Clamped(-afterCentipawns));  // line 1 may score below the move that was played
        return {loss, QualityOfLoss(loss)};
    }

}  // namespace BECore::Chess
