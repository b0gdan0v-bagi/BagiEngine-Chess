#include <Chess/Engine/OpponentLevels.h>
#include <Common/Printers.h>

/// The table of strengths of the computer opponent: pure data, nothing to start
namespace BECore::Chess::Tests {

    namespace {

        constexpr int kLowestSkillLevel = 0;
        constexpr int kHighestSkillLevel = 20;

        /// gtest cannot stream an EASTL string view
        std::string Text(eastl::string_view text) {
            return std::string(text.data(), text.size());
        }

        bool IsWireName(eastl::string_view id) {
            if (id.empty()) {
                return false;
            }
            for (const char c : id) {
                const bool lowerCase = c >= 'a' && c <= 'z';
                const bool digit = c >= '0' && c <= '9';
                if (!lowerCase && !digit && c != '_') {
                    return false;
                }
            }
            return true;
        }

    }  // namespace

    TEST(OpponentLevelsTest, ThereAreLevelsAndTheyHaveWords) {
        const auto levels = GetOpponentLevels();

        ASSERT_FALSE(levels.empty());
        for (const OpponentLevel& level : levels) {
            EXPECT_FALSE(level.name.empty()) << Text(level.id);
        }
    }

    TEST(OpponentLevelsTest, IdsAreLowerCaseAndUnique) {
        const auto levels = GetOpponentLevels();

        for (size_t i = 0; i < levels.size(); ++i) {
            EXPECT_TRUE(IsWireName(levels[i].id)) << "id '" << Text(levels[i].id) << "' is not lower case";
            for (size_t j = i + 1; j < levels.size(); ++j) {
                EXPECT_NE(levels[i].id, levels[j].id);
                EXPECT_NE(levels[i].name, levels[j].name);
            }
        }
    }

    TEST(OpponentLevelsTest, SkillLevelsAreInTheRangeOfTheEngine) {
        for (const OpponentLevel& level : GetOpponentLevels()) {
            EXPECT_GE(level.skillLevel, kLowestSkillLevel) << Text(level.id);
            EXPECT_LE(level.skillLevel, kHighestSkillLevel) << Text(level.id);
        }
    }

    TEST(OpponentLevelsTest, AStrongerLevelNeverPlaysWeakerOrFaster) {
        const auto levels = GetOpponentLevels();

        for (size_t i = 0; i + 1 < levels.size(); ++i) {
            EXPECT_LE(levels[i].skillLevel, levels[i + 1].skillLevel) << Text(levels[i + 1].id);
            EXPECT_LT(levels[i].moveTimeMs, levels[i + 1].moveTimeMs) << Text(levels[i + 1].id);
        }
    }

    TEST(OpponentLevelsTest, EveryLevelGetsTimeToThink) {
        for (const OpponentLevel& level : GetOpponentLevels()) {
            EXPECT_GT(level.moveTimeMs, 0) << Text(level.id);
        }
    }

    TEST(OpponentLevelsTest, TheDefaultLevelIsOneOfThem) {
        ASSERT_LT(kDefaultOpponentLevel, GetOpponentLevels().size());
        EXPECT_EQ(GetOpponentLevels()[kDefaultOpponentLevel].id, "medium");
    }

    TEST(OpponentLevelsTest, FindOpponentLevelFindsEveryIdAtItsIndex) {
        const auto levels = GetOpponentLevels();

        for (size_t index = 0; index < levels.size(); ++index) {
            const auto found = FindOpponentLevel(levels[index].id);
            ASSERT_TRUE(found.has_value()) << Text(levels[index].id);
            EXPECT_EQ(*found, index);
        }
        EXPECT_EQ(FindOpponentLevel("easy"), std::optional<size_t>(0));
        EXPECT_EQ(FindOpponentLevel("max"), std::optional<size_t>(levels.size() - 1));
    }

    TEST(OpponentLevelsTest, FindOpponentLevelMissesWhatIsNotAnId) {
        EXPECT_FALSE(FindOpponentLevel("").has_value());
        EXPECT_FALSE(FindOpponentLevel("expert").has_value());
        EXPECT_FALSE(FindOpponentLevel("Medium").has_value());  // the id is lower case, the word of the HUD is not an id
        EXPECT_FALSE(FindOpponentLevel("medium ").has_value());
        EXPECT_FALSE(FindOpponentLevel("med").has_value());
    }

    TEST(OpponentLevelsTest, LimitsAreTheMoveTimeAndNoDepth) {
        for (const OpponentLevel& level : GetOpponentLevels()) {
            const SearchLimits limits = LimitsOf(level);
            EXPECT_EQ(limits.moveTimeMs, level.moveTimeMs) << Text(level.id);
            EXPECT_EQ(limits.depth, 0) << Text(level.id);
        }
        const OpponentLevel custom{"custom", "Custom", 7, 1234};
        EXPECT_EQ(LimitsOf(custom).moveTimeMs, 1234);
    }

    TEST(OpponentLevelsTest, TheEngineIsToldTheSkillLevelAndNothingElse) {
        const OpponentLevel custom{"custom", "Custom", 7, 1234};

        const auto options = EngineOptionsOf(custom);

        ASSERT_EQ(options.size(), 1u);
        EXPECT_EQ(options[0].first, "Skill Level");
        EXPECT_EQ(options[0].second, "7");
    }

    TEST(OpponentLevelsTest, TheOptionsOfTheShippedLevelsAreTheirSkillLevels) {
        const auto levels = GetOpponentLevels();

        for (const OpponentLevel& level : levels) {
            const auto options = EngineOptionsOf(level);
            ASSERT_EQ(options.size(), 1u) << Text(level.id);
            EXPECT_EQ(options[0].first, "Skill Level");
            EXPECT_EQ(options[0].second, Format("{}", level.skillLevel));
        }
        const auto medium = EngineOptionsOf(levels[kDefaultOpponentLevel]);
        EXPECT_EQ(medium[0].second, "10");
    }

}  // namespace BECore::Chess::Tests
