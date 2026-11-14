#include "TestEngines.h"

#include <Chess/Engine/OpponentChoice.h>
#include <Chess/Game/ChessGameModel.h>
#include <Common/EngineEnvironment.h>

/// OpponentChoice over a real OpponentController and a CountingFactory (no process, no search is ever waited for): the person's view of the opponent (mode, color, engine, level
/// by id) against the program's view of it (the color it plays), the color that is remembered while nobody plays, and the ids that are refused
namespace BECore::Chess::Tests {

    namespace {

        constexpr eastl::string_view kHardId = "hard";
        constexpr eastl::string_view kMediumId = "medium";
        constexpr eastl::string_view kOwnId = "own";
        constexpr eastl::string_view kStockfishId = "stockfish";
        constexpr eastl::string_view kDefaultEngineId = kStockfishAvailable ? kStockfishId : kOwnId;  // the engine a game starts with in this build
        constexpr Optional<PieceColor> kNobody = std::nullopt;

        EngineError NoStockfish() {
            return EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"};
        }

        eastl::string Text(eastl::string_view text) {
            return eastl::string(text.data(), text.size());
        }

        /// A controller, its factory and the choice over them; the factory hands out an InstantEngine
        struct Seat {
            Seat() : factory({eastl::make_shared<InstantEngine>()}), controller(model), choice(controller) {
                controller.SetEngineFactory(factory.AsFactory());
            }

            ChessGameModel model;
            CountingFactory factory;
            OpponentController controller;
            OpponentChoice choice;
        };

        /// The spelling of a failed pick for a failed expectation
        std::string Describe(const std::expected<void, ChoiceError>& picked) {
            if (picked) {
                return "picked";
            }
            return std::string(EnumUtils<ChoiceErrorKind>::ToString(picked.error().kind).data()) + ": " + picked.error().detail.c_str();
        }

        /// The ids that no table has: empty, the wrong case, a prefix, a trailing or a leading space, a word of another table
        const eastl::vector<eastl::string>& BadIds() {
            static const eastl::vector<eastl::string> ids = {"",         "White",     "BLACK",     "whit", "black ",     " white", "Two",  "two players",
                                                             "Computer", "computer ", "Stockfish", "OWN",  "own engine", "med",    "Hard", "hard\n"};
            return ids;
        }

    }  // namespace

