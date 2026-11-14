#include "ChessTestUtil.h"

#include <Chess/Engine/OpponentEngines.h>
#include <Chess/Engine/OpponentLevels.h>
#include <Chess/Engine/OpponentText.h>

/// The words of the HUD for the computer opponent: pure, they read their arguments and make text
namespace BECore::Chess::Tests {

    namespace {

        /// The sentence of an error of the given kind; the detail is empty, because the sentence does not depend on it
        String ErrorWords(EngineErrorKind kind) {
            return OpponentText::Error(EngineError{kind, {}});
        }

        /// The name of a kind for a failed expectation (gtest cannot stream an EASTL string view)
        String KindName(EngineErrorKind kind) {
            const StringView name = EnumUtils<EngineErrorKind>::ToString(kind);
            return String(name.data(), name.size());
        }

    }  // namespace

    TEST(OpponentTextTest, TheModeAndTheColorOfThePanelAreSaidInTheirPinnedWords) {
        EXPECT_EQ(OpponentText::ModeLabel(false), "Two players");
        EXPECT_EQ(OpponentText::ModeLabel(true), "Computer");
        EXPECT_EQ(OpponentText::ColorLabel(PieceColor::White), "White");
        EXPECT_EQ(OpponentText::ColorLabel(PieceColor::Black), "Black");
    }

    TEST(OpponentTextTest, NoTwoSegmentsOfAGroupOfThePanelSayTheSameWord) {
        const auto distinct = [](const Vector<String>& words) {
            for (size_t a = 0; a < words.size(); ++a) {
                EXPECT_FALSE(words[a].empty());
                for (size_t b = a + 1; b < words.size(); ++b) {
                    EXPECT_NE(words[a], words[b]);
                }
            }
        };

        distinct({OpponentText::ModeLabel(false), OpponentText::ModeLabel(true)});
        distinct({OpponentText::ColorLabel(PieceColor::White), OpponentText::ColorLabel(PieceColor::Black)});
        Vector<String> engines;
        for (const OpponentEngine engine : EnumUtils<OpponentEngine>::Values()) {
            const StringView name = OpponentEngineName(engine);
            engines.emplace_back(name.data(), name.size());
        }
        distinct(engines);
        Vector<String> levels;
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
        Vector<String> sentences;
        for (const EngineErrorKind kind : EnumUtils<EngineErrorKind>::Values()) {
            const String sentence = ErrorWords(kind);
            const String name = KindName(kind);

            EXPECT_FALSE(sentence.empty()) << name;
            EXPECT_LE(sentence.size(), OpponentText::kMaxErrorLength) << name << ": '" << sentence.c_str() << "' is longer than the Status label is laid out for";
            EXPECT_NE(sentence, String(OpponentText::kUnknownErrorSentence.data(), OpponentText::kUnknownErrorSentence.size()))
                << name << " has no sentence of its own: it reached the fallback of Error()";
            for (const String& other : sentences) {
                EXPECT_NE(sentence, other) << name << ": two kinds of error with the same words";
            }
            sentences.push_back(sentence);
        }
        EXPECT_EQ(sentences.size(), EnumUtils<EngineErrorKind>::Count());
    }

    TEST(OpponentTextTest, TheSentenceDoesNotDependOnTheDetail) {
        for (const EngineErrorKind kind : EnumUtils<EngineErrorKind>::Values()) {
            const String plain = ErrorWords(kind);
            const String withDetail = OpponentText::Error(EngineError{kind, "/Users/someone/bin/stockfish: a very long sentence for the log that must not reach the window"});
            EXPECT_EQ(plain, withDetail);
        }
    }

}  // namespace BECore::Chess::Tests
