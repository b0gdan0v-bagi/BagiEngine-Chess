#include <BECore/Control/Json.h>
#include <Chess/ChessGame.h>
#include <Common/EngineEnvironment.h>
#include <tests/ChessTestUtil.h>
#include <tests/TestEngines.h>

/// The "review" member of the control state, set_state {"review": ..} and the review's share of the state version, over a real ChessGame (the adapter is wired by it) whose analyst
/// asks a scripted double: every position is worth +35 centipawns to the side to move there and its best move is the first legal one, so the losses are known without a search. The frames are
/// the analyst's Update(), as ChessBoardComponent calls it.
namespace BECore::Chess::Tests {

    namespace {

        /// Black solves: White's setup b2b3, then Ra8a1# ends the puzzle and the game
        constexpr StringView kBlackSolves = "black\tr3k3/8/8/8/8/8/1P3PPP/6K1 w - - 0 1\tb2b3 a8a1\t1000\tmateIn1\n";
        constexpr StringView kMatedFen = "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3";  // fool's mate: White is mated
        constexpr size_t kFoolsMateMoves = 4;
        constexpr size_t kFoolsMateSearched = 4;  // the mated position is not searched

        class ChessGameControlReviewTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                ::BECore::Tests::EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                game.GetAnalyst().SetEngineFactory(factory.AsFactory());
                String tsv = "id\tfen\tmoves\trating\tthemes\n";
                tsv.append(kBlackSolves.data(), kBlackSolves.size());
                game.SetPuzzles(PuzzleSet::Parse(StringView(tsv.data(), tsv.size())));
            }

            IControllableGame& Control() {
                return *game.AsControllable();
            }

            Json State() {
                auto state = Control().GetState();
                EXPECT_TRUE(state.has_value());
                return state ? *state : Json();
            }

            /// The "review" member as the state has it (null: no review)
            Json Review() {
                const Json state = State();
                const Json* found = FindJsonMember(state, "review");
                EXPECT_NE(found, nullptr) << "the member is there, null or not";
                return found ? *found : Json();
            }

            static String Text(const Json& json, StringView key) {
                const auto value = GetJsonString(json, key);
                return value ? String(value->c_str(), value->size()) : String("<missing>");
            }

            static int64_t Number(const Json& json, StringView key) {
                const Optional<int64_t> value = GetJsonInt(json, key);
                return value ? *value : -1;
            }

            static const Json& Member(const Json& json, StringView key) {
                static const Json kMissing;
                const Json* found = FindJsonMember(json, key);
                EXPECT_NE(found, nullptr) << String(key).c_str();
                return found ? *found : kMissing;
            }

            bool Move(const char* uci) {
                Json action = Json::object();
                action["type"] = "move";
                action["uci"] = uci;
                return Control().PerformAction(action).has_value();
            }

            /// The UCI moves of the state
            Vector<String> PlayedMoves() {
                Vector<String> moves;
                const Json state = State();
                for (const Json& move : Member(state, "moves")) {
                    moves.push_back(FromStdString(move.get<std::string>()));  //-NoStd: nlohmann's string, the JSON boundary
                }
                return moves;
            }

            bool PlayFoolsMate() {
                for (const char* uci : {"f2f3", "e7e5", "g2g4", "d8h4"}) {
                    if (!Move(uci)) {
                        return false;
                    }
                }
                return game.GetModel().IsGameOver();
            }

            /// {"review": {@p key: @p value}}
            static Json ReviewRequest(const char* key, const Json& value) {
                Json body = Json::object();
                body[key] = value;
                Json request = Json::object();
                request["review"] = std::move(body);
                return request;
            }

            std::expected<void, ControlError> StartReview() {
                return Control().SetState(ReviewRequest("start", Json(true)));
            }

            std::expected<void, ControlError> CancelReview() {
                return Control().SetState(ReviewRequest("cancel", Json(true)));
            }

            /// What a game loop does: the analyst's frame, until @p done
            template <typename Condition>
            bool FrameUntil(Condition done) {
                return WaitUntil([&] {
                    game.GetAnalyst().Update(game.GetModel(), game.GetOpponent().GetEngine());
                    return done();
                });
            }

            bool FrameUntilDone() {
                return FrameUntil([&] { return Text(Review(), "state") == "done"; });
            }

