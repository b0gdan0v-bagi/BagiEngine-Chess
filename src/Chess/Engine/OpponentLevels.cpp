#include "OpponentLevels.h"

#include <Chess/Engine/BlunderEngine.h>

namespace BECore::Chess {

    namespace {

        // The strength of each level. First guesses, to be tuned by playing: `Skill Level` of the engine, the time for a move and the share of moves the handicap
        // replaces. Measured with Stockfish 19: Skill Level 0 is the floor of the engine (about a 1300-1400 player, a casual person is 600-1000) and `UCI_Elo` at its
        // minimum (1320) is the same strength, so neither goes lower; a limit of depth or nodes does not make Skill Level 0 weaker. A random legal move costs about five
        // pawns, so the share of them (blunderPercent, BlunderEngine) is the knob that reaches a beginner. The move time of the low levels only makes the program answer
        // sooner, the quality there is the skill and the handicap
        constexpr int kEasySkillLevel = 0;
        constexpr int kEasyMoveTimeMs = 50;
        constexpr int kEasyBlunderPercent = 40;
        constexpr int kMediumSkillLevel = 0;
        constexpr int kMediumMoveTimeMs = 100;
        constexpr int kMediumBlunderPercent = 15;
        constexpr int kHardSkillLevel = 5;
        constexpr int kHardMoveTimeMs = 300;
        constexpr int kHardBlunderPercent = 0;
        constexpr int kMaxSkillLevel = 20;
        constexpr int kMaxMoveTimeMs = 2000;
        constexpr int kMaxBlunderPercent = 0;
        // The depth of the own engine at each level, first guesses too (tuned by hand like the Stockfish ones). Measured in a Debug build on macOS, one search with no
        // clock: the deepest level (depth 5) takes 0.18 s from the start position and 0.63 s from Kiwipete, 0.44 s at worst of the other perft positions; depth 6 would take
        // 1.4 s, 2.5 s and 3.2 s, so 5 leaves room for a slower machine
        constexpr int kEasyOwnDepth = 1;
        constexpr int kMediumOwnDepth = 2;
        constexpr int kHardOwnDepth = 3;
        constexpr int kMaxOwnDepth = 5;

        constexpr eastl::array<OpponentLevel, 4> kLevels{
            OpponentLevel{"easy", "Easy", kEasySkillLevel, kEasyMoveTimeMs, kEasyOwnDepth, kEasyBlunderPercent},
            OpponentLevel{"medium", "Medium", kMediumSkillLevel, kMediumMoveTimeMs, kMediumOwnDepth, kMediumBlunderPercent},
            OpponentLevel{"hard", "Hard", kHardSkillLevel, kHardMoveTimeMs, kHardOwnDepth, kHardBlunderPercent},
            OpponentLevel{"max", "Max", kMaxSkillLevel, kMaxMoveTimeMs, kMaxOwnDepth, kMaxBlunderPercent},
        };
        static_assert(!kLevels.empty());
        static_assert(kDefaultOpponentLevel < kLevels.size());

        constexpr eastl::string_view kSkillLevelOption = "Skill Level";

    }  // namespace

    eastl::span<const OpponentLevel> GetOpponentLevels() {
        return eastl::span<const OpponentLevel>(kLevels.data(), kLevels.size());
    }

    Optional<size_t> FindOpponentLevel(eastl::string_view id) {
        for (size_t index = 0; index < kLevels.size(); ++index) {
            if (kLevels[index].id == id) {
                return index;
            }
        }
        return std::nullopt;
    }

    eastl::shared_ptr<IChessEngine> WithHandicap(eastl::shared_ptr<IChessEngine> engine, const OpponentLevel& level) {
        if (level.blunderPercent <= 0) {
            return engine;
        }
        return eastl::make_shared<BlunderEngine>(std::move(engine), level.blunderPercent);
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
