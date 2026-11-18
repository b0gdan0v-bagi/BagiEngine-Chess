#include <BECore/Control/Json.h>
#include <Chess/ChessGame.h>
#include <Common/EngineEnvironment.h>
#include <tests/ChessTestUtil.h>

/// The "puzzle" member of the control state, set_state {"puzzle": ..} and the puzzle's share of the state version, over a real ChessGame (the adapter is wired by it). The frames are
/// ChessGame::UpdatePuzzle() with game time given by hand: no scene, no sleep.
namespace BECore::Chess::Tests {

    namespace {

        /// White solves: Black's setup h8g8, then Ra1a7, the reply g8h8 and Rb2b8#
        constexpr StringView kMateInTwo = "ladder\t7k/8/8/8/8/8/1R6/R6K b - - 0 1\th8g8 a1a7 g8h8 b2b8\t1200\tmateIn2 short\n";
        /// Black solves: White's setup b2b3, then Ra8a1#
        constexpr StringView kBlackSolves = "black\tr3k3/8/8/8/8/8/1P3PPP/6K1 w - - 0 1\tb2b3 a8a1\t1000\tmateIn1\n";

        constexpr float kFrame = 0.25f;  ///< a power of two, so that two frames are exactly kPuzzleReplySeconds

        class ChessGameControlPuzzleTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                ::BECore::Tests::EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                String tsv = "id\tfen\tmoves\trating\tthemes\n";
                for (const StringView row : {kMateInTwo, kBlackSolves}) {
                    tsv.append(row.data(), row.size());
                }
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

            Json PuzzleOf(const Json& state) {
                const Json* found = FindJsonMember(state, "puzzle");
                EXPECT_NE(found, nullptr) << "the member is there, null or not";
                return found ? *found : Json();
            }

            Json Puzzle() {
                return PuzzleOf(State());
            }

            static String Text(const Json& json, StringView key) {
                const auto value = GetJsonString(json, key);
                return value ? String(value->c_str(), value->size()) : String("<missing>");
            }

            static int64_t Number(const Json& json, StringView key) {
                const Optional<int64_t> value = GetJsonInt(json, key);
                return value ? *value : -1;
            }

            static bool Flag(const Json& json, StringView key) {
                const Json* found = FindJsonMember(json, key);
                EXPECT_NE(found, nullptr) << String(key).c_str();
                return found && found->is_boolean() && found->get<bool>();
            }

            /// set_state {"puzzle": @p body}
            std::expected<void, ControlError> SetPuzzle(const Json& body) {
                Json request = Json::object();
                request["puzzle"] = body;
                return Control().SetState(request);
            }

            std::expected<void, ControlError> SetPuzzle(const char* key, const Json& value) {
                Json body = Json::object();
                body[key] = value;
                return SetPuzzle(body);
            }

            void ExpectInvalid(const std::expected<void, ControlError>& result, const char* what) {
                ASSERT_FALSE(result.has_value()) << what;
                EXPECT_EQ(String(result.error().code.c_str(), result.error().code.size()), "invalid_params") << what;
            }

            bool Move(const char* uci) {
                Json action = Json::object();
                action["type"] = "move";
                action["uci"] = uci;
                return Control().PerformAction(action).has_value();
            }

            void Frames(int count) {
                for (int frame = 0; frame < count; ++frame) {
                    game.UpdatePuzzle(kFrame);
                }
            }

