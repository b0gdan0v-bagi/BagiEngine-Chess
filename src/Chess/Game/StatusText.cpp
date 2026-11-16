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
            if (model.IsViewing()) {
                return Viewing(model);
            }
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

        String Viewing(const ChessGameModel& model) {
            if (!model.IsViewing()) {
                return {};
            }
            if (model.GetShownMoveCount() == 0) {
                return "At the start - tap board";
            }
            // The fullmove number grows after Black's move, so a shown position with White to move came after Black's move of the number before
            const Position& shown = model.GetShownPosition();
            const int number = shown.SideToMove() == PieceColor::White ? shown.FullmoveNumber() - 1 : shown.FullmoveNumber();
            return Format("At move {} - tap board", number);
        }

    }  // namespace StatusText

}  // namespace BECore::Chess