            /// The call is refused as invalid_params and the state, the version included, is what it was
            void ExpectRefused(const Json& request, const char* what) {
                const String before = FromStdString(State().dump()) + " v" + ToString(Control().GetStateVersion());
                const auto result = Control().SetState(request);
                ASSERT_FALSE(result.has_value()) << what;
                EXPECT_EQ(String(result.error().code.c_str(), result.error().code.size()), "invalid_params") << what;
                EXPECT_EQ(FromStdString(State().dump()) + " v" + ToString(Control().GetStateVersion()), before) << what;
            }

            CountingAnalysisFactory factory{New<AnalysingEngine>()};
            ChessGame game;
        };

    }  // namespace

    TEST_F(ChessGameControlReviewTest, TheReviewIsNullAtTheStartAndOnAGameThatIsNotFinished) {
        EXPECT_TRUE(Review().is_null());
        ASSERT_TRUE(Move("e2e4"));
        EXPECT_TRUE(Review().is_null());
    }

    TEST_F(ChessGameControlReviewTest, AStartThatIsNotAllowedIsInvalidParamsAndChangesNothing) {
        ExpectRefused(ReviewRequest("start", Json(true)), "the game is not finished");
        ASSERT_TRUE(Move("e2e4"));
        ExpectRefused(ReviewRequest("start", Json(true)), "an ongoing game with moves");

        ASSERT_TRUE(Control()
                        .SetState([] {
                            Json fen = Json::object();
                            fen["fen"] = MakeJsonString(kMatedFen);
                            return fen;
                        }())
                        .has_value());
        ASSERT_TRUE(game.GetModel().IsGameOver());
        ExpectRefused(ReviewRequest("start", Json(true)), "a finished game with no moves");
        EXPECT_EQ(factory.Calls(), 0);
    }

    TEST_F(ChessGameControlReviewTest, AStartOnAPuzzleThatIsOnIsRefused) {
        Json puzzle = Json::object();
        puzzle["id"] = "black";
        Json begin = Json::object();
        begin["puzzle"] = puzzle;
        ASSERT_TRUE(Control().SetState(begin).has_value());
        ASSERT_TRUE(Move("a8a1"));
        ASSERT_TRUE(game.GetModel().IsGameOver());
        ASSERT_TRUE(game.GetPuzzle().IsActiveFor(game.GetModel()));

        ExpectRefused(ReviewRequest("start", Json(true)), "a puzzle is on");
    }

    TEST_F(ChessGameControlReviewTest, AMalformedRequestIsInvalidParamsAndChangesNothing) {
        ASSERT_TRUE(PlayFoolsMate());
        Json twoMembers = Json::object();
        twoMembers["start"] = true;
        twoMembers["cancel"] = true;
        Json other = Json::object();
        other["pause"] = true;
        Json fenAlso = ReviewRequest("start", Json(true));
        fenAlso["fen"] = MakeJsonString(kMatedFen);
        Json analysisAlso = ReviewRequest("start", Json(true));
        analysisAlso["analysis"] = Json::object();
        Json puzzleAlso = ReviewRequest("start", Json(true));
        puzzleAlso["puzzle"] = Json::object();
        Json takeBackAlso = ReviewRequest("cancel", Json(true));
        takeBackAlso["takeBack"] = Json::object();

        Json notObject = Json::object();
        notObject["review"] = "start";
        Json emptyObject = Json::object();
        emptyObject["review"] = Json::object();
        Json twoMembersRequest = Json::object();
        twoMembersRequest["review"] = twoMembers;
        Json otherRequest = Json::object();
        otherRequest["review"] = other;

        for (const Json& request : {notObject, emptyObject, twoMembersRequest, otherRequest, ReviewRequest("start", Json(false)), ReviewRequest("cancel", Json(false)),
                                    ReviewRequest("start", Json("true")), ReviewRequest("start", Json(1)), fenAlso, analysisAlso, puzzleAlso, takeBackAlso}) {
            ExpectRefused(request, DumpJson(request).c_str());
        }
        EXPECT_TRUE(Review().is_null());
        EXPECT_EQ(factory.Calls(), 0);
    }

    TEST_F(ChessGameControlReviewTest, TheUsageOfAnEmptySetStateNamesTheReview) {
        const auto result = Control().SetState(Json::object());

        ASSERT_FALSE(result.has_value());
        EXPECT_NE(String(result.error().message.c_str()).find("review"), String::npos);
    }

    TEST_F(ChessGameControlReviewTest, AStartOnAFinishedGameRunsWithTheFramesAndEndsDone) {
        ASSERT_TRUE(PlayFoolsMate());
        const uint64_t before = Control().GetStateVersion();

        ASSERT_TRUE(StartReview().has_value());

        EXPECT_GT(Control().GetStateVersion(), before) << "a start is a change a push reports";
        const Json started = Review();
        ASSERT_TRUE(started.is_object());
        EXPECT_EQ(Text(started, "state"), "running");
        EXPECT_EQ(Number(started, "done"), 0);
        EXPECT_EQ(Number(started, "total"), static_cast<int64_t>(kFoolsMateSearched));
        EXPECT_EQ(Number(started, "depth"), kReviewDepth);
        EXPECT_TRUE(Member(started, "error").is_null());
        ASSERT_EQ(Member(started, "positions").size(), kFoolsMateMoves + 1);
        for (size_t index = 0; index < kFoolsMateSearched; ++index) {
            EXPECT_TRUE(Member(started, "positions")[index].is_null()) << index << ": nothing is searched before the first frame";
        }
        EXPECT_TRUE(Member(started, "positions")[kFoolsMateSearched].is_object()) << "the mated position needs no search";
        ASSERT_EQ(Member(started, "moves").size(), kFoolsMateMoves);

        const uint64_t running = Control().GetStateVersion();
        ASSERT_TRUE(FrameUntilDone());

        EXPECT_GT(Control().GetStateVersion(), running) << "positions that arrive move the version";
        const Json done = Review();
        EXPECT_EQ(Number(done, "done"), static_cast<int64_t>(kFoolsMateSearched));
        EXPECT_EQ(Number(done, "total"), static_cast<int64_t>(kFoolsMateSearched));
        EXPECT_TRUE(Member(done, "error").is_null());
        EXPECT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Engines()[0], "own");
    }

    TEST_F(ChessGameControlReviewTest, ThePositionsAreInWhitesViewWithTheBestMoveAndTheMatedOneHasNone) {
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(StartReview().has_value());
        ASSERT_TRUE(FrameUntilDone());

        const Json positions = Member(Review(), "positions");
        ASSERT_EQ(positions.size(), kFoolsMateMoves + 1);
        for (size_t index = 0; index < kFoolsMateSearched; ++index) {
            const Json& position = positions[index];
            ASSERT_TRUE(position.is_object()) << index;
            const Json& score = Member(position, "score");
            EXPECT_EQ(Text(score, "kind"), "centipawns") << index;
            const bool whiteToMove = index % 2 == 0;
            EXPECT_EQ(Number(score, "value"), whiteToMove ? 35 : -35) << index << ": +35 to the side to move, in White's view";
            EXPECT_EQ(Text(position, "text"), whiteToMove ? "+0.35" : "-0.35") << index;
            EXPECT_EQ(Text(position, "best").size(), 4u) << index << ": a UCI move";
        }
        const Json& mated = positions[kFoolsMateMoves];
        ASSERT_TRUE(mated.is_object());
        EXPECT_EQ(Text(Member(mated, "score"), "kind"), "mate");
        EXPECT_EQ(Number(Member(mated, "score"), "value"), 0);
        EXPECT_EQ(Text(mated, "text"), "#0");
        EXPECT_TRUE(Member(mated, "best").is_null());
    }

    TEST_F(ChessGameControlReviewTest, EveryMoveHasItsLossAndQualityAgainstTheBestMoveOfItsPosition) {
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(StartReview().has_value());
        ASSERT_TRUE(FrameUntilDone());

        const Vector<String> played = PlayedMoves();
        const Json review = Review();
        const Json& moves = Member(review, "moves");
        ASSERT_EQ(moves.size(), kFoolsMateMoves);
        for (size_t index = 0; index < kFoolsMateMoves; ++index) {
            const Json& move = moves[index];
            ASSERT_TRUE(move.is_object()) << index;
            const String best = Text(move, "best");
            EXPECT_EQ(best.size(), 4u) << index;
            const String quality = Text(move, "quality");
            if (played[index] == best) {
                EXPECT_EQ(quality, "best") << index;
                EXPECT_EQ(Number(move, "loss"), 0) << index;
            } else if (index + 1 < kFoolsMateMoves) {
                EXPECT_EQ(Number(move, "loss"), 70) << index << ": +35 for the best move, -35 for the side that moved after it";
                EXPECT_EQ(quality, "inaccuracy") << index;
            } else {
                EXPECT_EQ(Number(move, "loss"), 0) << index << ": mating is never a loss";
            }
        }
    }

    TEST_F(ChessGameControlReviewTest, TheMovesAreNullUntilBothOfTheirPositionsAreThere) {
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(StartReview().has_value());

        ASSERT_TRUE(FrameUntil([&] {
            const Json review = Review();
            const Json& positions = Member(review, "positions");
            const Json& moves = Member(review, "moves");
            for (size_t index = 0; index < moves.size(); ++index) {
                EXPECT_EQ(moves[index].is_object(), positions[index].is_object() && positions[index + 1].is_object()) << index;
            }
            return Text(review, "state") == "done";
        }));
        EXPECT_TRUE(Member(Review(), "moves")[kFoolsMateMoves - 1].is_object());
    }

    TEST_F(ChessGameControlReviewTest, AStartAgainWhileItRunsOrIsDoneChangesNothing) {
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(StartReview().has_value());
        const uint64_t running = Control().GetStateVersion();
        ASSERT_TRUE(StartReview().has_value());
        EXPECT_EQ(Control().GetStateVersion(), running);

        ASSERT_TRUE(FrameUntilDone());
        const uint64_t done = Control().GetStateVersion();
        const Json before = Review();
        ASSERT_TRUE(StartReview().has_value());
        EXPECT_EQ(Control().GetStateVersion(), done);
        EXPECT_EQ(Review(), before);
        EXPECT_EQ(factory.Calls(), 1) << "no second engine, no second search";
    }

    TEST_F(ChessGameControlReviewTest, ACancelMakesTheReviewNullAndWithNoReviewDoesNothing) {
        ASSERT_TRUE(PlayFoolsMate());
        const uint64_t idle = Control().GetStateVersion();
        ASSERT_TRUE(CancelReview().has_value());
        EXPECT_EQ(Control().GetStateVersion(), idle) << "nothing to cancel";
        EXPECT_TRUE(Review().is_null());

        ASSERT_TRUE(StartReview().has_value());
        ASSERT_TRUE(FrameUntil([&] { return Number(Review(), "done") >= 1; }));
        const uint64_t running = Control().GetStateVersion();

        ASSERT_TRUE(CancelReview().has_value());

        EXPECT_TRUE(Review().is_null()) << "the partial result goes too";
        EXPECT_GT(Control().GetStateVersion(), running) << "a cancel is a change a push reports";
        game.GetAnalyst().Update(game.GetModel(), game.GetOpponent().GetEngine());
        EXPECT_TRUE(Review().is_null());
    }

    TEST_F(ChessGameControlReviewTest, ATakeBackAResetOrAFenEndsARunningOrDoneReviewAtOnce) {
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(StartReview().has_value());
        Json takeBack = Json::object();
        takeBack["takeBack"] = Json::object();
        ASSERT_TRUE(Control().SetState(takeBack).has_value());
        EXPECT_TRUE(Review().is_null()) << "a running review, a takeback";

        ASSERT_TRUE(Move("d8h4"));
        ASSERT_TRUE(StartReview().has_value());
        ASSERT_TRUE(FrameUntilDone());
        ASSERT_TRUE(Control().Reset(Json::object()).has_value());
        EXPECT_TRUE(Review().is_null()) << "a done review, a reset";

        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(StartReview().has_value());
        ASSERT_TRUE(FrameUntilDone());
        Json fen = Json::object();
        fen["fen"] = MakeJsonString(kMatedFen);
        ASSERT_TRUE(Control().SetState(fen).has_value());
        EXPECT_TRUE(Review().is_null()) << "a done review, a position set from outside";
    }

    TEST_F(ChessGameControlReviewTest, TheSameGameReviewedTwiceGivesTheSameJson) {
        ASSERT_TRUE(PlayFoolsMate());
        ASSERT_TRUE(StartReview().has_value());
        ASSERT_TRUE(FrameUntilDone());
        const Json first = Review();

        ASSERT_TRUE(CancelReview().has_value());
        ASSERT_TRUE(StartReview().has_value());
        ASSERT_TRUE(FrameUntilDone());

        EXPECT_EQ(Review(), first);
    }

}  // namespace BECore::Chess::Tests