            ChessGame game;
        };

    }  // namespace

    TEST_F(ChessGameControlPuzzleTest, ThePuzzleIsNullInAPlainGameAndTheObjectWhileOneIsOn) {
        EXPECT_TRUE(Puzzle().is_null());

        ASSERT_TRUE(SetPuzzle("index", Json(0)).has_value());

        const Json state = State();
        const Json puzzle = PuzzleOf(state);
        ASSERT_TRUE(puzzle.is_object());
        EXPECT_EQ(Text(puzzle, "id"), "ladder");
        EXPECT_EQ(Number(puzzle, "index"), 0);
        EXPECT_EQ(Number(puzzle, "count"), 2);
        EXPECT_EQ(Number(puzzle, "rating"), 1200);
        const Json* themes = FindJsonMember(puzzle, "themes");
        ASSERT_NE(themes, nullptr);
        ASSERT_TRUE(themes->is_array());
        ASSERT_EQ(themes->size(), 2u);
        EXPECT_EQ((*themes)[0], "mateIn2");
        EXPECT_EQ((*themes)[1], "short");
        EXPECT_EQ(Text(puzzle, "solver"), "white");
        EXPECT_EQ(Text(puzzle, "state"), "your_move");
        EXPECT_EQ(Number(puzzle, "mistakes"), 0);
        EXPECT_FALSE(Flag(puzzle, "wrong"));
        EXPECT_EQ(Number(puzzle, "step"), 0);
        EXPECT_EQ(Number(puzzle, "steps"), 2);
        EXPECT_EQ(Text(state, "startFen"), "7k/8/8/8/8/8/1R6/R6K b - - 0 1") << "the row's position before the setup move";
        EXPECT_EQ(Text(state, "turn"), "white");
        EXPECT_EQ(Number(state, "takeBackPlies"), 0) << "a takeback would break the line";
        const Json* moves = FindJsonMember(state, "moves");
        ASSERT_NE(moves, nullptr);
        ASSERT_EQ(moves->size(), 1u);
        EXPECT_EQ((*moves)[0], "h8g8");
    }

    TEST_F(ChessGameControlPuzzleTest, TheSolverOfAPuzzleThatBlackSolvesIsBlack) {
        ASSERT_TRUE(SetPuzzle("id", Json("black")).has_value());

        const Json puzzle = Puzzle();
        EXPECT_EQ(Text(puzzle, "solver"), "black");
        EXPECT_EQ(Number(puzzle, "index"), 1);
        EXPECT_EQ(Text(State(), "turn"), "black");
    }

    TEST_F(ChessGameControlPuzzleTest, EveryFormOfSetStateBeginsNextRetriesAndEnds) {
        ASSERT_TRUE(SetPuzzle("index", Json(1)).has_value());
        EXPECT_EQ(Text(Puzzle(), "id"), "black");

        ASSERT_TRUE(SetPuzzle("next", Json(true)).has_value());
        EXPECT_EQ(Text(Puzzle(), "id"), "ladder") << "the first after the last";

        ASSERT_TRUE(SetPuzzle("id", Json("black")).has_value()) << "a puzzle that is on is replaced";
        EXPECT_EQ(Text(Puzzle(), "id"), "black");

        ASSERT_TRUE(Move("a8a1"));
        game.UpdatePuzzle(kFrame);
        EXPECT_EQ(Text(Puzzle(), "state"), "solved");
        ASSERT_TRUE(SetPuzzle("retry", Json(true)).has_value());
        EXPECT_EQ(Text(Puzzle(), "state"), "your_move");
        EXPECT_EQ(State()["moves"].size(), 1u);

        ASSERT_TRUE(SetPuzzle("end", Json(true)).has_value());
        const Json state = State();
        EXPECT_TRUE(PuzzleOf(state).is_null());
        EXPECT_EQ(state["moves"].size(), 0u) << "a plain reset";
        EXPECT_EQ(Text(state, "fen"), String(Position::kStartFen.data(), Position::kStartFen.size()));
    }

    TEST_F(ChessGameControlPuzzleTest, EveryRefusalIsInvalidParamsAndChangesNothing) {
        ASSERT_TRUE(Move("e2e4"));
        const uint64_t version = Control().GetStateVersion();
        const String fen = Text(State(), "fen");

        ExpectInvalid(SetPuzzle("index", Json(2)), "an index past the end");
        ExpectInvalid(SetPuzzle("index", Json(-1)), "a negative index");
        ExpectInvalid(SetPuzzle("index", Json("0")), "an index that is a string");
        ExpectInvalid(SetPuzzle("index", Json(0.5)), "an index that is no integer");
        ExpectInvalid(SetPuzzle("id", Json("nobody")), "an unknown id");
        ExpectInvalid(SetPuzzle("id", Json(0)), "an id that is no string");
        ExpectInvalid(SetPuzzle("next", Json(true)), "next with no puzzle on");
        ExpectInvalid(SetPuzzle("retry", Json(true)), "retry with no puzzle on");
        ExpectInvalid(SetPuzzle("end", Json(true)), "end with no puzzle on");
        ExpectInvalid(SetPuzzle("next", Json(false)), "false is not a way to say nothing");
        ExpectInvalid(SetPuzzle("next", Json("yes")), "next that is no boolean");
        ExpectInvalid(SetPuzzle("skip", Json(true)), "a member that does not exist");
        ExpectInvalid(SetPuzzle(Json::object()), "an empty object");
        ExpectInvalid(SetPuzzle(Json(3)), "puzzle that is no object");
        Json two = Json::object();
        two["index"] = 0;
        two["next"] = true;
        ExpectInvalid(SetPuzzle(two), "two members");

        EXPECT_EQ(Control().GetStateVersion(), version);
        EXPECT_EQ(Text(State(), "fen"), fen);
        EXPECT_TRUE(Puzzle().is_null());
    }

    TEST_F(ChessGameControlPuzzleTest, AnEmptySetBeginsNothing) {
        game.SetPuzzles(PuzzleSet());
        ExpectInvalid(SetPuzzle("index", Json(0)), "no puzzle to begin");
        ExpectInvalid(SetPuzzle("next", Json(true)), "no puzzle to go on from");
        EXPECT_TRUE(Puzzle().is_null());
    }

    TEST_F(ChessGameControlPuzzleTest, PuzzleIsAloneInItsCall) {
        Json fen = Json::object();
        fen["puzzle"] = Json::object();
        fen["puzzle"]["index"] = 0;
        fen["fen"] = String(Position::kStartFen.data(), Position::kStartFen.size()).c_str();
        ExpectInvalid(Control().SetState(fen), "puzzle with fen");

        for (const char* other : {"opponent", "analysis", "takeBack", "resign", "draw"}) {
            Json request = Json::object();
            request["puzzle"] = Json::object();
            request["puzzle"]["index"] = 0;
            request[other] = Json::object();
            ExpectInvalid(Control().SetState(request), other);
        }
        EXPECT_TRUE(Puzzle().is_null());
        EXPECT_TRUE(State()["moves"].empty());
    }

    TEST_F(ChessGameControlPuzzleTest, TakeBackResignAndDrawAreRefusedWhileOneIsOn) {
        ASSERT_TRUE(SetPuzzle("index", Json(0)).has_value());
        const uint64_t version = Control().GetStateVersion();

        for (const char* what : {"takeBack", "resign", "draw"}) {
            Json request = Json::object();
            request[what] = Json::object();
            ExpectInvalid(Control().SetState(request), what);
        }

        EXPECT_EQ(Control().GetStateVersion(), version);
        EXPECT_TRUE(Puzzle().is_object());
        EXPECT_EQ(Text(State(), "status"), "ongoing");
    }

    TEST_F(ChessGameControlPuzzleTest, TheStateVersionMovesWithABeginAVerdictTheReplyAndAnEnd) {
        const uint64_t start = Control().GetStateVersion();
        ASSERT_TRUE(SetPuzzle("index", Json(0)).has_value());
        const uint64_t begun = Control().GetStateVersion();
        EXPECT_GT(begun, start);

        ASSERT_TRUE(Move("a1a7"));
        const uint64_t moved = Control().GetStateVersion();
        EXPECT_GT(moved, begun);
        game.UpdatePuzzle(kFrame);
        const uint64_t judged = Control().GetStateVersion();
        EXPECT_GT(judged, moved) << "the verdict is a change of its own";

        game.UpdatePuzzle(kFrame);
        EXPECT_EQ(Control().GetStateVersion(), judged) << "a frame that waits changes nothing";
        game.UpdatePuzzle(kFrame);
        EXPECT_GT(Control().GetStateVersion(), judged) << "the reply";

        const uint64_t replied = Control().GetStateVersion();
        ASSERT_TRUE(SetPuzzle("end", Json(true)).has_value());
        EXPECT_GT(Control().GetStateVersion(), replied);
    }

    TEST_F(ChessGameControlPuzzleTest, AWholePuzzleIsSolvedThroughPerformActionAndFramesWithGameTime) {
        ASSERT_TRUE(SetPuzzle("index", Json(0)).has_value());

        ASSERT_TRUE(Move("a1a7"));
        EXPECT_EQ(Text(Puzzle(), "state"), "replying") << "what is on the board is read as it stands; only the reply and the takeback of a wrong move wait for frames";
        game.UpdatePuzzle(kFrame);
        Json puzzle = Puzzle();
        EXPECT_EQ(Text(puzzle, "state"), "replying");
        EXPECT_EQ(Number(puzzle, "step"), 1);

        game.UpdatePuzzle(0.0f);
        EXPECT_EQ(State()["moves"].size(), 2u) << "a frame with no time never plays the reply";
        Frames(2);
        const Json state = State();
        ASSERT_EQ(state["moves"].size(), 3u);
        EXPECT_EQ(state["moves"][2], "g8h8");
        EXPECT_EQ(Text(PuzzleOf(state), "state"), "your_move");

        ASSERT_TRUE(Move("b2b8"));
        game.UpdatePuzzle(kFrame);
        puzzle = Puzzle();
        EXPECT_EQ(Text(puzzle, "state"), "solved");
        EXPECT_EQ(Number(puzzle, "step"), 2);
        EXPECT_EQ(Number(puzzle, "mistakes"), 0);
        EXPECT_EQ(Text(State(), "status"), "checkmate");
    }

    TEST_F(ChessGameControlPuzzleTest, AWrongMoveIsTakenBackCountedAndShownUntilTheNextMove) {
        ASSERT_TRUE(SetPuzzle("index", Json(0)).has_value());

        ASSERT_TRUE(Move("b2b3"));
        game.UpdatePuzzle(kFrame);

        Json state = State();
        Json puzzle = PuzzleOf(state);
        EXPECT_EQ(state["moves"].size(), 1u) << "the move is gone from the board";
        EXPECT_EQ(Number(puzzle, "mistakes"), 1);
        EXPECT_TRUE(Flag(puzzle, "wrong"));
        EXPECT_EQ(Text(puzzle, "state"), "your_move");
        EXPECT_EQ(Number(puzzle, "step"), 0);

        ASSERT_TRUE(Move("a1a7"));
        game.UpdatePuzzle(kFrame);
        puzzle = Puzzle();
        EXPECT_FALSE(Flag(puzzle, "wrong"));
        EXPECT_EQ(Number(puzzle, "mistakes"), 1) << "a mistake stays counted";
    }

    TEST_F(ChessGameControlPuzzleTest, AMoveForTheProgramsSideIsAcceptedAndTheJudgeDecidesAtTheFrames) {
        ASSERT_TRUE(SetPuzzle("index", Json(0)).has_value());
        ASSERT_TRUE(Move("a1a7"));
        game.UpdatePuzzle(kFrame);
        ASSERT_EQ(Text(Puzzle(), "state"), "replying");

        EXPECT_TRUE(Move("g8h8")) << "the reply of the line, played by the client";
        game.UpdatePuzzle(kFrame);

        EXPECT_TRUE(Puzzle().is_object()) << "it is the line's own move";
        EXPECT_EQ(State()["moves"].size(), 3u);
    }

    TEST_F(ChessGameControlPuzzleTest, ASetStateFenAndAResetEndItAtOnce) {
        ASSERT_TRUE(SetPuzzle("index", Json(0)).has_value());
        Json request = Json::object();
        request["fen"] = String(Position::kStartFen.data(), Position::kStartFen.size()).c_str();
        ASSERT_TRUE(Control().SetState(request).has_value());
        EXPECT_TRUE(Puzzle().is_null()) << "with no frame between";

        ASSERT_TRUE(SetPuzzle("index", Json(0)).has_value());
        ASSERT_TRUE(Control().Reset(Json::object()).has_value());
        EXPECT_TRUE(Puzzle().is_null());
    }

}  // namespace BECore::Chess::Tests
