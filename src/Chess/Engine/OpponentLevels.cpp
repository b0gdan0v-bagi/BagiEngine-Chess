#include "OpponentLevels.h"

namespace BECore::Chess {

    namespace {

        // The strength of each level. First guesses, to be tuned by hand against a real Stockfish: `Skill Level` of the engine and the time for a move
        constexpr int kEasySkillLevel = 3;
        constexpr int kEasyMoveTimeMs = 100;
        constexpr int kMediumSkillLevel = 10;
        constexpr int kMediumMoveTimeMs = 300;
        constexpr int kHardSkillLevel = 16;
        constexpr int kHardMoveTimeMs = 800;
        constexpr int kMaxSkillLevel = 20;
        constexpr int kMaxMoveTimeMs = 2000;

        constexpr eastl::array<OpponentLevel, 4> kLevels{
            OpponentLevel{"easy", "Easy", kEasySkillLevel, kEasyMoveTimeMs},
            OpponentLevel{"medium", "Medium", kMediumSkillLevel, kMediumMoveTimeMs},
            OpponentLevel{"hard", "Hard", kHardSkillLevel, kHardMoveTimeMs},
            OpponentLevel{"max", "Max", kMaxSkillLevel, kMaxMoveTimeMs},
        };
        static_assert(!kLevels.empty());
        static_assert(kDefaultOpponentLevel < kLevels.size());

        constexpr eastl::string_view kSkillLevelOption = "Skill Level";

    }  // namespace

    eastl::span<const OpponentLevel> GetOpponentLevels() {
        return eastl::span<const OpponentLevel>(kLevels.data(), kLevels.size());
    }

    std::optional<size_t> FindOpponentLevel(eastl::string_view id) {
        for (size_t index = 0; index < kLevels.size(); ++index) {
            if (kLevels[index].id == id) {
                return index;
            }
        }
        return std::nullopt;
    }

    SearchLimits LimitsOf(const OpponentLevel& level) {
        return SearchLimits{level.moveTimeMs, 0};
    }

    eastl::vector<eastl::pair<eastl::string, eastl::string>> EngineOptionsOf(const OpponentLevel& level) {
        eastl::vector<eastl::pair<eastl::string, eastl::string>> options;
        options.emplace_back(eastl::string(kSkillLevelOption.data(), kSkillLevelOption.size()), Format("{}", level.skillLevel));
        return options;
    }

}  // namespace BECore::Chess
