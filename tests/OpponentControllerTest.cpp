#include "ChessTestUtil.h"
#include "FakeEngineUtil.h"
#include "TestEngines.h"

#include <Chess/Engine/OpponentController.h>
#include <Chess/Engine/OwnEngine.h>
#include <Chess/Engine/UciEngine.h>
#include <Chess/Game/ChessGameModel.h>
#include <Common/EngineEnvironment.h>
#include <chrono>

/// OpponentController with the real TaskManager and engines made in the test (no process): the choice of side and level, the error rule, the click-free
/// life of the opponent. The default factory is tried against a missing file and against the fake UCI engine (a child process) behind a wrapper script.
/// Every wait polls a condition with a time limit; none is a fixed sleep
namespace BECore::Chess::Tests {

    namespace {

        constexpr int kManyUpdates = 1000;
        /// Well under the 5 s a UciEngine gives a stubborn child by default (UciEngineSettings::kDefaultQuitGraceMs); only the yardstick uses it
        constexpr std::chrono::milliseconds kShutdownBudget{3000};
        constexpr const char* kLogName = "fake.log";
#if defined(_WIN32)
        constexpr const char* kWrapperName = "stockfish-wrapper.cmd";  // CreateProcess runs a .cmd through cmd.exe
#else
        constexpr const char* kWrapperName = "stockfish-wrapper.sh";
#endif
        constexpr StringView kMissingPath = "/no/such/file";
        constexpr StringView kTildePath = "~/bin/stockfish";
        constexpr std::filesystem::perms kScriptPermissions = std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
                                                              std::filesystem::perms::others_read | std::filesystem::perms::others_exec;  // 755
        constexpr StringView kHardLevelId = "hard";

