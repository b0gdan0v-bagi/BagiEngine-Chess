#include "GamePlay.h"

#include <Chess/Engine/BlunderEngine.h>
#include <Chess/Engine/OwnEngine.h>
#include <Chess/Game/ChessGameModel.h>
#include <thread>

/// BlunderEngine alone, with no table and no controller: the wrapped engines are the doubles of TestEngines.h and OwnEngine, no process and no TaskManager.
/// Everything here is deterministic: the decision is a pure function of the position, so the rates and the golden decisions are the same on every run and platform.
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kMated = "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1";
        constexpr const char* kStalemated = "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1";
        constexpr SearchLimits kLimits{0, 1};
        constexpr size_t kGamePlies = 80;
        constexpr size_t kRatePositions = 400;
        constexpr uint64_t kGameSeed = 0x2545F4914F6CDD1DULL;
        constexpr size_t kSelfPlayPlies = 40;

        std::expected<Move, EngineError> Ask(IChessEngine& engine, const SearchRequest& request, const CancellationToken& token = CancellationToken::Create()) {
            return engine.FindMove(request, token);
        }

        /// The move of an answer, or nothing for an error: a form that EXPECT_EQ can compare and print
        Optional<Move> Found(const std::expected<Move, EngineError>& answer) {
            return answer ? Optional<Move>(*answer) : std::nullopt;
        }

        String Uci(const std::expected<Move, EngineError>& answer) {
            return answer ? answer->ToUci() : String("error");
        }

        bool IsLegalIn(const Position& position, Move move) {
            const MoveList moves = MoveGenerator::GenerateLegal(position);
            return eastl::find(moves.begin(), moves.end(), move) != moves.end();
        }

        /// The requests of seeded random games (the whole game so far each time), as many as @p count; every game is a new seed
        Vector<SearchRequest> SeededRequests(size_t count) {
            Vector<SearchRequest> requests;
            for (uint64_t game = 0; requests.size() < count; ++game) {
                SeededRandom random(kGameSeed + game * 7919);
                ChessGameModel model;
                for (size_t ply = 0; ply < kGamePlies && !model.IsGameOver() && requests.size() < count; ++ply) {
                    requests.push_back(SearchRequest::FromModel(model, kLimits));
                    const MoveList moves = MoveGenerator::GenerateLegal(model.GetPosition());
                    EXPECT_TRUE(model.TryMove(moves[random.Below(moves.size())]).has_value());
                }
            }
            return requests;
        }

        /// What one request did to a BlunderEngine over an InstantEngine: whether the wrapped engine was asked, and the answer
        struct Decision {
            bool blundered = false;
            Optional<Move> move;
        };

        Decision Decide(IChessEngine& engine, InstantEngine& inner, const SearchRequest& request) {
            const int before = inner.Calls();
            const auto answer = Ask(engine, request);
            return Decision{inner.Calls() == before, Found(answer)};
        }

        String Describe(const Decision& decision) {
            return Format("{} {}", decision.blundered ? "blunder" : "delegated", decision.move ? decision.move->ToUci() : String("none"));
        }

    }  // namespace

    TEST(BlunderEngineTest, ThePercentIsClampedToZeroAndAHundred) {
        const auto inner = MakeShared<InstantEngine>();

        EXPECT_EQ(BlunderEngine(inner, 150).Percent(), 100);
        EXPECT_EQ(BlunderEngine(inner, -5).Percent(), 0);
        EXPECT_EQ(BlunderEngine(inner, 40).Percent(), 40);
    }

    TEST(BlunderEngineTest, AClampedPercentBehavesAsTheLimit) {
        const SearchRequest request = SearchRequest::FromPosition(Position::Start(), kLimits);
        const auto over = MakeShared<InstantEngine>();
        const auto under = MakeShared<InstantEngine>();
        BlunderEngine overEngine(over, 150);
        BlunderEngine underEngine(under, -5);

        EXPECT_TRUE(Ask(overEngine, request).has_value());
        EXPECT_TRUE(Ask(underEngine, request).has_value());

        EXPECT_EQ(over->Calls(), 0) << "150 acts as 100: the wrapped engine is never asked";
        EXPECT_EQ(under->Calls(), 1) << "-5 acts as 0: it answers everything";
    }

    TEST(BlunderEngineTest, ZeroPercentNeverAltersTheAnswerAndAsksOncePerRequest) {
        const auto inner = MakeShared<InstantEngine>();
        BlunderEngine engine(inner, 0);
        const Vector<SearchRequest> requests = SeededRequests(kGamePlies);

        for (size_t i = 0; i < requests.size(); ++i) {
            const int before = inner->Calls();
            const auto answer = Ask(engine, requests[i]);

            ASSERT_TRUE(answer.has_value()) << i;
            EXPECT_EQ(*answer, *FirstLegalMove(requests[i])) << i;
            EXPECT_EQ(inner->Calls(), before + 1) << i;
        }
    }

    TEST(BlunderEngineTest, AHundredPercentAlwaysAnswersALegalMoveAndNeverAsksTheWrappedEngine) {
        const auto inner = MakeShared<InstantEngine>();
        BlunderEngine engine(inner, 100);
        const Vector<SearchRequest> requests = SeededRequests(kRatePositions);
        size_t differentFromFirst = 0;

        for (size_t i = 0; i < requests.size(); ++i) {
            const auto answer = Ask(engine, requests[i]);

            ASSERT_TRUE(answer.has_value()) << i << " " << DescribeError(answer ? std::nullopt : Optional<EngineError>(answer.error())).c_str();
            EXPECT_TRUE(IsLegalIn(*requests[i].FinalPosition(), *answer)) << i;
            differentFromFirst += *answer != *FirstLegalMove(requests[i]) ? 1 : 0;
        }

        EXPECT_EQ(inner->Calls(), 0);
        EXPECT_GT(differentFromFirst, requests.size() / 2) << "the move is picked among the legal ones, not always the first";
    }

    TEST(BlunderEngineTest, TheSameRequestGivesTheSameAnswerFromAnyObject) {
        const SearchRequest request = SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), kLimits);
        BlunderEngine first(MakeShared<InstantEngine>(), 100);
        BlunderEngine second(MakeShared<InstantEngine>(), 100);

        const auto a = Ask(first, request);
        const auto b = Ask(first, request);
        const auto c = Ask(second, request);

        ASSERT_TRUE(a.has_value());
        EXPECT_EQ(Found(a), Found(b));
        EXPECT_EQ(Found(a), Found(c)) << "another object, the same answer: nothing is kept between calls";
    }

    TEST(BlunderEngineTest, TheDecisionsForTheStartPositionAndKiwipeteArePinned) {
        // A golden list: the mixer is plain uint64_t arithmetic, so these hold on every platform. A number that moves means the mixer or the rule changed.
        // Each position is asked at a few percents ("blunder" when the wrapped engine was not asked, "delegated" otherwise); the move of a blunder is the one of 100 percent
        const auto inner = MakeShared<InstantEngine>();
        const SearchRequest start = SearchRequest::FromPosition(Position::Start(), kLimits);
        const SearchRequest kiwipete = SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), kLimits);
        const auto row = [&](const SearchRequest& request) {
            String text;
            for (const int percent : {10, 25, 50, 75, 90, 100}) {
                BlunderEngine engine(inner, percent);
                text += Format("{}:{} ", percent, Describe(Decide(engine, *inner, request)));
            }
            text.pop_back();
            return text;
        };

        EXPECT_EQ(row(start), "10:delegated a2a3 25:delegated a2a3 50:delegated a2a3 75:blunder e2e3 90:blunder e2e3 100:blunder e2e3");
        EXPECT_EQ(row(kiwipete), "10:delegated a2a3 25:delegated a2a3 50:delegated a2a3 75:delegated a2a3 90:delegated a2a3 100:blunder e1f1");
    }

    TEST(BlunderEngineTest, APositionThatBlundersAt15PercentBlundersAt40) {
        const auto inner = MakeShared<InstantEngine>();
        BlunderEngine low(inner, 15);
        BlunderEngine high(inner, 40);
        size_t lowBlunders = 0;

        const Vector<SearchRequest> requests = SeededRequests(kRatePositions);
        for (size_t i = 0; i < requests.size(); ++i) {
            const Decision atLow = Decide(low, *inner, requests[i]);
            const Decision atHigh = Decide(high, *inner, requests[i]);

            if (atLow.blundered) {
                ++lowBlunders;
                EXPECT_TRUE(atHigh.blundered) << i;
                EXPECT_EQ(atLow.move, atHigh.move) << i << ": the move of a blunder does not depend on the percent";
            }
        }

        EXPECT_GT(lowBlunders, 0u);
    }

    TEST(BlunderEngineTest, TheRateOverSeededGamesIsNearThePercent) {
        // The numbers are deterministic, so this is not flaky; the wide windows only guard against a broken mixer
        const auto inner = MakeShared<InstantEngine>();
        const Vector<SearchRequest> requests = SeededRequests(kRatePositions);
        const auto rate = [&](int percent) {
            BlunderEngine engine(inner, percent);
            size_t blunders = 0;
            for (const SearchRequest& request : requests) {
                blunders += Decide(engine, *inner, request).blundered ? 1 : 0;
            }
            return static_cast<double>(blunders) * 100.0 / static_cast<double>(requests.size());
        };

        const double at15 = rate(15);
        const double at40 = rate(40);

        EXPECT_GE(at15, 8.0);
        EXPECT_LE(at15, 24.0);
        EXPECT_GE(at40, 30.0);
        EXPECT_LE(at40, 50.0);
    }

    TEST(BlunderEngineTest, ABrokenRequestIsBadRequestAlsoAtAHundredPercent) {
        const auto inner = MakeShared<InstantEngine>();
        BlunderEngine engine(inner, 100);
        SearchRequest brokenFen = SearchRequest::FromPosition(Position::Start(), kLimits);
        brokenFen.startFen = "this is not a fen";
        SearchRequest illegalMove = SearchRequest::FromPosition(Position::Start(), kLimits);
        illegalMove.moves.push_back(Mv("e2e5"));

        for (const SearchRequest& request : {brokenFen, illegalMove, SearchRequest::FromPosition(Position::Start(), SearchLimits{0, 0})}) {
            const auto answer = Ask(engine, request);

            ASSERT_FALSE(answer.has_value());
            EXPECT_EQ(answer.error().kind, EngineErrorKind::BadRequest);
        }
        EXPECT_EQ(inner->Calls(), 0);
    }

    TEST(BlunderEngineTest, NoMoveInAMateAndInAStalemateAlsoAtAHundredPercent) {
        const auto inner = MakeShared<InstantEngine>();
        BlunderEngine engine(inner, 100);

        for (const char* fen : {kMated, kStalemated}) {
            const auto answer = Ask(engine, SearchRequest::FromPosition(PositionFromFen(fen), kLimits));

            ASSERT_FALSE(answer.has_value()) << fen;
            EXPECT_EQ(answer.error().kind, EngineErrorKind::NoMove) << fen;
        }
        EXPECT_EQ(inner->Calls(), 0);
    }

    TEST(BlunderEngineTest, ATokenCancelledBeforeTheCallIsCancelledAtAHundredPercent) {
        const auto inner = MakeShared<InstantEngine>();
        BlunderEngine engine(inner, 100);
        CancellationToken token = CancellationToken::Create();
        token.Cancel();

        const auto answer = Ask(engine, SearchRequest::FromPosition(Position::Start(), kLimits), token);

        ASSERT_FALSE(answer.has_value());
        EXPECT_EQ(answer.error().kind, EngineErrorKind::Cancelled);
        EXPECT_EQ(inner->Calls(), 0);
    }

    TEST(BlunderEngineTest, AWrappedEngineThatFailsIsNotHiddenAtZeroPercent) {
        const auto inner = MakeShared<FailingEngine>(EngineError{EngineErrorKind::Timeout, "no answer in time"});
        BlunderEngine engine(inner, 0);

        const auto answer = Ask(engine, SearchRequest::FromPosition(Position::Start(), kLimits));

        ASSERT_FALSE(answer.has_value());
        EXPECT_EQ(answer.error().kind, EngineErrorKind::Timeout);
        EXPECT_EQ(answer.error().detail, "no answer in time");
        EXPECT_EQ(inner->Calls(), 1);
    }

    TEST(BlunderEngineTest, TwoThreadsMayAskTheSameObject) {
        const auto inner = MakeShared<InstantEngine>();
        BlunderEngine engine(inner, 50);
        const Vector<SearchRequest> requests = SeededRequests(kGamePlies);
        Vector<Optional<Move>> alone;
        for (const SearchRequest& request : requests) {
            alone.push_back(Found(Ask(engine, request)));
        }
        Vector<Optional<Move>> a;
        Vector<Optional<Move>> b;

        std::thread other([&] {
            for (size_t i = requests.size(); i-- > 0;) {
                b.push_back(Found(Ask(engine, requests[i])));
            }
        });
        for (const SearchRequest& request : requests) {
            a.push_back(Found(Ask(engine, request)));
        }
        other.join();

        EXPECT_EQ(a, alone);
        eastl::reverse(b.begin(), b.end());
        EXPECT_EQ(b, alone);
    }

    TEST(BlunderEngineTest, TheOwnEngineInsideBlundersAtAHundredAndIsExactAtZero) {
        const auto own = MakeShared<OwnEngine>();
        BlunderEngine alwaysBlunders(own, 100);
        BlunderEngine neverBlunders(own, 0);
        const auto selfPlay = [&](IChessEngine& engine) {
            ChessGameModel model;
            const Player player = EnginePlayer(engine, SearchLimits{0, 2});
            const Vector<String> moves = PlayGame(model, player, player, kSelfPlayPlies);
            EXPECT_TRUE(model.IsGameOver() || model.GetMoves().size() == kSelfPlayPlies) << "the game ended or reached the cap, no move was refused";
            return moves;
        };

        const Vector<String> plain = selfPlay(*own);
        const Vector<String> exact = selfPlay(neverBlunders);
        const Vector<String> random = selfPlay(alwaysBlunders);

        EXPECT_GT(plain.size(), 20u);
        EXPECT_EQ(exact, plain) << "0 percent equals the plain engine, move for move";
        EXPECT_GT(random.size(), 5u);
        EXPECT_NE(random, plain) << "100 percent plays its own moves";
    }

    TEST(BlunderEngineTest, AnalyseIsForwardedUnchangedAtEveryPercent) {
        const auto own = MakeShared<OwnEngine>();
        const SearchRequest request = SearchRequest::FromPosition(PositionFromFen("6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1"), SearchLimits{0, 2});
        const auto plain = own->Analyse(request, CancellationToken::Create());
        ASSERT_TRUE(plain.has_value());

        for (const int percent : {0, 15, 100}) {
            BlunderEngine engine(own, percent);
            const auto wrapped = engine.Analyse(request, CancellationToken::Create());

            ASSERT_TRUE(wrapped.has_value()) << percent;
            EXPECT_EQ(wrapped->Best().move, plain->Best().move) << percent;
            EXPECT_EQ(wrapped->Best().score, plain->Best().score) << percent;
            EXPECT_EQ(wrapped->Best().depth, plain->Best().depth) << percent;
        }
    }

    TEST(BlunderEngineTest, SeveralLinesAreForwardedUnchangedAtEveryPercent) {
        const auto own = MakeShared<OwnEngine>();
        SearchRequest request = SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), SearchLimits{0, 2});
        request.lines = 3;
        const auto plain = own->Analyse(request, CancellationToken::Create());
        ASSERT_TRUE(plain.has_value());
        ASSERT_EQ(plain->LineCount(), 3u);

        for (const int percent : {0, 50, 100}) {
            BlunderEngine engine(own, percent);
            const auto wrapped = engine.Analyse(request, CancellationToken::Create());

            ASSERT_TRUE(wrapped.has_value()) << percent;
            ASSERT_EQ(wrapped->LineCount(), 3u) << percent;
            for (size_t i = 0; i < 3; ++i) {
                EXPECT_EQ(wrapped->lines[i].move, plain->lines[i].move) << percent << " line " << i;
                EXPECT_EQ(wrapped->lines[i].score, plain->lines[i].score) << percent << " line " << i;
                EXPECT_EQ(wrapped->lines[i].depth, plain->lines[i].depth) << percent << " line " << i;
            }
        }
    }

    TEST(BlunderEngineTest, TheRequestForSeveralLinesReachesTheWrappedEngineAsItIs) {
        SearchRequest request = SearchRequest::FromPosition(Position::Start(), kLimits);
        request.lines = 3;

        for (const int percent : {0, 50, 100}) {
            const auto inner = MakeShared<AnalysingEngine>();
            BlunderEngine engine(inner, percent);

            const auto answer = engine.Analyse(request, CancellationToken::Create());

            ASSERT_TRUE(answer.has_value()) << percent;
            EXPECT_EQ(inner->Calls(), 1) << percent;
            EXPECT_EQ(inner->LastRequest().lines, 3) << percent;
        }
    }

    TEST(BlunderEngineTest, AnalyseIsNeverHandicappedOverAPositionWhereTheBlunderWouldDiffer) {
        // At 100 percent FindMove answers a random move, and Analyse the best one: the hint is not spoilt by the handicap
        const auto own = MakeShared<OwnEngine>();
        BlunderEngine alwaysBlunders(own, 100);
        const SearchRequest request = SearchRequest::FromPosition(PositionFromFen("6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1"), SearchLimits{0, 2});

        const auto analysis = alwaysBlunders.Analyse(request, CancellationToken::Create());

        ASSERT_TRUE(analysis.has_value());
        EXPECT_EQ(analysis->Best().move, Mv("a1a8")) << "the mating move, not a random one";
        const auto played = alwaysBlunders.FindMove(request, CancellationToken::Create());
        ASSERT_TRUE(played.has_value());
        EXPECT_NE(*played, Mv("a1a8")) << "the blunder of this position (a pure function of it) is not the mate: the two answers differ";
    }

    TEST(BlunderEngineTest, AnalyseOverAnEngineThatDoesNotAnalyseIsUnsupported) {
        const auto inner = MakeShared<InstantEngine>();
        for (const int percent : {0, 100}) {
            BlunderEngine engine(inner, percent);

            const auto answer = engine.Analyse(SearchRequest::FromPosition(Position::Start(), kLimits), CancellationToken::Create());

            ASSERT_FALSE(answer.has_value()) << percent;
            EXPECT_EQ(answer.error().kind, EngineErrorKind::Unsupported) << percent;
        }
        EXPECT_EQ(inner->Calls(), 0) << "the double has no Analyse, so FindMove was not asked either";
    }

    TEST(BlunderEngineTest, BlunderEngineAsyncKeepsTheBlunderRuleAndForwards) {
        // The doubles never suspend, so a task driven on the test thread runs to its end
        const Vector<SearchRequest> requests = SeededRequests(60);
        for (const int percent : {0, 40, 100}) {
            const auto inner = MakeShared<InstantEngine>();
            BlunderEngine engine(inner, percent);
            size_t blunders = 0;

            for (size_t i = 0; i < requests.size(); ++i) {
                const int before = inner->Calls();
                const auto expected = Ask(engine, requests[i]);
                const bool blundered = inner->Calls() == before;
                blunders += blundered ? 1 : 0;
                const int afterBlocking = inner->Calls();

                auto task = engine.FindMoveAsync(requests[i], CancellationToken::Create());
                const auto result = task.GetResult();

                ASSERT_TRUE(result.has_value()) << percent << " " << i;
                EXPECT_EQ(Found(*result), Found(expected)) << percent << " " << i;
                EXPECT_EQ(inner->Calls() == afterBlocking, blundered) << percent << " " << i << ": a blunder does not ask the wrapped engine";
            }
            if (percent == 40) {
                EXPECT_GT(blunders, 0u);
                EXPECT_LT(blunders, requests.size());
            }
        }

        // The errors of the blocking call, at 0 and at 100 percent
        for (const int percent : {0, 100}) {
            BlunderEngine engine(MakeShared<InstantEngine>(), percent);
            auto mated = engine.FindMoveAsync(SearchRequest::FromPosition(PositionFromFen(kMated), kLimits), CancellationToken::Create());
            const auto mateResult = mated.GetResult();
            ASSERT_TRUE(mateResult.has_value());
            ASSERT_FALSE(mateResult->has_value()) << percent;
            EXPECT_EQ(mateResult->error().kind, EngineErrorKind::NoMove) << percent;
        }
        {
            BlunderEngine engine(MakeShared<InstantEngine>(), 100);
            CancellationToken cancelled = CancellationToken::Create();
            cancelled.Cancel();
            auto task = engine.FindMoveAsync(SearchRequest::FromPosition(Position::Start(), kLimits), cancelled);
            const auto result = task.GetResult();
            ASSERT_TRUE(result.has_value());
            ASSERT_FALSE(result->has_value());
            EXPECT_EQ(result->error().kind, EngineErrorKind::Cancelled);
        }

        // AnalyseAsync is the inner engine's, whatever the percent
        SearchRequest request = SearchRequest::FromPosition(Position::Start(), kLimits);
        request.lines = 3;
        for (const int percent : {0, 50, 100}) {
            const auto inner = MakeShared<AnalysingEngine>();
            BlunderEngine engine(inner, percent);
            const auto direct = inner->Analyse(request, CancellationToken::Create());
            ASSERT_TRUE(direct.has_value());

            auto task = engine.AnalyseAsync(request, CancellationToken::Create());
            const auto result = task.GetResult();

            ASSERT_TRUE(result.has_value()) << percent;
            ASSERT_TRUE(result->has_value()) << percent;
            EXPECT_EQ((*result)->Best().move, direct->Best().move) << percent;
            EXPECT_EQ((*result)->LineCount(), direct->LineCount()) << percent;
            EXPECT_EQ(inner->LastRequest().lines, 3) << percent;
            EXPECT_EQ(inner->Calls(), 2) << percent;
        }
    }

}  // namespace BECore::Chess::Tests
