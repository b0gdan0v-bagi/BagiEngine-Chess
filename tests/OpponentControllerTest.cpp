#include "FakeEngineUtil.h"
#include "TestEngines.h"

#include <Chess/Engine/OpponentController.h>
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
        constexpr std::chrono::milliseconds kShutdownBudget{3000};  // well under the 5 s a UciEngine gives a stubborn child by default (UciEngineSettings::kDefaultQuitGraceMs)
        constexpr const char* kLogName = "fake.log";
        constexpr const char* kWrapperName = "stockfish-wrapper.sh";
        constexpr eastl::string_view kMissingPath = "/no/such/file";
        constexpr eastl::string_view kTildePath = "~/bin/stockfish";
        constexpr std::filesystem::perms kScriptPermissions = std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
                                                              std::filesystem::perms::others_read | std::filesystem::perms::others_exec;  // 755
        constexpr eastl::string_view kHardLevelId = "hard";

        bool Play(ChessGameModel& model, eastl::string_view uci) {
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

        size_t LevelIndex(eastl::string_view id) {
            const std::optional<size_t> index = FindOpponentLevel(id);
            EXPECT_TRUE(index.has_value()) << id.data();
            return index.value_or(kDefaultOpponentLevel);
        }

        bool Contains(const eastl::string& text, eastl::string_view part) {
            return text.find(part.data(), 0, part.size()) != eastl::string::npos;
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
        ChessGameModel model;
        const auto engine = eastl::make_shared<GatedEngine>(true);
        CountingFactory factory({engine});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());

        const auto picked = controller.SetSide(PieceColor::Black);

        ASSERT_TRUE(picked.has_value()) << DescribeError(picked ? std::nullopt : std::optional<EngineError>(picked.error())).c_str();
        EXPECT_EQ(controller.GetPlays(), std::optional<PieceColor>(PieceColor::Black));
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
        EXPECT_EQ(engine->LastLimits().moveTimeMs, GetOpponentLevels()[kDefaultOpponentLevel].moveTimeMs) << "the limits are the level's";
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
        const std::optional<PieceColor> expected[] = {PieceColor::Black, PieceColor::White, std::nullopt, PieceColor::Black};

        uint64_t revision = controller.GetRevision();
        for (const std::optional<PieceColor>& plays : expected) {
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
        controller.SetEngineFactory([](const OpponentLevel&) -> MadeEngine { return MadeEngine(eastl::shared_ptr<IChessEngine>{}); });

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

    TEST_F(OpponentControllerTest, AChangeOfLevelDropsTheRunningSearchAndItsStaleAnswer) {
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
        EXPECT_EQ(quick->LastLimits().moveTimeMs, GetOpponentLevels()[hard].moveTimeMs);
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

    namespace {

        /// A shell script that runs the fake UCI engine with @p flags, as the "Stockfish" of a test: stockfishPath points at it
        std::filesystem::path WriteWrapper(const TempDirectory& directory, eastl::string_view flags) {
            const std::filesystem::path script = directory.File(kWrapperName);
            {
                std::ofstream out(script);
                out << "#!/bin/sh\n";
                out << "exec \"" << CHESS_TEST_PYTHON << "\" \"" << FakeEngineScript().string() << "\" " << std::string(flags.data(), flags.size()) << "\n";
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
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();
        const OpponentLevel& before = GetOpponentLevels()[kDefaultOpponentLevel];
        const size_t next = (kDefaultOpponentLevel + 1) % GetOpponentLevels().size();
        const OpponentLevel& after = GetOpponentLevels()[next];
        ASSERT_NE(before.skillLevel, after.skillLevel);

        ASSERT_TRUE(controller.SetLevel(next).has_value());
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

    TEST_F(OpponentControllerTest, TheDefaultFactoryGivesAStubbornChildAShortGraceWhenTheEngineIsDropped) {
        SKIP_WITHOUT_FAKE_ENGINE();
        const TempDirectory directory;
        const std::filesystem::path wrapper = WriteWrapper(directory, "--ignore-quit --moves e7e5");
        ChessGameModel model;
        OpponentController controller(model, SettingsFor(wrapper));
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();

        // The search is over, so this thread holds the last reference to the engine: dropping it closes the child here, which ignores `quit` until it is signalled
        const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
        controller.Shutdown();
        const std::chrono::steady_clock::duration closing = std::chrono::steady_clock::now() - begin;

        EXPECT_LT(closing, kShutdownBudget) << "the default grace of a UciEngine is " << UciEngineSettings::kDefaultQuitGraceMs << " ms";
    }

}  // namespace BECore::Chess::Tests