        bool Play(ChessGameModel& model, StringView uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        /// What a game loop does: Update every frame until @p done
        template <typename Condition>
        bool UpdateUntil(OpponentController& controller, Condition done) {
            return WaitUntil([&] {
                controller.Update();
                return done();
            });
        }

        EngineError LaunchFailed() {
            return EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"};
        }

        size_t LevelIndex(StringView id) {
            const Optional<size_t> index = FindOpponentLevel(id);
            EXPECT_TRUE(index.has_value()) << id.data();
            return index.value_or(kDefaultOpponentLevel);
        }

        bool Contains(const eastl::string& text, StringView part) {
            return text.find(part.data(), 0, part.size()) != eastl::string::npos;
        }

        /// The own engine that remembers the limits it was asked with (RecordingEngine); what it answers is the real OwnEngine's
        class RecordingOwnEngine final : public RecordingEngine {
        protected:
            std::expected<Move, EngineError> Answer(const SearchRequest& request, const CancellationToken& token) override {
                return OwnEngine().FindMove(request, token);
            }
        };

        /// The reply of the own engine through the default factory (no factory set, no settings, no process) to e2e4 at @p level, or an empty string when none came
        eastl::string OwnReplyToE2e4(size_t level) {
            ChessGameModel model;
            OpponentController controller(model);
            EXPECT_TRUE(controller.Choose(PieceColor::Black, level, OpponentEngine::Own).has_value());
            EXPECT_TRUE(Play(model, "e2e4"));
            const bool answered = UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; });
            EXPECT_TRUE(answered) << DescribeError(controller.GetError()).c_str();
            return answered ? model.GetMoves()[1].ToUci() : eastl::string();
        }

    }  // namespace

    class OpponentControllerTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ::BECore::Tests::EngineEnvironment::EnsureCoreServices();  // TaskManager
        }
    };

    TEST_F(OpponentControllerTest, ANewGameHasNobodyAsTheOpponentAndStartsNothing) {
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(Play(model, "e7e5"));
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());

        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }

        EXPECT_FALSE(controller.GetPlays().has_value());
        EXPECT_FALSE(controller.PlaysFor(PieceColor::White));
        EXPECT_FALSE(controller.PlaysFor(PieceColor::Black));
        EXPECT_FALSE(controller.IsThinking());
        EXPECT_FALSE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetRevision(), 0u);
        EXPECT_EQ(controller.GetLevel(), kDefaultOpponentLevel);
        EXPECT_EQ(factory.Calls(), 0) << "nothing is looked up before a side is picked";
        EXPECT_EQ(model.GetMoves().size(), 2u);
    }

    TEST_F(OpponentControllerTest, PickingBlackMakesTheProgramAnswerAPersonsMove) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto engine = eastl::make_shared<GatedEngine>(true);
        CountingFactory factory({engine});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());

        const auto picked = controller.SetSide(PieceColor::Black);

        ASSERT_TRUE(picked.has_value()) << DescribeError(picked ? std::nullopt : Optional<EngineError>(picked.error())).c_str();
        EXPECT_EQ(controller.GetPlays(), Optional<PieceColor>(PieceColor::Black));
        EXPECT_TRUE(controller.PlaysFor(PieceColor::Black));
        EXPECT_FALSE(controller.PlaysFor(PieceColor::White));
        EXPECT_EQ(controller.GetRevision(), 1u);
        EXPECT_FALSE(controller.GetError().has_value());
        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Levels()[0], GetOpponentLevels()[kDefaultOpponentLevel].id);
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
            ASSERT_FALSE(controller.IsThinking()) << "white is to move: the person's turn";
        }
        EXPECT_EQ(engine->Calls(), 0);

        ASSERT_TRUE(Play(model, "e2e4"));
        controller.Update();
        EXPECT_TRUE(controller.IsThinking());
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; })) << "the engine has the search and waits at its gate";
        EXPECT_EQ(engine->LastLimits().moveTimeMs, LimitsOf(controller.GetEngine(), GetOpponentLevels()[kDefaultOpponentLevel]).moveTimeMs) << "the limits are the level's for the engine";
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }
        EXPECT_TRUE(controller.IsThinking());
        EXPECT_EQ(model.GetMoves().size(), 1u) << "the answer is not there yet";

        engine->Open();
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();

        EXPECT_EQ(model.GetTurn(), PieceColor::White);
        EXPECT_FALSE(controller.IsThinking());
        EXPECT_FALSE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetRevision(), 1u) << "a search that starts and ends is not a change of the choice";
    }

    TEST_F(OpponentControllerTest, PickingWhiteMakesTheProgramOpen) {
        ChessGameModel model;
        const auto engine = eastl::make_shared<InstantEngine>();
        CountingFactory factory({engine});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());

        ASSERT_TRUE(controller.SetSide(PieceColor::White).has_value());
        EXPECT_TRUE(controller.PlaysFor(PieceColor::White));
        EXPECT_FALSE(controller.PlaysFor(PieceColor::Black));
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 1; })) << DescribeError(controller.GetError()).c_str();

        EXPECT_EQ(model.GetTurn(), PieceColor::Black) << "the program played white's move; black is the person's";
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
            ASSERT_FALSE(controller.IsThinking());
        }
        EXPECT_EQ(engine->Calls(), 1);
    }

    TEST_F(OpponentControllerTest, ASideCycleWalksNobodyBlackWhiteNobody) {
        ChessGameModel model;
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        const Optional<PieceColor> expected[] = {PieceColor::Black, PieceColor::White, std::nullopt, PieceColor::Black};

        uint64_t revision = controller.GetRevision();
        for (const Optional<PieceColor>& plays : expected) {
            ASSERT_TRUE(controller.CycleSide().has_value());
            EXPECT_EQ(controller.GetPlays(), plays);
            EXPECT_EQ(controller.GetRevision(), revision + 1) << "every step is a change";
            revision = controller.GetRevision();
        }

        EXPECT_EQ(factory.Calls(), 3) << "nobody needs no engine";
    }

    TEST_F(OpponentControllerTest, ALevelCycleWalksTheTableAndWraps) {
        ChessGameModel model;
        OpponentController controller(model);
        const size_t count = GetOpponentLevels().size();
        ASSERT_GE(count, 2u);

        for (size_t step = 1; step <= count + 1; ++step) {
            const uint64_t revision = controller.GetRevision();
            ASSERT_TRUE(controller.CycleLevel().has_value());
            EXPECT_EQ(controller.GetLevel(), (kDefaultOpponentLevel + step) % count);
            EXPECT_EQ(controller.GetRevision(), revision + 1);
        }

        EXPECT_FALSE(controller.GetPlays().has_value()) << "the level is kept while nobody plays";
    }

    TEST_F(OpponentControllerTest, ALevelThatDoesNotExistChangesNothing) {
        ChessGameModel model;
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        const uint64_t revision = controller.GetRevision();

        const auto refused = controller.SetLevel(GetOpponentLevels().size());

        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().kind, EngineErrorKind::BadRequest);
        EXPECT_EQ(controller.GetLevel(), kDefaultOpponentLevel);
        EXPECT_EQ(controller.GetRevision(), revision);
        EXPECT_EQ(factory.Calls(), 1);
        EXPECT_TRUE(controller.PlaysFor(PieceColor::Black));
        EXPECT_FALSE(controller.GetError().has_value());
    }

    TEST_F(OpponentControllerTest, ThePickOfTheSameSideAndLevelKeepsTheSearch) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto engine = eastl::make_shared<GatedEngine>(true);
        CountingFactory factory({engine});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(Play(model, "e2e4"));
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));
        const uint64_t revision = controller.GetRevision();

        EXPECT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        EXPECT_TRUE(controller.SetLevel(kDefaultOpponentLevel).has_value());

        EXPECT_TRUE(controller.IsThinking()) << "nothing changed: the engine is not asked to start over";
        EXPECT_EQ(controller.GetRevision(), revision);
        EXPECT_EQ(factory.Calls(), 1);
        EXPECT_FALSE(engine->WasCancelled());
        engine->Open();
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();
    }

    TEST_F(OpponentControllerTest, ANoEngineLeavesTwoPlayersAndReportsLaunchFailed) {
        ChessGameModel model;
        CountingFactory factory(LaunchFailed());
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());

        const auto picked = controller.SetSide(PieceColor::Black);

        ASSERT_FALSE(picked.has_value());
        EXPECT_EQ(picked.error().kind, EngineErrorKind::LaunchFailed);
        EXPECT_FALSE(controller.GetPlays().has_value());
        EXPECT_FALSE(controller.PlaysFor(PieceColor::Black));
        EXPECT_FALSE(controller.IsThinking());
        ASSERT_TRUE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetError()->kind, EngineErrorKind::LaunchFailed);
        EXPECT_EQ(controller.GetError()->detail, LaunchFailed().detail);
        EXPECT_EQ(controller.GetRevision(), 1u);

        // The error stands while the game stands still, and no frame asks the factory again
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }
        EXPECT_TRUE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetRevision(), 1u);
        EXPECT_EQ(factory.Calls(), 1);

        // Both colors can still be moved by people, and the move is what the person has seen the error for
        EXPECT_TRUE(Play(model, "e2e4"));
        controller.Update();
        EXPECT_FALSE(controller.GetError().has_value()) << "the game moved on";
        EXPECT_EQ(controller.GetRevision(), 2u);
        EXPECT_TRUE(Play(model, "e7e5"));
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }
        EXPECT_EQ(controller.GetRevision(), 2u);
        EXPECT_EQ(model.GetMoves().size(), 2u);
        EXPECT_EQ(factory.Calls(), 1);
    }

    TEST_F(OpponentControllerTest, AFactoryThatMakesNoEngineIsLaunchFailedToo) {
        ChessGameModel model;
        OpponentController controller(model);
        controller.SetEngineFactory([](OpponentEngine, const OpponentLevel&) -> MadeEngine { return MadeEngine(eastl::shared_ptr<IChessEngine>{}); });

        const auto picked = controller.SetSide(PieceColor::Black);

        ASSERT_FALSE(picked.has_value());
        EXPECT_EQ(picked.error().kind, EngineErrorKind::LaunchFailed);
        EXPECT_FALSE(controller.GetPlays().has_value());
        ASSERT_TRUE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetError()->kind, EngineErrorKind::LaunchFailed);
        EXPECT_TRUE(Play(model, "e2e4"));
        controller.Update();
        EXPECT_FALSE(controller.IsThinking());
    }

    TEST_F(OpponentControllerTest, ANewPickClearsTheErrorAndCanSucceedAfterTheFailure) {
        ChessGameModel model;
        CountingFactory failing(LaunchFailed());
        CountingFactory working({eastl::make_shared<InstantEngine>()});
        OpponentController controller(model);
        controller.SetEngineFactory(failing.AsFactory());
        ASSERT_FALSE(controller.SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(controller.GetError().has_value());
        const uint64_t revision = controller.GetRevision();

        controller.SetEngineFactory(working.AsFactory());  // the person installed Stockfish and pressed the button again
        ASSERT_TRUE(controller.CycleSide().has_value()) << "nobody to Black: the same pick again";

        EXPECT_FALSE(controller.GetError().has_value());
        EXPECT_TRUE(controller.PlaysFor(PieceColor::Black));
        EXPECT_GT(controller.GetRevision(), revision);
        EXPECT_EQ(working.Calls(), 1);

        // A failed pick leaves its error behind, and picking nobody is the way out of it
        controller.SetEngineFactory(failing.AsFactory());
        ASSERT_FALSE(controller.SetSide(PieceColor::White).has_value());
        ASSERT_TRUE(controller.GetError().has_value());
        EXPECT_TRUE(controller.SetSide(std::nullopt).has_value());
        EXPECT_FALSE(controller.GetError().has_value()) << "two players is a way out of a broken engine";
        EXPECT_FALSE(controller.GetPlays().has_value());
    }

    TEST_F(OpponentControllerTest, ARefusedStockfishPickKeepsTheRunningOpponentAndItsSearch) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        // The own engine is there (gated, honors its token); Stockfish is not
        const auto own = eastl::make_shared<GatedEngine>(true, Mv("e7e5"));
        eastl::vector<OpponentEngine> asked;
        OpponentController controller(model);
        controller.SetEngineFactory([&](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            asked.push_back(engine);
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(LaunchFailed());
            }
            return own;
        });
        const size_t easy = LevelIndex("easy");
        ASSERT_TRUE(controller.Choose(PieceColor::Black, easy, OpponentEngine::Own).has_value());
        ASSERT_TRUE(Play(model, "e2e4"));
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return own->Calls() == 1; })) << "the search is at its gate";
        ASSERT_TRUE(controller.IsThinking());
        const uint64_t revision = controller.GetRevision();

        const auto refused = controller.SetEngine(OpponentEngine::Stockfish);

        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().kind, EngineErrorKind::LaunchFailed);
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own) << "an engine that cannot be had cannot be saved";
        EXPECT_EQ(controller.GetLevel(), easy);
        EXPECT_EQ(controller.GetPlays(), Optional<PieceColor>(PieceColor::Black));
        EXPECT_TRUE(controller.PlaysFor(PieceColor::Black));
        EXPECT_TRUE(controller.IsThinking()) << "the search of the old opponent runs on";
        EXPECT_FALSE(own->WasCancelled());
        ASSERT_TRUE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetError()->kind, EngineErrorKind::LaunchFailed);
        EXPECT_EQ(controller.GetRevision(), revision + 1) << "the error that appeared is shown once";
        ASSERT_EQ(asked.size(), 2u);

        // The error does not stop the old opponent: it answers, and only a move of someone else takes the error away
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }
        EXPECT_EQ(asked.size(), 2u) << "no frame asks the factory again";
        EXPECT_EQ(controller.GetRevision(), revision + 1);
        EXPECT_EQ(model.GetMoves().size(), 1u);
        own->Open();
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();
        EXPECT_EQ(model.GetMoves()[1].ToUci(), "e7e5") << "the program that played before goes on playing";
        controller.Update();
        EXPECT_TRUE(controller.GetError().has_value()) << "the program's own move does not take the sentence away";
        EXPECT_EQ(controller.GetRevision(), revision + 1);
        ASSERT_TRUE(Play(model, "g1f3"));
        controller.Update();
        EXPECT_FALSE(controller.GetError().has_value()) << "a move of the person does";
    }

    TEST_F(OpponentControllerTest, ARefusedChooseChangesNothingAtAll) {
        ChessGameModel model;
        const auto own = eastl::make_shared<InstantEngine>();
        eastl::vector<OpponentEngine> asked;
        OpponentController controller(model);
        controller.SetEngineFactory([&](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            asked.push_back(engine);
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(LaunchFailed());
            }
            return own;
        });
        const size_t easy = LevelIndex("easy");
        ASSERT_TRUE(controller.Choose(PieceColor::Black, easy, OpponentEngine::Own).has_value());

        const auto refused = controller.Choose(PieceColor::White, LevelIndex(kHardLevelId), OpponentEngine::Stockfish);

        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(controller.GetPlays(), Optional<PieceColor>(PieceColor::Black)) << "not the side";
        EXPECT_EQ(controller.GetLevel(), easy) << "not the level";
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own) << "not the engine";
        EXPECT_TRUE(controller.GetError().has_value());

        // The same refusal again asks again (it is the person's retry) and still changes nothing
        EXPECT_FALSE(controller.Choose(PieceColor::White, LevelIndex(kHardLevelId), OpponentEngine::Stockfish).has_value());
        EXPECT_EQ(asked.size(), 3u);
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own);
    }

    TEST_F(OpponentControllerTest, AfterARefusedPickThePickOfWhatPlaysClearsTheErrorWithoutRemakingTheOpponent) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto own = eastl::make_shared<GatedEngine>(true, Mv("e7e5"));
        CountingFactory factory({own});
        OpponentController controller(model);
        controller.SetEngineFactory([&](OpponentEngine engine, const OpponentLevel& level) -> MadeEngine {
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(LaunchFailed());
            }
            return factory.AsFactory()(engine, level);
        });
        ASSERT_TRUE(controller.Choose(PieceColor::Black, LevelIndex("easy"), OpponentEngine::Own).has_value());
        ASSERT_TRUE(Play(model, "e2e4"));
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return own->Calls() == 1; }));
        ASSERT_FALSE(controller.SetEngine(OpponentEngine::Stockfish).has_value());
        ASSERT_TRUE(controller.GetError().has_value());
        const uint64_t revision = controller.GetRevision();

        ASSERT_TRUE(controller.SetEngine(OpponentEngine::Own).has_value()) << "the person goes back to the engine that plays";

        EXPECT_FALSE(controller.GetError().has_value());
        EXPECT_GT(controller.GetRevision(), revision);
        EXPECT_EQ(factory.Calls(), 1) << "nothing was remade";
        EXPECT_TRUE(controller.IsThinking());
        EXPECT_FALSE(own->WasCancelled());
        own->Open();
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();
    }

    TEST_F(OpponentControllerTest, ALevelPickedWhileNobodyPlaysIsKeptEvenWithoutStockfishAndAppliesAtTheNextSide) {
        ChessGameModel model;
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        const size_t max = LevelIndex("max");

        ASSERT_TRUE(controller.SetLevel(max).has_value());

        EXPECT_EQ(factory.Calls(), 0) << "nobody plays: nothing to ask for, so nothing to refuse";
        EXPECT_EQ(controller.GetLevel(), max);
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Levels()[0], "max");
    }

    TEST_F(OpponentControllerTest, AChangeOfLevelDropsTheRunningSearchAndItsStaleAnswer) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        // The first engine ignores its token and holds the answer h7h5 until the end of the test; the second answers at once
        const auto slow = eastl::make_shared<GatedEngine>(false, Mv("h7h5"));
        const auto quick = eastl::make_shared<InstantEngine>();
        CountingFactory factory({slow, quick});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(Play(model, "e2e4"));
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return slow->Calls() == 1; })) << "the engine has the search and waits at its gate";
        ASSERT_TRUE(controller.IsThinking());
        const uint64_t revision = controller.GetRevision();
        const size_t hard = LevelIndex(kHardLevelId);

        ASSERT_TRUE(controller.SetLevel(hard).has_value());

        EXPECT_FALSE(controller.IsThinking()) << "cancelled at once, though the worker is still inside the engine";
        EXPECT_EQ(controller.GetLevel(), hard);
        EXPECT_GT(controller.GetRevision(), revision);
        EXPECT_TRUE(controller.PlaysFor(PieceColor::Black));
        ASSERT_EQ(factory.Calls(), 2);
        EXPECT_EQ(factory.Levels()[1], kHardLevelId) << "the new opponent was made for the new level";
        EXPECT_EQ(model.GetMoves().size(), 1u);

        // The next frame asks the new engine, with the limits of the new level
        controller.Update();
        EXPECT_TRUE(controller.IsThinking());
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();
        EXPECT_EQ(quick->LastLimits().moveTimeMs, LimitsOf(controller.GetEngine(), GetOpponentLevels()[hard]).moveTimeMs);
        slow->Open();  // and now the old search answers h7h5, into a game that has moved on
        ASSERT_TRUE(WaitUntil([&] { return slow->Answered() == 1; }));
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }

        EXPECT_NE(model.GetMoves()[1].ToUci(), "h7h5") << "the answer of the old search was not played";
        EXPECT_EQ(model.GetMoves().size(), 2u);
        EXPECT_FALSE(controller.GetError().has_value());
    }

    TEST_F(OpponentControllerTest, ChangingFromWhiteToBlackRemakesTheOpponentAndCancelsTheOldSearch) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto asWhite = eastl::make_shared<GatedEngine>(true);
        const auto asBlack = eastl::make_shared<InstantEngine>();
        CountingFactory factory({asWhite, asBlack});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(controller.SetSide(PieceColor::White).has_value());
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return asWhite->Calls() == 1; })) << "the program, as white, is thinking about the first move";
        ASSERT_TRUE(controller.IsThinking());

        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());

        EXPECT_FALSE(controller.IsThinking());
        EXPECT_TRUE(controller.PlaysFor(PieceColor::Black));
        EXPECT_FALSE(controller.PlaysFor(PieceColor::White));
        EXPECT_EQ(factory.Calls(), 2);
        EXPECT_TRUE(WaitUntil([&] { return asWhite->WasCancelled(); })) << "the search of the old opponent was cancelled, it was not left to finish";
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
            ASSERT_FALSE(controller.IsThinking()) << "white is to move, and the program is black now";
        }
        EXPECT_EQ(asBlack->Calls(), 0);
        EXPECT_TRUE(model.GetMoves().empty());
        EXPECT_FALSE(controller.GetError().has_value()) << "the Cancelled of the old search is not an error";
    }

    TEST_F(OpponentControllerTest, AnEngineThatFailsIsReportedOnceAndAskedAgainOnlyWhenTheGameMovesOn) {
        ChessGameModel model;
        const auto engine = eastl::make_shared<FailingEngine>(EngineError{EngineErrorKind::Timeout, "no answer in the test"});
        CountingFactory factory({engine});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(controller.SetSide(PieceColor::White).has_value());
        const uint64_t afterPick = controller.GetRevision();

        ASSERT_TRUE(UpdateUntil(controller, [&] { return controller.GetError().has_value(); }));

        ASSERT_TRUE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetError()->kind, EngineErrorKind::Timeout) << DescribeError(controller.GetError()).c_str();
        EXPECT_EQ(controller.GetRevision(), afterPick + 1) << "the error is a change";
        EXPECT_FALSE(controller.IsThinking());
        EXPECT_TRUE(controller.PlaysFor(PieceColor::White)) << "the program still holds its side; the game waits for the person to act";
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
            ASSERT_FALSE(controller.IsThinking()) << "no search while the error stands";
        }
        EXPECT_EQ(engine->Calls(), 1);
        EXPECT_EQ(factory.Calls(), 1);
        EXPECT_EQ(controller.GetRevision(), afterPick + 1) << "reported once, not once per frame";
        EXPECT_TRUE(model.GetMoves().empty());

        model.Reset();  // the person pressed the button: a new game
        controller.Update();
        EXPECT_FALSE(controller.GetError().has_value()) << "an error of an earlier game is gone";
        EXPECT_EQ(controller.GetRevision(), afterPick + 2);
        EXPECT_TRUE(controller.IsThinking()) << "it is white's turn in the new game: the program asks the engine again";
        ASSERT_TRUE(UpdateUntil(controller, [&] { return controller.GetError().has_value(); }));
        EXPECT_EQ(engine->Calls(), 2);
        EXPECT_EQ(controller.GetRevision(), afterPick + 3);
        EXPECT_EQ(factory.Calls(), 1) << "the engine was kept: only the search was asked again";
    }

    TEST_F(OpponentControllerTest, AMoveOfSomeoneElseIsAlsoTheGameMovingOn) {
        ChessGameModel model;
        const auto engine = eastl::make_shared<FailingEngine>(EngineError{EngineErrorKind::ProcessDied, "gone in the test"});
        CountingFactory factory({engine});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(controller, [&] { return controller.GetError().has_value(); }));

        ASSERT_TRUE(Play(model, "e7e5")) << "a person moves for the program's side, as the control API may";
        controller.Update();

        EXPECT_FALSE(controller.GetError().has_value());
        EXPECT_EQ(engine->Calls(), 1) << "it is white's turn: nothing to ask";
    }

    TEST_F(OpponentControllerTest, PickingTheSideToMoveDropsAHalfMadeMoveOfThePerson) {
        ChessGameModel model;
        CountingFactory factory({eastl::make_shared<GatedEngine>()});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        model.OnSquareClicked(Sq("g1"));
        ASSERT_TRUE(model.HasSelection());
        ASSERT_FALSE(model.GetLegalMoves().empty());

        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        EXPECT_TRUE(model.HasSelection()) << "white is to move and the program plays black: the person's selection stays";
        ASSERT_TRUE(controller.SetSide(PieceColor::White).has_value());

        EXPECT_FALSE(model.HasSelection()) << "the program takes the side to move: its pieces are not selected by a person";
        EXPECT_TRUE(model.GetLegalMoves().empty());
    }

    TEST_F(OpponentControllerTest, AFailedPickKeepsTheSelection) {
        ChessGameModel model;
        CountingFactory factory(LaunchFailed());
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        model.OnSquareClicked(Sq("g1"));
        ASSERT_TRUE(model.HasSelection());

        ASSERT_FALSE(controller.SetSide(PieceColor::White).has_value());

        EXPECT_TRUE(model.HasSelection()) << "the program did not take the turn";
    }

    TEST_F(OpponentControllerTest, ShutdownCancelsTheSearchAndLaterUpdatesDoNothing) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto engine = eastl::make_shared<GatedEngine>(true);
        CountingFactory factory({engine});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(Play(model, "e2e4"));
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));
        ASSERT_TRUE(controller.IsThinking());

        controller.Shutdown();

        EXPECT_FALSE(controller.IsThinking());
        EXPECT_FALSE(controller.GetPlays().has_value());
        EXPECT_FALSE(controller.PlaysFor(PieceColor::Black));
        EXPECT_TRUE(WaitUntil([&] { return engine->WasCancelled(); })) << "the search was cancelled, it did not run to the end of its gate";
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }
        EXPECT_EQ(engine->Calls(), 1);
        EXPECT_EQ(factory.Calls(), 1);
        EXPECT_EQ(model.GetMoves().size(), 1u);
        EXPECT_FALSE(controller.GetError().has_value());
    }

    TEST_F(OpponentControllerTest, DestroyingTheControllerCancelsTheSearch) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto engine = eastl::make_shared<GatedEngine>(true);
        CountingFactory factory({engine});
        {
            OpponentController controller(model);
            controller.SetEngineFactory(factory.AsFactory());
            ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
            ASSERT_TRUE(Play(model, "e2e4"));
            controller.Update();
            ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));
        }  // the destructor does not wait for the engine

        EXPECT_TRUE(WaitUntil([&] { return engine->WasCancelled(); }));
        EXPECT_EQ(model.GetMoves().size(), 1u);
    }

