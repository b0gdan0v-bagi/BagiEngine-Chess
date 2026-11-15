#pragma once

#include <Chess/Game/Position.h>

namespace BECore::Chess {

    /// The finalizer of splitmix64: plain uint64_t arithmetic, the same on every platform
    [[nodiscard]] uint64_t Mix(uint64_t value) noexcept;

    /**
     * @brief The one number that the random choices of a move are made from: a pure function of the position, the number of moves of the request and the seed.
     *
     * Seed 0 is "no variety" and gives `position.Key() ^ plies * kPlyStep`, the number the handicap always used, so the choices of a request without a seed stay what they were.
     * Another seed shifts every choice of the request in one go (the same position and seed always roll the same). No clock, no random device, no state.
     */
    [[nodiscard]] uint64_t RollKey(const Position& position, size_t plies, uint32_t seed) noexcept;

}  // namespace BECore::Chess