    class OpponentChoiceTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ::BECore::Tests::EngineEnvironment::EnsureCoreServices();  // TaskManager
        }
    };

    TEST_F(OpponentChoiceTest, ANewChoiceIsTwoPlayersWhiteTheDefaultEngineMediumAndAsksNoEngine) {
        Seat seat;

        EXPECT_EQ(seat.choice.Mode(), OpponentMode::Two);
        EXPECT_EQ(seat.choice.Color(), PieceColor::White);
        EXPECT_EQ(seat.choice.ModeId(), "two");
        EXPECT_EQ(seat.choice.ColorId(), "white");
        EXPECT_EQ(seat.choice.EngineId(), kDefaultEngineId);
        EXPECT_EQ(seat.choice.LevelId(), kMediumId);
        EXPECT_EQ(seat.factory.Calls(), 0);
        EXPECT_EQ(seat.controller.GetRevision(), 0u);
    }

    TEST_F(OpponentChoiceTest, ComputerMakesTheProgramPlayBlackAndThePersonHasWhite) {
        Seat seat;

        const auto picked = seat.choice.SetMode("computer");

        ASSERT_TRUE(picked.has_value()) << Describe(picked);
        EXPECT_EQ(seat.controller.GetPlays(), Optional<PieceColor>(PieceColor::Black));
        EXPECT_EQ(seat.choice.Mode(), OpponentMode::Computer);
        EXPECT_EQ(seat.choice.ModeId(), "computer");
        EXPECT_EQ(seat.choice.Color(), PieceColor::White);
        ASSERT_EQ(seat.factory.Calls(), 1);
        EXPECT_EQ(seat.factory.Engines()[0], Text(kDefaultEngineId));
        EXPECT_EQ(seat.factory.Levels()[0], Text(kMediumId));
    }

    TEST_F(OpponentChoiceTest, TheSameModeAgainKeepsTheEngineThatSearches) {
        Seat seat;
        ASSERT_TRUE(seat.choice.SetMode("computer").has_value());
        const uint64_t revision = seat.controller.GetRevision();

        EXPECT_TRUE(seat.choice.SetMode("computer").has_value());
        EXPECT_TRUE(seat.choice.SetColor("white").has_value());

        EXPECT_EQ(seat.factory.Calls(), 1);
        EXPECT_EQ(seat.controller.GetRevision(), revision);
    }

    TEST_F(OpponentChoiceTest, TheColorPickedWhileTwoPlayAsksNoEngineAndTheNextComputerPlaysTheOtherColor) {
        Seat seat;

        ASSERT_TRUE(seat.choice.SetColor("black").has_value());

        EXPECT_EQ(seat.choice.Color(), PieceColor::Black);
        EXPECT_EQ(seat.choice.Mode(), OpponentMode::Two);
        EXPECT_FALSE(seat.controller.GetPlays().has_value());
        EXPECT_EQ(seat.factory.Calls(), 0) << "nobody plays: nothing is looked up";
        EXPECT_EQ(seat.controller.GetRevision(), 0u) << "the controller is not told";

        ASSERT_TRUE(seat.choice.SetMode("computer").has_value());

        EXPECT_EQ(seat.controller.GetPlays(), Optional<PieceColor>(PieceColor::White)) << "the person has Black, the program plays White";
        EXPECT_EQ(seat.choice.Color(), PieceColor::Black);
        EXPECT_EQ(seat.factory.Calls(), 1);
    }

    TEST_F(OpponentChoiceTest, TheColorPickedWhileTheProgramPlaysRemakesTheOpponentOnce) {
        Seat seat;
        ASSERT_TRUE(seat.choice.SetMode("computer").has_value());
        const uint64_t revision = seat.controller.GetRevision();

        ASSERT_TRUE(seat.choice.SetColor("black").has_value());

        EXPECT_EQ(seat.controller.GetPlays(), Optional<PieceColor>(PieceColor::White));
        EXPECT_EQ(seat.choice.Color(), PieceColor::Black);
        EXPECT_EQ(seat.factory.Calls(), 2) << "one engine for the first pick, one for the new side";
        EXPECT_EQ(seat.controller.GetRevision(), revision + 1);

        ASSERT_TRUE(seat.choice.SetColor("white").has_value());

        EXPECT_EQ(seat.controller.GetPlays(), Optional<PieceColor>(PieceColor::Black));
        EXPECT_EQ(seat.factory.Calls(), 3);
    }

    TEST_F(OpponentChoiceTest, TwoPlayersKeepTheColorThePersonHad) {
        Seat seat;
        ASSERT_TRUE(seat.choice.SetColor("black").has_value());
        ASSERT_TRUE(seat.choice.SetMode("computer").has_value());

        ASSERT_TRUE(seat.choice.SetMode("two").has_value());

        EXPECT_FALSE(seat.controller.GetPlays().has_value());
        EXPECT_EQ(seat.choice.Mode(), OpponentMode::Two);
        EXPECT_EQ(seat.choice.Color(), PieceColor::Black) << "the color is not forgotten when nobody plays";
        EXPECT_EQ(seat.factory.Calls(), 1);

        ASSERT_TRUE(seat.choice.SetMode("computer").has_value());

        EXPECT_EQ(seat.controller.GetPlays(), Optional<PieceColor>(PieceColor::White)) << "and Computer again plays against that color";
    }

    TEST_F(OpponentChoiceTest, TheEngineAndTheLevelAreReadBackAndOnlyThePlayingProgramAsksAnEngine) {
        Seat seat;

        ASSERT_TRUE(seat.choice.SetEngine("own").has_value());
        ASSERT_TRUE(seat.choice.SetLevel("hard").has_value());

        EXPECT_EQ(seat.choice.EngineId(), kOwnId);
        EXPECT_EQ(seat.choice.LevelId(), kHardId);
        EXPECT_EQ(seat.controller.GetEngine(), OpponentEngine::Own);
        EXPECT_EQ(GetOpponentLevels()[seat.controller.GetLevel()].id, kHardId);
        EXPECT_EQ(seat.factory.Calls(), 0) << "nobody plays: the choice only changes";

        ASSERT_TRUE(seat.choice.SetMode("computer").has_value());

        ASSERT_EQ(seat.factory.Calls(), 1);
        EXPECT_EQ(seat.factory.Engines()[0], Text(kOwnId));
        EXPECT_EQ(seat.factory.Levels()[0], Text(kHardId));

        ASSERT_TRUE(seat.choice.SetLevel("easy").has_value());

        ASSERT_EQ(seat.factory.Calls(), 2) << "a level change remakes the opponent, once";
        EXPECT_EQ(seat.factory.Engines()[1], Text(kOwnId));
        EXPECT_EQ(seat.factory.Levels()[1], "easy");

        ASSERT_TRUE(seat.choice.SetEngine("stockfish").has_value());

        ASSERT_EQ(seat.factory.Calls(), 3);
        EXPECT_EQ(seat.factory.Engines()[2], Text(kStockfishId));
        EXPECT_EQ(seat.factory.Levels()[2], "easy");
    }

    TEST_F(OpponentChoiceTest, AnIdThatIsInNoTableIsAnErrorAndChangesNothingAsksNoEngineAndLeavesTheRevision) {
        for (const bool programPlays : {false, true}) {
            Seat seat;
            if (programPlays) {
                ASSERT_TRUE(seat.choice.SetMode("computer").has_value());
            }
            const int calls = seat.factory.Calls();
            const uint64_t revision = seat.controller.GetRevision();

            for (const eastl::string& id : BadIds()) {
                const eastl::string_view view(id.data(), id.size());
                const std::expected<void, ChoiceError> results[] = {seat.choice.SetMode(view), seat.choice.SetColor(view), seat.choice.SetEngine(view), seat.choice.SetLevel(view)};
                for (const auto& result : results) {
                    ASSERT_FALSE(result.has_value()) << "'" << id.c_str() << "' was taken";
                    EXPECT_EQ(result.error().kind, ChoiceErrorKind::BadId) << id.c_str();
                    EXPECT_FALSE(result.error().detail.empty()) << id.c_str();
                }
            }

            EXPECT_EQ(seat.factory.Calls(), calls) << "no engine is asked for a bad id";
            EXPECT_EQ(seat.controller.GetRevision(), revision);
            EXPECT_EQ(seat.choice.Mode(), programPlays ? OpponentMode::Computer : OpponentMode::Two);
            EXPECT_EQ(seat.choice.Color(), PieceColor::White);
            EXPECT_EQ(seat.choice.EngineId(), kDefaultEngineId);
            EXPECT_EQ(seat.choice.LevelId(), kMediumId);
        }
    }

    TEST_F(OpponentChoiceTest, ABadColorDoesNotChangeTheRememberedColor) {
        Seat seat;
        ASSERT_TRUE(seat.choice.SetColor("black").has_value());

        EXPECT_FALSE(seat.choice.SetColor("Black").has_value());
        EXPECT_FALSE(seat.choice.SetColor("").has_value());

        EXPECT_EQ(seat.choice.Color(), PieceColor::Black);
    }

    TEST_F(OpponentChoiceTest, ABadModeLeavesTheProgramWhereItWas) {
        Seat seat;
        ASSERT_TRUE(seat.choice.SetMode("computer").has_value());

        EXPECT_FALSE(seat.choice.SetMode("Two").has_value());

        EXPECT_EQ(seat.controller.GetPlays(), Optional<PieceColor>(PieceColor::Black));
    }

    TEST_F(OpponentChoiceTest, AFailedStockfishPickLeavesTwoPlayersAndTheErrorAndTheOwnEngineIsTheWayOn) {
        ChessGameModel model;
        OpponentController controller(model);
        OpponentChoice choice(controller);
        eastl::vector<eastl::string> asked;
        controller.SetEngineFactory([&asked](OpponentEngine engine, const OpponentLevel&) -> std::expected<eastl::shared_ptr<IChessEngine>, EngineError> {
            asked.push_back(Text(OpponentEngineId(engine)));
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(NoStockfish());
            }
            return eastl::make_shared<InstantEngine>();
        });
        ASSERT_TRUE(choice.SetEngine("stockfish").has_value());  // already the engine of a new game where Stockfish is available; nobody plays: no factory call

        const auto failed = choice.SetMode("computer");

        ASSERT_FALSE(failed.has_value());
        EXPECT_EQ(failed.error().kind, ChoiceErrorKind::Engine);
        EXPECT_EQ(failed.error().engine.kind, EngineErrorKind::LaunchFailed);
        EXPECT_FALSE(failed.error().detail.empty());
        EXPECT_EQ(choice.Mode(), OpponentMode::Two) << "the screen shows the truth: nobody plays";
        EXPECT_EQ(choice.Color(), PieceColor::White);
        ASSERT_TRUE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetError()->kind, EngineErrorKind::LaunchFailed);
        ASSERT_EQ(asked.size(), 1u);

        ASSERT_TRUE(choice.SetEngine("own").has_value());

        EXPECT_FALSE(controller.GetError().has_value()) << "a new pick clears the error";
        EXPECT_EQ(choice.Mode(), OpponentMode::Two) << "the engine alone does not start the program";
        EXPECT_EQ(asked.size(), 1u) << "nobody plays: the engine is only chosen";

        const auto picked = choice.SetMode("computer");

        ASSERT_TRUE(picked.has_value()) << Describe(picked);
        EXPECT_EQ(choice.Mode(), OpponentMode::Computer);
        ASSERT_EQ(asked.size(), 2u);
        EXPECT_EQ(asked[1], Text(kOwnId));
        EXPECT_EQ(controller.GetPlays(), Optional<PieceColor>(PieceColor::Black));
    }

