#include "TestEngines.h"

#include <Chess/Engine/OpponentChoice.h>
#include <Chess/Engine/OpponentController.h>
#include <Chess/Game/ChessGameModel.h>
#include <Common/EngineEnvironment.h>
#include <chrono>

/// The two seats of OpponentController: a program on both sides, a choice per seat, the pick as a whole (ChooseSeats), the single-seat API as a wrapper over it, and the
/// OpponentChoice of the person next to two programs. Real TaskManager, engines made in the test (no process); every wait polls a condition with a time limit
namespace BECore::Chess::Tests {

    namespace {

        constexpr int kManyUpdates = 500;
        constexpr ProgramSides kBoth{true, true};
        constexpr ProgramSides kNobody{};

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

        SeatChoice Choice(OpponentEngine engine, StringView level) {
            return SeatChoice{engine, LevelIndex(level)};
        }

    }  // namespace

    class OpponentSeatsTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ::BECore::Tests::EngineEnvironment::EnsureCoreServices();  // TaskManager
        }
    };

    TEST_F(OpponentSeatsTest, BothSeatsPlayAndTakeTurns) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto white = eastl::make_shared<GatedEngine>(true, Mv("e2e4"));
        const auto black = eastl::make_shared<GatedEngine>(true, Mv("e7e5"));
        CountingFactory factory({white, black});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());

        const auto picked = controller.ChooseSeats(kBoth, {}, {});

        ASSERT_TRUE(picked.has_value()) << picked.error().detail.c_str();
        EXPECT_EQ(controller.GetRevision(), 1u);
        EXPECT_EQ(factory.Calls(), 2);
        controller.Update();
        EXPECT_TRUE(controller.IsThinking());
        ASSERT_TRUE(WaitUntil([&] { return white->Calls() == 1; })) << "White opens";
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }
        EXPECT_EQ(black->Calls(), 0) << "it is not Black's turn";
        EXPECT_TRUE(model.GetMoves().empty());

        white->Open();
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 1; })) << DescribeError(controller.GetError()).c_str();
        EXPECT_EQ(model.GetMoves()[0].ToUci(), "e2e4");
        ASSERT_TRUE(UpdateUntil(controller, [&] { return black->Calls() == 1; })) << "Black answers the move of the other program";
        EXPECT_TRUE(controller.IsThinking());

        black->Open();
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();
        EXPECT_EQ(model.GetMoves()[1].ToUci(), "e7e5");
        EXPECT_EQ(model.GetTurn(), PieceColor::White);
        EXPECT_EQ(white->Calls(), 1);
        EXPECT_FALSE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetRevision(), 1u) << "moves and searches are not a change of the choice";
    }

    TEST_F(OpponentSeatsTest, EachSeatGetsItsOwnEngineAndTheFactorySeesTheChoiceOfEachSeat) {
        ChessGameModel model;
        const auto white = eastl::make_shared<InstantEngine>();
        const auto black = eastl::make_shared<InstantEngine>();
        CountingFactory factory({white, black});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());

        ASSERT_TRUE(controller.ChooseSeats(kBoth, Choice(OpponentEngine::Own, "easy"), Choice(OpponentEngine::Stockfish, "max")).has_value());

        ASSERT_EQ(factory.Calls(), 2);
        EXPECT_EQ(factory.Engines()[0], "own");
        EXPECT_EQ(factory.Levels()[0], "easy") << "the first call is White's seat";
        EXPECT_EQ(factory.Engines()[1], "stockfish");
        EXPECT_EQ(factory.Levels()[1], "max");
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::White), Choice(OpponentEngine::Own, "easy"));
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::Black), Choice(OpponentEngine::Stockfish, "max"));
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own) << "two seats play: White's choice is the single-seat answer";
        EXPECT_EQ(controller.GetLevel(), LevelIndex("easy"));

        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 4; })) << DescribeError(controller.GetError()).c_str();
        EXPECT_EQ(white->Calls(), 2);
        EXPECT_EQ(black->Calls(), 2);
        EXPECT_EQ(white->LastLimits().moveTimeMs, LimitsOf(OpponentEngine::Own, GetOpponentLevels()[LevelIndex("easy")]).moveTimeMs);
        EXPECT_EQ(black->LastLimits().moveTimeMs, LimitsOf(OpponentEngine::Stockfish, GetOpponentLevels()[LevelIndex("max")]).moveTimeMs) << "the limits are those of the seat's own choice";
    }

    TEST_F(OpponentSeatsTest, TwoSeatsOfOnePickAreTwoEngineObjects) {
        ChessGameModel model;
        eastl::vector<eastl::shared_ptr<InstantEngine>> made;
        OpponentController controller(model);
        controller.SetEngineFactory([&](OpponentEngine, const OpponentLevel&) -> MadeEngine {
            made.push_back(eastl::make_shared<InstantEngine>());
            return made.back();
        });

        ASSERT_TRUE(controller.ChooseSeats(kBoth, {}, {}).has_value());

        ASSERT_EQ(made.size(), 2u) << "the factory is asked once per seat, also for the same engine and level";
        EXPECT_NE(made[0].get(), made[1].get());
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 6; })) << DescribeError(controller.GetError()).c_str();
        EXPECT_EQ(made[0]->Calls(), 3);
        EXPECT_EQ(made[1]->Calls(), 3);
    }

    TEST_F(OpponentSeatsTest, ASeatChangedWhileTheOtherThinksKeepsTheOthersSearch) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto white = eastl::make_shared<GatedEngine>(true, Mv("e2e4"));
        const auto black = eastl::make_shared<InstantEngine>();
        const auto newBlack = eastl::make_shared<InstantEngine>();
        CountingFactory factory({white, black, newBlack});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        const SeatChoice common{};
        ASSERT_TRUE(controller.ChooseSeats(kBoth, common, common).has_value());
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return white->Calls() == 1; }));
        ASSERT_TRUE(controller.IsThinking());
        const uint64_t revision = controller.GetRevision();

        ASSERT_TRUE(controller.ChooseSeats(kBoth, common, Choice(OpponentEngine::Own, "hard")).has_value());

        EXPECT_EQ(factory.Calls(), 3) << "only the changed seat asks";
        EXPECT_EQ(factory.Engines().back(), "own");
        EXPECT_EQ(factory.Levels().back(), "hard");
        EXPECT_EQ(controller.GetRevision(), revision + 1);
        EXPECT_TRUE(controller.IsThinking());
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }
        EXPECT_FALSE(white->WasCancelled()) << "the search of the unchanged seat was not thrown away";
        EXPECT_EQ(white->Calls(), 1);

        white->Open();
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();
        EXPECT_EQ(model.GetMoves()[0].ToUci(), "e2e4");
        EXPECT_EQ(white->Calls(), 1) << "the old search answered, no second one was made";
        EXPECT_EQ(newBlack->Calls(), 1) << "Black answers with the engine of its new choice";
        EXPECT_EQ(black->Calls(), 0);
    }

    TEST_F(OpponentSeatsTest, ChangingTheSeatThatThinksCancelsItsSearch) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto white = eastl::make_shared<GatedEngine>(true, Mv("e2e4"));
        const auto newWhite = eastl::make_shared<GatedEngine>(true, Mv("d2d4"));
        const auto black = eastl::make_shared<GatedEngine>(true);
        CountingFactory factory({white, black, newWhite});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        const SeatChoice common{};
        ASSERT_TRUE(controller.ChooseSeats(kBoth, common, common).has_value());
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return white->Calls() == 1; }));

        ASSERT_TRUE(controller.ChooseSeats(kBoth, Choice(OpponentEngine::Own, "hard"), common).has_value());

        EXPECT_EQ(factory.Calls(), 3);
        EXPECT_TRUE(WaitUntil([&] { return white->WasCancelled(); })) << "the old opponent of the changed seat cancels its search";
        ASSERT_TRUE(UpdateUntil(controller, [&] { return newWhite->Calls() == 1; })) << "and the new one asks again at once";
        newWhite->Open();
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 1; })) << DescribeError(controller.GetError()).c_str();
        EXPECT_EQ(model.GetMoves()[0].ToUci(), "d2d4");
    }

    TEST_F(OpponentSeatsTest, ARefusedSeatRefusesTheWholePick) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto white = eastl::make_shared<GatedEngine>(true, Mv("e2e4"));
        const auto black = eastl::make_shared<InstantEngine>();
        int calls = 0;
        OpponentController controller(model);
        controller.SetEngineFactory([&](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            ++calls;
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(LaunchFailed());
            }
            if (calls == 1) {
                return white;
            }
            if (calls == 2) {
                return black;
            }
            return eastl::make_shared<InstantEngine>();
        });
        const SeatChoice easy = Choice(OpponentEngine::Own, "easy");
        ASSERT_TRUE(controller.ChooseSeats(kBoth, easy, easy).has_value());
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return white->Calls() == 1; }));
        const uint64_t revision = controller.GetRevision();

        // White's seat is fine (a new level) and Black's seat asks for Stockfish, which is not there
        const auto refused = controller.ChooseSeats(kBoth, Choice(OpponentEngine::Own, "hard"), Choice(OpponentEngine::Stockfish, "hard"));

        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().kind, EngineErrorKind::LaunchFailed);
        EXPECT_EQ(calls, 4) << "White's engine was made first, then Black's was refused";
        EXPECT_EQ(controller.GetSides(), kBoth);
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::White), easy) << "nothing of the pick is saved";
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::Black), easy);
        ASSERT_TRUE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetError()->kind, EngineErrorKind::LaunchFailed);
        EXPECT_EQ(controller.GetRevision(), revision + 1) << "the error that appeared is shown once";
        EXPECT_TRUE(controller.IsThinking()) << "the running search stays";
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }
        EXPECT_FALSE(white->WasCancelled());
        EXPECT_EQ(calls, 4) << "no frame asks the factory again";

        // The error does not stop the opponents, and the program's own moves do not take it away
        white->Open();
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();
        EXPECT_EQ(model.GetMoves()[0].ToUci(), "e2e4");
        ASSERT_TRUE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetError()->kind, EngineErrorKind::LaunchFailed);
        EXPECT_EQ(controller.GetRevision(), revision + 1);
    }

    TEST_F(OpponentSeatsTest, APickThatFindsNoEngineFromNobodyAsksOnceAndChangesNothing) {
        ChessGameModel model;
        CountingFactory factory(LaunchFailed());
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());

        const auto refused = controller.ChooseSeats(kBoth, {}, {});

        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().kind, EngineErrorKind::LaunchFailed);
        EXPECT_EQ(factory.Calls(), 1) << "the second seat is not asked once the first has failed";
        EXPECT_EQ(controller.GetSides(), kNobody);
        EXPECT_FALSE(controller.IsWatching());
        EXPECT_EQ(controller.GetRevision(), 1u);
        EXPECT_TRUE(controller.GetError().has_value());
        EXPECT_FALSE(controller.IsThinking());
    }

    TEST_F(OpponentSeatsTest, ALevelOutsideTheTableIsBadRequestForEitherSeatAndChangesNothing) {
        ChessGameModel model;
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        const size_t outside = GetOpponentLevels().size();

        const auto first = controller.ChooseSeats(kBoth, SeatChoice{OpponentEngine::Own, outside}, {});
        const auto second = controller.ChooseSeats(kBoth, {}, SeatChoice{OpponentEngine::Own, outside});

        ASSERT_FALSE(first.has_value());
        ASSERT_FALSE(second.has_value());
        EXPECT_EQ(first.error().kind, EngineErrorKind::BadRequest);
        EXPECT_EQ(second.error().kind, EngineErrorKind::BadRequest);
        EXPECT_EQ(factory.Calls(), 0);
        EXPECT_EQ(controller.GetSides(), kNobody);
        EXPECT_EQ(controller.GetRevision(), 0u);
        EXPECT_FALSE(controller.GetError().has_value());
    }

    TEST_F(OpponentSeatsTest, TheSamePickAgainDoesNothingAndAClearedErrorIsNotARemake) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto white = eastl::make_shared<GatedEngine>(true, Mv("e2e4"));
        const auto black = eastl::make_shared<GatedEngine>(true);
        int calls = 0;
        OpponentController controller(model);
        controller.SetEngineFactory([&](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            ++calls;
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(LaunchFailed());
            }
            return calls == 1 ? white : black;
        });
        const SeatChoice own = Choice(OpponentEngine::Own, "easy");
        ASSERT_TRUE(controller.ChooseSeats(kBoth, own, own).has_value());
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return white->Calls() == 1; }));
        const uint64_t revision = controller.GetRevision();

        EXPECT_TRUE(controller.ChooseSeats(kBoth, own, own).has_value());

        EXPECT_EQ(calls, 2);
        EXPECT_EQ(controller.GetRevision(), revision) << "nothing changed, nothing is published";

        ASSERT_FALSE(controller.ChooseSeats(kBoth, own, Choice(OpponentEngine::Stockfish, "easy")).has_value());
        const uint64_t afterRefusal = controller.GetRevision();
        EXPECT_EQ(afterRefusal, revision + 1);

        EXPECT_TRUE(controller.ChooseSeats(kBoth, own, own).has_value());

        EXPECT_FALSE(controller.GetError().has_value()) << "the person picked what plays: they have seen the error";
        EXPECT_EQ(controller.GetRevision(), afterRefusal + 1);
        EXPECT_EQ(calls, 3) << "only the refused ask was made, nothing was remade";
        EXPECT_TRUE(controller.IsThinking());
        EXPECT_FALSE(white->WasCancelled());
    }

    TEST_F(OpponentSeatsTest, PlaysForCoversBothColorsGetPlaysIsNothingAndTheProgramIsWatching) {
        ChessGameModel model;
        CountingFactory factory({eastl::make_shared<GatedEngine>(true), eastl::make_shared<GatedEngine>(true)});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        EXPECT_FALSE(controller.IsWatching());

        ASSERT_TRUE(controller.ChooseSeats(kBoth, {}, {}).has_value());

        EXPECT_TRUE(controller.PlaysFor(PieceColor::White));
        EXPECT_TRUE(controller.PlaysFor(PieceColor::Black));
        EXPECT_TRUE(controller.IsWatching());
        EXPECT_EQ(controller.GetSides(), kBoth);
        EXPECT_FALSE(controller.GetPlays().has_value()) << "the one color the program plays: none for two programs, so White stays at the bottom";

        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        EXPECT_FALSE(controller.IsWatching());
        EXPECT_EQ(controller.GetPlays(), Optional<PieceColor>(PieceColor::Black));
        EXPECT_FALSE(controller.PlaysFor(PieceColor::White));

        ASSERT_TRUE(controller.ChooseSeats(ProgramSides{true, false}, {}, {}).has_value());
        EXPECT_EQ(controller.GetPlays(), Optional<PieceColor>(PieceColor::White));
        EXPECT_FALSE(controller.IsWatching());
    }

    TEST_F(OpponentSeatsTest, IsThinkingIsTrueForEitherSeat) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto white = eastl::make_shared<GatedEngine>(true);
        const auto black = eastl::make_shared<GatedEngine>(true);
        CountingFactory factory({white, black});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(controller.ChooseSeats(kBoth, {}, {}).has_value());
        EXPECT_FALSE(controller.IsThinking());

        controller.Update();

        EXPECT_TRUE(controller.IsThinking()) << "Black is to move and thinks";
        ASSERT_TRUE(WaitUntil([&] { return black->Calls() == 1; }));
        EXPECT_EQ(white->Calls(), 0);
    }

    TEST_F(OpponentSeatsTest, AnErrorOfBlacksSeatStallsTheGameAndGoesWithTheNextVersion) {
        ChessGameModel model;
        const auto white = eastl::make_shared<InstantEngine>();
        const auto black = eastl::make_shared<FailingEngine>(EngineError{EngineErrorKind::Timeout, "no answer in the test"});
        CountingFactory factory({white, black});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(controller.ChooseSeats(kBoth, {}, {}).has_value());
        const uint64_t afterPick = controller.GetRevision();

        ASSERT_TRUE(UpdateUntil(controller, [&] { return controller.GetError().has_value(); }));

        ASSERT_TRUE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetError()->kind, EngineErrorKind::Timeout) << DescribeError(controller.GetError()).c_str();
        EXPECT_EQ(model.GetMoves().size(), 1u);
        EXPECT_EQ(controller.GetRevision(), afterPick + 1);
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
            ASSERT_FALSE(controller.IsThinking()) << "no search while the error stands";
        }
        EXPECT_EQ(white->Calls(), 1);
        EXPECT_EQ(black->Calls(), 1);
        EXPECT_EQ(model.GetMoves().size(), 1u) << "the game waits";
        EXPECT_EQ(controller.GetRevision(), afterPick + 1) << "reported once, not once per frame";
        EXPECT_TRUE(controller.PlaysFor(PieceColor::Black));

        ASSERT_TRUE(Play(model, "e7e5")) << "a person (or the control API) moves for the stalled side";
        controller.Update();

        EXPECT_FALSE(controller.GetError().has_value()) << "the version moved: the error is gone";
        EXPECT_EQ(controller.GetRevision(), afterPick + 2);
        EXPECT_TRUE(controller.IsThinking()) << "White is to move and asks again";
    }

    TEST_F(OpponentSeatsTest, AnErrorOfWhitesSeatStallsTheGame) {
        ChessGameModel model;
        const auto white = eastl::make_shared<FailingEngine>(EngineError{EngineErrorKind::ProcessDied, "gone in the test"});
        const auto black = eastl::make_shared<InstantEngine>();
        CountingFactory factory({white, black});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(controller.ChooseSeats(kBoth, {}, {}).has_value());

        ASSERT_TRUE(UpdateUntil(controller, [&] { return controller.GetError().has_value(); }));

        EXPECT_EQ(controller.GetError()->kind, EngineErrorKind::ProcessDied);
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }
        EXPECT_TRUE(model.GetMoves().empty());
        EXPECT_EQ(black->Calls(), 0) << "it is never Black's turn";
        EXPECT_EQ(white->Calls(), 1);

        model.Reset();
        controller.Update();
        EXPECT_FALSE(controller.GetError().has_value());
    }

    TEST_F(OpponentSeatsTest, AStaleAnswerOfWhitesSeatIsDroppedAfterASetPosition) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        // White's engine ignores its token and holds e2e4 until it is opened; Black's seat never answers, so a dropped answer leaves the game as the position was set
        const auto white = eastl::make_shared<GatedEngine>(false, Mv("e2e4"));
        const auto black = eastl::make_shared<GatedEngine>(true);
        CountingFactory factory({white, black});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(controller.ChooseSeats(kBoth, {}, {}).has_value());
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return white->Calls() == 1; }));

        model.SetPosition(PositionFromFen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"));
        ASSERT_TRUE(UpdateUntil(controller, [&] { return black->Calls() == 1; })) << "the stale search is let go and it is Black's turn";
        white->Open();
        ASSERT_TRUE(WaitUntil([&] { return white->Answered() == 1; }));
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }

        EXPECT_TRUE(model.GetMoves().empty()) << "the late e2e4 was not played";
        EXPECT_EQ(model.GetTurn(), PieceColor::Black);
        EXPECT_FALSE(controller.GetError().has_value()) << DescribeError(controller.GetError()).c_str();
    }

    TEST_F(OpponentSeatsTest, AStaleAnswerOfBlacksSeatIsDroppedAfterAReset) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto white = eastl::make_shared<GatedEngine>(true);
        const auto black = eastl::make_shared<GatedEngine>(false, Mv("e7e5"));
        CountingFactory factory({white, black});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(controller.ChooseSeats(kBoth, {}, {}).has_value());
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return black->Calls() == 1; }));

        model.Reset();
        ASSERT_TRUE(UpdateUntil(controller, [&] { return white->Calls() == 1; })) << "the stale search is let go and it is White's turn";
        black->Open();
        ASSERT_TRUE(WaitUntil([&] { return black->Answered() == 1; }));
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }

        EXPECT_TRUE(model.GetMoves().empty()) << "the late e7e5 was not played";
        EXPECT_FALSE(controller.GetError().has_value()) << DescribeError(controller.GetError()).c_str();
    }

    TEST_F(OpponentSeatsTest, ASideTakesTheChoiceOfItsSeatFromAWatchAndKeepsItsOpponent) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto white = eastl::make_shared<GatedEngine>(true, Mv("e2e4"));
        const auto black = eastl::make_shared<InstantEngine>();
        CountingFactory factory({white, black});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        const SeatChoice whiteChoice = Choice(OpponentEngine::Own, "easy");
        const SeatChoice blackChoice = Choice(OpponentEngine::Stockfish, "max");
        ASSERT_TRUE(controller.ChooseSeats(kBoth, whiteChoice, blackChoice).has_value());
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return white->Calls() == 1; }));

        ASSERT_TRUE(controller.SetSide(PieceColor::White).has_value());

        EXPECT_EQ(controller.GetPlays(), Optional<PieceColor>(PieceColor::White));
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::White), whiteChoice);
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::Black), whiteChoice) << "the single-seat API writes the choice of the seat into both";
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own);
        EXPECT_EQ(controller.GetLevel(), LevelIndex("easy"));
        EXPECT_EQ(factory.Calls(), 2) << "White's opponent is the one that played, nothing was made";
        EXPECT_TRUE(controller.IsThinking());
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }
        EXPECT_FALSE(white->WasCancelled()) << "a takeover of Black's side does not touch White's search";
        white->Open();
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 1; })) << DescribeError(controller.GetError()).c_str();
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }
        EXPECT_EQ(model.GetMoves().size(), 1u) << "Black's program is gone: the person has Black";
        EXPECT_EQ(black->Calls(), 0);
    }

    TEST_F(OpponentSeatsTest, TheSingleSeatCallsWriteTheirValueIntoBothSeats) {
        ChessGameModel model;
        CountingFactory factory({eastl::make_shared<InstantEngine>(), eastl::make_shared<InstantEngine>()});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        const size_t hard = LevelIndex("hard");

        ASSERT_TRUE(controller.SetLevel(hard).has_value());
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::White).level, hard);
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::Black).level, hard);
        ASSERT_TRUE(controller.SetEngine(OpponentEngine::Own).has_value());
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::White).engine, OpponentEngine::Own);
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::Black).engine, OpponentEngine::Own);
        EXPECT_EQ(factory.Calls(), 0) << "nobody plays: the choice is only remembered";

        ASSERT_TRUE(controller.Choose(PieceColor::Black, LevelIndex("easy")).has_value());
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::White), Choice(OpponentEngine::Own, "easy"));
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::Black), Choice(OpponentEngine::Own, "easy"));
        EXPECT_EQ(factory.Calls(), 1);

        ASSERT_TRUE(controller.Choose(PieceColor::White, hard, OpponentEngine::Stockfish).has_value());
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::White), Choice(OpponentEngine::Stockfish, "hard"));
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::Black), Choice(OpponentEngine::Stockfish, "hard"));
        EXPECT_EQ(controller.GetPlays(), Optional<PieceColor>(PieceColor::White));
        EXPECT_EQ(factory.Calls(), 2);

        ASSERT_TRUE(controller.SetSide(std::nullopt).has_value());
        EXPECT_EQ(controller.GetLevel(), hard) << "nobody remembers the level";
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Stockfish);
    }

    TEST_F(OpponentSeatsTest, TheSingleSeatCallsAgreeInAWatchByTheirValueAndKeepTheRest) {
        ChessGameModel model;
        CountingFactory factory({eastl::make_shared<GatedEngine>(true), eastl::make_shared<GatedEngine>(true)});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(controller.ChooseSeats(kBoth, Choice(OpponentEngine::Own, "easy"), Choice(OpponentEngine::Stockfish, "max")).has_value());

        ASSERT_TRUE(controller.SetLevel(LevelIndex("hard")).has_value());

        EXPECT_TRUE(controller.IsWatching()) << "the sides stay";
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::White), Choice(OpponentEngine::Own, "hard")) << "the engine of each seat is kept";
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::Black), Choice(OpponentEngine::Stockfish, "hard"));

        ASSERT_TRUE(controller.SetEngine(OpponentEngine::Own).has_value());

        EXPECT_TRUE(controller.IsWatching());
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::White), Choice(OpponentEngine::Own, "hard"));
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::Black), Choice(OpponentEngine::Own, "hard"));

        ASSERT_TRUE(controller.CycleSide().has_value());
        EXPECT_FALSE(controller.PlaysFor(PieceColor::White) || controller.PlaysFor(PieceColor::Black)) << "the cycle from two programs goes to nobody";
    }

    TEST_F(OpponentSeatsTest, PickingNobodyFromAWatchRemovesBothAndRemembersTheChoices) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        CountingFactory factory({eastl::make_shared<GatedEngine>(true), eastl::make_shared<GatedEngine>(true)});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(controller.ChooseSeats(kBoth, Choice(OpponentEngine::Own, "easy"), Choice(OpponentEngine::Stockfish, "max")).has_value());
        controller.Update();
        ASSERT_TRUE(controller.IsThinking());
        const int calls = factory.Calls();

        ASSERT_TRUE(controller.ChooseSeats(kNobody, Choice(OpponentEngine::Own, "easy"), Choice(OpponentEngine::Stockfish, "max")).has_value());

        EXPECT_EQ(controller.GetSides(), kNobody);
        EXPECT_FALSE(controller.IsThinking());
        EXPECT_FALSE(controller.IsWatching());
        EXPECT_EQ(factory.Calls(), calls);
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::Black), Choice(OpponentEngine::Stockfish, "max"));
    }

    TEST_F(OpponentSeatsTest, StartingAWatchDropsAHalfMadeMoveOfThePersonWhoseTurnItIs) {
        ChessGameModel model;
        CountingFactory factory({eastl::make_shared<GatedEngine>(true), eastl::make_shared<GatedEngine>(true)});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        model.OnSquareClicked(Sq("g1"));
        ASSERT_TRUE(model.HasSelection());

        ASSERT_TRUE(controller.ChooseSeats(kBoth, {}, {}).has_value());

        EXPECT_FALSE(model.HasSelection());
        EXPECT_TRUE(model.GetLegalMoves().empty());
    }

    TEST_F(OpponentSeatsTest, ShutdownWithAnEngineThatIgnoresItsTokenInEachSeatJoinsNothing) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        const auto white = eastl::make_shared<GatedEngine>(false);
        const auto black = eastl::make_shared<GatedEngine>(false);
        CountingFactory factory({white, black});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(controller.ChooseSeats(kBoth, {}, {}).has_value());
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return white->Calls() == 1; }));
        ASSERT_TRUE(controller.IsThinking());
        const uint64_t revision = controller.GetRevision();

        controller.Shutdown();

        EXPECT_EQ(white->Answered(), 0) << "the search is still at its gate: nothing joined it";
        EXPECT_EQ(controller.GetSides(), kNobody);
        EXPECT_FALSE(controller.IsThinking());
        EXPECT_EQ(controller.GetRevision(), revision + 1);
        for (int frame = 0; frame < kManyUpdates; ++frame) {
            controller.Update();
        }
        EXPECT_EQ(white->Calls(), 1);
        EXPECT_EQ(black->Calls(), 0);
        white->Open();
        black->Open();
        EXPECT_TRUE(model.GetMoves().empty());
        EXPECT_TRUE(WaitUntil([&] { return white->Answered() == 1; })) << "the worker frees itself";
    }

    TEST_F(OpponentSeatsTest, TwoOwnEnginesOfTheDefaultFactoryPlayALegalGameAndPlayItAgainAfterAReset) {
        constexpr size_t kPlyCap = 60;
        constexpr std::chrono::milliseconds kGameBudget{60000};
        ChessGameModel model;
        OpponentController controller(model);  // no factory, no settings: the default factory makes the own engine, no process

        // Two engines at depths 1 and 2, each behind the deterministic handicap of its level, so the game is the same every time
        ASSERT_TRUE(controller.ChooseSeats(kBoth, Choice(OpponentEngine::Own, "easy"), Choice(OpponentEngine::Own, "medium")).has_value());
        ASSERT_FALSE(controller.GetError().has_value());

        const auto playedTheGame = [&] {
            const bool done = WaitUntil(
                [&] {
                    controller.Update();
                    return model.IsGameOver() || model.GetMoves().size() >= kPlyCap;
                },
                kGameBudget);
            EXPECT_TRUE(done) << "moves so far: " << model.GetMoves().size() << ", " << DescribeError(controller.GetError()).c_str();
            EXPECT_FALSE(controller.GetError().has_value()) << DescribeError(controller.GetError()).c_str();
            eastl::vector<eastl::string> moves;
            for (const Move& played : model.GetMoves()) {
                if (moves.size() == kPlyCap) {
                    break;  // a search that was already running when the cap was reached may have played one more
                }
                moves.push_back(played.ToUci());
            }
            return moves;
        };

        const eastl::vector<eastl::string> first = playedTheGame();
        ASSERT_FALSE(first.empty());

        // Every move is legal in its position (so the turns alternate): the moves replayed on a model of our own
        ChessGameModel replay;
        for (size_t ply = 0; ply < first.size(); ++ply) {
            ASSERT_TRUE(Play(replay, first[ply])) << "ply " << ply << " " << first[ply].c_str() << " is not legal";
        }

        // A reset starts the game again, with the same two programs, and it goes the same way
        model.Reset();
        const eastl::vector<eastl::string> second = playedTheGame();
        EXPECT_EQ(first, second);
    }

    TEST_F(OpponentSeatsTest, TheChoiceOfThePersonOverTwoProgramsKeepsWorking) {
        ChessGameModel model;
        CountingFactory factory({eastl::make_shared<GatedEngine>(true), eastl::make_shared<GatedEngine>(true)});
        OpponentController controller(model);
        controller.SetEngineFactory(factory.AsFactory());
        OpponentChoice choice(controller);
        ASSERT_TRUE(controller.ChooseSeats(kBoth, {}, {}).has_value());
        const uint64_t revision = controller.GetRevision();

        // The known gap: the mode enum has no Watch yet, so two programs read as Two; the color is the remembered one (White) and nothing crashes
        EXPECT_EQ(choice.Mode(), OpponentMode::Two);
        EXPECT_EQ(choice.Color(), PieceColor::White);
        EXPECT_EQ(choice.ModeId(), StringView("two"));
        EXPECT_EQ(choice.ColorId(), StringView("white"));

        ASSERT_TRUE(choice.SetColor("black").has_value());
        EXPECT_TRUE(controller.IsWatching()) << "a color picked while no one program plays is only remembered";
        EXPECT_EQ(controller.GetRevision(), revision);
        EXPECT_EQ(choice.Color(), PieceColor::Black);

        ASSERT_TRUE(choice.SetMode("computer").has_value());
        EXPECT_FALSE(controller.IsWatching());
        EXPECT_EQ(controller.GetPlays(), Optional<PieceColor>(PieceColor::White)) << "the person has Black, the program plays White";
        EXPECT_EQ(choice.Mode(), OpponentMode::Computer);

        ASSERT_TRUE(controller.ChooseSeats(kBoth, {}, {}).has_value());
        ASSERT_TRUE(choice.SetMode("two").has_value());
        EXPECT_EQ(controller.GetSides(), kNobody);
        EXPECT_EQ(choice.Mode(), OpponentMode::Two);
    }

}  // namespace BECore::Chess::Tests