#if CHESS_NO_STOCKFISH

    TEST_F(OpponentControllerTest, WithoutStockfishThePickOfStockfishIsRefusedAlsoWhileNobodyPlaysAndNothingChanges) {
        ChessGameModel model;
        OpponentController controller(model);  // the default factory: no SetEngineFactory
        ASSERT_EQ(controller.GetEngine(), OpponentEngine::Own);
        const uint64_t revision = controller.GetRevision();

        const auto refused = controller.SetEngine(OpponentEngine::Stockfish);

        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().kind, EngineErrorKind::Unsupported);
        EXPECT_FALSE(refused.error().detail.empty());
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own) << "an engine that cannot be had cannot be saved";
        EXPECT_FALSE(controller.GetPlays().has_value());
        ASSERT_TRUE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetError()->kind, EngineErrorKind::Unsupported);
        EXPECT_EQ(controller.GetRevision(), revision + 1) << "the error that appeared is shown once";

        // The same refusal again is the person's retry: still refused, and nothing else changes
        EXPECT_FALSE(controller.SetEngine(OpponentEngine::Stockfish).has_value());
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own);
        EXPECT_FALSE(controller.GetPlays().has_value());
    }

    TEST_F(OpponentControllerTest, WithoutStockfishAPickWithAStockfishSeatIsRefusedAsAWholeAlsoForTheSeatNobodyPlays) {
        ChessGameModel model;
        OpponentController controller(model);
        const size_t easy = LevelIndex("easy");
        const SeatChoice own{OpponentEngine::Own, easy};
        const SeatChoice stockfish{OpponentEngine::Stockfish, easy};

        const auto refused = controller.ChooseSeats(ProgramSides::Of(PieceColor::White), own, stockfish);  // Black's seat is not played, and still refused

        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().kind, EngineErrorKind::Unsupported);
        EXPECT_FALSE(controller.PlaysFor(PieceColor::White)) << "the whole pick is refused: White's good seat is not applied either";
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::Black).engine, OpponentEngine::Own);
        EXPECT_FALSE(controller.Choose(PieceColor::Black, easy, OpponentEngine::Stockfish).has_value());
        EXPECT_FALSE(controller.GetPlays().has_value());
    }

    TEST_F(OpponentControllerTest, WithoutStockfishTheOwnEnginePlaysThroughTheDefaultFactory) {
        ChessGameModel model;
        OpponentController controller(model);
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value()) << "the default engine is the own one: nothing to refuse";
        ASSERT_TRUE(controller.SetLevel(LevelIndex("easy")).has_value());
        EXPECT_FALSE(controller.GetError().has_value());
        ASSERT_TRUE(Play(model, "e2e4"));

        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();

        EXPECT_EQ(model.GetTurn(), PieceColor::White);
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own);
    }

    TEST_F(OpponentControllerTest, WithoutStockfishAFactoryOfTheTestIsStillAskedForStockfish) {
        ChessGameModel model;
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());  // the test world: its doubles are called Stockfish and Own

        ASSERT_TRUE(controller.Choose(PieceColor::Black, LevelIndex("easy"), OpponentEngine::Stockfish).has_value());

        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Engines()[0], OpponentEngineId(OpponentEngine::Stockfish));
    }

