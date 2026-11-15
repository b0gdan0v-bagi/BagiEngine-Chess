#include "VarietyRoll.h"

namespace BECore::Chess {

    namespace {

        constexpr uint64_t kPlyStep = 0x9E3779B97F4A7C15ULL;
        constexpr uint64_t kSeedSalt = 0xD1B54A32D192ED03ULL;

    }  // namespace

    uint64_t Mix(uint64_t value) noexcept {
        value ^= value >> 30;
        value *= 0xBF58476D1CE4E5B9ULL;
        value ^= value >> 27;
        value *= 0x94D049BB133111EBULL;
        value ^= value >> 31;
        return value;
    }

    uint64_t RollKey(const Position& position, size_t plies, uint32_t seed) noexcept {
        const uint64_t seedPart = seed == 0 ? 0 : Mix(static_cast<uint64_t>(seed) ^ kSeedSalt);
        return position.Key() ^ (static_cast<uint64_t>(plies) * kPlyStep) ^ seedPart;
    }

}  // namespace BECore::Chess
