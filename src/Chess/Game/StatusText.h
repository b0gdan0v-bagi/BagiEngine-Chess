#pragma once

#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/PositionEditor.h>
#include <Chess/Game/PuzzleSession.h>

namespace BECore::Chess {

    /// The words of the HUD. Pure: they read the model and make text, nothing else
    namespace StatusText {

        /// Whose move it is, or the result once the game is over ("White wins", "Draw"). While the game is on and the
        /// computer opponent is searching (@p opponentThinking) it says "Computer is thinking" instead; a finished game never does
        [[nodiscard]] String Turn(const ChessGameModel& model, bool opponentThinking = false);

        /// The line under the board: while an earlier position is viewed (ChessGameModel::IsViewing()), which one and how to get back (Viewing()); else "" while the game goes
        /// on (but "Check" for a king in check), the reason once it is over ("Checkmate", "White resigned", "Draw agreed", ...)
        [[nodiscard]] String Status(const ChessGameModel& model);

        /// The line a refused draw offer puts on the status line (a person offered, the program said no); the game's own status is not meant by it
        [[nodiscard]] String DrawDeclined();

        /// The questions the menu section "Game" asks before it ends the game: a person resigning, and two people agreeing a draw
        [[nodiscard]] String ResignQuestion();
        [[nodiscard]] String DrawQuestion();

        /// "At move 12 - tap board" (the score-sheet number of the last shown move, the same for White's and Black's) or "At the start - tap board"; "" when nothing earlier is viewed.
        /// Short on purpose: a longer line, "Viewing move 12" with the whole hint, does not fit the Status of the narrowest window
        [[nodiscard]] String Viewing(const ChessGameModel& model);

        /// The HUD's Turn line while a position is edited: @p message (the reason a "Play from here" was refused) when there is one, else the reason the editor last refused an edit, else "Editing".
        /// Never longer than EditorText::kMaxRefusalChars
        [[nodiscard]] String Editing(const PositionEditor& editor, StringView message);

        /// The HUD's Turn line while a puzzle is on: "Your move" while the person is to move (also after a wrong move, which is taken back), a short word while the program's reply is due, "Solved"
        [[nodiscard]] String PuzzleTurn(PuzzlePhase phase);

        /// The HUD's status line over a puzzle: "Wrong move" while @p wrong (PuzzleSession::WasWrong()), else "". The game's own status ("Checkmate") stands when this is empty
        [[nodiscard]] String PuzzleStatus(bool wrong);

        /// The words of the menu section "Puzzles". @p index counts from 0, the word from 1 ("Puzzle 12 of 130")
        [[nodiscard]] String PuzzleNumber(size_t index, size_t count);
        [[nodiscard]] String PuzzleRating(int32_t rating);
        /// The state line: the Turn word (or "Wrong move" while @p wrong) and, after the first mistake, how many there were
        [[nodiscard]] String PuzzleProgress(PuzzlePhase phase, bool wrong, size_t mistakes);
        /// The one button of Start and Next: "Next" while a puzzle is on, else "Start"
        [[nodiscard]] String PuzzleNextWord(bool puzzleOn);
        /// The state line of a game that has no puzzle set
        [[nodiscard]] String NoPuzzles();

    }  // namespace StatusText

}  // namespace BECore::Chess