#else

    TEST_F(OpponentControllerTest, TheDefaultFactoryNamesThePathItTriedWhenThereIsNoFile) {
        ChessGameModel model;
        OpponentController controller(model, OpponentSettings{eastl::string(kMissingPath.data(), kMissingPath.size())});  // an explicit path decides: $STOCKFISH and PATH are not asked

        const auto picked = controller.SetSide(PieceColor::Black);

        ASSERT_FALSE(picked.has_value());
        EXPECT_EQ(picked.error().kind, EngineErrorKind::LaunchFailed);
        EXPECT_TRUE(Contains(picked.error().detail, kMissingPath)) << picked.error().detail.c_str();
        EXPECT_FALSE(controller.GetPlays().has_value());
        ASSERT_TRUE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetError()->detail, picked.error().detail);

        // A path with a tilde names no file either (nothing expands it), and the sentence says so
        controller.SetSettings(OpponentSettings{eastl::string(kTildePath.data(), kTildePath.size())});
        const auto tilde = controller.SetSide(PieceColor::Black);
        ASSERT_FALSE(tilde.has_value());
        EXPECT_EQ(tilde.error().kind, EngineErrorKind::LaunchFailed);
        EXPECT_TRUE(Contains(tilde.error().detail, kTildePath)) << tilde.error().detail.c_str();
        EXPECT_TRUE(Contains(tilde.error().detail, "'~'")) << tilde.error().detail.c_str();
    }

