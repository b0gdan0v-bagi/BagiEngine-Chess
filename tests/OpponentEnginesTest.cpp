#include <Chess/Engine/OpponentEngines.h>
#include <Common/Printers.h>

/// The choice of engine of the computer opponent: pure data, nothing to start
namespace BECore::Chess::Tests {

    namespace {

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

    TEST(OpponentEnginesTest, IdsAreLowerCaseAndUnique) {
        const auto engines = EnumUtils<OpponentEngine>::Values();

        for (size_t i = 0; i < engines.size(); ++i) {
            EXPECT_TRUE(IsWireName(OpponentEngineId(engines[i]))) << "id '" << Text(OpponentEngineId(engines[i])) << "' is not lower case";
            for (size_t j = i + 1; j < engines.size(); ++j) {
                EXPECT_NE(OpponentEngineId(engines[i]), OpponentEngineId(engines[j]));
            }
        }
        EXPECT_EQ(OpponentEngineId(OpponentEngine::Stockfish), "stockfish");
        EXPECT_EQ(OpponentEngineId(OpponentEngine::Own), "own");
    }

    TEST(OpponentEnginesTest, NamesAreWordsAndUnique) {
        const auto engines = EnumUtils<OpponentEngine>::Values();

        for (size_t i = 0; i < engines.size(); ++i) {
            EXPECT_FALSE(OpponentEngineName(engines[i]).empty());
            for (size_t j = i + 1; j < engines.size(); ++j) {
                EXPECT_NE(OpponentEngineName(engines[i]), OpponentEngineName(engines[j]));
            }
        }
        EXPECT_EQ(OpponentEngineName(OpponentEngine::Stockfish), "Stockfish");
        EXPECT_EQ(OpponentEngineName(OpponentEngine::Own), "Own engine");
    }

    TEST(OpponentEnginesTest, FindOpponentEngineFindsEveryIdAtItsValue) {
        for (const OpponentEngine engine : EnumUtils<OpponentEngine>::Values()) {
            const auto found = FindOpponentEngine(OpponentEngineId(engine));
            ASSERT_TRUE(found.has_value()) << Text(OpponentEngineId(engine));
            EXPECT_EQ(*found, engine);
        }
        EXPECT_EQ(FindOpponentEngine("stockfish"), std::optional<OpponentEngine>(OpponentEngine::Stockfish));
        EXPECT_EQ(FindOpponentEngine("own"), std::optional<OpponentEngine>(OpponentEngine::Own));
    }

    TEST(OpponentEnginesTest, FindOpponentEngineMissesWhatIsNotAnId) {
        EXPECT_FALSE(FindOpponentEngine("").has_value());
        EXPECT_FALSE(FindOpponentEngine("Stockfish").has_value());  // the word of the HUD is not an id
        EXPECT_FALSE(FindOpponentEngine("OWN").has_value());
        EXPECT_FALSE(FindOpponentEngine("Own engine").has_value());
        EXPECT_FALSE(FindOpponentEngine("own ").has_value());
        EXPECT_FALSE(FindOpponentEngine("ow").has_value());
        EXPECT_FALSE(FindOpponentEngine("stock").has_value());
    }

    TEST(OpponentEnginesTest, TheCycleWalksStockfishOwnAndBackWhereStockfishIsAvailable) {
        if constexpr (!kStockfishAvailable) {
            GTEST_SKIP() << "no Stockfish in this build (CHESS_NO_STOCKFISH)";
        } else {
            EXPECT_EQ(NextOpponentEngine(OpponentEngine::Stockfish), OpponentEngine::Own);
            EXPECT_EQ(NextOpponentEngine(OpponentEngine::Own), OpponentEngine::Stockfish);
        }
    }

    TEST(OpponentEnginesTest, TheCycleSkipsAnEngineThatIsNotAvailable) {
        if constexpr (kStockfishAvailable) {
            GTEST_SKIP() << "Stockfish is available in this build";
        } else {
            EXPECT_EQ(NextOpponentEngine(OpponentEngine::Stockfish), OpponentEngine::Own);
            EXPECT_EQ(NextOpponentEngine(OpponentEngine::Own), OpponentEngine::Own) << "the cycle stays on the one engine there is";
        }
    }

    TEST(OpponentEnginesTest, TheCycleAlwaysEndsOnAnAvailableEngine) {
        for (const OpponentEngine engine : EnumUtils<OpponentEngine>::Values()) {
            EXPECT_TRUE(IsOpponentEngineAvailable(NextOpponentEngine(engine))) << Text(OpponentEngineId(engine));
        }
    }

    TEST(OpponentEnginesTest, TheOwnEngineIsAlwaysAvailableAndStockfishOnlyWithoutTheSwitch) {
        EXPECT_TRUE(IsOpponentEngineAvailable(OpponentEngine::Own));
        EXPECT_EQ(IsOpponentEngineAvailable(OpponentEngine::Stockfish), kStockfishAvailable);
#if CHESS_NO_STOCKFISH
        EXPECT_FALSE(kStockfishAvailable);
#else
        EXPECT_TRUE(kStockfishAvailable);
#endif
    }

    TEST(OpponentEnginesTest, AGameStartsWithStockfishAndWithTheOwnEngineWhereThereIsNone) {
        EXPECT_EQ(kDefaultOpponentEngine, kStockfishAvailable ? OpponentEngine::Stockfish : OpponentEngine::Own);
        EXPECT_TRUE(IsOpponentEngineAvailable(kDefaultOpponentEngine)) << "a default that cannot start would be an error at the first pick";
    }

}  // namespace BECore::Chess::Tests
