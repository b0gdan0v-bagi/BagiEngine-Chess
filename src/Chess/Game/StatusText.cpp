#include "StatusText.h"

#include <Chess/Game/EditorText.h>

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
                case GameStatus::Resigned:
                    return Format("{} resigned", ColorName(model.GetResigned().value_or(PieceColor::White)));
                case GameStatus::DrawAgreed:
                    return "Draw agreed";
            }
            return "";
        }

        String DrawDeclined() {
            return "Draw declined";
        }

        String ResignQuestion() {
            return "Resign?";
        }

        String DrawQuestion() {
            return "Agree a draw?";
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

        String Editing(const PositionEditor& editor, StringView message) {
            if (!message.empty()) {
                return String(message);
            }
            if (const Optional<EditRefusal> refusal = editor.GetLastRefusal()) {
                return String(EditorText::Refusal(*refusal));
            }
            return "Editing";
        }

        String PuzzleTurn(PuzzlePhase phase) {
            switch (phase) {
                case PuzzlePhase::YourMove:
                    return "Your move";
                case PuzzlePhase::Replying:
                    return "Replying";
                case PuzzlePhase::Solved:
                    return "Solved";
            }
            return "";
        }

        String PuzzleStatus(bool wrong) {
            return wrong ? "Wrong move" : "";
        }

        String PuzzleNumber(size_t index, size_t count) {
            return Format("Puzzle {} of {}", index + 1, count);
        }

        String PuzzleRating(int32_t rating) {
            return Format("Rating {}", rating);
        }

        String PuzzleProgress(PuzzlePhase phase, bool wrong, size_t mistakes) {
            String line = wrong ? PuzzleStatus(true) : PuzzleTurn(phase);
            if (mistakes > 0) {
                line += Format(", {} {}", mistakes, mistakes == 1 ? "mistake" : "mistakes");
            }
            return line;
        }

        String PuzzleNextWord(bool puzzleOn) {
            return puzzleOn ? "Next" : "Start";
        }

        String NoPuzzles() {
            return "No puzzles";
        }

    }  // namespace StatusText

}  // namespace BECore::Chess
