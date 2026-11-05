#pragma once


namespace BECore::Chess::Tests {

    /// A small generator with a fixed seed (xorshift64): the same numbers on every platform, unlike std::uniform_int_distribution
    class SeededRandom {
    public:
        explicit SeededRandom(uint64_t seed) : _state(seed) {}

        size_t Below(size_t bound) {
            _state ^= _state << 13;
            _state ^= _state >> 7;
            _state ^= _state << 17;
            return static_cast<size_t>(_state % bound);
        }

    private:
        uint64_t _state;
    };

}  // namespace BECore::Chess::Tests
