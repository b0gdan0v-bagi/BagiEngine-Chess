#include "DrawOffer.h"

namespace BECore::Chess {

    Optional<DrawOfferRefusal> DrawOfferRefusalFor(const ChessGameModel& model, ProgramSides programPlays) {
        if (model.IsGameOver()) {
            return DrawOfferRefusal::GameOver;
        }
        if (programPlays.white && programPlays.black) {
            return DrawOfferRefusal::NoPerson;
        }
        if (programPlays.Plays(model.GetTurn())) {
            return DrawOfferRefusal::NotYourTurn;
        }
        return std::nullopt;
    }

    bool ProgramAcceptsDraw(AnalysisScore scoreForSideToMove, PieceColor sideToMove, PieceColor programColor) noexcept {
        const int forProgram = sideToMove == programColor ? scoreForSideToMove.value : -scoreForSideToMove.value;
        if (scoreForSideToMove.kind == AnalysisScoreKind::Mate) {
            return forProgram < 0;
        }
        return forProgram <= kDrawAcceptCentipawns;
    }

}  // namespace BECore::Chess