#if CHESS_NO_STOCKFISH

    TEST_F(OpponentChoiceTest, WithoutStockfishThePickOfStockfishIsAnEngineErrorUnderTheDefaultFactoryAlsoWhileNobodyPlays) {
        ChessGameModel model;
        OpponentController controller(model);  // the default factory
        OpponentChoice choice(controller);
        ASSERT_EQ(choice.EngineId(), kOwnId);

        const auto refused = choice.SetEngine("stockfish");

        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().kind, ChoiceErrorKind::Engine);
        EXPECT_EQ(refused.error().engine.kind, EngineErrorKind::Unsupported);
        EXPECT_FALSE(refused.error().detail.empty());
        EXPECT_EQ(choice.EngineId(), kOwnId) << "nothing changed";
        EXPECT_EQ(choice.Mode(), OpponentMode::Two);
        EXPECT_TRUE(controller.GetError().has_value());

        EXPECT_TRUE(choice.SetEngine("own").has_value()) << "the own engine is a pick that changes nothing, and clears the error";
        EXPECT_FALSE(controller.GetError().has_value());
        EXPECT_TRUE(choice.SetMode("computer").has_value());
        EXPECT_EQ(controller.GetPlays(), Optional<PieceColor>(PieceColor::Black));
    }

#endif

    TEST_F(OpponentChoiceTest, AFailedColorPickWhileTheProgramPlaysIsRefusedAndChangesNothing) {
        ChessGameModel model;
        OpponentController controller(model);
        OpponentChoice choice(controller);
        CountingFactory ok({eastl::make_shared<InstantEngine>()});
        bool failNext = false;
        controller.SetEngineFactory([&](OpponentEngine engine, const OpponentLevel& level) -> std::expected<eastl::shared_ptr<IChessEngine>, EngineError> {
            if (failNext) {
                return std::unexpected(NoStockfish());
            }
            return ok.AsFactory()(engine, level);
        });
        ASSERT_TRUE(choice.SetMode("computer").has_value());
        failNext = true;

        const auto failed = choice.SetColor("black");

        ASSERT_FALSE(failed.has_value());
        EXPECT_EQ(failed.error().kind, ChoiceErrorKind::Engine);
        EXPECT_EQ(choice.Mode(), OpponentMode::Computer) << "the old opponent goes on";
        EXPECT_EQ(choice.Color(), PieceColor::White) << "the color of the refused pick is not remembered";
        EXPECT_EQ(controller.GetPlays(), Optional<PieceColor>(PieceColor::Black));
        EXPECT_TRUE(controller.GetError().has_value());
    }

    TEST_F(OpponentChoiceTest, TheColorFollowsTheControllerWhenSomeoneElseSetsTheSideAndIsKeptWhenNobodyPlaysAgain) {
        Seat seat;

        ASSERT_TRUE(seat.controller.SetSide(PieceColor::White).has_value());  // the control API's path: set_state {"plays": "white"}

        EXPECT_EQ(seat.choice.Mode(), OpponentMode::Computer);
        EXPECT_EQ(seat.choice.Color(), PieceColor::Black) << "the program plays White, the person has Black";
        EXPECT_EQ(seat.choice.ColorId(), "black");

        ASSERT_TRUE(seat.controller.SetSide(kNobody).has_value());  // plays: null

        EXPECT_EQ(seat.choice.Mode(), OpponentMode::Two);
        EXPECT_EQ(seat.choice.Color(), PieceColor::Black) << "the color that was seen is remembered";

        ASSERT_TRUE(seat.choice.SetMode("computer").has_value());

        EXPECT_EQ(seat.controller.GetPlays(), Optional<PieceColor>(PieceColor::White));
    }

    TEST_F(OpponentChoiceTest, LeavingComputerRemembersTheColorEvenIfNobodyReadItBefore) {
        Seat seat;
        ASSERT_TRUE(seat.controller.SetSide(PieceColor::White).has_value());

        ASSERT_TRUE(seat.choice.SetMode("two").has_value());

        EXPECT_EQ(seat.choice.Color(), PieceColor::Black) << "SetMode reads the color before the side goes";
    }

    TEST_F(OpponentChoiceTest, ResetForgetsTheRememberedColorAndLeavesTheControllerAlone) {
        Seat seat;
        ASSERT_TRUE(seat.choice.SetColor("black").has_value());
        ASSERT_EQ(seat.choice.Color(), PieceColor::Black);
        const uint64_t revision = seat.controller.GetRevision();

        seat.choice.Reset();

        EXPECT_EQ(seat.choice.Color(), PieceColor::White);
        EXPECT_EQ(seat.controller.GetRevision(), revision);
    }

    TEST_F(OpponentChoiceTest, TheIdsRoundTripWithTheTablesOfTheControlApi) {
        Seat seat;

        for (const OpponentEngine engine : EnumUtils<OpponentEngine>::Values()) {
            ASSERT_TRUE(seat.choice.SetEngine(OpponentEngineId(engine)).has_value());
            EXPECT_EQ(seat.controller.GetEngine(), engine);
            EXPECT_EQ(seat.choice.EngineId(), OpponentEngineId(engine));
        }
        for (size_t index = 0; index < GetOpponentLevels().size(); ++index) {
            const OpponentLevel& level = GetOpponentLevels()[index];
            ASSERT_TRUE(seat.choice.SetLevel(level.id).has_value()) << level.id.data();
            EXPECT_EQ(seat.controller.GetLevel(), index);
            EXPECT_EQ(seat.choice.LevelId(), level.id);
        }
        for (const OpponentMode mode : EnumUtils<OpponentMode>::Values()) {
            EXPECT_EQ(ParseMode(OpponentModeId(mode)), Optional<OpponentMode>(mode));
        }
        for (const PieceColor color : {PieceColor::White, PieceColor::Black}) {
            EXPECT_EQ(ParseColor(PieceColorId(color)), Optional<PieceColor>(color));
        }
    }

    TEST_F(OpponentChoiceTest, TheIdsOfModeAndColorArePinned) {
        EXPECT_EQ(OpponentModeId(OpponentMode::Two), "two");
        EXPECT_EQ(OpponentModeId(OpponentMode::Computer), "computer");
        EXPECT_EQ(PieceColorId(PieceColor::White), "white");
        EXPECT_EQ(PieceColorId(PieceColor::Black), "black");
        EXPECT_FALSE(ParseMode("Two").has_value());
        EXPECT_FALSE(ParseMode("").has_value());
        EXPECT_FALSE(ParseColor("White").has_value());
        EXPECT_FALSE(ParseColor("white ").has_value());
    }

}  // namespace BECore::Chess::Tests
