#include "ChessTestUtil.h"

#include <Chess/Engine/OpponentText.h>

/// The words of the HUD for the computer opponent: pure, they read their arguments and make text
namespace BECore::Chess::Tests {

    namespace {

        constexpr std::optional<PieceColor> kNobody = std::nullopt;

        /// The sentence of an error of the given kind; the detail is empty, because the sentence does not depend on it
        eastl::string ErrorWords(EngineErrorKind kind) {
            return OpponentText::Error(EngineError{kind, {}});
        }

        /// The name of a kind for a failed expectation (gtest cannot stream an EASTL string view)
        std::string KindName(EngineErrorKind kind) {
            const eastl::string_view name = EnumUtils<EngineErrorKind>::ToString(kind);
            return std::string(name.data(), name.size());
        }

    }  // namespace

    TEST(OpponentTextTest, TheSideIsSaidFromThePersonsPointOfView) {
        EXPECT_EQ(OpponentText::SideLabel(kNobody), "Two players");
        EXPECT_EQ(OpponentText::SideLabel(PieceColor::Black), "You: White") << "the program plays Black, so the person has White";
        EXPECT_EQ(OpponentText::SideLabel(PieceColor::White), "You: Black") << "the program plays White, so the person has Black";
    }

    TEST(OpponentTextTest, TheLevelIsNamedAfterItsWord) {
        const auto levels = GetOpponentLevels();
        ASSERT_FALSE(levels.empty());

        for (const OpponentLevel& level : levels) {
            const eastl::string expected = Format("Level: {}", level.name);
            EXPECT_EQ(OpponentText::LevelLabel(level), expected);
        }
        EXPECT_EQ(OpponentText::LevelLabel(levels[kDefaultOpponentLevel]), "Level: Medium");
        EXPECT_EQ(OpponentText::LevelLabel(levels.front()), "Level: Easy");
        EXPECT_EQ(OpponentText::LevelLabel(levels.back()), "Level: Max");
    }

    TEST(OpponentTextTest, ALaunchFailureSaysTheEngineIsNotFound) {
        // The commonest failure and the one a person can fix, so its wording is pinned; the detail with the paths is for the log
        EXPECT_EQ(OpponentText::Error(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish found: put its path in config/ChessSettings.local.xml"}), "Engine not found");
    }

    TEST(OpponentTextTest, EveryKindOfErrorHasItsOwnShortSentence) {
        eastl::vector<eastl::string> sentences;
        for (const EngineErrorKind kind : EnumUtils<EngineErrorKind>::Values()) {
            const eastl::string sentence = ErrorWords(kind);
            const std::string name = KindName(kind);

            EXPECT_FALSE(sentence.empty()) << name;
            EXPECT_LE(sentence.size(), OpponentText::kMaxErrorLength) << name << ": '" << sentence.c_str() << "' does not fit beside the longest word of the game";
            for (const eastl::string& other : sentences) {
                EXPECT_NE(sentence, other) << name << ": two kinds of error with the same words";
            }
            sentences.push_back(sentence);
        }
        EXPECT_EQ(sentences.size(), EnumUtils<EngineErrorKind>::Count());
    }

    TEST(OpponentTextTest, TheSentenceDoesNotDependOnTheDetail) {
        for (const EngineErrorKind kind : EnumUtils<EngineErrorKind>::Values()) {
            const eastl::string plain = ErrorWords(kind);
            const eastl::string withDetail = OpponentText::Error(EngineError{kind, "/Users/someone/bin/stockfish: a very long sentence for the log that must not reach the window"});
            EXPECT_EQ(plain, withDetail);
        }
    }

    TEST(OpponentTextTest, TheLongestSideLabelIsOneOfTheLabelsAndNoneIsLonger) {
        const eastl::string longest = OpponentText::LongestSideLabel();
        const std::optional<PieceColor> sides[] = {kNobody, PieceColor::White, PieceColor::Black};

        bool isOne = false;
        for (const std::optional<PieceColor> side : sides) {
            const eastl::string label = OpponentText::SideLabel(side);
            EXPECT_LE(label.size(), longest.size()) << label.c_str();
            isOne = isOne || label == longest;
        }
        EXPECT_TRUE(isOne) << longest.c_str();
    }

    TEST(OpponentTextTest, TheLongestLevelLabelIsOneOfTheLabelsAndNoneIsLonger) {
        const eastl::string longest = OpponentText::LongestLevelLabel();

        bool isOne = false;
        for (const OpponentLevel& level : GetOpponentLevels()) {
            const eastl::string label = OpponentText::LevelLabel(level);
            EXPECT_LE(label.size(), longest.size()) << label.c_str();
            isOne = isOne || label == longest;
        }
        EXPECT_TRUE(isOne) << longest.c_str();
    }

    TEST(OpponentTextTest, TheLongestErrorIsOneOfTheSentencesAndNoneIsLonger) {
        const eastl::string longest = OpponentText::LongestError();

        bool isOne = false;
        for (const EngineErrorKind kind : EnumUtils<EngineErrorKind>::Values()) {
            const eastl::string sentence = ErrorWords(kind);
            EXPECT_LE(sentence.size(), longest.size()) << sentence.c_str();
            isOne = isOne || sentence == longest;
        }
        EXPECT_TRUE(isOne) << longest.c_str();
        EXPECT_LE(longest.size(), OpponentText::kMaxErrorLength);
    }

}  // namespace BECore::Chess::Tests
