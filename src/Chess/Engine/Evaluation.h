#pragma once

#include <Chess/Game/Position.h>

namespace BECore::Chess {

    // Values of the pieces in centipawns (a pawn is 100). Evaluate() counts the pieces of each side with them.
    inline constexpr int kPawnValue = 100;
    inline constexpr int kKnightValue = 320;
    inline constexpr int kBishopValue = 330;
    inline constexpr int kRookValue = 500;
    inline constexpr int kQueenValue = 900;
    /// A king is never counted by Evaluate() (each side has exactly one); the value is large so that a search which orders
    /// captures by "most valuable victim, least valuable attacker" sends the king in last
    inline constexpr int kKingValue = 20000;

    /// The material of both sides without pawns and kings, at or below which the king plays the endgame: it stops hiding in
    /// the corner and goes to the center. Two queens and two rooks, so a queen and a rook each (or less) is an endgame
    inline constexpr int kEndgameMaterialLimit = 2 * (kQueenValue + kRookValue);

    /// The score of a mate on the move: the larger the score, the better. A side that is mated scores -MateIn(ply)
    inline constexpr int kMateScore = 30000;
    inline constexpr int kDrawScore = 0;
    /// No mate in a search is further from the root than this many plies, so the scores from kMateScore - kMaxMatePly up
    /// are mates and no evaluation of a position gets near them
    inline constexpr int kMaxMatePly = 256;

    /// The score of a mate that falls @p ply plies from the root of the search (0 = the root position is mated or mates).
    /// A mate that comes sooner is worth more, so a search that takes the best score takes the shortest mate
    [[nodiscard]] constexpr int MateIn(int ply) noexcept {
        return kMateScore - ply;
    }

    /// Whether @p score is a mate for one side or the other (a result of MateIn() or its negation), not an evaluation
    [[nodiscard]] constexpr bool IsMateScore(int score) noexcept {
        return score >= MateIn(kMaxMatePly) || score <= -MateIn(kMaxMatePly);
    }

    /// The value of a piece kind from the list above; 0 for no piece
    [[nodiscard]] int PieceValue(PieceType type) noexcept;

    /// Whether the king table of the endgame is used for @p position: the pieces of both sides without pawns and kings are
    /// worth kEndgameMaterialLimit or less together
    [[nodiscard]] bool IsEndgame(const Position& position) noexcept;

    /**
     * @brief The static evaluation of a position in centipawns, from the point of view of the side to move.
     *
     * Material plus a piece-square table per piece kind (the pawn, knight, bishop, rook, queen and the king in the
     * middlegame or in the endgame, see IsEndgame()), the tables of the simplified evaluation function of the chess
     * programming wiki. White reads a table by the square (a1 = 0, like Bitboard), Black by the square mirrored over the
     * horizontal middle, so the evaluation of a position equals the one of its color-mirrored position. A positive score is
     * good for the side to move. The start position is 0.
     *
     * Pure and deterministic: no search, no allocation, no state. It knows nothing about mates, draws or whose move it is
     * beyond the sign; the search handles those with kMateScore and kDrawScore.
     */
    [[nodiscard]] int Evaluate(const Position& position) noexcept;

}  // namespace BECore::Chess