#endif  // CHESS_NO_STOCKFISH

    namespace {

        /// A shell script that runs the fake UCI engine with @p flags, as the "Stockfish" of a test: stockfishPath points at it
        std::filesystem::path WriteWrapper(const TempDirectory& directory, StringView flags) {
            const std::filesystem::path script = directory.File(kWrapperName);
            {
                std::ofstream out(script);
#if defined(_WIN32)
                // No exec: cmd.exe stays the parent of the fake, and UciProcess's job ends both
                out << "@\"" << CHESS_TEST_PYTHON << "\" \"" << FakeEngineScript().string() << "\" " << std::string(flags.data(), flags.size()) << "\r\n";
#else
                out << "#!/bin/sh\n";
                out << "exec \"" << CHESS_TEST_PYTHON << "\" \"" << FakeEngineScript().string() << "\" " << std::string(flags.data(), flags.size()) << "\n";
#endif
            }
            std::filesystem::permissions(script, kScriptPermissions, std::filesystem::perm_options::replace);
            return script;
        }

        OpponentSettings SettingsFor(const std::filesystem::path& program) {
            const std::string path = program.string();
            return OpponentSettings{eastl::string(path.c_str(), path.size())};
        }

    }  // namespace

    TEST_F(OpponentControllerTest, TheDefaultFactoryStartsTheProgramOfThePathWithTheSkillOfTheLevel) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        const std::filesystem::path wrapper = WriteWrapper(directory, Format("--log \"{}\" --moves e7e5", log));
        ChessGameModel model;
        OpponentController controller(model, SettingsFor(wrapper));
        const size_t hard = LevelIndex(kHardLevelId);
        const OpponentLevel& level = GetOpponentLevels()[hard];

        ASSERT_TRUE(controller.SetLevel(hard).has_value());
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();

        EXPECT_EQ(model.GetMoves()[1].ToUci(), "e7e5");
        EXPECT_FALSE(controller.GetError().has_value());
        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), Format("setoption name Skill Level value {}", level.skillLevel))) << "the picked level reached the engine";
        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), Format("go movetime {}", level.moveTimeMs))) << "and so did its time";
    }

    TEST_F(OpponentControllerTest, AChangeOfLevelStartsAnEngineWithTheNewSkill) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const eastl::string log = directory.FileText(kLogName);
        const std::filesystem::path wrapper = WriteWrapper(directory, Format("--log \"{}\" --moves e7e5", log));  // every process answers e7e5 to its first search
        ChessGameModel model;
        OpponentController controller(model, SettingsFor(wrapper));
        // Hard and max: neither blunders (the handicap would answer e2e4 with a random move of its own and the fake would not be asked), and their skills differ
        // (easy and medium have the same Skill Level)
        const size_t hard = LevelIndex(kHardLevelId);
        const size_t max = LevelIndex("max");
        ASSERT_TRUE(controller.Choose(PieceColor::Black, hard).has_value());
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();
        const OpponentLevel& before = GetOpponentLevels()[hard];
        const OpponentLevel& after = GetOpponentLevels()[max];
        ASSERT_NE(before.skillLevel, after.skillLevel);

        ASSERT_TRUE(controller.SetLevel(max).has_value());
        model.Reset();
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();

        // The new process got the new Skill Level at its handshake: the log of the fake has one line of each, the old process and the new one
        const eastl::string first = Format("setoption name Skill Level value {}", before.skillLevel);
        const eastl::string second = Format("setoption name Skill Level value {}", after.skillLevel);
        EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), second));
        const eastl::vector<eastl::string> lines = ReadFileLines(directory.File(kLogName));
        EXPECT_EQ(std::count(lines.begin(), lines.end(), first), 1);
        EXPECT_EQ(std::count(lines.begin(), lines.end(), second), 1);
    }

    namespace {

        /// The controller of the default factory plays one move against a fake child that ignores `quit`, then is shut down. The search is over, so this thread holds the last
        /// reference to the engine: dropping it closes the child here, which ignores `quit` until it is signalled. With @p checkSpeed the close must also have taken less
        /// than kShutdownBudget (the yardstick)
        void ShutDownWithAStubbornChild(const TempDirectory& directory, bool checkSpeed) {
            const eastl::string log = directory.FileText(kLogName);
            const std::filesystem::path wrapper = WriteWrapper(directory, Format("--log \"{}\" --ignore-quit --moves e7e5", log));
            ChessGameModel model;
            OpponentController controller(model, SettingsFor(wrapper));
            ASSERT_TRUE(controller.Choose(PieceColor::Black, LevelIndex(kHardLevelId)).has_value());  // hard never blunders: the child is asked, so there is a child to close
            ASSERT_TRUE(Play(model, "e2e4"));
            ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();

            const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
            controller.Shutdown();
            const std::chrono::steady_clock::duration closing = std::chrono::steady_clock::now() - begin;

            if (checkSpeed) {
                EXPECT_LT(closing, kShutdownBudget) << "the default grace of a UciEngine is " << UciEngineSettings::kDefaultQuitGraceMs << " ms";
            }
            EXPECT_TRUE(WaitForLogLine(directory.File(kLogName), "quit")) << "the child was asked to quit before it was signalled";
        }

    }  // namespace

    TEST_F(OpponentControllerTest, TheDefaultFactoryClosesAStubbornChildWhenTheEngineIsDroppedAndAsksItToQuitFirst) {
        SKIP_WITHOUT_FAKE_ENGINE();
        static_assert(OpponentController::kQuitGraceMs < UciEngineSettings::kDefaultQuitGraceMs, "the controller gives a stubborn child a shorter grace than a UciEngine does by default");
        const TempDirectory directory;
        ShutDownWithAStubbornChild(directory, false);  // Shutdown() returning is the event: the grace is over and the child is closed
    }

    TEST_F(OpponentControllerTest, DISABLED_StubbornChildShortGraceYardstick) {
        // The speed of the test above: the close of a child that ignores `quit` takes the short grace of the controller (OpponentController::kQuitGraceMs) and not the
        // default of a UciEngine. A deadline on a machine under load is not a thing a normal run may decide on, so it is checked here, by hand, on a quiet one:
        //   ChessTests --gtest_also_run_disabled_tests --gtest_filter=OpponentControllerTest.DISABLED_StubbornChildShortGraceYardstick
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        ShutDownWithAStubbornChild(directory, true);
    }

    TEST_F(OpponentControllerTest, ANewControllerHasTheDefaultEngineAndPickingTheOtherWithNobodyPlayingStartsNothing) {
        ChessGameModel model;
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        EXPECT_EQ(controller.GetEngine(), kDefaultOpponentEngine);
        EXPECT_EQ(controller.GetEngine(), kStockfishAvailable ? OpponentEngine::Stockfish : OpponentEngine::Own);
        const OpponentEngine other = kDefaultOpponentEngine == OpponentEngine::Own ? OpponentEngine::Stockfish : OpponentEngine::Own;  // a factory of the test is asked for either

        EXPECT_TRUE(controller.SetEngine(other).has_value());

        EXPECT_EQ(controller.GetEngine(), other);
        EXPECT_EQ(controller.GetRevision(), 1u) << "a display of the engine has to redraw";
        EXPECT_EQ(factory.Calls(), 0) << "nobody plays: nothing is looked up or started";
        EXPECT_FALSE(controller.GetPlays().has_value());

        EXPECT_TRUE(controller.SetEngine(other).has_value());
        EXPECT_EQ(controller.GetRevision(), 1u) << "the same pick again changes nothing";
        EXPECT_EQ(factory.Calls(), 0);
    }

    TEST_F(OpponentControllerTest, AnEngineCycleWalksStockfishOwnStockfishWhereStockfishIsAvailableElseStaysOnOwn) {
        ChessGameModel model;
        OpponentController controller(model);

        EXPECT_EQ(controller.GetEngine(), kDefaultOpponentEngine);
        if constexpr (kStockfishAvailable) {
            EXPECT_TRUE(controller.CycleEngine().has_value());
            EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own);
            EXPECT_TRUE(controller.CycleEngine().has_value());
            EXPECT_EQ(controller.GetEngine(), OpponentEngine::Stockfish);
            EXPECT_EQ(controller.GetRevision(), 2u);
        } else {
            EXPECT_TRUE(controller.CycleEngine().has_value());
            EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own) << "the cycle skips the engine this build cannot run";
            EXPECT_EQ(controller.GetRevision(), 0u) << "and the pick changes nothing";
            EXPECT_FALSE(controller.GetError().has_value());
        }
    }

    TEST_F(OpponentControllerTest, ChangingTheEngineWhileTheProgramPlaysRemakesTheOpponentAndDropsTheStaleAnswer) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        // The first (Stockfish) engine ignores its token and holds h7h5 until the end of the test; the second (Own) answers at once
        const auto slow = eastl::make_shared<GatedEngine>(false, Mv("h7h5"));
        const auto quick = eastl::make_shared<InstantEngine>();
        CountingFactory factory({slow, quick});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(controller.Choose(PieceColor::Black, controller.GetLevel(), OpponentEngine::Stockfish).has_value());  // a factory of the test is asked for Stockfish in a build without it too
        ASSERT_TRUE(Play(model, "e2e4"));
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return slow->Calls() == 1; }));
        ASSERT_TRUE(controller.IsThinking());
        const uint64_t revision = controller.GetRevision();
        const size_t level = controller.GetLevel();

        ASSERT_TRUE(controller.SetEngine(OpponentEngine::Own).has_value());

        EXPECT_FALSE(controller.IsThinking()) << "cancelled at once, though the worker is still inside the old engine";
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own);
        EXPECT_GT(controller.GetRevision(), revision);
        EXPECT_EQ(controller.GetPlays(), Optional<PieceColor>(PieceColor::Black)) << "the side is not touched by an engine change";
        EXPECT_TRUE(controller.PlaysFor(PieceColor::Black));
        EXPECT_FALSE(controller.PlaysFor(PieceColor::White));
        EXPECT_EQ(controller.GetLevel(), level);
        ASSERT_EQ(factory.Calls(), 2);
        EXPECT_EQ(factory.Engines()[0], OpponentEngineId(OpponentEngine::Stockfish));
        EXPECT_EQ(factory.Engines()[1], OpponentEngineId(OpponentEngine::Own));
        EXPECT_EQ(factory.Levels()[1], GetOpponentLevels()[level].id);

        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();
        slow->Open();
        ASSERT_TRUE(WaitUntil([&] { return slow->Answered() == 1; }));
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }

        EXPECT_NE(model.GetMoves()[1].ToUci(), "h7h5") << "the answer of the old engine was not played";
        EXPECT_EQ(model.GetMoves().size(), 2u);
        EXPECT_FALSE(controller.GetError().has_value());
    }

    TEST_F(OpponentControllerTest, ChooseWithAnEngineMakesOneEngineAndTheSamePickAgainKeepsTheSearch) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto engine = eastl::make_shared<GatedEngine>(true);
        CountingFactory factory({engine});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        const size_t hard = LevelIndex(kHardLevelId);

        ASSERT_TRUE(controller.Choose(PieceColor::Black, hard, OpponentEngine::Own).has_value());

        ASSERT_EQ(factory.Calls(), 1) << "side, level and engine in one pick: one engine";
        EXPECT_EQ(factory.Engines()[0], OpponentEngineId(OpponentEngine::Own));
        EXPECT_EQ(factory.Levels()[0], kHardLevelId);
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own);
        EXPECT_EQ(controller.GetLevel(), hard);
        EXPECT_TRUE(controller.PlaysFor(PieceColor::Black));
        ASSERT_TRUE(Play(model, "e2e4"));
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));
        const uint64_t revision = controller.GetRevision();

        EXPECT_TRUE(controller.Choose(PieceColor::Black, hard, OpponentEngine::Own).has_value());
        EXPECT_TRUE(controller.SetEngine(OpponentEngine::Own).has_value());

        EXPECT_TRUE(controller.IsThinking()) << "nothing changed: the search in flight is kept";
        EXPECT_EQ(controller.GetRevision(), revision);
        EXPECT_EQ(factory.Calls(), 1);
        EXPECT_FALSE(engine->WasCancelled());
        engine->Open();
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();

        // The three-argument pick checks the level first, like the others: nothing changes
        const uint64_t before = controller.GetRevision();
        const auto refused = controller.Choose(std::nullopt, GetOpponentLevels().size(), OpponentEngine::Stockfish);
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().kind, EngineErrorKind::BadRequest);
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own);
        EXPECT_TRUE(controller.PlaysFor(PieceColor::Black));
        EXPECT_EQ(controller.GetRevision(), before);
    }

    TEST_F(OpponentControllerTest, AFailedStockfishPickIsNotSavedByTheOwnEngineOnItsOwnButByThePersonPickingIt) {
        ChessGameModel model;
        // Stockfish is not there, the own engine always is. The log says which kinds were asked for
        eastl::vector<OpponentEngine> asked;
        const auto instant = eastl::make_shared<InstantEngine>();
        OpponentController controller(model);
        controller.SetEngineFactory([&](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            asked.push_back(engine);
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(LaunchFailed());
            }
            return instant;
        });
        ASSERT_TRUE(controller.SetEngine(OpponentEngine::Stockfish).has_value());  // the engine the person had picked (already the default where Stockfish is available); nobody plays: no factory call

        const auto first = controller.SetSide(PieceColor::Black);

        ASSERT_FALSE(first.has_value());
        EXPECT_EQ(first.error().kind, EngineErrorKind::LaunchFailed);
        EXPECT_FALSE(controller.GetPlays().has_value());
        ASSERT_TRUE(controller.GetError().has_value());
        ASSERT_EQ(asked.size(), 1u) << "no fallback: the other engine is not tried by itself";
        EXPECT_EQ(asked[0], OpponentEngine::Stockfish);
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }
        EXPECT_EQ(asked.size(), 1u);
        EXPECT_TRUE(controller.GetError().has_value());

        // The person presses the Engine button: the error goes with the pick, nothing is asked while nobody plays
        const uint64_t revision = controller.GetRevision();
        EXPECT_TRUE(controller.CycleEngine().has_value());
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own);
        EXPECT_FALSE(controller.GetError().has_value());
        EXPECT_GT(controller.GetRevision(), revision);
        EXPECT_EQ(asked.size(), 1u);

        // and then the side again: now the factory is asked for the own engine and the program plays
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        ASSERT_EQ(asked.size(), 2u);
        EXPECT_EQ(asked[1], OpponentEngine::Own);
        EXPECT_TRUE(controller.PlaysFor(PieceColor::Black));
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();
    }

    TEST_F(OpponentControllerTest, TheDefaultFactoryMakesTheOwnEngineWithNoSettingsNoFileAndNoProcess) {
        ChessGameModel model;
        OpponentController controller(model);  // no factory, no settings
        ASSERT_TRUE(controller.SetEngine(OpponentEngine::Own).has_value());
        ASSERT_TRUE(controller.SetLevel(LevelIndex("easy")).has_value());
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        EXPECT_FALSE(controller.GetError().has_value());
        ASSERT_TRUE(Play(model, "e2e4"));

        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();

        // The reply is a legal move of Black: the model accepted it, and it is White's turn again
        EXPECT_EQ(model.GetTurn(), PieceColor::White);
        EXPECT_FALSE(controller.IsThinking());
        EXPECT_FALSE(controller.GetError().has_value());
        const eastl::string reply = model.GetMoves()[1].ToUci();

        // A fixed depth is deterministic: another controller on another model answers the same (at easy this goes through the handicap, a pure function of the position)
        EXPECT_EQ(OwnReplyToE2e4(LevelIndex("easy")), reply);
    }

    TEST_F(OpponentControllerTest, AtMaxTheDefaultFactoryOwnEngineIsThePlainOwnEngineWithNoHandicap) {
        const size_t max = LevelIndex("max");
        ASSERT_EQ(GetOpponentLevels()[max].blunderPercent, 0);
        const SearchRequest request = [max] {
            ChessGameModel model;
            EXPECT_TRUE(Play(model, "e2e4"));
            return SearchRequest::FromModel(model, LimitsOf(OpponentEngine::Own, GetOpponentLevels()[max]));
        }();
        const auto plain = OwnEngine().FindMove(request, CancellationToken::Create());
        ASSERT_TRUE(plain.has_value());

        const eastl::string reply = OwnReplyToE2e4(max);

        EXPECT_EQ(reply, plain->ToUci()) << "depth 5, no wrapper at 0 percent: the reply is the one of an OwnEngine given the request of the level";
    }

    TEST_F(OpponentControllerTest, TheOwnEngineIsMadeWhateverTheStockfishPathIsAndTheStockfishPickStillNamesThePath) {
        ChessGameModel model;
        OpponentController controller(model, OpponentSettings{eastl::string(kMissingPath.data(), kMissingPath.size())});

        ASSERT_TRUE(controller.Choose(PieceColor::Black, LevelIndex("easy"), OpponentEngine::Own).has_value()) << "the settings are not read for the own engine";
        EXPECT_TRUE(controller.PlaysFor(PieceColor::Black));
        EXPECT_FALSE(controller.GetError().has_value());

        const auto stockfish = controller.SetEngine(OpponentEngine::Stockfish);

        ASSERT_FALSE(stockfish.has_value()) << "no fallback to the own engine";
#if CHESS_NO_STOCKFISH
        EXPECT_EQ(stockfish.error().kind, EngineErrorKind::Unsupported) << "the path is not even looked at";
#else
        EXPECT_EQ(stockfish.error().kind, EngineErrorKind::LaunchFailed);
        EXPECT_TRUE(Contains(stockfish.error().detail, kMissingPath)) << stockfish.error().detail.c_str();
#endif
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own) << "a failed pick is refused: nothing changes";
        EXPECT_TRUE(controller.PlaysFor(PieceColor::Black));
        EXPECT_TRUE(controller.GetError().has_value());
    }

    TEST_F(OpponentControllerTest, TheOwnEngineSearchesToTheDepthOfTheLevelWithTheCapOfTheWindow) {
        ChessGameModel model;
        const auto recorder = eastl::make_shared<RecordingOwnEngine>();
        CountingFactory factory({recorder});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        const size_t hard = LevelIndex(kHardLevelId);
        ASSERT_TRUE(controller.Choose(PieceColor::Black, hard, OpponentEngine::Own).has_value());
        ASSERT_TRUE(Play(model, "e2e4"));

        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();

        EXPECT_EQ(recorder->LastLimits().depth, GetOpponentLevels()[hard].ownDepth);
        EXPECT_EQ(recorder->LastLimits().moveTimeMs, kOwnMoveTimeCapMs);
    }

    TEST_F(OpponentControllerTest, ShutdownWhileTheOwnEngineOpensAtItsDeepestLevelPlaysNothing) {
        // Depth 5 on the start position takes a fraction of a second even in Debug, so this does not prove a long search is cut; the long one is OwnEngineTest's
        // (a depth that never ends, through a factory). What it shows here is the default factory's engine and the controller's Shutdown() together
        ChessGameModel model;
        OpponentController controller(model);
        ASSERT_TRUE(controller.Choose(PieceColor::White, LevelIndex("max"), OpponentEngine::Own).has_value());
        controller.Update();
        ASSERT_TRUE(controller.IsThinking());

        controller.Shutdown();

        EXPECT_FALSE(controller.IsThinking());
        controller.Update();
        EXPECT_TRUE(model.GetMoves().empty());
    }

    // ---- The premoves of the person, played by the controller ----

    namespace {

        /// The person (White) has played e2e4 against a program that plays Black through the double and the program's search is held at the gate
        struct PremoveGame {
            explicit PremoveGame(eastl::shared_ptr<GatedEngine> gate) : engine(std::move(gate)), factory({engine}), controller(model) {
                controller.SetEngineFactory(factory.AsFactory());
                EXPECT_TRUE(controller.SetSide(PieceColor::Black).has_value());
                EXPECT_TRUE(Play(model, "e2e4"));
                controller.Update();
                EXPECT_TRUE(WaitUntil([&] { return engine->Calls() == 1; })) << "the engine has the search and waits at its gate";
            }

            /// The premoves of the person, each a click on the from and on the to square
            void Queue(StringView from, StringView to) {
                model.OnPremoveClicked(PieceColor::White, Sq(from));
                EXPECT_TRUE(model.OnPremoveClicked(PieceColor::White, Sq(to)));
            }

            ChessGameModel model;
            eastl::shared_ptr<GatedEngine> engine;
            CountingFactory factory;
            OpponentController controller;
        };

    }  // namespace

    TEST_F(OpponentControllerTest, APremoveQueuedWhileTheProgramThinksPlaysInTheFrameOfTheAnswerAndTheNextAfterTheNextAnswer) {
        SKIP_WITHOUT_WORKER_THREADS();
        PremoveGame game(eastl::make_shared<GatedEngine>(true));
        game.Queue("d2", "d4");
        game.Queue("b1", "c3");
        const uint64_t revision = game.controller.GetRevision();

        game.engine->Open();
        ASSERT_TRUE(UpdateUntil(game.controller, [&] { return game.model.GetMoves().size() >= 3; })) << DescribeError(game.controller.GetError()).c_str();

        ASSERT_EQ(game.model.GetMoves().size(), 3u) << "the answer and the first premove land in the same Update, the second waits for the next answer";
        EXPECT_EQ(game.model.GetMoves()[2].ToUci(), "d2d4");
        ASSERT_EQ(game.model.GetPremoves().size(), 1u);
        EXPECT_EQ(game.model.GetPremoves()[0], (Premove{Sq("b1"), Sq("c3")}));
        EXPECT_EQ(game.model.GetTurn(), PieceColor::Black);

        ASSERT_TRUE(UpdateUntil(game.controller, [&] { return game.model.GetMoves().size() >= 5; })) << DescribeError(game.controller.GetError()).c_str();

        EXPECT_EQ(game.model.GetMoves().size(), 5u);
        EXPECT_EQ(game.model.GetMoves()[4].ToUci(), "b1c3");
        EXPECT_TRUE(game.model.GetPremoves().empty());
        EXPECT_FALSE(game.controller.GetError().has_value());
        EXPECT_EQ(game.controller.GetRevision(), revision) << "a premove is no change of the choice";
    }

    TEST_F(OpponentControllerTest, APremoveTheAnswerMadeIllegalDropsTheQueueAndLeavesTheAnswerOnTheBoardWithoutAnError) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel scratch;
        ASSERT_TRUE(Play(scratch, "e2e4"));
        const auto answer = MoveGenerator::ParseUci(scratch.GetPosition(), "e7e5");
        ASSERT_TRUE(answer.has_value());
        PremoveGame game(eastl::make_shared<GatedEngine>(true, *answer));
        game.Queue("e4", "e5");
        game.Queue("d2", "d4");

        game.engine->Open();
        ASSERT_TRUE(UpdateUntil(game.controller, [&] { return game.model.GetMoves().size() >= 2; }));
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            game.controller.Update();
        }

        ASSERT_EQ(game.model.GetMoves().size(), 2u) << "e4e5 is blocked by the answer: it does not play, and the d2d4 behind it does not either";
        EXPECT_EQ(game.model.GetMoves()[1].ToUci(), "e7e5");
        EXPECT_TRUE(game.model.GetPremoves().empty());
        EXPECT_FALSE(game.model.GetPremoveColor().has_value());
        EXPECT_FALSE(game.controller.GetError().has_value());
    }

    TEST_F(OpponentControllerTest, APickThatChangesTheSeatsDropsTheQueue) {
        SKIP_WITHOUT_WORKER_THREADS();
        PremoveGame game(eastl::make_shared<GatedEngine>(true));
        game.Queue("d2", "d4");
        const size_t other = (kDefaultOpponentLevel + 1) % GetOpponentLevels().size();

        ASSERT_TRUE(game.controller.SetLevel(other).has_value());

        EXPECT_TRUE(game.model.GetPremoves().empty());
        EXPECT_FALSE(game.model.GetPremoveColor().has_value());
    }

    TEST_F(OpponentControllerTest, APickThatChangesNothingKeepsTheQueue) {
        SKIP_WITHOUT_WORKER_THREADS();
        PremoveGame game(eastl::make_shared<GatedEngine>(true));
        game.Queue("d2", "d4");

        ASSERT_TRUE(game.controller.SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(game.controller.SetLevel(kDefaultOpponentLevel).has_value());

        EXPECT_EQ(game.model.GetPremoves().size(), 1u);
        EXPECT_EQ(game.factory.Calls(), 1) << "nothing was remade";
    }

    TEST_F(OpponentControllerTest, ARefusedPickKeepsTheQueue) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto engine = eastl::make_shared<GatedEngine>(true);
        bool refuse = false;
        OpponentController controller(model);
        controller.SetEngineFactory([&](OpponentEngine, const OpponentLevel&) -> std::expected<eastl::shared_ptr<IChessEngine>, EngineError> {
            if (refuse) {
                return std::unexpected(LaunchFailed());
            }
            return engine;
        });
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(Play(model, "e2e4"));
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; }));
        model.OnPremoveClicked(PieceColor::White, Sq("d2"));
        ASSERT_TRUE(model.OnPremoveClicked(PieceColor::White, Sq("d4")));

        refuse = true;
        const auto picked = controller.SetLevel((kDefaultOpponentLevel + 1) % GetOpponentLevels().size());

        ASSERT_FALSE(picked.has_value());
        EXPECT_EQ(model.GetPremoves().size(), 1u);
        EXPECT_TRUE(controller.PlaysFor(PieceColor::Black)) << "the old opponent stays";
    }

    TEST_F(OpponentControllerTest, APickOfTwoPlayersDropsTheQueueAndAHalfMadePremove) {
        SKIP_WITHOUT_WORKER_THREADS();
        PremoveGame game(eastl::make_shared<GatedEngine>(true));
        game.Queue("d2", "d4");
        game.model.OnPremoveClicked(PieceColor::White, Sq("g1"));
        ASSERT_TRUE(game.model.HasSelection());

        ASSERT_TRUE(game.controller.SetSide(std::nullopt).has_value());

        EXPECT_TRUE(game.model.GetPremoves().empty());
        EXPECT_FALSE(game.model.HasSelection()) << "no premove target stays drawn for a program that no longer plays";
    }

    TEST_F(OpponentControllerTest, ShutdownDropsTheQueue) {
        SKIP_WITHOUT_WORKER_THREADS();
        PremoveGame game(eastl::make_shared<GatedEngine>(true));
        game.Queue("d2", "d4");

        game.controller.Shutdown();

        EXPECT_TRUE(game.model.GetPremoves().empty());
    }

    TEST_F(OpponentControllerTest, AQueueWaitsWhileTheProgramHasTheMove) {
        SKIP_WITHOUT_WORKER_THREADS();
        PremoveGame game(eastl::make_shared<GatedEngine>(true));
        game.Queue("d2", "d4");

        for (int frame = 0; frame < kManyUpdates; ++frame) {
            game.controller.Update();
        }

        EXPECT_EQ(game.model.GetMoves().size(), 1u) << "the answer is held at the gate, so the premove has not played";
        EXPECT_EQ(game.model.GetPremoves().size(), 1u);
    }

}  // namespace BECore::Chess::Tests
