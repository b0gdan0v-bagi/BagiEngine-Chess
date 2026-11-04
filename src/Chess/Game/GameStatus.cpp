#include "GameStatus.h"

#include <Chess/Game/MoveGenerator.h>

namespace BECore::Chess {

    namespace {

        constexpr int kThreefoldRepetitions = 3;

    }  // namespace

    GameStatus EvaluateStatus(const Position& position) {
        if (MoveGenerator::GenerateLegal(position).empty()) {
            return position.IsInCheck() ? GameStatus::Checkmate : GameStatus::Stalemate;
        }
        if (position.IsInsufficientMaterial()) {
            return GameStatus::DrawInsufficientMaterial;
        }
        if (position.RepetitionCount() >= kThreefoldRepetitions) {
            return GameStatus::DrawThreefold;
        }
        if (position.HalfmoveClock() >= kFiftyMovePlies) {
            return GameStatus::DrawFiftyMoves;
        }
        return GameStatus::Ongoing;
    }

}  // namespace BECore::Chess
