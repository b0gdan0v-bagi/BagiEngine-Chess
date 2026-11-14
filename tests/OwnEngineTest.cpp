#include "GamePlay.h"

#include <Chess/Engine/Analysis.h>
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
        constexpr const char* kMateInTwo = "kbK5/pp6/1P6/8/8/8/8/R7 w - - 0 1";          // Ra6! and bxa6 b7#, else Rxa7#
        constexpr const char* kMatedInOne = "kbK5/pp6/RP6/8/8/8/8/8 b - - 0 1";          // every Black move loses to a mate in one
        constexpr const char* kMated = "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1";
        constexpr const char* kStalemated = "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1";
        constexpr const char* kOneLegalMove = "k7/8/2K5/8/8/8/8/1R6 b - - 0 1";  // Black has Ka7 and nothing else
        constexpr const char* kTwoLegalMoves = "k7/8/2K5/8/8/8/8/7R b - - 0 1";  // Ka7 or Kb8

        constexpr int kShallow = 2;
        constexpr int kMateInTwoDepth = 4;
        constexpr int kSelfPlayDepth = 2;
        constexpr int kFloorDepth = 3;
        constexpr int kDepthThatNeverEnds = 64;
        constexpr int kMoveTimeMs = 100;
        /// The most a search with a move time of kMoveTimeMs may take: the poll is a thousand nodes, the rest is a slow machine. Only the
        /// DISABLED_ yardsticks use it: a normal test never compares a duration to an upper bound (tests/CLAUDE.md)
        constexpr auto kTimeTolerance = std::chrono::milliseconds(1500);
        constexpr auto kCancelAfter = std::chrono::milliseconds(50);
        /// The time a cancelled search and Shutdown() have to return in. Only the DISABLED_ yardsticks use it
        constexpr auto kStopBudget = std::chrono::milliseconds(1000);
        /// A game of the tests is stopped after this many plies, if it has not ended
        constexpr size_t kPlyCap = kDefaultPlyCap;
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

        std::expected<Analysis, EngineError> Analyse(IChessEngine& engine, const SearchRequest& request, const CancellationToken& token = CancellationToken::Create()) {
            return engine.Analyse(request, token);
        }

        std::expected<Analysis, EngineError> AnalyseFen(IChessEngine& engine, eastl::string_view fen, SearchLimits limits) {
            return Analyse(engine, SearchRequest::FromPosition(PositionFromFen(fen), limits));
        }

        /// The kind of an error, or nothing for a value: a form that EXPECT_EQ can compare and print
        std::optional<EngineErrorKind> KindOf(const std::expected<Analysis, EngineError>& answer) {
            return answer ? std::nullopt : std::optional<EngineErrorKind>(answer.error().kind);
        }

        /// The move of an answer, or nothing for an error: a form that EXPECT_EQ can compare and print
        std::optional<Move> Found(const std::expected<Move, EngineError>& answer) {
            return answer ? std::optional<Move>(*answer) : std::nullopt;
        }

        /// @p request asking for @p lines lines
        SearchRequest WithLines(SearchRequest request, int lines) {
            request.lines = lines;
            return request;
        }

        bool Play(ChessGameModel& model, eastl::string_view uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        bool IsLegalIn(const Position& position, Move move) {
            const MoveList moves = MoveGenerator::GenerateLegal(position);
            return eastl::find(moves.begin(), moves.end(), move) != moves.end();
        }

        /// How long @p search takes when its token is cancelled kCancelAfter after it began (for the yardsticks)
        template <class SearchCall>
        std::chrono::steady_clock::duration TimeCancelledSearch(SearchCall&& search) {
            CancellationToken token = CancellationToken::Create();
            std::thread canceller([token]() mutable {
                std::this_thread::sleep_for(kCancelAfter);
                token.Cancel();
            });
            const auto started = std::chrono::steady_clock::now();
            search(token);
            const auto took = std::chrono::steady_clock::now() - started;
            canceller.join();
            return took;
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

    TEST_F(OwnEngineTest, ASearchCancelledFromAnotherThreadIsCancelled) {
        CancellationToken token = CancellationToken::Create();
        std::thread canceller([token]() mutable {
            std::this_thread::sleep_for(kCancelAfter);
            token.Cancel();
        });

        // A depth that never ends: the call returning at all is the proof that the token stopped it
        const auto answer = Ask(_engine, SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), Depth(kDepthThatNeverEnds)), token);
        canceller.join();

        ASSERT_FALSE(answer.has_value());
        EXPECT_EQ(answer.error().kind, EngineErrorKind::Cancelled);
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

    TEST_F(OwnEngineTest, AMoveTimeAloneGivesALegalMove) {
        const SearchRequest request = SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), SearchLimits{kMoveTimeMs, 0});

        const auto started = std::chrono::steady_clock::now();
        const auto answer = Ask(_engine, request);
        const auto took = std::chrono::steady_clock::now() - started;

        ASSERT_TRUE(answer.has_value()) << DescribeError(answer ? std::nullopt : std::optional<EngineError>(answer.error())).c_str();
        EXPECT_TRUE(IsLegalIn(*request.FinalPosition(), *answer));
        EXPECT_GE(took, std::chrono::milliseconds(kMoveTimeMs) / 2) << "a search that has the time uses it";
    }

    TEST_F(OwnEngineTest, DepthAndTimeStopAtTheEarlier) {
        // The depth ends first: the search reached the depth it was given, a minute is far more than it needs
        const auto shallow = AnalyseFen(_engine, Position::kStartFen, SearchLimits{60000, kShallow});
        // The time ends first: the depth could take for ever, so a search that returned stopped on the time
        const auto timed = AnalyseFen(_engine, kKiwipeteFen, SearchLimits{kMoveTimeMs, kDepthThatNeverEnds});

        ASSERT_TRUE(shallow.has_value());
        EXPECT_EQ(shallow->Best().depth, kShallow);
        ASSERT_TRUE(timed.has_value());
        EXPECT_LT(timed->Best().depth, kDepthThatNeverEnds);
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
            const Player player = EnginePlayer(engine, Depth(kSelfPlayDepth));
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
            const Player engine = EnginePlayer(_engine, Depth(kFloorDepth));
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

    TEST_F(OwnEngineTest, ShutdownDuringADeepSearchCancelsItAndALateAnswerIsNeverPlayed) {
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        const auto engine = eastl::make_shared<LimitedEngine>(eastl::make_shared<OwnEngine>(), Depth(kDepthThatNeverEnds));
        OpponentController controller(model);
        controller.SetEngineFactory([engine](OpponentEngine, const OpponentLevel&) -> MadeEngine { return engine; });
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; })) << "the search runs on the pool";
        ASSERT_TRUE(controller.IsThinking());

        controller.Shutdown();

        EXPECT_TRUE(WaitUntil([&] { return engine->Answered() == 1; })) << "the token was cancelled and the search left its loop";
        EXPECT_EQ(model.GetMoves().size(), 1u) << "a late answer is never played";
    }

    // -- Analyse ---------------------------------------------------------------------------------------------------------------

    TEST_F(OwnEngineTest, AnalyseAMateInOneForEitherColorGivesTheMovingMateAndAMateScore) {
        const auto white = AnalyseFen(_engine, kMateInOneForWhite, Depth(kShallow));
        const auto black = AnalyseFen(_engine, kMateInOneForBlack, Depth(kShallow));

        ASSERT_TRUE(white.has_value());
        ASSERT_TRUE(black.has_value());
        ASSERT_EQ(white->lines.size(), 1u);
        EXPECT_EQ(white->Best().move, Mv("a1a8"));
        EXPECT_EQ(white->Best().score, (AnalysisScore{AnalysisScoreKind::Mate, 1}));
        EXPECT_EQ(black->Best().move, Mv("a8a1"));
        EXPECT_EQ(black->Best().score, (AnalysisScore{AnalysisScoreKind::Mate, 1})) << "the side to move mates: positive for Black too";
        EXPECT_GE(white->Best().depth, 1);
    }

    TEST_F(OwnEngineTest, AnalyseAMateInTwoGivesTheKeyMoveAndTwoMoves) {
        const auto answer = AnalyseFen(_engine, kMateInTwo, Depth(kMateInTwoDepth));

        ASSERT_TRUE(answer.has_value());
        EXPECT_EQ(answer->Best().move, Mv("a1a6"));
        EXPECT_EQ(answer->Best().score, (AnalysisScore{AnalysisScoreKind::Mate, 2}));
    }

    TEST_F(OwnEngineTest, AnalyseTheSideToBeMatedHasANegativeMate) {
        const auto answer = AnalyseFen(_engine, kMatedInOne, Depth(kMateInTwoDepth));

        ASSERT_TRUE(answer.has_value());
        EXPECT_EQ(answer->Best().score, (AnalysisScore{AnalysisScoreKind::Mate, -1}));
        EXPECT_TRUE(IsLegalIn(PositionFromFen(kMatedInOne), answer->Best().move));
    }

    TEST_F(OwnEngineTest, AnalyseAHangingQueenIsTheCaptureAndARookUp) {
        const auto answer = AnalyseFen(_engine, kHangingQueen, Depth(kShallow));

        ASSERT_TRUE(answer.has_value());
        EXPECT_EQ(answer->Best().move, Mv("d1d4"));
        EXPECT_EQ(answer->Best().score.kind, AnalysisScoreKind::Centipawns);
        // The queen is the only piece Black has: after the capture White is a rook up (500 plus the tables), so the bound is a rook and not a queen
        EXPECT_GT(answer->Best().score.value, 400);
    }

    TEST_F(OwnEngineTest, AnalyseTheStartPositionIsNearZero) {
        const auto answer = AnalyseFen(_engine, Position::kStartFen, Depth(kFloorDepth));

        ASSERT_TRUE(answer.has_value());
        EXPECT_EQ(answer->Best().score.kind, AnalysisScoreKind::Centipawns);
        EXPECT_EQ(answer->Best().depth, kFloorDepth);
        // The value was computed once (depth 3, Evaluation.h as it is) and is written out: +50 (the piece-square tables favour the side to move a little).
        // The window of 40 around it is the guard against a change of the evaluation that nobody meant
        EXPECT_GE(answer->Best().score.value, 10);
        EXPECT_LE(answer->Best().score.value, 90);
    }

    TEST_F(OwnEngineTest, AnalyseAtADepthIsTheSameLineTwiceAndFindMoveGivesItsMove) {
        const SearchRequest request = SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), Depth(kFloorDepth));

        const auto first = Analyse(_engine, request);
        const auto second = Analyse(_engine, request);
        OwnEngine other;
        const auto third = Analyse(other, request);
        const auto played = Ask(_engine, request);

        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        ASSERT_TRUE(third.has_value());
        ASSERT_TRUE(played.has_value());
        const auto same = [&](const Analysis& a, const Analysis& b) { return a.Best().move == b.Best().move && a.Best().score == b.Best().score && a.Best().depth == b.Best().depth; };
        EXPECT_TRUE(same(*first, *second));
        EXPECT_TRUE(same(*first, *third)) << "another object, the same line: nothing is kept between calls";
        EXPECT_EQ(first->Best().move, *played) << "FindMove and Analyse run the same search";
    }

    TEST_F(OwnEngineTest, AnalyseFollowsTheHistoryOfAGame) {
        ChessGameModel model;
        for (const char* uci : {"e2e4", "e7e5", "g1f3", "d8h4"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }

        const auto answer = Analyse(_engine, SearchRequest::FromModel(model, Depth(kShallow)));

        ASSERT_TRUE(answer.has_value());
        EXPECT_EQ(answer->Best().move, Mv("f3h4"));
        EXPECT_GT(answer->Best().score.value, 600) << "a queen up, from the side to move";
    }

    TEST_F(OwnEngineTest, AnalyseLimitsThatAskForNothingAndAnIllegalMoveAreBadRequests) {
        SearchRequest illegal = SearchRequest::FromPosition(Position::Start(), Depth(kShallow));
        illegal.moves.push_back(Mv("e2e5"));

        for (const SearchLimits limits : {SearchLimits{0, 0}, SearchLimits{-1, 0}, SearchLimits{0, -3}}) {
            EXPECT_EQ(KindOf(AnalyseFen(_engine, Position::kStartFen, limits)), EngineErrorKind::BadRequest) << limits.moveTimeMs << "/" << limits.depth;
        }
        EXPECT_EQ(KindOf(Analyse(_engine, illegal)), EngineErrorKind::BadRequest);
    }

    TEST_F(OwnEngineTest, AnalyseIsNoMoveInAMateAndInAStalemate) {
        for (const char* fen : {kMated, kStalemated}) {
            EXPECT_EQ(KindOf(AnalyseFen(_engine, fen, Depth(kShallow))), EngineErrorKind::NoMove) << fen;
        }
    }

    TEST_F(OwnEngineTest, AnalyseATokenCancelledBeforeTheCallIsCancelled) {
        CancellationToken token = CancellationToken::Create();
        token.Cancel();

        EXPECT_EQ(KindOf(Analyse(_engine, SearchRequest::FromPosition(Position::Start(), Depth(kShallow)), token)), EngineErrorKind::Cancelled);
    }

    TEST_F(OwnEngineTest, AnalyseCancelledFromAnotherThreadIsCancelled) {
        CancellationToken token = CancellationToken::Create();
        std::thread canceller([token]() mutable {
            std::this_thread::sleep_for(kCancelAfter);
            token.Cancel();
        });

        const auto answer = Analyse(_engine, SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), Depth(kDepthThatNeverEnds)), token);
        canceller.join();

        EXPECT_EQ(KindOf(answer), EngineErrorKind::Cancelled);
    }

    TEST_F(OwnEngineTest, AnalyseAMoveTimeAloneGivesALegalMove) {
        const SearchRequest request = SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), SearchLimits{kMoveTimeMs, 0});

        const auto answer = Analyse(_engine, request);

        ASSERT_TRUE(answer.has_value());
        EXPECT_TRUE(IsLegalIn(*request.FinalPosition(), answer->Best().move));
        EXPECT_GE(answer->Best().depth, 1);
    }

    TEST_F(OwnEngineTest, TwoThreadsMayAnalyseWithTheSameObject) {
        const SearchRequest request = SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), Depth(kFloorDepth));
        const auto expected = Analyse(_engine, request);
        std::expected<Analysis, EngineError> a = std::unexpected(EngineError{});
        std::expected<Analysis, EngineError> b = std::unexpected(EngineError{});

        std::thread other([&] { b = Analyse(_engine, request); });
        a = Analyse(_engine, request);
        other.join();

        ASSERT_TRUE(expected.has_value());
        ASSERT_TRUE(a.has_value());
        ASSERT_TRUE(b.has_value());
        EXPECT_EQ(a->Best().move, expected->Best().move);
        EXPECT_EQ(a->Best().score, expected->Best().score);
        EXPECT_EQ(b->Best().move, expected->Best().move);
        EXPECT_EQ(b->Best().score, expected->Best().score);
    }

    // -- Analyse with several lines ----------------------------------------------------------------------------------------------

    TEST_F(OwnEngineTest, ThreeLinesFromTheStartAreThreeDifferentLegalMovesAndLineOneIsTheOneLineAnswer) {
        const SearchRequest one = SearchRequest::FromPosition(Position::Start(), Depth(kFloorDepth));

        const auto single = Analyse(_engine, one);
        const auto played = Ask(_engine, one);
        const auto several = Analyse(_engine, WithLines(one, 3));

        ASSERT_TRUE(single.has_value());
        ASSERT_TRUE(played.has_value());
        ASSERT_TRUE(several.has_value());
        ASSERT_EQ(several->LineCount(), 3u);
        for (size_t i = 0; i < several->lines.size(); ++i) {
            EXPECT_TRUE(IsLegalIn(Position::Start(), several->lines[i].move)) << i;
            EXPECT_EQ(several->lines[i].depth, kFloorDepth) << i;
            for (size_t j = 0; j < i; ++j) {
                EXPECT_NE(several->lines[i].move, several->lines[j].move) << i << " and " << j;
            }
        }
        EXPECT_EQ(several->Best().move, single->Best().move);
        EXPECT_EQ(several->Best().score, single->Best().score);
        EXPECT_EQ(several->Best().depth, single->Best().depth);
        EXPECT_EQ(several->Best().move, *played) << "line 1 is the move FindMove plays";
        EXPECT_EQ(single->LineCount(), 1u);
    }

    TEST_F(OwnEngineTest, AnalyseOneLineByNameIsTheDefault) {
        const SearchRequest request = SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), Depth(kFloorDepth));

        const auto byDefault = Analyse(_engine, request);
        const auto named = Analyse(_engine, WithLines(request, 1));

        ASSERT_TRUE(byDefault.has_value());
        ASSERT_TRUE(named.has_value());
        EXPECT_EQ(byDefault->LineCount(), 1u);
        EXPECT_EQ(named->LineCount(), 1u);
        EXPECT_EQ(byDefault->Second(), nullptr);
        EXPECT_EQ(named->Best().move, byDefault->Best().move);
        EXPECT_EQ(named->Best().score, byDefault->Best().score);
    }

    TEST_F(OwnEngineTest, AHangingQueenIsLineOneAndTheSecondLineIsClearlyWorse) {
        const auto answer = Analyse(_engine, WithLines(SearchRequest::FromPosition(PositionFromFen(kHangingQueen), Depth(kShallow)), 2));

        ASSERT_TRUE(answer.has_value());
        ASSERT_EQ(answer->LineCount(), 2u);
        const AnalysisLine* second = answer->Second();
        ASSERT_NE(second, nullptr);
        EXPECT_EQ(answer->Best().move, Mv("d1d4"));
        EXPECT_EQ(answer->Best().score.kind, AnalysisScoreKind::Centipawns);
        EXPECT_EQ(second->score.kind, AnalysisScoreKind::Centipawns);
        // Both from the side to move: taking the queen leaves a rook up, any other move leaves the rook against the queen
        EXPECT_GT(answer->Best().score.value, 400);
        EXPECT_LT(second->score.value, answer->Best().score.value - 400);
    }

    TEST_F(OwnEngineTest, AMateInOneIsLineOneAndTheSecondLineIsNotAMateInOne) {
        for (const char* fen : {kMateInOneForWhite, kMateInOneForBlack}) {
            const auto answer = Analyse(_engine, WithLines(SearchRequest::FromPosition(PositionFromFen(fen), Depth(kMateInTwoDepth)), 2));

            ASSERT_TRUE(answer.has_value()) << fen;
            ASSERT_EQ(answer->LineCount(), 2u) << fen;
            EXPECT_EQ(answer->Best().score, (AnalysisScore{AnalysisScoreKind::Mate, 1})) << fen;
            const AnalysisScore second = answer->Second()->score;
            EXPECT_FALSE(second.kind == AnalysisScoreKind::Mate && second.value == 1) << fen << ": only one move mates";
            EXPECT_NE(answer->Second()->move, answer->Best().move) << fen;
        }
    }

    TEST_F(OwnEngineTest, APositionWithOneLegalMoveGivesOneLineWhateverWasAsked) {
        ASSERT_EQ(MoveGenerator::GenerateLegal(PositionFromFen(kOneLegalMove)).size(), 1u);

        for (const int lines : {1, 2, 4, kMaxAnalysisLines}) {
            const auto answer = Analyse(_engine, WithLines(SearchRequest::FromPosition(PositionFromFen(kOneLegalMove), Depth(kShallow)), lines));

            ASSERT_TRUE(answer.has_value()) << lines;
            EXPECT_EQ(answer->LineCount(), 1u) << lines;
            EXPECT_EQ(answer->Second(), nullptr) << lines;
            EXPECT_EQ(answer->Best().move, Mv("a8a7")) << lines;
        }
    }

    TEST_F(OwnEngineTest, AFewLegalMovesGiveAsManyLinesAsThereAreMoves) {
        ASSERT_EQ(MoveGenerator::GenerateLegal(PositionFromFen(kTwoLegalMoves)).size(), 2u);

        const auto answer = Analyse(_engine, WithLines(SearchRequest::FromPosition(PositionFromFen(kTwoLegalMoves), Depth(kShallow)), 4));

        ASSERT_TRUE(answer.has_value());
        ASSERT_EQ(answer->LineCount(), 2u);
        EXPECT_NE(answer->lines[0].move, answer->lines[1].move);
        EXPECT_TRUE(IsLegalIn(PositionFromFen(kTwoLegalMoves), answer->lines[0].move));
        EXPECT_TRUE(IsLegalIn(PositionFromFen(kTwoLegalMoves), answer->lines[1].move));
    }

    TEST_F(OwnEngineTest, EightLinesFromTheStartAreEightDifferentMoves) {
        const auto answer = Analyse(_engine, WithLines(SearchRequest::FromPosition(Position::Start(), Depth(kShallow)), kMaxAnalysisLines));

        ASSERT_TRUE(answer.has_value());
        ASSERT_EQ(answer->LineCount(), static_cast<size_t>(kMaxAnalysisLines));
        for (size_t i = 0; i < answer->lines.size(); ++i) {
            for (size_t j = 0; j < i; ++j) {
                EXPECT_NE(answer->lines[i].move, answer->lines[j].move) << i << " and " << j;
            }
        }
    }

    TEST_F(OwnEngineTest, SeveralLinesAreTheSameOnEveryRunAndFromAnotherObject) {
        const SearchRequest request = WithLines(SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), Depth(kFloorDepth)), 3);

        const auto first = Analyse(_engine, request);
        const auto second = Analyse(_engine, request);
        OwnEngine other;
        const auto third = Analyse(other, request);

        ASSERT_TRUE(first.has_value());
        ASSERT_TRUE(second.has_value());
        ASSERT_TRUE(third.has_value());
        ASSERT_EQ(first->LineCount(), 3u);
        ASSERT_EQ(second->LineCount(), 3u);
        ASSERT_EQ(third->LineCount(), 3u);
        for (size_t i = 0; i < 3; ++i) {
            EXPECT_EQ(second->lines[i].move, first->lines[i].move) << i;
            EXPECT_EQ(second->lines[i].score, first->lines[i].score) << i;
            EXPECT_EQ(second->lines[i].depth, first->lines[i].depth) << i;
            EXPECT_EQ(third->lines[i].move, first->lines[i].move) << i;
            EXPECT_EQ(third->lines[i].score, first->lines[i].score) << i;
        }
    }

    TEST_F(OwnEngineTest, TwoThreadsMayAnalyseSeveralLinesWithTheSameObject) {
        const SearchRequest request = WithLines(SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), Depth(kFloorDepth)), 3);
        const auto expected = Analyse(_engine, request);
        std::expected<Analysis, EngineError> a = std::unexpected(EngineError{});
        std::expected<Analysis, EngineError> b = std::unexpected(EngineError{});

        std::thread other([&] { b = Analyse(_engine, request); });
        a = Analyse(_engine, request);
        other.join();

        ASSERT_TRUE(expected.has_value());
        ASSERT_TRUE(a.has_value());
        ASSERT_TRUE(b.has_value());
        ASSERT_EQ(a->LineCount(), expected->LineCount());
        ASSERT_EQ(b->LineCount(), expected->LineCount());
        for (size_t i = 0; i < expected->lines.size(); ++i) {
            EXPECT_EQ(a->lines[i].move, expected->lines[i].move) << i;
            EXPECT_EQ(a->lines[i].score, expected->lines[i].score) << i;
            EXPECT_EQ(b->lines[i].move, expected->lines[i].move) << i;
            EXPECT_EQ(b->lines[i].score, expected->lines[i].score) << i;
        }
    }

    TEST_F(OwnEngineTest, SeveralLinesCancelledFromAnotherThreadAreCancelled) {
        CancellationToken token = CancellationToken::Create();
        std::thread canceller([token]() mutable {
            std::this_thread::sleep_for(kCancelAfter);
            token.Cancel();
        });

        const auto answer = Analyse(_engine, WithLines(SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), Depth(kDepthThatNeverEnds)), 3), token);
        canceller.join();

        EXPECT_EQ(KindOf(answer), EngineErrorKind::Cancelled);
    }

    TEST_F(OwnEngineTest, SeveralLinesWithAMoveTimeAloneAreLegal) {
        const SearchRequest request = WithLines(SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), SearchLimits{kMoveTimeMs, 0}), 3);

        const auto answer = Analyse(_engine, request);

        ASSERT_TRUE(answer.has_value());
        ASSERT_EQ(answer->LineCount(), 3u);
        for (const AnalysisLine& line : answer->lines) {
            EXPECT_TRUE(IsLegalIn(*request.FinalPosition(), line.move));
            EXPECT_GE(line.depth, 1);
        }
        EXPECT_NE(answer->lines[0].move, answer->lines[1].move);
        EXPECT_NE(answer->lines[1].move, answer->lines[2].move);
        EXPECT_NE(answer->lines[0].move, answer->lines[2].move);
    }

    TEST_F(OwnEngineTest, ANumberOfLinesOutsideOneToEightIsABadRequest) {
        for (const int lines : {0, -1, kMaxAnalysisLines + 1, 100}) {
            const auto answer = Analyse(_engine, WithLines(SearchRequest::FromPosition(Position::Start(), Depth(kShallow)), lines));

            EXPECT_EQ(KindOf(answer), EngineErrorKind::BadRequest) << lines;
        }
    }

    TEST_F(OwnEngineTest, SeveralLinesOfAFinishedGameAreNoMoveAndWithACancelledTokenCancelled) {
        for (const char* fen : {kMated, kStalemated}) {
            EXPECT_EQ(KindOf(Analyse(_engine, WithLines(SearchRequest::FromPosition(PositionFromFen(fen), Depth(kShallow)), 3))), EngineErrorKind::NoMove) << fen;
        }

        CancellationToken token = CancellationToken::Create();
        token.Cancel();
        EXPECT_EQ(KindOf(Analyse(_engine, WithLines(SearchRequest::FromPosition(Position::Start(), Depth(kShallow)), 3), token)), EngineErrorKind::Cancelled);
    }

    TEST_F(OwnEngineTest, FindMoveIgnoresTheNumberOfLines) {
        const SearchRequest request = SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), Depth(kFloorDepth));
        SearchRequest many = request;
        many.lines = 5;

        EXPECT_EQ(Found(Ask(_engine, many)), Found(Ask(_engine, request)));
    }

    // -- By-hand yardsticks: the speed claims no normal test makes, because they depend on the load of the machine --------------------
    // ChessTests --gtest_also_run_disabled_tests --gtest_filter='OwnEngineTest.DISABLED_*' on a quiet machine; SearchTest has the one of the search itself

    TEST_F(OwnEngineTest, DISABLED_MoveTimeYardstick) {
        const std::chrono::steady_clock::duration moveTimeLimit = std::chrono::milliseconds(kMoveTimeMs) + kTimeTolerance;
        const auto timeOf = [](auto&& search) {
            const auto started = std::chrono::steady_clock::now();
            search();
            return std::chrono::steady_clock::now() - started;
        };
        const SearchRequest moveTime = SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), SearchLimits{kMoveTimeMs, 0});
        const SearchRequest shallow = SearchRequest::FromPosition(Position::Start(), SearchLimits{60000, kShallow});
        const SearchRequest timed = SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), SearchLimits{kMoveTimeMs, kDepthThatNeverEnds});

        EXPECT_LT(timeOf([&] { Ask(_engine, moveTime); }), moveTimeLimit) << "FindMove honours the move time";
        EXPECT_LT(timeOf([&] { Analyse(_engine, moveTime); }), moveTimeLimit) << "Analyse honours the move time";
        EXPECT_LT(timeOf([&] { Analyse(_engine, WithLines(moveTime, 3)); }), moveTimeLimit) << "three lines honour the move time";
        EXPECT_LT(timeOf([&] { Ask(_engine, shallow); }), kTimeTolerance) << "the depth ends a shallow search long before the minute";
        EXPECT_LT(timeOf([&] { Ask(_engine, timed); }), moveTimeLimit) << "the time ends a search whose depth never would";
    }

    TEST_F(OwnEngineTest, DISABLED_StopLatencyYardstick) {
        const SearchRequest deep = SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), Depth(kDepthThatNeverEnds));

        EXPECT_LT(TimeCancelledSearch([&](const CancellationToken& token) { Ask(_engine, deep, token); }), kStopBudget) << "a cancelled FindMove";
        EXPECT_LT(TimeCancelledSearch([&](const CancellationToken& token) { Analyse(_engine, deep, token); }), kStopBudget) << "a cancelled Analyse";
        EXPECT_LT(TimeCancelledSearch([&](const CancellationToken& token) { Analyse(_engine, WithLines(deep, 3), token); }), kStopBudget) << "a cancelled search of three lines";

        // The controller part needs the pool's worker beside this thread
        SKIP_WITHOUT_WORKER_THREADS();
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        const auto engine = eastl::make_shared<LimitedEngine>(eastl::make_shared<OwnEngine>(), Depth(kDepthThatNeverEnds));
        OpponentController controller(model);
        controller.SetEngineFactory([engine](OpponentEngine, const OpponentLevel&) -> MadeEngine { return engine; });
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        controller.Update();
        ASSERT_TRUE(WaitUntil([&] { return engine->Calls() == 1; })) << "the search runs on the pool";
        const auto started = std::chrono::steady_clock::now();
        controller.Shutdown();
        EXPECT_LT(std::chrono::steady_clock::now() - started, kStopBudget) << "Shutdown with a deep search in flight";
        EXPECT_TRUE(WaitUntil([&] { return engine->Answered() == 1; })) << "the cancelled search ends";
    }

}  // namespace BECore::Chess::Tests
