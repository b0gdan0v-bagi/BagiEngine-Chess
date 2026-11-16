#pragma once

#include <Chess/Engine/Analysis.h>
#include <Chess/Engine/OpponentController.h>

namespace BECore::Chess {

    /// The program accepts a draw when its own score is at most this many centipawns in its favour: even or worse for it
    inline constexpr int kDrawAcceptCentipawns = 50;

    /// The depth the own engine judges a draw offer at: fixed, so the same position gets the same answer twice
    inline constexpr int kDrawOfferDepth = 5;

    /// Why a draw cannot be offered. GameOver: the game has ended. NotYourTurn: the program plays the side to move, and the offer is the person's, made on their turn. NoPerson: two programs
    CORE_ENUM(DrawOfferRefusal, uint8_t, GameOver, NotYourTurn, NoPerson)

    /// The rule of when a draw may be offered, for every caller (the control API, the menu): refused on a finished game, with two programs and on the turn of the one color the program plays
    /// (@p programPlays). Nothing: the offer may be made; two people agree among themselves, so it is never refused for them
    [[nodiscard]] Optional<DrawOfferRefusal> DrawOfferRefusalFor(const ChessGameModel& model, ProgramSides programPlays);

    /**
     * @brief Whether the program takes the draw, from its own score for the position the person offered in.
     *
     * @p scoreForSideToMove is as the engine gave it (the side to move's view, Analysis.h), @p sideToMove the side it was to move there and @p programColor the color the program plays. Centipawns
     * accept when they are at most kDrawAcceptCentipawns in the program's favour, exactly that included; a mate for the program refuses, a mate against it accepts.
     */
    [[nodiscard]] bool ProgramAcceptsDraw(AnalysisScore scoreForSideToMove, PieceColor sideToMove, PieceColor programColor) noexcept;

}  // namespace BECore::Chess
