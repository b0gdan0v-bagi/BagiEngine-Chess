#include "ChessTestUtil.h"

#include <Chess/Engine/BlunderEngine.h>
#include <Chess/Engine/OpponentLevels.h>
#include <Chess/Engine/OwnEngine.h>
#include <Chess/Engine/UciProtocol.h>
#include <Chess/Game/MoveGenerator.h>
#include <Chess/Uci/UciClock.h>
#include <Chess/Uci/UciServer.h>
#include <chrono>
#include <thread>

/// UciServer: the lines it answers, over the real OwnEngine at a shallow depth and over a scripted engine that records its limits and can be held
/// until it is cancelled. No process, no TaskManager: the server's worker is a std::thread of its own
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kMated = "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1";
        constexpr const char* kStalemated = "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1";

        constexpr auto kPollStep = std::chrono::milliseconds(1);
        constexpr auto kWaitBudget = std::chrono::seconds(10);
        constexpr int kShallow = 2;
        constexpr int kBlockedDepth = 4;

        /// The words a server line may begin with
        constexpr const char* kKnownKeywords[] = {"id", "option", "uciok", "readyok", "bestmove", "info"};

        bool StartsWith(const eastl::string& text, eastl::string_view prefix) {
            return eastl::string_view(text.data(), text.size()).starts_with(prefix);
        }

        /// An engine of the tests: records every limit it is asked with, answers the legal move numbered by the depth (so the answer of each depth is told apart), and
        /// from a depth on holds the call until the token is cancelled (a search that does not end), or fails every call
        class ScriptedEngine final : public IChessEngine {
        public:
            /// 0: never held. Else a call with a depth of at least this one waits for the cancel and answers Cancelled
            void HoldFromDepth(int depth) {
                _holdFromDepth = depth;
            }

            void FailWith(EngineErrorKind kind) {
                _failure = kind;
            }

            [[nodiscard]] eastl::vector<SearchLimits> Calls() const {
                const std::lock_guard lock(_mutex);
                return _calls;
            }

            [[nodiscard]] bool WaitForCall(int depth) const {
                return WaitUntil([&] {
                    for (const SearchLimits& limits : Calls()) {
                        if (limits.depth == depth) {
                            return true;
                        }
                    }
                    return false;
                });
            }

            /// The move this engine answers for @p depth in the game of @p request
            static Move MoveFor(const SearchRequest& request, int depth) {
                const MoveList legal = MoveGenerator::GenerateLegal(*request.FinalPosition());
                return legal[static_cast<size_t>(std::max(depth, 1) - 1) % legal.size()];
            }

            std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken& token) override {
                return Answer(request, token);
            }

            std::expected<Analysis, EngineError> Analyse(const SearchRequest& request, const CancellationToken& token) override {
                auto move = Answer(request, token);
                if (!move) {
                    return std::unexpected(std::move(move.error()));
                }
                return Analysis{{AnalysisLine{*move, AnalysisScore{}, request.limits.depth}}};
            }

            template <class Condition>
            static bool WaitUntil(Condition condition) {
                const auto end = std::chrono::steady_clock::now() + kWaitBudget;
                while (!condition()) {
                    if (std::chrono::steady_clock::now() > end) {
                        return false;
                    }
                    std::this_thread::sleep_for(kPollStep);
                }
                return true;
            }

        private:
            std::expected<Move, EngineError> Answer(const SearchRequest& request, const CancellationToken& token) {
                {
                    const std::lock_guard lock(_mutex);
                    _calls.push_back(request.limits);
                }
                if (_failure) {
                    return std::unexpected(EngineError{*_failure, "the scripted engine fails"});
                }
                const auto position = request.FinalPosition();
                if (!position || MoveGenerator::GenerateLegal(*position).empty()) {
                    return std::unexpected(EngineError{EngineErrorKind::NoMove, "no move"});
                }
                if (_holdFromDepth > 0 && request.limits.depth >= _holdFromDepth) {
                    while (!token.IsCancelled()) {
                        std::this_thread::sleep_for(kPollStep);
                    }
                    return std::unexpected(EngineError{EngineErrorKind::Cancelled, "cancelled"});
                }
                return MoveFor(request, request.limits.depth);
            }

            mutable std::mutex _mutex;
            eastl::vector<SearchLimits> _calls;
            std::atomic<int> _holdFromDepth = 0;
            Optional<EngineErrorKind> _failure;
        };

        /// A server and the lines it printed. The lines are declared first, so that they outlive the server (its worker may still print while it is destroyed)
        class Session {
        public:
            explicit Session(eastl::shared_ptr<IChessEngine> engine = eastl::make_shared<OwnEngine>())
                : _server(std::move(engine), [this](eastl::string_view line) {
                      const std::lock_guard lock(_mutex);
                      _lines.emplace_back(line.data(), line.size());
                  }) {}

            void Send(eastl::string_view line) {
                _server.HandleLine(line);
            }

            /// Sends the lines, each one waiting until the search it started has printed its `bestmove` (so a `go` is never refused for a search still running). Only
            /// for searches that end by themselves
            void Play(std::initializer_list<const char*> lines) {
                for (const char* line : lines) {
                    Send(line);
                    _server.WaitIdle();
                }
            }

            [[nodiscard]] eastl::vector<eastl::string> Lines() const {
                const std::lock_guard lock(_mutex);
                return _lines;
            }

            [[nodiscard]] bool WaitForLine(eastl::string_view prefix) const {
                return ScriptedEngine::WaitUntil([&] { return CountOf(prefix) > 0; });
            }

            [[nodiscard]] size_t CountOf(eastl::string_view prefix) const {
                size_t count = 0;
                for (const eastl::string& line : Lines()) {
                    count += StartsWith(line, prefix) ? 1 : 0;
                }
                return count;
            }

            /// The moves of the `bestmove` lines, in order ("(none)" as it is)
            [[nodiscard]] eastl::vector<eastl::string> BestMoves() const {
                eastl::vector<eastl::string> moves;
                for (const eastl::string& line : Lines()) {
                    if (const auto parsed = UciProtocol::ParseBestMove(eastl::string_view(line.data(), line.size()))) {
                        moves.push_back(parsed->move.empty() ? eastl::string("(none)") : parsed->move);
                    }
                }
                return moves;
            }

            [[nodiscard]] eastl::string OnlyBestMove() const {
                const eastl::vector<eastl::string> moves = BestMoves();
                EXPECT_EQ(moves.size(), 1u);
                return moves.empty() ? eastl::string() : moves.front();
            }

            Uci::UciServer& Server() {
                return _server;
            }

        private:
            mutable std::mutex _mutex;
            eastl::vector<eastl::string> _lines;
            Uci::UciServer _server;
        };

        /// The request the tests ask a base engine, the game of `position startpos moves ...`
        SearchRequest StartRequest(std::initializer_list<const char*> moves, SearchLimits limits) {
            SearchRequest request;
            request.startFen.assign(Position::kStartFen.data(), Position::kStartFen.size());
            for (const char* move : moves) {
                request.moves.push_back(Mv(move));
            }
            request.limits = limits;
            return request;
        }

        /// The move that BlunderEngine over OwnEngine answers for @p request
        eastl::string BlunderAnswer(int percent, const SearchRequest& request) {
            BlunderEngine engine(eastl::make_shared<OwnEngine>(), percent);
            const auto move = engine.FindMove(request, CancellationToken::Create());
            EXPECT_TRUE(move.has_value());
            return move ? move->ToUci() : eastl::string();
        }

        bool IsLegalIn(const SearchRequest& request, const eastl::string& uci) {
            const auto position = request.FinalPosition();
            return position && MoveGenerator::ParseUci(*position, eastl::string_view(uci.data(), uci.size())).has_value();
        }

        eastl::shared_ptr<ScriptedEngine> MakeScripted() {
            return eastl::make_shared<ScriptedEngine>();
        }

    }  // namespace

    TEST(UciServerTest, UciAnswersIdOptionsAndEndsWithUciok) {
        Session session;
        session.Send("uci");
        const eastl::vector<eastl::string> lines = session.Lines();
        ASSERT_EQ(lines.size(), 6u);
        EXPECT_EQ(lines[0], "id name BagiChess Own");
        EXPECT_TRUE(StartsWith(lines[1], "id author "));
        EXPECT_EQ(lines[2], "option name Depth type spin default 0 min 0 max 64");
        EXPECT_EQ(lines[3], "option name BlunderPercent type spin default 0 min 0 max 100");
        EXPECT_EQ(lines[4], "option name Level type combo default custom var custom var easy var medium var hard var max");
        EXPECT_EQ(lines[5], "uciok");
    }

    TEST(UciServerTest, IsReadyAnswersReadyok) {
        Session session;
        session.Send("isready");
        EXPECT_EQ(session.Lines(), eastl::vector<eastl::string>{"readyok"});
    }

    TEST(UciServerTest, UnknownLinesAreSkippedInSilence) {
        Session session;
        session.Play({"", "foo bar", "   ", "ponderhit", "stop", "position", "position nothing"});
        EXPECT_TRUE(session.Lines().empty());
    }

    TEST(UciServerTest, GoAfterMovesAnswersALegalMoveOfTheSideToMove) {
        Session session;
        session.Play({"position startpos moves e2e4 e7e5", "go depth 2"});
        const eastl::string move = session.OnlyBestMove();
        EXPECT_TRUE(IsLegalIn(StartRequest({"e2e4", "e7e5"}, SearchLimits{0, kShallow}), move)) << move.c_str();
    }

    TEST(UciServerTest, TheMoveIsReadByTheClientSideOfTheProtocol) {
        Session session;
        session.Play({"position startpos moves e2e4", "go depth 1"});
        const eastl::vector<eastl::string> lines = session.Lines();
        ASSERT_EQ(lines.size(), 1u);
        const auto parsed = UciProtocol::ParseBestMove(eastl::string_view(lines[0].data(), lines[0].size()));
        ASSERT_TRUE(parsed.has_value());
        EXPECT_FALSE(parsed->IsNone());
    }

    TEST(UciServerTest, AMateInOneIsFound) {
        Session session;
        session.Play({"position fen 6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1", "go depth 2"});
        EXPECT_EQ(session.OnlyBestMove(), "a1a8");
    }

    TEST(UciServerTest, TheSameGameAndDepthGiveTheSameMove) {
        Session session;
        session.Play({"position startpos moves d2d4 g8f6 c2c4", "go depth 3", "go depth 3"});
        const eastl::vector<eastl::string> moves = session.BestMoves();
        ASSERT_EQ(moves.size(), 2u);
        EXPECT_EQ(moves[0], moves[1]);
    }

    TEST(UciServerTest, ABadPositionIsRefusedAndTheOldOneStays) {
        Session session;
        session.Play({"position startpos moves e2e4", "position startpos moves e2e5", "position fen not a fen", "position startpos moves e2e4 e2e4", "go depth 1"});
        EXPECT_EQ(session.CountOf("info string "), 3u);
        // The game is still the one after e2e4: the answer is a move of Black
        EXPECT_TRUE(IsLegalIn(StartRequest({"e2e4"}, SearchLimits{0, 1}), session.OnlyBestMove()));
    }

    TEST(UciServerTest, UciNewGameForgetsTheMovesAndKeepsTheOptions) {
        Session session;
        session.Play({"setoption name BlunderPercent value 100", "position startpos moves e2e4", "ucinewgame", "go depth 2"});
        EXPECT_EQ(session.OnlyBestMove(), BlunderAnswer(100, StartRequest({}, SearchLimits{0, kShallow})));
    }

    TEST(UciServerTest, BlunderPercentPlaysTheMoveOfBlunderEngine) {
        const SearchRequest request = StartRequest({"e2e4", "e7e5", "g1f3"}, SearchLimits{0, kShallow});
        Session session;
        session.Play({"setoption name BlunderPercent value 100", "position startpos moves e2e4 e7e5 g1f3", "go depth 2"});
        EXPECT_EQ(session.OnlyBestMove(), BlunderAnswer(100, request));
    }

    TEST(UciServerTest, OptionNamesMatchWithoutRegardToCase) {
        const SearchRequest request = StartRequest({"e2e4"}, SearchLimits{0, kShallow});
        Session session;
        session.Play({"setoption name blunderpercent value 100", "position startpos moves e2e4", "go depth 2"});
        EXPECT_EQ(session.CountOf("info string "), 0u);
        EXPECT_EQ(session.OnlyBestMove(), BlunderAnswer(100, request));
    }

    TEST(UciServerTest, ALevelPlaysAsTheGamePlaysIt) {
        const OpponentLevel& easy = GetOpponentLevels()[*FindOpponentLevel("easy")];
        ASSERT_GT(easy.ownDepth, 0);
        ASSERT_GT(easy.blunderPercent, 0);
        const eastl::vector<eastl::vector<const char*>> games = {{}, {"e2e4"}, {"d2d4", "d7d5"}, {"e2e4", "e7e5", "g1f3"}};
        for (const eastl::vector<const char*>& moves : games) {
            eastl::string position = "position startpos";
            SearchRequest request = StartRequest({}, SearchLimits{0, easy.ownDepth});
            if (!moves.empty()) {
                position.append(" moves");
            }
            for (const char* move : moves) {
                position = Format("{} {}", position, move);
                request.moves.push_back(Mv(move));
            }
            Session session;
            session.Play({"setoption name Level value easy", position.c_str(), "go"});
            EXPECT_EQ(session.OnlyBestMove(), BlunderAnswer(easy.blunderPercent, request)) << position.c_str();
        }
    }

    TEST(UciServerTest, TheLaterOptionWinsOverALevel) {
        const OpponentLevel& hard = GetOpponentLevels()[*FindOpponentLevel("hard")];
        auto engine = MakeScripted();
        Session session(engine);
        session.Play({"setoption name Level value hard", "go"});
        session.Play({"setoption name Depth value 3", "go"});
        session.Play({"setoption name Level value custom", "go"});
        session.Play({"setoption name Level value hard", "setoption name Level value custom", "go"});
        const eastl::vector<SearchLimits> calls = engine->Calls();
        ASSERT_EQ(calls.size(), 4u);
        EXPECT_EQ(calls[0].depth, hard.ownDepth);
        EXPECT_EQ(calls[0].moveTimeMs, 0);
        EXPECT_EQ(calls[1].depth, 3);
        EXPECT_EQ(calls[2].depth, 3);
        EXPECT_EQ(calls[3].depth, hard.ownDepth);

        // BlunderPercent set after a level replaces the level's share
        Session blunders;
        blunders.Play({"setoption name Level value easy", "setoption name BlunderPercent value 0", "position startpos moves e2e4", "go depth 2"});
        EXPECT_EQ(blunders.OnlyBestMove(), BlunderAnswer(0, StartRequest({"e2e4"}, SearchLimits{0, kShallow})));
    }

    TEST(UciServerTest, ABadOptionChangesNothing) {
        auto engine = MakeScripted();
        Session session(engine);
        session.Send("setoption name Depth value 5");
        for (const char* bad : {"setoption name Depth value x", "setoption name Depth value 65", "setoption name Depth value -1", "setoption name Depth", "setoption name Depth value 2.5",
                                "setoption name BlunderPercent value 101", "setoption name Hash value 16", "setoption name Level value legendary", "setoption"}) {
            session.Send(bad);
        }
        EXPECT_EQ(session.CountOf("info string option refused"), 9u);
        session.Play({"go"});
        ASSERT_EQ(engine->Calls().size(), 1u);
        EXPECT_EQ(engine->Calls()[0].depth, 5);
    }

    TEST(UciServerTest, TheClockOfTheSideToMoveSetsTheMoveTime) {
        auto engine = MakeScripted();
        Session session(engine);
        session.Play({"setoption name Depth value 3", "go wtime 60000 btime 1000"});
        session.Play({"position startpos moves e2e4", "go wtime 60000 btime 1000 binc 500 movestogo 10"});
        const eastl::vector<SearchLimits> calls = engine->Calls();
        ASSERT_EQ(calls.size(), 2u);
        EXPECT_EQ(calls[0].depth, 3);
        EXPECT_EQ(calls[0].moveTimeMs, Uci::MoveTimeFor(60000, 0, std::nullopt));
        EXPECT_EQ(calls[1].depth, 3);
        EXPECT_EQ(calls[1].moveTimeMs, Uci::MoveTimeFor(1000, 500, 10));
    }

    TEST(UciServerTest, AClockAloneIsABoundedSearchByTime) {
        auto engine = MakeScripted();
        Session session(engine);
        session.Play({"go wtime 10000 btime 10000 winc 100 binc 100"});
        const eastl::vector<SearchLimits> calls = engine->Calls();
        ASSERT_EQ(calls.size(), 1u);
        EXPECT_EQ(calls[0].depth, 0);
        EXPECT_EQ(calls[0].moveTimeMs, Uci::MoveTimeFor(10000, 100, std::nullopt));
    }

    TEST(UciServerTest, MoveTimeBeatsTheClockAndDepthTakesTheSmallerOfTwo) {
        auto engine = MakeScripted();
        Session session(engine);
        session.Play({"go movetime 50 wtime 60000 btime 60000"});
        session.Play({"setoption name Depth value 5", "go depth 3"});
        session.Play({"setoption name Depth value 2", "go depth 6"});
        const eastl::vector<SearchLimits> calls = engine->Calls();
        ASSERT_EQ(calls.size(), 3u);
        EXPECT_EQ(calls[0].moveTimeMs, 50);
        EXPECT_EQ(calls[0].depth, 0);
        EXPECT_EQ(calls[1].depth, 3);
        EXPECT_EQ(calls[2].depth, 2);
    }

    TEST(UciServerTest, InfiniteDeepensAndStopAnswersTheLastFinishedDepth) {
        auto engine = MakeScripted();
        engine->HoldFromDepth(kBlockedDepth);
        Session session(engine);
        session.Send("go infinite");
        ASSERT_TRUE(engine->WaitForCall(kBlockedDepth));
        EXPECT_TRUE(session.BestMoves().empty());
        session.Send("stop");
        session.Server().WaitIdle();
        EXPECT_EQ(session.OnlyBestMove(), ScriptedEngine::MoveFor(StartRequest({}, {}), kBlockedDepth - 1).ToUci());
    }

    TEST(UciServerTest, InfiniteEndsByItselfOnAMate) {
        Session session;
        session.Play({"position fen 6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1", "go infinite"});
        EXPECT_EQ(session.OnlyBestMove(), "a1a8");
    }

    TEST(UciServerTest, StopDuringABoundedSearchAnswersADepthOneMove) {
        auto engine = MakeScripted();
        engine->HoldFromDepth(2);
        Session session(engine);
        session.Send("go depth 5");
        ASSERT_TRUE(engine->WaitForCall(5));
        session.Send("stop");
        session.Server().WaitIdle();
        EXPECT_EQ(session.OnlyBestMove(), ScriptedEngine::MoveFor(StartRequest({}, {}), 1).ToUci());
        EXPECT_TRUE(engine->WaitForCall(1));
    }

    TEST(UciServerTest, PositionAndGoRightBehindStopAreNotRefused) {
        auto engine = MakeScripted();
        engine->HoldFromDepth(kBlockedDepth);
        Session session(engine);
        session.Send("go infinite");
        ASSERT_TRUE(engine->WaitForCall(kBlockedDepth));
        // No wait for the first bestmove: a GUI may send the three lines in one write
        session.Send("stop");
        session.Send("position startpos moves d2d4");
        session.Send("go depth 1");
        session.Server().WaitIdle();
        EXPECT_EQ(session.CountOf("info string "), 0u);
        const eastl::vector<eastl::string> moves = session.BestMoves();
        ASSERT_EQ(moves.size(), 2u);
        EXPECT_EQ(moves[0], ScriptedEngine::MoveFor(StartRequest({}, {}), kBlockedDepth - 1).ToUci());
        EXPECT_EQ(moves[1], ScriptedEngine::MoveFor(StartRequest({"d2d4"}, {}), 1).ToUci());
    }

    TEST(UciServerTest, PonderHitIsIgnoredAndTheSearchGoesOn) {
        auto engine = MakeScripted();
        engine->HoldFromDepth(2);
        Session session(engine);
        session.Send("go ponder wtime 60000 btime 60000");
        ASSERT_TRUE(engine->WaitForCall(2));
        session.Send("ponderhit");
        EXPECT_TRUE(session.Lines().empty());
        session.Send("stop");
        EXPECT_EQ(session.CountOf("bestmove"), 1u);
        EXPECT_EQ(session.CountOf("info string "), 0u);
    }

    TEST(UciServerTest, IsReadyIsAnsweredWhileASearchRuns) {
        auto engine = MakeScripted();
        engine->HoldFromDepth(2);
        Session session(engine);
        session.Send("go depth 5");
        ASSERT_TRUE(engine->WaitForCall(5));
        session.Send("isready");
        EXPECT_EQ(session.CountOf("readyok"), 1u);
        EXPECT_EQ(session.CountOf("bestmove"), 0u);
        session.Send("stop");
        session.Server().WaitIdle();
        EXPECT_EQ(session.CountOf("bestmove"), 1u);
    }

    TEST(UciServerTest, QuitDuringASearchReturnsWithOneAnswer) {
        auto engine = MakeScripted();
        engine->HoldFromDepth(2);
        Session session(engine);
        session.Send("go infinite");
        ASSERT_TRUE(engine->WaitForCall(2));
        EXPECT_FALSE(session.Server().WantsQuit());
        session.Send("quit");
        EXPECT_TRUE(session.Server().WantsQuit());
        EXPECT_EQ(session.OnlyBestMove(), ScriptedEngine::MoveFor(StartRequest({}, {}), 1).ToUci());
    }

    TEST(UciServerTest, CommandsThatChangeTheGameAreRefusedWhileASearchRuns) {
        auto engine = MakeScripted();
        engine->HoldFromDepth(2);
        Session session(engine);
        session.Send("go depth 5");
        ASSERT_TRUE(engine->WaitForCall(5));
        session.Send("position startpos moves d2d4");
        session.Send("ucinewgame");
        session.Send("setoption name Depth value 3");
        session.Send("go depth 6");
        EXPECT_EQ(session.CountOf("info string "), 4u);
        session.Send("stop");
        session.Server().WaitIdle();
        // One search ran, on the position it began with
        EXPECT_EQ(session.OnlyBestMove(), ScriptedEngine::MoveFor(StartRequest({}, {}), 1).ToUci());
        EXPECT_EQ(engine->Calls().size(), 2u);
    }

    TEST(UciServerTest, AFinishedGameAnswersNone) {
        for (const char* fen : {kMated, kStalemated}) {
            for (const char* go : {"go depth 2", "go infinite", "go movetime 50"}) {
                Session session;
                session.Play({Format("position fen {}", fen).c_str(), go});
                EXPECT_EQ(session.OnlyBestMove(), "(none)") << fen << " " << go;
                EXPECT_EQ(session.CountOf("info string "), 0u);
            }
        }
    }

    TEST(UciServerTest, AnEngineErrorIsReportedAndStillAnswersALegalMove) {
        auto engine = MakeScripted();
        engine->FailWith(EngineErrorKind::ProtocolError);
        Session session(engine);
        session.Play({"position startpos moves e2e4", "go depth 2"});
        EXPECT_EQ(session.CountOf("info string "), 1u);
        const SearchRequest request = StartRequest({"e2e4"}, {});
        EXPECT_EQ(session.OnlyBestMove(), MoveGenerator::GenerateLegal(*request.FinalPosition()).front().ToUci());
    }

    TEST(UciServerTest, ThePromotionIsSpelledWithItsPiece) {
        Session session;
        session.Play({"position fen 8/P6k/8/8/8/8/8/K7 w - - 0 1", "go depth 1"});
        const eastl::string move = session.OnlyBestMove();
        ASSERT_EQ(move.size(), 5u) << move.c_str();
        EXPECT_EQ(move, "a7a8q");
    }

    TEST(UciServerTest, EveryLineStartsWithAUciKeyword) {
        Session session;
        session.Play({"uci", "isready", "setoption name Foo value 1", "position startpos moves e2e5", "position startpos moves e2e4", "go depth 1", "position fen 6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1",
                      "go infinite", "ucinewgame", "go wtime 100 btime 100", "stop"});
        const eastl::vector<eastl::string> lines = session.Lines();
        EXPECT_GT(lines.size(), 6u);
        for (const eastl::string& line : lines) {
            bool known = false;
            for (const char* keyword : kKnownKeywords) {
                known = known || line == keyword || StartsWith(line, Format("{} ", keyword));
            }
            EXPECT_TRUE(known) << line.c_str();
            EXPECT_EQ(line.find('\n'), eastl::string::npos);
        }
        EXPECT_EQ(session.BestMoves().size(), 3u);
    }

}  // namespace BECore::Chess::Tests
