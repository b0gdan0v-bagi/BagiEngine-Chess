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

    TEST(OpponentTextTest, TheModeAndTheColorOfThePanelAreSaidInTheirPinnedWords) {
        EXPECT_EQ(OpponentText::ModeLabel(false), "Two players");
        EXPECT_EQ(OpponentText::ModeLabel(true), "Computer");
        EXPECT_EQ(OpponentText::ColorLabel(PieceColor::White), "White");
        EXPECT_EQ(OpponentText::ColorLabel(PieceColor::Black), "Black");
        EXPECT_EQ(OpponentText::ModeLabel(false), OpponentText::SideLabel(kNobody)) << "the same words in the Opponent button and in its panel";
    }

    TEST(OpponentTextTest, NoTwoSegmentsOfAGroupOfThePanelSayTheSameWord) {
        const auto distinct = [](const eastl::vector<eastl::string>& words) {
            for (size_t a = 0; a < words.size(); ++a) {
                EXPECT_FALSE(words[a].empty());
                for (size_t b = a + 1; b < words.size(); ++b) {
                    EXPECT_NE(words[a], words[b]);
                }
            }
        };

        distinct({OpponentText::ModeLabel(false), OpponentText::ModeLabel(true)});
        distinct({OpponentText::ColorLabel(PieceColor::White), OpponentText::ColorLabel(PieceColor::Black)});
        eastl::vector<eastl::string> engines;
        for (const OpponentEngine engine : EnumUtils<OpponentEngine>::Values()) {
            const eastl::string_view name = OpponentEngineName(engine);
            engines.emplace_back(name.data(), name.size());
        }
        distinct(engines);
        eastl::vector<eastl::string> levels;
        for (const OpponentLevel& level : GetOpponentLevels()) {
            levels.emplace_back(level.name.data(), level.name.size());
        }
        distinct(levels);
    }

    TEST(OpponentTextTest, ALaunchFailureSaysTheEngineIsNotFound) {
        // The commonest failure and the one a person can fix, so its wording is pinned; the detail with the paths is for the log
        EXPECT_EQ(OpponentText::Error(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish found: put its path in config/ChessSettings.local.xml"}), "Engine not found");
    }

    TEST(OpponentTextTest, AnIllegalMoveIsSaidInTheWidthOfTheLongestStatus) {
        // The Status label is laid out for "Draw: insufficient material"; the sentence was longer once and did not fit the width of that label
        EXPECT_EQ(OpponentText::Error(EngineError{EngineErrorKind::IllegalMove, "e2e5"}), "Illegal engine move");
    }

    TEST(OpponentTextTest, EveryKindOfErrorHasItsOwnShortSentence) {
        eastl::vector<eastl::string> sentences;
        for (const EngineErrorKind kind : EnumUtils<EngineErrorKind>::Values()) {
            const eastl::string sentence = ErrorWords(kind);
            const std::string name = KindName(kind);

            EXPECT_FALSE(sentence.empty()) << name;
            EXPECT_LE(sentence.size(), OpponentText::kMaxErrorLength) << name << ": '" << sentence.c_str() << "' is longer than the Status label is laid out for";
            EXPECT_NE(sentence, eastl::string(OpponentText::kUnknownErrorSentence.data(), OpponentText::kUnknownErrorSentence.size()))
                << name << " has no sentence of its own: it reached the fallback of Error()";
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

}  // namespace BECore::Chess::Tests
