#include "GameStatus.h"

#include <Chess/Game/MoveGenerator.h>

namespace BECore::Chess {

    namespace {

        constexpr int kFivefoldRepetitions = 5;
        constexpr int kSeventyFiveMovePlies = 150;

    }  // namespace

    GameStatus EvaluateStatus(const Position& position) {
        if (MoveGenerator::GenerateLegal(position).empty()) {
            return position.IsInCheck() ? GameStatus::Checkmate : GameStatus::Stalemate;
        }
        if (position.IsInsufficientMaterial()) {
            return GameStatus::DrawInsufficientMaterial;
        }
        if (position.RepetitionCount() >= kFivefoldRepetitions) {
            return GameStatus::DrawFivefold;
        }
        if (position.HalfmoveClock() >= kSeventyFiveMovePlies) {
            return GameStatus::DrawSeventyFiveMoves;
        }
        return GameStatus::Ongoing;
    }

}  // namespace BECore::Chess
