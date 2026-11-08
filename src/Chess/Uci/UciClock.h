#pragma once

namespace BECore::Chess::Uci {

    /// The share of the remaining time spent on one move when the GUI does not say how many moves the time must last (`movestogo` absent): a game is guessed to
    /// have this many moves left
    inline constexpr int kDefaultMovesToGo = 30;

    /// The part of an increment that is added to the time of one move (numerator over denominator): the rest is kept for the next moves
    inline constexpr int kIncrementShareNumerator = 3;
    inline constexpr int kIncrementShareDenominator = 4;

    /// Milliseconds always left on the clock after a move for the pipe, the process and the GUI: a move that takes the whole clock loses on time
    inline constexpr int kClockMarginMs = 50;

    /// The least time of a move: a search asks for at least one millisecond
    inline constexpr int kMinMoveTimeMs = 1;

    /**
     * @brief How long to think on this move, from the clock of the side to move.
     *
     * The remaining time divided by the moves it must last (@p movesToGo, else kDefaultMovesToGo) plus most of the increment, but never more than what the clock
     * holds beyond kClockMarginMs, and never less than kMinMoveTimeMs (also for a clock that has run out). Integer arithmetic only, so the answer is the same on
     * every machine, and a longer clock never gives a shorter time.
     *
     * @param remainingMs time left on the clock; 0 or less means none
     * @param incrementMs added to the clock after each move
     * @param movesToGo moves until the next time control; 0 or less is read as absent
     */
    [[nodiscard]] int MoveTimeFor(int remainingMs, int incrementMs, std::optional<int> movesToGo);

}  // namespace BECore::Chess::Uci
