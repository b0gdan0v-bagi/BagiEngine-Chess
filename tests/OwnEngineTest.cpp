#include "SeededRandom.h"
#include "TestEngines.h"

#include <Chess/Engine/OpponentController.h>
#include <Chess/Engine/OwnEngine.h>
#include <Chess/Engine/Search.h>
#include <Chess/Game/ChessGameModel.h>
#include <Common/EngineEnvironment.h>
#include <chrono>
#include <thread>

/// OwnEngine through IChessEngine: the contract of UciEngine::FindMove without a process, a whole game against itself and against a random mover, and
/// the engine inside a running OpponentController (the real TaskManager). The fixture starts the core services (TaskManager) for the last one.
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kMateInOneForWhite = "6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1";  // Ra8#
        constexpr const char* kMateInOneForBlack = "r3k3/8/8/8/8/8/5PPP/6K1 b - - 0 1";  // Ra1#
        constexpr const char* kHangingQueen = "k7/8/8/8/3q4/8/8/3RK3 w - - 0 1";         // Rxd4: nothing guards the queen
        constexpr const char* kMated = "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1";
        constexpr const char* kStalemated = "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1";

        constexpr int kShallow = 2;
        constexpr int kSelfPlayDepth = 2;
        constexpr int kFloorDepth = 3;
        constexpr int kDepthThatNeverEnds = 64;
        constexpr int kMoveTimeMs = 100;
        /// The most a search with a move time of kMoveTimeMs may take: the poll is a thousand nodes, the rest is a slow machine
        constexpr auto kTimeTolerance = std::chrono::milliseconds(1500);
        constexpr auto kCancelAfter = std::chrono::milliseconds(50);
        constexpr auto kStopBudget = std::chrono::milliseconds(1000);
        /// A game of the tests is stopped after this many plies, if it has not ended
        constexpr size_t kPlyCap = 120;
        constexpr int kFloorGames = 4;
        constexpr uint64_t kRandomSeed = 0x2545F4914F6CDD1DULL;

        SearchLimits Depth(int depth) {
            return SearchLimits{0, depth};
        }

        std::expected<Move, EngineError> Ask(IChessEngine& engine, const SearchRequest& request, const CancellationToken& token = CancellationToken::Create()) {
            return engine.FindMove(request, token);
        }

        std::expected<Move, EngineError> AskFen(IChessEngine& engine, eastl::string_view fen, SearchLimits limits) {
            return Ask(engine, SearchRequest::FromPosition(PositionFromFen(fen), limits));
        }

        /// The move of an answer, or nothing for an error: a form that EXPECT_EQ can compare and print
        std::optional<Move> Found(const std::expected<Move, EngineError>& answer) {
            return answer ? std::optional<Move>(*answer) : std::nullopt;
        }

        bool Play(ChessGameModel& model, eastl::string_view uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        bool IsLegalIn(const Position& position, Move move) {
            const MoveList moves = MoveGenerator::GenerateLegal(position);
            return eastl::find(moves.begin(), moves.end(), move) != moves.end();
        }

        /// What a player of a test game is: the next move for the side to move of @p model
        using Player = std::function<Move(const ChessGameModel&)>;

        Player EnginePlayer(OwnEngine& engine, int depth) {
            return [&engine, depth](const ChessGameModel& model) {
                const auto move = Ask(engine, SearchRequest::FromModel(model, Depth(depth)));
                EXPECT_TRUE(move.has_value()) << DescribeError(move ? std::nullopt : std::optional<EngineError>(move.error())).c_str();
                return move.value_or(Move{});
            };
        }

        Player RandomPlayer(SeededRandom& random) {
            return [&random](const ChessGameModel& model) {
                const MoveList moves = MoveGenerator::GenerateLegal(model.GetPosition());
                return moves[random.Below(moves.size())];
            };
        }

        /// Plays @p white and @p black against each other until the game ends or kPlyCap plies; every move must be accepted. Returns the moves in UCI
        eastl::vector<eastl::string> PlayGame(ChessGameModel& model, const Player& white, const Player& black) {
            eastl::vector<eastl::string> played;
            while (!model.IsGameOver() && model.GetMoves().size() < kPlyCap) {
                const Move move = (model.GetTurn() == PieceColor::White ? white : black)(model);
                const auto accepted = model.TryMove(move);
                EXPECT_TRUE(accepted.has_value()) << "move " << move.ToUci().c_str() << " in " << model.GetPosition().ToFen().c_str();
                if (!accepted) {
                    break;
                }
                played.push_back(move.ToUci());
            }
            return played;
        }

        /// An engine that asks another with limits of its own and counts the searches that have ended: the levels of the game give a move time, and these tests want a depth
        class LimitedEngine final : public IChessEngine {
        public:
            LimitedEngine(eastl::shared_ptr<IChessEngine> inner, SearchLimits limits) : _inner(std::move(inner)), _limits(limits) {}

            std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken& token) override {
                SearchRequest limited = request;
                limited.limits = _limits;
                _calls.fetch_add(1);
                std::expected<Move, EngineError> answer = _inner->FindMove(limited, token);
                _answered.fetch_add(1);
                return answer;
            }

            [[nodiscard]] int Calls() const {
                return _calls.load();
            }

            [[nodiscard]] int Answered() const {
                return _answered.load();
            }

        private:
            eastl::shared_ptr<IChessEngine> _inner;
            SearchLimits _limits;
            std::atomic<int> _calls{0};
            std::atomic<int> _answered{0};
        };

    }  // namespace

    class OwnEngineTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ::BECore::Tests::EngineEnvironment::EnsureCoreServices();  // TaskManager, for the tests of the running opponent
        }

        OwnEngine _engine;
    };

    TEST_F(OwnEngineTest, LimitsThatAskForNothingOrLessAreBadRequests) {
        for (const SearchLimits limits : {SearchLimits{0, 0}, SearchLimits{-1, 0}, SearchLimits{0, -3}, SearchLimits{-100, 5}}) {
            const auto answer = AskFen(_engine, Position::kStartFen, limits);

            ASSERT_FALSE(answer.has_value()) << limits.moveTimeMs << "/" << limits.depth;
            EXPECT_EQ(answer.error().kind, EngineErrorKind::BadRequest) << limits.moveTimeMs << "/" << limits.depth;
        }
    }

    TEST_F(OwnEngineTest, ABrokenStartFenAndAnIllegalMoveAreBadRequests) {
        SearchRequest broken = SearchRequest::FromPosition(Position::Start(), Depth(kShallow));
        broken.startFen = "this is not a fen";
        SearchRequest illegal = SearchRequest::FromPosition(Position::Start(), Depth(kShallow));
        illegal.moves.push_back(Mv("e2e5"));

        const auto fromFen = Ask(_engine, broken);
        const auto fromMove = Ask(_engine, illegal);

        ASSERT_FALSE(fromFen.has_value());
        EXPECT_EQ(fromFen.error().kind, EngineErrorKind::BadRequest);
        ASSERT_FALSE(fromMove.has_value());
        EXPECT_EQ(fromMove.error().kind, EngineErrorKind::BadRequest);
    }

    TEST_F(OwnEngineTest, NoMoveInAMateAndInAStalemate) {
        for (const char* fen : {kMated, kStalemated}) {
            const auto answer = AskFen(_engine, fen, Depth(kShallow));

            ASSERT_FALSE(answer.has_value()) << fen;
            EXPECT_EQ(answer.error().kind, EngineErrorKind::NoMove) << fen;
        }
    }

    TEST_F(OwnEngineTest, ATokenCancelledBeforeTheCallIsCancelled) {
        CancellationToken token = CancellationToken::Create();
        token.Cancel();

        const auto answer = Ask(_engine, SearchRequest::FromPosition(Position::Start(), Depth(kShallow)), token);

        ASSERT_FALSE(answer.has_value());
        EXPECT_EQ(answer.error().kind, EngineErrorKind::Cancelled);
    }

    TEST_F(OwnEngineTest, ASearchCancelledFromAnotherThreadReturnsCancelledSoon) {
        CancellationToken token = CancellationToken::Create();
        std::thread canceller([token]() mutable {
            std::this_thread::sleep_for(kCancelAfter);
            token.Cancel();
        });

        const auto started = std::chrono::steady_clock::now();
        const auto answer = Ask(_engine, SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), Depth(kDepthThatNeverEnds)), token);
        const auto took = std::chrono::steady_clock::now() - started;
        canceller.join();

        ASSERT_FALSE(answer.has_value());
        EXPECT_EQ(answer.error().kind, EngineErrorKind::Cancelled);
        EXPECT_LT(took, kStopBudget);
    }

    TEST_F(OwnEngineTest, ItFindsAMateInOneForEitherColor) {
        EXPECT_EQ(Found(AskFen(_engine, kMateInOneForWhite, Depth(kShallow))), Mv("a1a8"));
        EXPECT_EQ(Found(AskFen(_engine, kMateInOneForBlack, Depth(kShallow))), Mv("a8a1"));
    }

    TEST_F(OwnEngineTest, ItTakesAHangingQueen) {
        EXPECT_EQ(Found(AskFen(_engine, kHangingQueen, Depth(kShallow))), Mv("d1d4"));
    }

    TEST_F(OwnEngineTest, TheHistoryOfAGameGoesThroughTheRequest) {
        // 1. e4 e5 2. Nf3 Qh4?? and the knight takes the queen. The request carries the moves, not only the position
        ChessGameModel model;
        for (const char* uci : {"e2e4", "e7e5", "g1f3", "d8h4"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }

        const auto answer = Ask(_engine, SearchRequest::FromModel(model, Depth(kShallow)));

        EXPECT_EQ(Found(answer), Mv("f3h4"));
    }

    TEST_F(OwnEngineTest, AMoveTimeAloneGivesALegalMoveInTime) {
        const SearchRequest request = SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), SearchLimits{kMoveTimeMs, 0});

        const auto started = std::chrono::steady_clock::now();
        const auto answer = Ask(_engine, request);
        const auto took = std::chrono::steady_clock::now() - started;

        ASSERT_TRUE(answer.has_value()) << DescribeError(answer ? std::nullopt : std::optional<EngineError>(answer.error())).c_str();
        EXPECT_TRUE(IsLegalIn(*request.FinalPosition(), *answer));
        EXPECT_GE(took, std::chrono::milliseconds(kMoveTimeMs) / 2) << "a search that has the time uses it";
        EXPECT_LT(took, std::chrono::milliseconds(kMoveTimeMs) + kTimeTolerance);
    }

    TEST_F(OwnEngineTest, DepthAndTimeStopAtTheEarlier) {
        // The depth ends first: a shallow search is far shorter than the long time it was given
        const auto shallowStart = std::chrono::steady_clock::now();
        const auto shallow = Ask(_engine, SearchRequest::FromPosition(Position::Start(), SearchLimits{60000, kShallow}));
        const auto shallowTook = std::chrono::steady_clock::now() - shallowStart;
        // The time ends first: the depth could take for ever
        const auto timedStart = std::chrono::steady_clock::now();
        const auto timed = Ask(_engine, SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), SearchLimits{kMoveTimeMs, kDepthThatNeverEnds}));
        const auto timedTook = std::chrono::steady_clock::now() - timedStart;

        EXPECT_TRUE(shallow.has_value());
        EXPECT_LT(shallowTook, kTimeTolerance);
        EXPECT_TRUE(timed.has_value());
        EXPECT_LT(timedTook, std::chrono::milliseconds(kMoveTimeMs) + kTimeTolerance);
    }

    TEST_F(OwnEngineTest, ADepthAloneIsDeterministic) {
        const SearchRequest request = SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), Depth(kFloorDepth));

        const auto first = Ask(_engine, request);
        const auto second = Ask(_engine, request);
        OwnEngine other;
        const auto third = Ask(other, request);

        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(Found(first), Found(second));
        EXPECT_EQ(Found(first), Found(third)) << "another object, the same answer: nothing is kept between calls";
    }

    TEST_F(OwnEngineTest, TwoThreadsMayAskTheSameObject) {
        const SearchRequest request = SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), Depth(kFloorDepth));
        const auto expected = Ask(_engine, request);
        std::expected<Move, EngineError> a = std::unexpected(EngineError{});
        std::expected<Move, EngineError> b = std::unexpected(EngineError{});

        std::thread other([&] { b = Ask(_engine, request); });
        a = Ask(_engine, request);
        other.join();

        EXPECT_EQ(Found(a), Found(expected));
        EXPECT_EQ(Found(b), Found(expected));
    }

    TEST_F(OwnEngineTest, AGameAgainstItselfIsLegalAndTheSameOnEveryRun) {
        OwnEngine second;
        const auto play = [&](OwnEngine& engine) {
            ChessGameModel model;
            const Player player = EnginePlayer(engine, kSelfPlayDepth);
            eastl::vector<eastl::string> moves = PlayGame(model, player, player);
            EXPECT_TRUE(model.IsGameOver() || model.GetMoves().size() == kPlyCap) << "the game ended or reached the cap, no move was refused";
            return moves;
        };

        const eastl::vector<eastl::string> first = play(_engine);
        const eastl::vector<eastl::string> again = play(second);

        EXPECT_GT(first.size(), 20u) << "a game, not a stop at the first move";
        EXPECT_EQ(first, again);
    }

    TEST_F(OwnEngineTest, ItBeatsARandomMoverWithEitherColor) {
        // The seed is fixed and the engine is deterministic at a depth, so the outcome is the same on every run and platform: the numbers
        // pinned here (four mates in about thirty plies) are not a measurement of strength
        SeededRandom random(kRandomSeed);
        int won = 0;
        for (int game = 0; game < kFloorGames; ++game) {
            const bool engineIsWhite = game % 2 == 0;
            ChessGameModel model;
            const Player engine = EnginePlayer(_engine, kFloorDepth);
            const Player mover = RandomPlayer(random);
            PlayGame(model, engineIsWhite ? engine : mover, engineIsWhite ? mover : engine);

            const std::optional<PieceColor> winner = model.GetWinner();
            const PieceColor engineColor = engineIsWhite ? PieceColor::White : PieceColor::Black;
            EXPECT_TRUE(winner.has_value() && *winner == engineColor) << "game " << game << " ended " << EnumUtils<GameStatus>::ToString(model.GetStatus()).data();
            won += winner.has_value() && *winner == engineColor ? 1 : 0;
        }
        EXPECT_EQ(won, kFloorGames);
    }

    TEST_F(OwnEngineTest, AControllerWithItPlaysALegalAnswer) {
        ChessGameModel model;
        const auto engine = eastl::make_shared<LimitedEngine>(eastl::make_shared<OwnEngine>(), Depth(kFloorDepth));
        OpponentController controller(model);
        controller.SetEngineFactory([engine](OpponentEngine, const OpponentLevel&) -> MadeEngine { return engine; });
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(Play(model, "e2e4"));

        const bool answered = WaitUntil([&] {
            controller.Update();
            return model.GetMoves().size() == 2;
        });

        ASSERT_TRUE(answered) << DescribeError(controller.GetError()).c_str();
        EXPECT_EQ(model.GetTurn(), PieceColor::White);
        EXPECT_EQ(engine->Calls(), 1);
        EXPECT_FALSE(controller.GetError().has_value());
        EXPECT_FALSE(controller.IsThinking());
    }

    TEST_F(OwnEngineTest, ShutdownDuringADeepSearchReturnsAtOnceAndTheSearchEndsSoon) {
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        const auto engine = eastl::make_shared<LimitedEngine>(eastl::make_shared<OwnEngine>(), Depth(kDepthThatNeverEnds));
        OpponentController controller(model);
        controller.SetEngineFactory([engine](OpponentEngine, const OpponentLevel&) -> MadeEngine { return engine; });
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; })) << "the search runs on the pool";
        ASSERT_TRUE(controller.IsThinking());

        const auto started = std::chrono::steady_clock::now();
        controller.Shutdown();
        const auto took = std::chrono::steady_clock::now() - started;

        EXPECT_LT(took, kStopBudget);
        EXPECT_TRUE(WaitUntil([&] { return engine->Answered() == 1; }, kStopBudget)) << "the token was cancelled and the search left its loop";
        EXPECT_EQ(model.GetMoves().size(), 1u) << "a late answer is never played";
    }

}  // namespace BECore::Chess::Tests
