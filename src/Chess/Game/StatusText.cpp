#include "StatusText.h"

namespace BECore::Chess {

    namespace StatusText {

        namespace {

            const char* ColorName(PieceColor color) {
                return color == PieceColor::White ? "White" : "Black";
            }

        }  // namespace

        String Turn(const ChessGameModel& model, bool opponentThinking) {
            if (!model.IsGameOver()) {
                if (opponentThinking) {
                    return "Computer is thinking";
                }
                return Format("{} to move", ColorName(model.GetTurn()));
            }
            if (const auto winner = model.GetWinner()) {
                return Format("{} wins", ColorName(*winner));
            }
            return "Draw";
        }

        String Status(const ChessGameModel& model) {
            switch (model.GetStatus()) {
                case GameStatus::Ongoing:
                    return model.IsInCheck() ? "Check" : "";
                case GameStatus::Checkmate:
                    return "Checkmate";
                case GameStatus::Stalemate:
                    return "Stalemate";
                case GameStatus::DrawInsufficientMaterial:
                    return "Draw: insufficient material";
                case GameStatus::DrawThreefold:
                    return "Draw: threefold repetition";
                case GameStatus::DrawFiftyMoves:
                    return "Draw: fifty-move rule";
            }
            return "";
        }

    }  // namespace StatusText

}  // namespace BECore::Chess
