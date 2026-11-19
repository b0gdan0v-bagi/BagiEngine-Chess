#pragma once

#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/Puzzle.h>

namespace BECore::Chess {

    /// Where a puzzle that is on stands; read from the model, never stored
    CORE_ENUM(PuzzlePhase, uint8_t, YourMove, Replying, Solved)

    /// How much game time passes between the person's right move being seen and the program's reply being played
    inline constexpr float kPuzzleReplySeconds = 0.5f;

    /**
     * @brief The judge of a puzzle over a ChessGameModel: it holds the set, the puzzle that is picked and the mistakes, and keeps no copy of the game.
     *
     * A puzzle is **on** exactly while the model's start FEN is the puzzle's and its moves are a prefix of the line, plus at most one move of the person at the end
     * (IsActiveFor()). That is judged from the model at every read, so a Reset(), a SetPosition(), a takeback under the setup or a move the line does not have at the
     * program's turn ends the puzzle at once, with no frame between. The verdicts and the program's reply are made by Update(), once per frame.
     *
     * The person's move is **right** when it is the line's move, or when it gives checkmate (the lichess rule: any mate solves). Any other move is wrong: Update()
     * takes it back and counts a mistake. The session never plays a move for the person.
     */
    class PuzzleSession {
    public:
        /// Replaces the set and forgets the puzzle that was picked
        void SetPuzzles(PuzzleSet puzzles);

        [[nodiscard]] const PuzzleSet& GetPuzzles() const noexcept {
            return _puzzles;
        }

        /**
         * @brief Starts puzzle @p index in @p model: SetPosition() of its FEN and the setup move, mistakes 0.
         *
         * @return false (nothing changes) for an index outside the set
         */
        bool Begin(ChessGameModel& model, size_t index);

        /// Begins the same puzzle again; false when no puzzle is on
        bool Retry(ChessGameModel& model);

        /// Begins the puzzle after the one that is on (the first after the last); the first one when none is on. False for an empty set
        bool Next(ChessGameModel& model);

        /// Forgets the puzzle. The game is not touched: the caller starts a new one
        void End();

        /**
         * @brief Leaves the puzzle: End(), and a plain Reset() of @p model when the puzzle was on (the new game the caller promises).
         *
         * @return whether a puzzle was on
         */
        bool Leave(ChessGameModel& model);

        /// Whether a puzzle is on in @p model: pure, no side effect, true also for a puzzle that is solved
        [[nodiscard]] bool IsActiveFor(const ChessGameModel& model) const;

        /**
         * @brief The frame of the judge: judges the person's move not yet seen, then plays the program's reply once kPuzzleReplySeconds of @p deltaSeconds have passed.
         *
         * The wait counts from the frame **after** the one that saw the person's move, and a frame of 0 never completes it. A person's move that was wrong is taken
         * back with ChessGameModel::TakeBack(), so the model's version moves.
         *
         * @return whether the session's own state changed (a verdict, a mistake, the reply)
         */
        bool Update(ChessGameModel& model, float deltaSeconds);

        /// Meaningful while IsActiveFor(); a game that is over by the person's mate or by the end of the line is Solved
        [[nodiscard]] PuzzlePhase GetPhase(const ChessGameModel& model) const;

        [[nodiscard]] size_t GetMistakes() const noexcept {
            return _mistakes;
        }
        /// The last move of the person was wrong and taken back; clears with the person's next move and with a retry
        [[nodiscard]] bool WasWrong() const noexcept {
            return _wrong;
        }

        /// The person's right moves on the board; meaningful while IsActiveFor()
        [[nodiscard]] size_t GetStep(const ChessGameModel& model) const;
        /// The person's moves of the line; 0 with no puzzle
        [[nodiscard]] size_t GetSteps() const noexcept;

        /// The index of the picked puzzle in the set; the pick outlives the puzzle's being on (a reset), so ask IsActiveFor() first
        [[nodiscard]] Optional<size_t> GetIndex() const noexcept {
            return _index;
        }
        [[nodiscard]] const Puzzle* GetCurrent() const;

        /// Grows with every change of the session's own state (a begin, a verdict, a mistake, the reply, a retry, an end) and never goes back
        [[nodiscard]] uint64_t GetRevision() const noexcept {
            return _revision;
        }

    private:
        /// The person's last move on the board is the line's or a mate; needs an even, non-zero number of moves
        [[nodiscard]] bool LastPersonMoveIsRight(const ChessGameModel& model) const;

        PuzzleSet _puzzles;
        Optional<size_t> _index;
        /// The picked puzzle's start FEN as the model writes it (Position::ToFen()), which the model's start FEN is compared with
        String _startFen;
        size_t _mistakes = 0;
        bool _wrong = false;
        /// How many moves of the model the last Update() has seen, so a move is judged once
        size_t _seenMoves = 0;
        float _replyWait = 0.0f;
        uint64_t _revision = 0;
    };

}  // namespace BECore::Chess
