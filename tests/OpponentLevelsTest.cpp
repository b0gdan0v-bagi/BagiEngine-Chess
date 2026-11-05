#include "TestEngines.h"

#include <Chess/Engine/OpponentLevels.h>
#include <Chess/Engine/Search.h>
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

    TEST(OpponentLevelsTest, TheShippedLadderIsPinned) {
        // The numbers as the owner reads them: first guesses, tuned by playing. A change here is a decision, so it is a change of this test too
        struct Row {
            const char* id;
            int skillLevel;
            int moveTimeMs;
            int blunderPercent;
            int ownDepth;
        };
        const Row expected[] = {
            {"easy", 0, 50, 40, 1},
            {"medium", 0, 100, 15, 2},
            {"hard", 5, 300, 0, 3},
            {"max", 20, 2000, 0, 5},
        };
        const auto levels = GetOpponentLevels();

        ASSERT_EQ(levels.size(), std::size(expected));
        for (size_t i = 0; i < levels.size(); ++i) {
            EXPECT_EQ(Text(levels[i].id), expected[i].id);
            EXPECT_EQ(levels[i].skillLevel, expected[i].skillLevel) << expected[i].id;
            EXPECT_EQ(levels[i].moveTimeMs, expected[i].moveTimeMs) << expected[i].id;
            EXPECT_EQ(levels[i].blunderPercent, expected[i].blunderPercent) << expected[i].id;
            EXPECT_EQ(levels[i].ownDepth, expected[i].ownDepth) << expected[i].id;
        }
    }

    TEST(OpponentLevelsTest, BlunderPercentsAreInTheRangeAndOnlyTheLowLevelsHaveOne) {
        const auto levels = GetOpponentLevels();

        for (const OpponentLevel& level : levels) {
            EXPECT_GE(level.blunderPercent, 0) << Text(level.id);
            EXPECT_LE(level.blunderPercent, 100) << Text(level.id);
        }
        EXPECT_GT(levels[*FindOpponentLevel("easy")].blunderPercent, 0);
        EXPECT_GT(levels[*FindOpponentLevel("medium")].blunderPercent, 0);
        EXPECT_EQ(levels[*FindOpponentLevel("hard")].blunderPercent, 0);
        EXPECT_EQ(levels[*FindOpponentLevel("max")].blunderPercent, 0);
    }

    TEST(OpponentLevelsTest, NoKnobMakesAStrongerLevelWeaker) {
        const auto levels = GetOpponentLevels();

        for (size_t i = 0; i + 1 < levels.size(); ++i) {
            const std::string at = Text(levels[i + 1].id);
            EXPECT_LE(levels[i].skillLevel, levels[i + 1].skillLevel) << at;
            EXPECT_LT(levels[i].moveTimeMs, levels[i + 1].moveTimeMs) << at;
            EXPECT_GE(levels[i].blunderPercent, levels[i + 1].blunderPercent) << at;
            EXPECT_LE(levels[i].ownDepth, levels[i + 1].ownDepth) << at;
        }
    }

    TEST(OpponentLevelsTest, MaxIsTheFullStrengthOfTheEngine) {
        const OpponentLevel& max = GetOpponentLevels().back();

        EXPECT_EQ(Text(max.id), "max");
        EXPECT_EQ(max.skillLevel, kHighestSkillLevel);
        EXPECT_EQ(max.blunderPercent, 0);
    }

    TEST(OpponentLevelsTest, WithHandicapKeepsTheEngineWhenThereIsNoBlunderPercent) {
        const auto engine = eastl::make_shared<InstantEngine>();
        const OpponentLevel plain{"plain", "Plain", 5, 100, 2, 0};

        EXPECT_EQ(WithHandicap(engine, plain), engine);
        EXPECT_EQ(WithHandicap(engine, OpponentLevel{"short", "Short", 5, 100, 2}), engine) << "the default of the field is none";
        for (const OpponentLevel& level : GetOpponentLevels()) {
            if (level.blunderPercent == 0) {
                EXPECT_EQ(WithHandicap(engine, level), engine) << Text(level.id);
            }
        }
    }

    TEST(OpponentLevelsTest, WithHandicapWrapsTheEngineInABlunderEngineOfTheLevelsPercent) {
        const auto engine = eastl::make_shared<InstantEngine>();
        const OpponentLevel custom{"custom", "Custom", 5, 100, 2, 50};

        const eastl::shared_ptr<IChessEngine> wrapped = WithHandicap(engine, custom);

        ASSERT_NE(wrapped, nullptr);
        EXPECT_NE(wrapped, eastl::shared_ptr<IChessEngine>(engine));
        const auto blunder = eastl::dynamic_pointer_cast<BlunderEngine>(wrapped);
        ASSERT_NE(blunder, nullptr);
        EXPECT_EQ(blunder->Percent(), 50);
        for (const OpponentLevel& level : GetOpponentLevels()) {
            if (level.blunderPercent > 0) {
                const auto shipped = eastl::dynamic_pointer_cast<BlunderEngine>(WithHandicap(engine, level));
                ASSERT_NE(shipped, nullptr) << Text(level.id);
                EXPECT_EQ(shipped->Percent(), level.blunderPercent) << Text(level.id);
            }
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

    TEST(OpponentLevelsTest, TheOwnDepthIsPositiveAndNeverFallsWithStrength) {
        const auto levels = GetOpponentLevels();

        for (size_t i = 0; i < levels.size(); ++i) {
            EXPECT_GT(levels[i].ownDepth, 0) << Text(levels[i].id);
            EXPECT_LE(levels[i].ownDepth, kMaxSearchDepth) << Text(levels[i].id);
            if (i + 1 < levels.size()) {
                EXPECT_LE(levels[i].ownDepth, levels[i + 1].ownDepth) << Text(levels[i + 1].id);
            }
        }
        EXPECT_LT(levels.front().ownDepth, levels.back().ownDepth);
    }

    TEST(OpponentLevelsTest, StockfishGetsTheMoveTimeAndNoDepth) {
        for (const OpponentLevel& level : GetOpponentLevels()) {
            const SearchLimits limits = LimitsOf(OpponentEngine::Stockfish, level);
            EXPECT_EQ(limits.moveTimeMs, level.moveTimeMs) << Text(level.id);
            EXPECT_EQ(limits.depth, 0) << Text(level.id);
        }
        const OpponentLevel custom{"custom", "Custom", 7, 1234};
        EXPECT_EQ(LimitsOf(OpponentEngine::Stockfish, custom).moveTimeMs, 1234);
    }

    TEST(OpponentLevelsTest, TheOwnEngineGetsTheDepthAndTheCap) {
        for (const OpponentLevel& level : GetOpponentLevels()) {
            const SearchLimits limits = LimitsOf(OpponentEngine::Own, level);
            EXPECT_EQ(limits.depth, level.ownDepth) << Text(level.id);
            EXPECT_EQ(limits.moveTimeMs, kOwnMoveTimeCapMs) << Text(level.id);
            EXPECT_GT(limits.depth, 0) << Text(level.id);
            EXPECT_GT(limits.moveTimeMs, 0) << Text(level.id);
        }
    }

    TEST(OpponentLevelsTest, ALevelWithoutAnOwnDepthGivesTheOwnEngineNone) {
        const OpponentLevel custom{"custom", "Custom", 7, 1234};

        const SearchLimits limits = LimitsOf(OpponentEngine::Own, custom);

        EXPECT_EQ(custom.ownDepth, 0);
        EXPECT_EQ(limits.depth, 0);  // the cap alone is still a valid limit, but the level did not say how deep: the table must give every level a depth
        EXPECT_EQ(limits.moveTimeMs, kOwnMoveTimeCapMs);
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
        EXPECT_EQ(medium[0].second, "0") << "Skill Level 0 is the floor of Stockfish; the handicap does the rest of the way down";
    }

}  // namespace BECore::Chess::Tests
