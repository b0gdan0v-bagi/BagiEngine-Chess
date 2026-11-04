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
        // The depth of the own engine at each level, first guesses too (tuned by hand like the Stockfish ones). Measured in a Debug build on macOS, one search with no
        // clock: the deepest level (depth 5) takes 0.18 s from the start position and 0.63 s from Kiwipete, 0.44 s at worst of the other perft positions; depth 6 would take
        // 1.4 s, 2.5 s and 3.2 s, so 5 leaves room for a slower machine
        constexpr int kEasyOwnDepth = 1;
        constexpr int kMediumOwnDepth = 3;
        constexpr int kHardOwnDepth = 4;
        constexpr int kMaxOwnDepth = 5;

        constexpr eastl::array<OpponentLevel, 4> kLevels{
            OpponentLevel{"easy", "Easy", kEasySkillLevel, kEasyMoveTimeMs, kEasyOwnDepth},
            OpponentLevel{"medium", "Medium", kMediumSkillLevel, kMediumMoveTimeMs, kMediumOwnDepth},
            OpponentLevel{"hard", "Hard", kHardSkillLevel, kHardMoveTimeMs, kHardOwnDepth},
            OpponentLevel{"max", "Max", kMaxSkillLevel, kMaxMoveTimeMs, kMaxOwnDepth},
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

    SearchLimits LimitsOf(OpponentEngine engine, const OpponentLevel& level) {
        switch (engine) {
            case OpponentEngine::Stockfish:
                return SearchLimits{level.moveTimeMs, 0};
            case OpponentEngine::Own:
                return SearchLimits{kOwnMoveTimeCapMs, level.ownDepth};
        }
        return SearchLimits{};
    }

    eastl::vector<eastl::pair<eastl::string, eastl::string>> EngineOptionsOf(const OpponentLevel& level) {
        eastl::vector<eastl::pair<eastl::string, eastl::string>> options;
        options.emplace_back(eastl::string(kSkillLevelOption.data(), kSkillLevelOption.size()), Format("{}", level.skillLevel));
        return options;
    }

}  // namespace BECore::Chess
