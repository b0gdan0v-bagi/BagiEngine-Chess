#include "OpponentLevels.h"

#include <Chess/Engine/BlunderEngine.h>

namespace BECore::Chess {

    namespace {

        // First guesses. Skill Level 0 is the floor of Stockfish, so below it only blunderPercent weakens a level; the move time of the low levels only makes the program
        // answer sooner
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
        // The depth of the own engine at each level; the deepest stops at 5 so that a slower machine still answers in a short time
        constexpr int kEasyOwnDepth = 1;
        constexpr int kMediumOwnDepth = 2;
        constexpr int kHardOwnDepth = 3;
        constexpr int kMaxOwnDepth = 5;

        constexpr Array<OpponentLevel, 4> kLevels{
            OpponentLevel{"easy", "Easy", kEasySkillLevel, kEasyMoveTimeMs, kEasyOwnDepth, kEasyBlunderPercent},
            OpponentLevel{"medium", "Medium", kMediumSkillLevel, kMediumMoveTimeMs, kMediumOwnDepth, kMediumBlunderPercent},
            OpponentLevel{"hard", "Hard", kHardSkillLevel, kHardMoveTimeMs, kHardOwnDepth, kHardBlunderPercent},
            OpponentLevel{"max", "Max", kMaxSkillLevel, kMaxMoveTimeMs, kMaxOwnDepth, kMaxBlunderPercent},
        };
        static_assert(!kLevels.empty());
        static_assert(kDefaultOpponentLevel < kLevels.size());

        constexpr StringView kSkillLevelOption = "Skill Level";

    }  // namespace

    Span<const OpponentLevel> GetOpponentLevels() {
        return Span<const OpponentLevel>(kLevels.data(), kLevels.size());
    }

    Optional<size_t> FindOpponentLevel(StringView id) {
        for (size_t index = 0; index < kLevels.size(); ++index) {
            if (kLevels[index].id == id) {
                return index;
            }
        }
        return std::nullopt;
    }

    SharedPtr<IChessEngine> WithHandicap(SharedPtr<IChessEngine> engine, const OpponentLevel& level) {
        if (level.blunderPercent <= 0) {
            return engine;
        }
        return MakeShared<BlunderEngine>(std::move(engine), level.blunderPercent);
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

    Vector<Pair<String, String>> EngineOptionsOf(const OpponentLevel& level) {
        Vector<Pair<String, String>> options;
        options.emplace_back(String(kSkillLevelOption.data(), kSkillLevelOption.size()), Format("{}", level.skillLevel));
        return options;
    }

}  // namespace BECore::Chess
