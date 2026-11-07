#include "ChessTestUtil.h"
#include "TestEngines.h"

#include <Chess/ChessGameControl.h>
#include <Chess/Game/MoveGenerator.h>
#include <Common/EngineEnvironment.h>
#include <EASTL/algorithm.h>

/**
 * Coverage of ChessGameControl: the state, the actions and the ways to act, on a model and an opponent controller that the test owns.
 * The adapter needs no scene. The opponent's engines are doubles of TestEngines.h (no process), so the tests do not depend on a Stockfish on the machine;
 * a search runs on the real TaskManager and every wait polls with a time limit.
 */
namespace BECore::Chess::Tests {

    namespace {

        constexpr eastl::string_view kMateInOneFen = "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1";
        constexpr eastl::string_view kPromotionFen = "8/P6k/8/8/8/8/8/K7 w - - 0 1";

        std::string ErrorCodeOf(const ControlError& error) {
            return std::string(error.code.c_str(), error.code.size());
        }

        Json MoveAction(const char* uci) {
            Json action = Json::object();
            action["type"] = "move";
            action["uci"] = uci;
            return action;
        }

        Json FenState(eastl::string_view fen) {
            Json state = Json::object();
            state["fen"] = MakeJsonString(fen);
            return state;
        }

        std::string StringMember(const Json& json, eastl::string_view key) {
            const auto value = GetJsonString(json, key);
            return value ? std::string(value->c_str(), value->size()) : std::string("<missing>");
        }

        Json OpponentState(const Json& opponent) {
            Json state = Json::object();
            state["opponent"] = opponent;
            return state;
        }

        /// {"opponent": {"plays": .., "level": .., "engine": ..}}; a null @p plays is "two people", an empty @p level or @p engine leaves it out
        Json OpponentRequest(const char* plays, const char* level = nullptr, const char* engine = nullptr) {
            Json opponent = Json::object();
            opponent["plays"] = plays ? Json(plays) : Json();
            if (level) {
                opponent["level"] = level;
            }
            if (engine) {
                opponent["engine"] = engine;
            }
            return OpponentState(opponent);
        }

        /// {"opponent": {"engine": ..}} and nothing else
        Json EngineRequest(const Json& engine) {
            Json opponent = Json::object();
            opponent["engine"] = engine;
            return OpponentState(opponent);
        }

        class ChessGameControlTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                ::BECore::Tests::EngineEnvironment::EnsureCoreServices();  // TaskManager: a search of the opponent runs on its pool
            }

            ChessGameControlTest() : opponent(model), control(model, opponent) {}

            /// The state as the JSON object that GetState() returned; fails the test when it is an error
            Json State() {
                auto state = control.GetState();
                EXPECT_TRUE(state.has_value());
                return state ? *state : Json();
            }

            Json Actions() {
                auto actions = control.GetActions();
                EXPECT_TRUE(actions.has_value());
                return actions ? *actions : Json();
            }

            /// Plays a move through the adapter and fails the test when it is refused; gives the outcome
            Json Perform(const char* uci) {
                auto outcome = control.PerformAction(MoveAction(uci));
                EXPECT_TRUE(outcome.has_value()) << uci << (outcome ? "" : ": " + ErrorCodeOf(outcome.error()));
                return outcome ? *outcome : Json();
            }

            void SetFen(eastl::string_view fen) {
                const auto result = control.SetState(FenState(fen));
                ASSERT_TRUE(result.has_value()) << result.error().message.c_str();
            }

            /// The UCI spellings of the actions, in the order they were listed
            eastl::vector<eastl::string> ActionMoves() {
                eastl::vector<eastl::string> moves;
                const Json actions = Actions();
                for (size_t index = 0; index < actions.size(); ++index) {
                    moves.push_back(GetJsonString(actions[index], "uci").value_or(eastl::string()));
                }
                return moves;
            }

            /// The "opponent" object of the state
            Json OpponentOfState() {
                const Json state = State();
                const Json* found = FindJsonMember(state, "opponent");
                EXPECT_NE(found, nullptr);
                return found ? *found : Json();
            }

            /// A member of the "opponent" object, copied (null when it is not there)
            Json OpponentMember(const char* key) {
                const Json computer = OpponentOfState();
                const Json* found = FindJsonMember(computer, key);
                return found ? *found : Json();
            }

            /// What a game loop does: Update every frame until @p done
            template <typename Condition>
            bool UpdateUntil(Condition done) {
                return WaitUntil([&] {
                    opponent.Update();
                    return done();
                });
            }

            size_t MoveCount() {
                const Json state = State();
                const Json* moves = FindJsonMember(state, "moves");
                return moves ? moves->size() : 0;
            }

            /// set_state with @p request; fails the test when it is refused
            void SetOpponent(const Json& request) {
                const auto result = control.SetState(request);
                ASSERT_TRUE(result.has_value()) << DumpJson(request).c_str() << ": " << (result ? "" : result.error().message.c_str());
            }

            /// The code of the error that set_state answers with; the test fails when it is accepted
            std::string SetStateError(const Json& request) {
                const auto result = control.SetState(request);
                EXPECT_FALSE(result.has_value()) << DumpJson(request).c_str();
                return result ? std::string("<accepted>") : ErrorCodeOf(result.error());
            }

            ChessGameModel model;
            OpponentController opponent;
            ChessGameControl control;
        };

    }  // namespace

    // ---- State ----

    TEST_F(ChessGameControlTest, StateDescribesTheStartPosition) {
        const Json state = State();

        EXPECT_EQ(StringMember(state, "fen"), std::string(Position::kStartFen.data(), Position::kStartFen.size()));
        EXPECT_EQ(StringMember(state, "startFen"), StringMember(state, "fen"));
        EXPECT_EQ(StringMember(state, "turn"), "white");
        EXPECT_EQ(StringMember(state, "status"), "ongoing");
        EXPECT_EQ(GetJsonBool(state, "inCheck"), false);
        EXPECT_EQ(GetJsonInt(state, "legalMoveCount"), 20);

        const Json* winner = FindJsonMember(state, "winner");
        ASSERT_NE(winner, nullptr);
        EXPECT_TRUE(winner->is_null()) << "nobody has won yet";

        const Json* moves = FindJsonMember(state, "moves");
        ASSERT_NE(moves, nullptr);
        ASSERT_TRUE(moves->is_array());
        EXPECT_TRUE(moves->empty());
    }

    // ---- The material ----

    namespace {

        /// The "material" object of a state (null when it is not there)
        Json MaterialOf(const Json& state) {
            const Json* found = FindJsonMember(state, "material");
            EXPECT_NE(found, nullptr);
            return found ? *found : Json();
        }

        int CountOf(const Json& side, const char* piece) {
            const Json* found = FindJsonMember(side, piece);
            EXPECT_NE(found, nullptr) << piece;
            EXPECT_TRUE(found && found->is_number_integer()) << piece;
            return found && found->is_number_integer() ? found->get<int>() : -1;
        }

        void ExpectCaptured(const Json& material, const char* key, int queen, int rook, int bishop, int knight, int pawn) {
            const Json* side = FindJsonMember(material, key);
            ASSERT_NE(side, nullptr) << key;
            ASSERT_TRUE(side->is_object()) << key;
            EXPECT_EQ(side->size(), 5u) << key << ": exactly the five capturable pieces";
            EXPECT_EQ(CountOf(*side, "queen"), queen) << key;
            EXPECT_EQ(CountOf(*side, "rook"), rook) << key;
            EXPECT_EQ(CountOf(*side, "bishop"), bishop) << key;
            EXPECT_EQ(CountOf(*side, "knight"), knight) << key;
            EXPECT_EQ(CountOf(*side, "pawn"), pawn) << key;
        }

    }  // namespace

    TEST_F(ChessGameControlTest, StateHasTheMaterialOfTheStartPosition) {
        const Json material = MaterialOf(State());
        ASSERT_TRUE(material.is_object());
        EXPECT_EQ(material.size(), 3u) << "balance, capturedByWhite, capturedByBlack";
        EXPECT_EQ(GetJsonInt(material, "balance"), 0);
        ExpectCaptured(material, "capturedByWhite", 0, 0, 0, 0, 0);
        ExpectCaptured(material, "capturedByBlack", 0, 0, 0, 0, 0);
    }

    TEST_F(ChessGameControlTest, MaterialFollowsACapture) {
        Perform("e2e4");
        Perform("d7d5");
        Perform("e4d5");

        const Json material = MaterialOf(State());
        EXPECT_EQ(GetJsonInt(material, "balance"), 1);
        ExpectCaptured(material, "capturedByWhite", 0, 0, 0, 0, 1);
        ExpectCaptured(material, "capturedByBlack", 0, 0, 0, 0, 0);

        Perform("d8d5");  // Black takes the pawn back: level on pawns, and Black's capture is listed
        const Json after = MaterialOf(State());
        EXPECT_EQ(GetJsonInt(after, "balance"), 0);
        ExpectCaptured(after, "capturedByWhite", 0, 0, 0, 0, 1);
        ExpectCaptured(after, "capturedByBlack", 0, 0, 0, 0, 1);
    }

    TEST_F(ChessGameControlTest, MaterialFollowsSetStateAndReset) {
        // White: a queen and two pawns; Black: the king alone
        SetFen("4k3/8/8/8/8/8/PP6/Q3K3 w - - 0 1");
        const Json material = MaterialOf(State());
        EXPECT_EQ(GetJsonInt(material, "balance"), 9 + 2);
        ExpectCaptured(material, "capturedByWhite", 1, 2, 2, 2, 8);
        ExpectCaptured(material, "capturedByBlack", 0, 2, 2, 2, 6);

        SetFen("4k3/8/8/8/8/8/8/R3K3 w - - 0 1");
        EXPECT_EQ(GetJsonInt(MaterialOf(State()), "balance"), 5);

        const auto reset = control.Reset(Json::object());
        ASSERT_TRUE(reset.has_value());
        const Json fresh = MaterialOf(State());
        EXPECT_EQ(GetJsonInt(fresh, "balance"), 0);
        ExpectCaptured(fresh, "capturedByWhite", 0, 0, 0, 0, 0);
        ExpectCaptured(fresh, "capturedByBlack", 0, 0, 0, 0, 0);
    }

    TEST_F(ChessGameControlTest, MaterialCountsAPromotionAsPointsAndNotAsATake) {
        SetFen("4k3/P7/8/8/8/8/1PPPPPPP/3QK3 w - - 0 1");
        EXPECT_EQ(GetJsonInt(MaterialOf(State()), "balance"), 17);

        Perform("a7a8q");
        const Json material = MaterialOf(State());
        EXPECT_EQ(GetJsonInt(material, "balance"), 25) << "the new queen is on the board";
        ExpectCaptured(material, "capturedByWhite", 1, 2, 2, 2, 8);
        ExpectCaptured(material, "capturedByBlack", 0, 2, 2, 2, 0);
    }

    TEST_F(ChessGameControlTest, MaterialDoesNotMoveTheStateVersion) {
        Perform("e2e4");
        const uint64_t before = control.GetStateVersion();
        (void)State();
        (void)State();
        EXPECT_EQ(control.GetStateVersion(), before);
    }

    TEST_F(ChessGameControlTest, BoardIsEightRowsWithRankEightFirst) {
        const Json state = State();
        const Json* board = FindJsonMember(state, "board");
        ASSERT_NE(board, nullptr);
        ASSERT_TRUE(board->is_array());
        ASSERT_EQ(board->size(), 8u);

        const char* const expected[] = {"rnbqkbnr", "pppppppp", "........", "........", "........", "........", "PPPPPPPP", "RNBQKBNR"};
        for (size_t row = 0; row < board->size(); ++row) {
            ASSERT_TRUE((*board)[row].is_string());
            EXPECT_EQ((*board)[row].get_ref<const std::string&>(), expected[row]) << "row " << row;
        }
    }

    TEST_F(ChessGameControlTest, BoardFollowsTheGame) {
        Perform("e2e4");
        Perform("c7c5");

        const Json state = State();
        const Json* board = FindJsonMember(state, "board");
        ASSERT_NE(board, nullptr);
        EXPECT_EQ((*board)[1].get_ref<const std::string&>(), "pp.ppppp") << "rank 7, the c pawn left";
        EXPECT_EQ((*board)[3].get_ref<const std::string&>(), "..p.....") << "rank 5";
        EXPECT_EQ((*board)[4].get_ref<const std::string&>(), "....P...") << "rank 4";
        EXPECT_EQ((*board)[6].get_ref<const std::string&>(), "PPPP.PPP") << "rank 2, the e pawn left";
        EXPECT_EQ(StringMember(state, "turn"), "white");
    }

    TEST_F(ChessGameControlTest, StateKeepsTheMovesAndWhereTheGameBegan) {
        Perform("e2e4");
        Perform("e7e5");
        Perform("g1f3");

        const Json state = State();
        const Json* moves = FindJsonMember(state, "moves");
        ASSERT_NE(moves, nullptr);
        ASSERT_EQ(moves->size(), 3u);
        EXPECT_EQ((*moves)[0].get_ref<const std::string&>(), "e2e4");
        EXPECT_EQ((*moves)[2].get_ref<const std::string&>(), "g1f3");
        EXPECT_EQ(StringMember(state, "startFen"), std::string(Position::kStartFen.data(), Position::kStartFen.size()));
    }

    TEST_F(ChessGameControlTest, StartFenAndMovesRebuildTheGame) {
        for (const char* uci : {"e2e4", "d7d5", "e4d5", "g8f6", "f1b5", "c7c6", "d5c6"}) {
            Perform(uci);
        }
        const Json state = State();

        auto replayed = Position::FromFen(eastl::string_view(StringMember(state, "startFen").c_str()));
        ASSERT_TRUE(replayed.has_value());
        const Json* moves = FindJsonMember(state, "moves");
        ASSERT_NE(moves, nullptr);
        for (size_t index = 0; index < moves->size(); ++index) {
            const std::string& uci = (*moves)[index].get_ref<const std::string&>();
            const auto move = MoveGenerator::ParseUci(*replayed, eastl::string_view(uci.c_str(), uci.size()));
            ASSERT_TRUE(move.has_value()) << uci;
            replayed->DoMove(*move);
        }
        EXPECT_EQ(StringMember(state, "fen"), std::string(replayed->ToFen().c_str()));
    }

    TEST_F(ChessGameControlTest, StateReportsCheck) {
        for (const char* uci : {"e2e4", "f7f5", "d1h5"}) {
            Perform(uci);
        }
        const Json state = State();
        EXPECT_EQ(GetJsonBool(state, "inCheck"), true);
        EXPECT_EQ(StringMember(state, "status"), "ongoing");
        EXPECT_EQ(StringMember(state, "turn"), "black");
        EXPECT_EQ(GetJsonInt(state, "legalMoveCount"), 1) << "g7g6 is the only answer";
    }

    // ---- Actions ----

    TEST_F(ChessGameControlTest, ActionsListEveryLegalMoveOnce) {
        const Json actions = Actions();
        const MoveList legal = MoveGenerator::GenerateLegal(model.GetPosition());

        ASSERT_TRUE(actions.is_array());
        ASSERT_EQ(actions.size(), legal.size());
        for (size_t index = 0; index < legal.size(); ++index) {
            EXPECT_EQ(StringMember(actions[index], "type"), "move");
            EXPECT_EQ(StringMember(actions[index], "uci"), std::string(legal[index].ToUci().c_str()));
        }
    }

    TEST_F(ChessGameControlTest, ActionsAreTheLegalMovesOfALoadedPosition) {
        SetFen(kKiwipeteFen);
        EXPECT_EQ(Actions().size(), 48u) << "the node count of Kiwipete at depth 1";

        // Five king moves, four promotions on a8 and four promoting captures of the rook on b8
        SetFen("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1");
        const eastl::vector<eastl::string> moves = ActionMoves();
        EXPECT_EQ(moves.size(), 13u);
        for (const char* promotion : {"a7b8q", "a7b8r", "a7b8b", "a7b8n", "a7a8q", "a7a8n"}) {
            EXPECT_NE(eastl::find(moves.begin(), moves.end(), promotion), moves.end()) << promotion;
        }
    }

    TEST_F(ChessGameControlTest, EveryListedActionIsAccepted) {
        // The contract of list_actions: each element goes to perform_action as it is
        SetFen(kKiwipeteFen);
        const Json actions = Actions();
        ASSERT_EQ(actions.size(), 48u);
        for (size_t index = 0; index < actions.size(); ++index) {
            SetFen(kKiwipeteFen);
            const auto outcome = control.PerformAction(actions[index]);
            EXPECT_TRUE(outcome.has_value()) << StringMember(actions[index], "uci");
        }
    }

    TEST_F(ChessGameControlTest, ActionsOfAFinishedGameAreEmpty) {
        SetFen(kMateInOneFen);
        Perform("a1a8");
        ASSERT_TRUE(model.IsGameOver());
        EXPECT_TRUE(Actions().empty());

        SetFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        EXPECT_TRUE(Actions().empty()) << "stalemate";
    }

    TEST_F(ChessGameControlTest, ActionsOfADrawnGameAreEmptyEvenWhenMovesExist) {
        SetFen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
        ASSERT_EQ(model.GetStatus(), GameStatus::DrawInsufficientMaterial);
        EXPECT_TRUE(Actions().empty()) << "the kings could move, but the game is over";
        EXPECT_EQ(GetJsonInt(State(), "legalMoveCount"), 0);
    }

    // ---- PerformAction ----

    TEST_F(ChessGameControlTest, PerformActionPlaysTheMove) {
        const uint64_t version = control.GetStateVersion();
        const Json outcome = Perform("e2e4");

        EXPECT_EQ(StringMember(outcome, "move"), "e2e4");
        EXPECT_EQ(StringMember(outcome, "turn"), "black");
        EXPECT_EQ(StringMember(outcome, "status"), "ongoing");
        EXPECT_EQ(GetJsonBool(outcome, "inCheck"), false);
        EXPECT_GT(control.GetStateVersion(), version);
        EXPECT_TRUE(model.GetPieceAt(Sq("e4")).type == PieceType::Pawn);
        EXPECT_TRUE(model.GetTurn() == PieceColor::Black);
    }

    TEST_F(ChessGameControlTest, PerformActionPlaysTheSpecialMoves) {
        for (const char* uci : {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5"}) {
            Perform(uci);
        }
        Perform("e1g1");
        EXPECT_TRUE(model.GetPieceAt(Sq("g1")).type == PieceType::King);
        EXPECT_TRUE(model.GetPieceAt(Sq("f1")).type == PieceType::Rook) << "castling";

        SetFen("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3");
        Perform("e5d6");
        EXPECT_TRUE(model.GetPieceAt(Sq("d5")).IsEmpty()) << "en passant takes the pawn beside, not the one on the target";

        SetFen(kPromotionFen);
        Perform("a7a8n");
        EXPECT_TRUE(model.GetPieceAt(Sq("a8")).type == PieceType::Knight) << "an under-promotion";
    }

    TEST_F(ChessGameControlTest, PerformActionReportsTheEndOfTheGame) {
        SetFen(kMateInOneFen);
        const Json outcome = Perform("a1a8");

        EXPECT_EQ(StringMember(outcome, "status"), "checkmate");
        EXPECT_EQ(StringMember(outcome, "winner"), "white");
        EXPECT_EQ(GetJsonBool(outcome, "inCheck"), true);

        const Json state = State();
        EXPECT_EQ(StringMember(state, "status"), "checkmate");
        EXPECT_EQ(StringMember(state, "winner"), "white");
        EXPECT_EQ(StringMember(state, "turn"), "black") << "the side that was mated is to move";
        EXPECT_EQ(GetJsonInt(state, "legalMoveCount"), 0);
    }

    TEST_F(ChessGameControlTest, ADrawHasNoWinner) {
        for (int cycle = 0; cycle < 2; ++cycle) {
            for (const char* uci : {"g1f3", "g8f6", "f3g1", "f6g8"}) {
                Perform(uci);
            }
        }
        const Json state = State();
        EXPECT_EQ(StringMember(state, "status"), "draw_threefold");
        const Json* winner = FindJsonMember(state, "winner");
        ASSERT_NE(winner, nullptr);
        EXPECT_TRUE(winner->is_null());
    }

    TEST_F(ChessGameControlTest, AMoveThatIsNotLegalIsRefusedAndChangesNothing) {
        Perform("e2e4");
        const Json before = State();
        const uint64_t version = control.GetStateVersion();

        for (const char* uci : {"e2e4", "e4e6", "d2d4", "a1a8", "e8e7", "a2a1q"}) {  // black is to move
            const auto outcome = control.PerformAction(MoveAction(uci));
            ASSERT_FALSE(outcome.has_value()) << uci;
            EXPECT_EQ(ErrorCodeOf(outcome.error()), "illegal_action") << uci;
        }
        EXPECT_EQ(control.GetStateVersion(), version);
        EXPECT_EQ(State(), before);
    }

    TEST_F(ChessGameControlTest, AMoveThatLeavesTheKingInCheckIsRefused) {
        SetFen("4k3/4r3/8/8/8/8/4B3/4K3 w - - 0 1");
        const auto outcome = control.PerformAction(MoveAction("e2d3"));
        ASSERT_FALSE(outcome.has_value());
        EXPECT_EQ(ErrorCodeOf(outcome.error()), "illegal_action") << "the bishop is pinned to the king";
    }

    TEST_F(ChessGameControlTest, APromotionWithoutAPieceNeedsOne) {
        SetFen(kPromotionFen);
        const std::string fen = StringMember(State(), "fen");
        const uint64_t version = control.GetStateVersion();

        const auto outcome = control.PerformAction(MoveAction("a7a8"));
        ASSERT_FALSE(outcome.has_value());
        EXPECT_EQ(ErrorCodeOf(outcome.error()), "promotion_required");
        EXPECT_NE(std::string(outcome.error().message.c_str()).find("a7a8q"), std::string::npos) << "the message says what to send";
        EXPECT_EQ(StringMember(State(), "fen"), fen);
        EXPECT_EQ(control.GetStateVersion(), version);

        EXPECT_TRUE(control.PerformAction(MoveAction("a7a8q")).has_value());
        EXPECT_TRUE(model.GetPieceAt(Sq("a8")).type == PieceType::Queen);
    }

    TEST_F(ChessGameControlTest, AMoveOnAFinishedGameIsRefusedAsGameOver) {
        SetFen(kMateInOneFen);
        Perform("a1a8");
        const uint64_t version = control.GetStateVersion();

        for (const char* uci : {"g8f8", "h7h6", "a8a7"}) {
            const auto outcome = control.PerformAction(MoveAction(uci));
            ASSERT_FALSE(outcome.has_value()) << uci;
            EXPECT_EQ(ErrorCodeOf(outcome.error()), "game_over") << uci;
        }
        EXPECT_EQ(control.GetStateVersion(), version);
    }

    TEST_F(ChessGameControlTest, MalformedActionsAreInvalidParams) {
        Json notAMove = Json::object();
        notAMove["type"] = "resign";
        notAMove["uci"] = "e2e4";
        Json noType = Json::object();
        noType["uci"] = "e2e4";
        Json noMove = Json::object();
        noMove["type"] = "move";
        Json moveIsNumber = MoveAction("e2e4");
        moveIsNumber["uci"] = 2424;
        Json typeIsNumber = MoveAction("e2e4");
        typeIsNumber["type"] = 1;

        const uint64_t version = control.GetStateVersion();
        for (const Json& action : {notAMove, noType, noMove, moveIsNumber, typeIsNumber, Json::array(), Json("e2e4"), Json()}) {
            const auto outcome = control.PerformAction(action);
            ASSERT_FALSE(outcome.has_value()) << DumpJson(action).c_str();
            EXPECT_EQ(ErrorCodeOf(outcome.error()), "invalid_params") << DumpJson(action).c_str();
        }
        for (const char* uci : {"", "e2", "e2e", "e2e4e", "e2e4qq", "banana", "E2E4", "e9e4", "i2e4", "e0e4", "e2e4x", "e7e8Q", "e2 e4", "e2-e4"}) {
            const auto outcome = control.PerformAction(MoveAction(uci));
            ASSERT_FALSE(outcome.has_value()) << "'" << uci << "'";
            EXPECT_EQ(ErrorCodeOf(outcome.error()), "invalid_params") << "'" << uci << "'";
        }
        EXPECT_EQ(control.GetStateVersion(), version);
    }

    // ---- Reset and SetState ----

    TEST_F(ChessGameControlTest, ResetStartsANewGame) {
        Perform("e2e4");
        const uint64_t version = control.GetStateVersion();

        ASSERT_TRUE(control.Reset(Json::object()).has_value());
        EXPECT_GT(control.GetStateVersion(), version);
        EXPECT_EQ(StringMember(State(), "fen"), std::string(Position::kStartFen.data(), Position::kStartFen.size()));
        EXPECT_TRUE(FindJsonMember(State(), "moves")->empty());
    }

    TEST_F(ChessGameControlTest, ResetRestartsAFinishedGame) {
        SetFen(kMateInOneFen);
        Perform("a1a8");
        ASSERT_TRUE(control.Reset(Json::object()).has_value());
        EXPECT_EQ(StringMember(State(), "status"), "ongoing");
        EXPECT_EQ(Actions().size(), 20u);
    }

    TEST_F(ChessGameControlTest, SetStateLoadsAPosition) {
        Perform("e2e4");
        const uint64_t version = control.GetStateVersion();

        SetFen(kKiwipeteFen);
        EXPECT_GT(control.GetStateVersion(), version);

        const Json state = State();
        EXPECT_EQ(StringMember(state, "fen"), std::string(kKiwipeteFen.data(), kKiwipeteFen.size()));
        EXPECT_EQ(StringMember(state, "startFen"), std::string(kKiwipeteFen.data(), kKiwipeteFen.size()));
        EXPECT_TRUE(FindJsonMember(state, "moves")->empty()) << "the moves before it belong to another game";
    }

    TEST_F(ChessGameControlTest, SetStateCanLoadAFinishedGame) {
        SetFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        EXPECT_EQ(StringMember(State(), "status"), "stalemate");
    }

    TEST_F(ChessGameControlTest, SetStateRefusesWhatIsNotAPlayablePosition) {
        Perform("e2e4");
        const Json before = State();
        const uint64_t version = control.GetStateVersion();

        const char* const bad[] = {
            "",
            "not a fen",
            "8/8/8/8/8/8/8/8 w - - 0 1",                                 // no kings
            "4k3/8/8/8/8/8/8/4K3 x - - 0 1",                             // no such side
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - x 1",  // a clock that is not a number
            "4k3/8/8/8/8/N7/PPPPPPPP/NNNNKNNN w - - 0 1",                // seventeen white pieces, which no handle in the scene could show
        };
        for (const char* fen : bad) {
            const auto result = control.SetState(FenState(fen));
            ASSERT_FALSE(result.has_value()) << "'" << fen << "'";
            EXPECT_EQ(ErrorCodeOf(result.error()), "invalid_params") << "'" << fen << "'";
        }
        EXPECT_EQ(State(), before);
        EXPECT_EQ(control.GetStateVersion(), version);
    }

    TEST_F(ChessGameControlTest, SetStateNeedsAFenString) {
        Json number = Json::object();
        number["fen"] = 5;
        Json missing = Json::object();
        for (const Json& state : {number, missing, Json::array(), Json("fen"), Json()}) {
            const auto result = control.SetState(state);
            ASSERT_FALSE(result.has_value()) << DumpJson(state).c_str();
            EXPECT_EQ(ErrorCodeOf(result.error()), "invalid_params") << DumpJson(state).c_str();
        }
    }

    // ---- Version ----

    TEST_F(ChessGameControlTest, ReadingNeverMovesTheVersion) {
        const uint64_t version = control.GetStateVersion();
        State();
        Actions();
        EXPECT_EQ(control.GetStateVersion(), version);
    }

    TEST_F(ChessGameControlTest, EveryMoveMovesTheVersion) {
        uint64_t version = control.GetStateVersion();
        for (const char* uci : {"e2e4", "e7e5", "g1f3"}) {
            Perform(uci);
            EXPECT_GT(control.GetStateVersion(), version) << uci;
            version = control.GetStateVersion();
        }
    }

    TEST_F(ChessGameControlTest, AMoveMadeByAClickIsSeenThroughTheVersion) {
        const uint64_t version = control.GetStateVersion();
        model.OnSquareClicked(Sq("e2"));
        EXPECT_EQ(control.GetStateVersion(), version) << "a selection is not a change";
        model.OnSquareClicked(Sq("e4"));
        EXPECT_GT(control.GetStateVersion(), version) << "a person's move is pushed to the controller like its own";
    }

    // ---- The opponent ----

    TEST_F(ChessGameControlTest, TheOpponentOfANewGameIsNobodyAtTheDefaultLevel) {
        const Json computer = OpponentOfState();

        ASSERT_TRUE(computer.is_object());
        const Json* plays = FindJsonMember(computer, "plays");
        ASSERT_NE(plays, nullptr);
        EXPECT_TRUE(plays->is_null()) << "two people";
        EXPECT_EQ(StringMember(computer, "level"), "medium");
        EXPECT_EQ(StringMember(computer, "engine"), "stockfish") << "the engine a game starts with: the own one is a pick of the person";
        EXPECT_EQ(GetJsonBool(computer, "thinking"), false);
        const Json* error = FindJsonMember(computer, "error");
        ASSERT_NE(error, nullptr);
        EXPECT_TRUE(error->is_null());
    }

    TEST_F(ChessGameControlTest, ASideAndALevelAreSetAndReadBack) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());

        SetOpponent(OpponentRequest("black", "easy"));

        const Json computer = OpponentOfState();
        EXPECT_EQ(StringMember(computer, "plays"), "black");
        EXPECT_EQ(StringMember(computer, "level"), "easy");
        EXPECT_EQ(factory.Calls(), 1) << "one pick makes one engine, at the level asked for";
        EXPECT_EQ(factory.Levels()[0], "easy");
        EXPECT_TRUE(opponent.PlaysFor(PieceColor::Black));
        EXPECT_EQ(model.GetMoves().size(), 0u) << "the position is not touched by a pick";
    }

    TEST_F(ChessGameControlTest, AnEngineAloneIsReadBackAsksNoFactoryAndMovesTheVersion) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        const uint64_t version = control.GetStateVersion();

        SetOpponent(EngineRequest(Json("own")));

        const Json computer = OpponentOfState();
        EXPECT_EQ(StringMember(computer, "engine"), "own");
        EXPECT_TRUE(FindJsonMember(computer, "plays")->is_null()) << "the side is untouched";
        EXPECT_EQ(StringMember(computer, "level"), "medium");
        EXPECT_EQ(factory.Calls(), 0) << "nobody plays, so no engine is made";
        EXPECT_GT(control.GetStateVersion(), version);
    }

    TEST_F(ChessGameControlTest, ASideAndTheOwnEngineMakeOneEngineForTheOwnEngine) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());

        SetOpponent(OpponentRequest("black", "easy", "own"));

        const Json computer = OpponentOfState();
        EXPECT_EQ(StringMember(computer, "plays"), "black");
        EXPECT_EQ(StringMember(computer, "level"), "easy");
        EXPECT_EQ(StringMember(computer, "engine"), "own");
        ASSERT_EQ(factory.Calls(), 1) << "one pick makes one engine";
        EXPECT_EQ(factory.Engines()[0], "own");
        EXPECT_EQ(factory.Levels()[0], "easy");
    }

    TEST_F(ChessGameControlTest, ALevelOrASideKeepsTheEngine) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(EngineRequest(Json("own")));

        Json onlyLevel = Json::object();
        onlyLevel["level"] = "hard";
        SetOpponent(OpponentState(onlyLevel));
        EXPECT_EQ(StringMember(OpponentOfState(), "engine"), "own") << "a member that is missing is unchanged";

        SetOpponent(OpponentRequest("white"));
        EXPECT_EQ(StringMember(OpponentOfState(), "engine"), "own");
        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Engines()[0], "own");
        EXPECT_EQ(factory.Levels()[0], "hard");

        SetOpponent(EngineRequest(Json("stockfish")));
        EXPECT_EQ(StringMember(OpponentOfState(), "engine"), "stockfish");
        EXPECT_EQ(StringMember(OpponentOfState(), "plays"), "white") << "the side is kept through an engine change";
        ASSERT_EQ(factory.Calls(), 2) << "the program plays, so the engine change makes a new one";
        EXPECT_EQ(factory.Engines()[1], "stockfish");
    }

    TEST_F(ChessGameControlTest, ABadEngineIsRefusedAndChangesNothing) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("black", "easy", "own"));
        Perform("e2e4");
        const int calls = factory.Calls();
        const Json before = State();
        const uint64_t version = control.GetStateVersion();

        const Json refused[] = {EngineRequest(Json("Own")), EngineRequest(Json("Stockfish")), EngineRequest(Json("")),   EngineRequest(Json("own ")),  EngineRequest(Json("other")),
                                EngineRequest(Json()),      EngineRequest(Json(1)),           EngineRequest(Json(true)), EngineRequest(Json::array()), OpponentRequest("white", "hard", "other")};
        for (const Json& request : refused) {
            const auto result = control.SetState(request);
            ASSERT_FALSE(result.has_value()) << DumpJson(request).c_str();
            EXPECT_EQ(ErrorCodeOf(result.error()), "invalid_params") << DumpJson(request).c_str();
            EXPECT_NE(std::string(result.error().message.c_str()).find("stockfish, own"), std::string::npos) << "the message names the ids: " << result.error().message.c_str();
        }

        // A valid position next to a bad engine: neither half is applied
        Json fenAndBadEngine = EngineRequest(Json("other"));
        fenAndBadEngine["fen"] = MakeJsonString(kMateInOneFen);
        EXPECT_EQ(SetStateError(fenAndBadEngine), "invalid_params");

        EXPECT_EQ(State(), before);
        EXPECT_EQ(control.GetStateVersion(), version);
        EXPECT_EQ(factory.Calls(), calls) << "no engine was asked for by a refused call";
    }

    TEST_F(ChessGameControlTest, AClientThatGetsEngineUnavailableForStockfishCanSwitchToTheOwnEngine) {
        // Stockfish is missing, the own engine is not: the factory fails only for the first
        eastl::vector<eastl::string> asked;
        opponent.SetEngineFactory([&asked](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            const eastl::string_view id = OpponentEngineId(engine);
            asked.emplace_back(id.data(), id.size());
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
            }
            return eastl::make_shared<InstantEngine>();
        });
        Perform("e2e4");
        const std::string fenBefore = StringMember(State(), "fen");

        Json request = OpponentRequest("black", nullptr, "stockfish");
        request["fen"] = MakeJsonString(kMateInOneFen);
        EXPECT_EQ(SetStateError(request), "engine_unavailable");
        EXPECT_EQ(StringMember(State(), "fen"), fenBefore) << "the position is not replaced";
        EXPECT_TRUE(OpponentMember("plays").is_null());
        EXPECT_EQ(StringMember(OpponentOfState(), "engine"), "stockfish");
        EXPECT_EQ(StringMember(OpponentMember("error"), "kind"), "launch_failed");
        ASSERT_EQ(asked.size(), 1u);
        EXPECT_EQ(asked[0], "stockfish") << "nothing asked for the own engine on its own";

        // The client switches: the same call with the other engine succeeds, the error goes with the pick
        request = OpponentRequest("black", nullptr, "own");
        request["fen"] = MakeJsonString(kMateInOneFen);
        SetOpponent(request);
        EXPECT_EQ(StringMember(State(), "fen"), std::string(kMateInOneFen.data(), kMateInOneFen.size()));
        const Json computer = OpponentOfState();
        EXPECT_EQ(StringMember(computer, "plays"), "black");
        EXPECT_EQ(StringMember(computer, "engine"), "own");
        EXPECT_TRUE(FindJsonMember(computer, "error")->is_null());
        ASSERT_EQ(asked.size(), 2u);
        EXPECT_EQ(asked[1], "own");
    }

    TEST_F(ChessGameControlTest, AFailedSwitchFromTheOwnEngineToStockfishIsRefusedAndTheOpponentStaysAsItWas) {
        eastl::vector<eastl::string> asked;
        opponent.SetEngineFactory([&asked](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            const eastl::string_view id = OpponentEngineId(engine);
            asked.emplace_back(id.data(), id.size());
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
            }
            return eastl::make_shared<InstantEngine>();
        });
        SetOpponent(OpponentRequest("black", "easy", "own"));
        ASSERT_EQ(StringMember(OpponentOfState(), "plays"), "black");
        Perform("e2e4");
        const std::string fenBefore = StringMember(State(), "fen");

        Json request = OpponentRequest("black", nullptr, "stockfish");
        request["fen"] = MakeJsonString(kMateInOneFen);
        EXPECT_EQ(SetStateError(request), "engine_unavailable");

        EXPECT_EQ(StringMember(State(), "fen"), fenBefore) << "the position is not replaced";
        EXPECT_EQ(StringMember(OpponentOfState(), "engine"), "own") << "an engine that cannot be had cannot be saved: the pick is refused and the old engine stays";
        EXPECT_EQ(StringMember(OpponentOfState(), "plays"), "black") << "the program that played before goes on playing";
        EXPECT_EQ(StringMember(OpponentOfState(), "level"), "easy");
        EXPECT_EQ(StringMember(OpponentMember("error"), "kind"), "launch_failed") << "the refusal is shown";
        ASSERT_EQ(asked.size(), 2u);
        EXPECT_EQ(asked[0], "own");
        EXPECT_EQ(asked[1], "stockfish") << "the own engine is not asked for on its own";
    }

    TEST_F(ChessGameControlTest, TheSameEnginePickAgainDoesNothing) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("black", "easy", "own"));
        const uint64_t version = control.GetStateVersion();

        SetOpponent(OpponentRequest("black", "easy", "own"));
        SetOpponent(EngineRequest(Json("own")));

        EXPECT_EQ(factory.Calls(), 1);
        EXPECT_EQ(control.GetStateVersion(), version);
    }

    TEST_F(ChessGameControlTest, AnEnginePickOfNobodyMovesTheVersionBothWays) {
        uint64_t version = control.GetStateVersion();
        SetOpponent(EngineRequest(Json("own")));
        EXPECT_GT(control.GetStateVersion(), version);
        version = control.GetStateVersion();
        SetOpponent(EngineRequest(Json("stockfish")));
        EXPECT_GT(control.GetStateVersion(), version);
    }

    TEST_F(ChessGameControlTest, ResetKeepsTheEngine) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("white", "hard", "own"));
        Perform("e2e4");

        ASSERT_TRUE(control.Reset(Json::object()).has_value());

        EXPECT_EQ(StringMember(OpponentOfState(), "engine"), "own");
        EXPECT_EQ(factory.Calls(), 1);
    }

    TEST_F(ChessGameControlTest, NullPutsTwoPeopleBack) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("white", "hard"));
        ASSERT_EQ(StringMember(OpponentOfState(), "plays"), "white");

        SetOpponent(OpponentRequest(nullptr));

        const Json computer = OpponentOfState();
        EXPECT_TRUE(FindJsonMember(computer, "plays")->is_null());
        EXPECT_EQ(StringMember(computer, "level"), "hard") << "a member that is missing is unchanged";
        EXPECT_FALSE(opponent.PlaysFor(PieceColor::White));
    }

    TEST_F(ChessGameControlTest, OnlyALevelKeepsTheSide) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());

        Json onlyLevel = Json::object();
        onlyLevel["level"] = "max";
        SetOpponent(OpponentState(onlyLevel));
        EXPECT_EQ(StringMember(OpponentOfState(), "level"), "max");
        EXPECT_TRUE(OpponentMember("plays").is_null());
        EXPECT_EQ(factory.Calls(), 0) << "nobody plays, so no engine is asked for";

        SetOpponent(OpponentRequest("black"));
        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Levels()[0], "max") << "the level picked before is the one used";

        onlyLevel["level"] = "easy";
        SetOpponent(OpponentState(onlyLevel));
        const Json computer = OpponentOfState();
        EXPECT_EQ(StringMember(computer, "plays"), "black") << "the side is kept";
        EXPECT_EQ(StringMember(computer, "level"), "easy");
        EXPECT_EQ(factory.Calls(), 2);
    }

    TEST_F(ChessGameControlTest, AnEmptyOpponentObjectChangesNothing) {
        const Json before = State();
        const uint64_t version = control.GetStateVersion();
        SetOpponent(OpponentState(Json::object()));
        EXPECT_EQ(State(), before);
        EXPECT_EQ(control.GetStateVersion(), version);
    }

    TEST_F(ChessGameControlTest, AFenAndAnOpponentTogether) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        Perform("e2e4");

        Json both = OpponentRequest("black", "hard");
        both["fen"] = MakeJsonString(kMateInOneFen);
        SetOpponent(both);

        const Json state = State();
        EXPECT_EQ(StringMember(state, "fen"), std::string(kMateInOneFen.data(), kMateInOneFen.size()));
        EXPECT_TRUE(FindJsonMember(state, "moves")->empty());
        EXPECT_EQ(StringMember(*FindJsonMember(state, "opponent"), "plays"), "black");
        EXPECT_EQ(StringMember(*FindJsonMember(state, "opponent"), "level"), "hard");
        EXPECT_EQ(factory.Calls(), 1);
    }

    TEST_F(ChessGameControlTest, SetStateNeedsAFenOrAnOpponent) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        const Json before = State();
        const uint64_t version = control.GetStateVersion();

        Json other = Json::object();
        other["level"] = "easy";  // not a member of the state
        EXPECT_EQ(SetStateError(Json::object()), "invalid_params");
        EXPECT_EQ(SetStateError(other), "invalid_params");
        EXPECT_EQ(SetStateError(Json::array()), "invalid_params");

        EXPECT_EQ(State(), before);
        EXPECT_EQ(control.GetStateVersion(), version);
        EXPECT_EQ(factory.Calls(), 0);
    }

    TEST_F(ChessGameControlTest, ABadOpponentIsRefusedAndChangesNothing) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("black", "easy"));
        Perform("e2e4");
        const int calls = factory.Calls();
        const Json before = State();
        const uint64_t version = control.GetStateVersion();

        Json playsNumber = Json::object();
        playsNumber["plays"] = 1;
        Json playsBool = Json::object();
        playsBool["plays"] = true;
        Json playsList = Json::object();
        playsList["plays"] = Json::array();
        Json levelNumber = Json::object();
        levelNumber["level"] = 2;
        Json levelNull = Json::object();
        levelNull["level"] = Json();
        // The other member is fine in each of these, and still nothing changes
        Json goodSideBadLevel = OpponentRequest("white", "godlike");
        Json badSideGoodLevel = OpponentRequest("green", "hard");

        const Json refused[] = {OpponentRequest("green"),
                                OpponentRequest("White"),
                                OpponentRequest(""),
                                OpponentState(playsNumber),
                                OpponentState(playsBool),
                                OpponentState(playsList),
                                OpponentRequest("white", "godlike"),
                                OpponentRequest("white", "Easy"),
                                OpponentRequest("white", ""),
                                OpponentState(levelNumber),
                                OpponentState(levelNull),
                                goodSideBadLevel,
                                badSideGoodLevel,
                                OpponentState(Json()),
                                OpponentState(Json("black")),
                                OpponentState(Json::array())};
        for (const Json& request : refused) {
            EXPECT_EQ(SetStateError(request), "invalid_params") << DumpJson(request).c_str();
        }

        // A good position next to a bad opponent, and the other way round: neither half is applied
        Json fenAndBadOpponent = OpponentRequest("green");
        fenAndBadOpponent["fen"] = MakeJsonString(kMateInOneFen);
        Json badFenAndOpponent = OpponentRequest(nullptr, "hard");
        badFenAndOpponent["fen"] = "not a fen";
        Json fenNull = OpponentRequest(nullptr);
        fenNull["fen"] = Json();
        for (const Json& request : {fenAndBadOpponent, badFenAndOpponent, fenNull}) {
            EXPECT_EQ(SetStateError(request), "invalid_params") << DumpJson(request).c_str();
        }

        EXPECT_EQ(State(), before);
        EXPECT_EQ(control.GetStateVersion(), version);
        EXPECT_EQ(factory.Calls(), calls) << "no engine was asked for by a refused call";
    }

    TEST_F(ChessGameControlTest, NoEngineIsEngineUnavailableAndTheFenIsNotLoaded) {
        CountingFactory factory(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
        opponent.SetEngineFactory(factory.AsFactory());
        Perform("e2e4");
        const std::string fenBefore = StringMember(State(), "fen");
        const uint64_t version = control.GetStateVersion();

        Json request = OpponentRequest("black", "hard");
        request["fen"] = MakeJsonString(kMateInOneFen);
        const auto result = control.SetState(request);

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(ErrorCodeOf(result.error()), "engine_unavailable");
        EXPECT_NE(std::string(result.error().message.c_str()).find("no Stockfish in the test"), std::string::npos) << "the sentence of the engine's error is in the message";
        EXPECT_EQ(StringMember(State(), "fen"), fenBefore) << "the position is not replaced";
        EXPECT_EQ(MoveCount(), 1u);
        EXPECT_GT(control.GetStateVersion(), version) << "the error that appeared is a change a client can see";

        const Json computer = OpponentOfState();
        EXPECT_TRUE(FindJsonMember(computer, "plays")->is_null()) << "nobody played and nobody plays: the pick is refused as a whole";
        EXPECT_EQ(StringMember(computer, "level"), "medium") << "the level of the refused pick is not saved either";
        EXPECT_EQ(StringMember(computer, "engine"), "stockfish");
        const Json* error = FindJsonMember(computer, "error");
        ASSERT_NE(error, nullptr);
        EXPECT_EQ(StringMember(*error, "kind"), "launch_failed");
        EXPECT_EQ(StringMember(*error, "detail"), "no Stockfish in the test");
        EXPECT_TRUE(control.PerformAction(MoveAction("e7e5")).has_value()) << "both colors can still move";
    }

    TEST_F(ChessGameControlTest, AnErrorStandingIsAskedAgainByTheSameSideAndClearedByNobody) {
        CountingFactory factory(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
        opponent.SetEngineFactory(factory.AsFactory());

        EXPECT_EQ(SetStateError(OpponentRequest("black")), "engine_unavailable");
        EXPECT_EQ(SetStateError(OpponentRequest("black")), "engine_unavailable");
        EXPECT_EQ(factory.Calls(), 2) << "with an error standing a pick of the same side goes through the factory again";

        SetOpponent(OpponentRequest(nullptr));
        EXPECT_TRUE(OpponentMember("error").is_null());
        EXPECT_EQ(factory.Calls(), 2) << "nobody needs no engine";
    }

    TEST_F(ChessGameControlTest, AnUnchangedOpponentDoesNothing) {
        const auto engine = eastl::make_shared<InstantEngine>();
        CountingFactory factory({engine});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("black", "easy"));
        Perform("e2e4");
        ASSERT_TRUE(UpdateUntil([&] { return MoveCount() == 2; })) << "the program answers";
        const uint64_t version = control.GetStateVersion();

        SetOpponent(OpponentRequest("black", "easy"));
        SetOpponent(OpponentRequest("black"));

        EXPECT_EQ(factory.Calls(), 1) << "the same pick does not make another engine";
        EXPECT_EQ(control.GetStateVersion(), version) << "and does not move the version";
        EXPECT_EQ(MoveCount(), 2u);
    }

    TEST_F(ChessGameControlTest, ThinkingIsTrueWhileTheEngineSearches) {
        const auto engine = eastl::make_shared<GatedEngine>(true);
        CountingFactory factory({engine});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("black"));
        EXPECT_EQ(GetJsonBool(OpponentOfState(), "thinking"), false) << "it is the person's turn";

        Perform("e2e4");
        ASSERT_TRUE(UpdateUntil([&] { return engine->Calls() == 1; })) << "the search started";
        EXPECT_EQ(GetJsonBool(OpponentOfState(), "thinking"), true);
        EXPECT_EQ(MoveCount(), 1u);

        engine->Open();
        ASSERT_TRUE(UpdateUntil([&] { return MoveCount() == 2; }));
        EXPECT_EQ(GetJsonBool(OpponentOfState(), "thinking"), false);
        EXPECT_TRUE(OpponentMember("error").is_null());
    }

    TEST_F(ChessGameControlTest, ASearchThatFailsShowsInTheStateAndMovesTheVersion) {
        CountingFactory factory({eastl::make_shared<FailingEngine>(EngineError{EngineErrorKind::Timeout, "no answer in the test"})});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("black"));
        Perform("e2e4");
        const uint64_t version = control.GetStateVersion();

        ASSERT_TRUE(UpdateUntil([&] { return !OpponentMember("error").is_null(); }));

        const Json error = OpponentMember("error");
        EXPECT_EQ(StringMember(error, "kind"), "timeout");
        EXPECT_EQ(StringMember(error, "detail"), "no answer in the test");
        EXPECT_GT(control.GetStateVersion(), version) << "a client waiting for a change sees the error";

        // The game moves on, so the error is gone: a move of someone else is the version rule
        const uint64_t withError = control.GetStateVersion();
        Perform("e7e5");
        opponent.Update();
        EXPECT_TRUE(OpponentMember("error").is_null());
        EXPECT_GT(control.GetStateVersion(), withError);
    }

    TEST_F(ChessGameControlTest, TheErrorNamesAreOnTheWireInSnakeCase) {
        const std::pair<EngineErrorKind, const char*> kinds[] = {
            {EngineErrorKind::BadRequest, "bad_request"},   {EngineErrorKind::LaunchFailed, "launch_failed"},   {EngineErrorKind::Timeout, "timeout"},
            {EngineErrorKind::ProcessDied, "process_died"}, {EngineErrorKind::ProtocolError, "protocol_error"}, {EngineErrorKind::NoMove, "no_move"},
            {EngineErrorKind::IllegalMove, "illegal_move"}, {EngineErrorKind::Cancelled, "cancelled"},          {EngineErrorKind::Unsupported, "unsupported"}};
        EXPECT_EQ(EnumUtils<EngineErrorKind>::Count(), std::size(kinds)) << "a new kind needs its name here and on the wire";
        for (const auto& [kind, name] : kinds) {
            CountingFactory factory(EngineError{kind, "x"});
            opponent.SetEngineFactory(factory.AsFactory());
            EXPECT_EQ(SetStateError(OpponentRequest("white")), "engine_unavailable");
            EXPECT_EQ(StringMember(OpponentMember("error"), "kind"), name);
        }
    }

    TEST_F(ChessGameControlTest, TheVersionGrowsWithAPickAndNeverGoesBack) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        uint64_t version = control.GetStateVersion();

        SetOpponent(OpponentRequest("black"));
        EXPECT_GT(control.GetStateVersion(), version) << "a side";
        version = control.GetStateVersion();

        SetOpponent(OpponentRequest("white"));
        EXPECT_GT(control.GetStateVersion(), version) << "another side";
        version = control.GetStateVersion();

        SetOpponent(OpponentRequest("white", "max"));
        EXPECT_GT(control.GetStateVersion(), version) << "a level";
        version = control.GetStateVersion();

        SetOpponent(OpponentRequest(nullptr));
        EXPECT_GT(control.GetStateVersion(), version) << "back to two people";
        version = control.GetStateVersion();

        ASSERT_TRUE(control.Reset(Json::object()).has_value());
        EXPECT_GE(control.GetStateVersion(), version);
        version = control.GetStateVersion();

        CountingFactory failing(EngineError{EngineErrorKind::LaunchFailed, "none"});
        opponent.SetEngineFactory(failing.AsFactory());
        EXPECT_EQ(SetStateError(OpponentRequest("black")), "engine_unavailable");
        EXPECT_GT(control.GetStateVersion(), version) << "an error";
        version = control.GetStateVersion();

        ASSERT_TRUE(control.Reset(Json::object()).has_value());
        EXPECT_GT(control.GetStateVersion(), version) << "the reset moves it, whatever the error rule does to the revision";
    }

    TEST_F(ChessGameControlTest, ResetKeepsTheOpponent) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("white", "hard"));
        Perform("e2e4");

        ASSERT_TRUE(control.Reset(Json::object()).has_value());

        const Json computer = OpponentOfState();
        EXPECT_EQ(StringMember(computer, "plays"), "white");
        EXPECT_EQ(StringMember(computer, "level"), "hard");
        EXPECT_EQ(factory.Calls(), 1);
    }

    TEST_F(ChessGameControlTest, AControllerMayMoveForTheProgramsSide) {
        const auto engine = eastl::make_shared<GatedEngine>(true);
        CountingFactory factory({engine});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("black"));
        Perform("e2e4");
        ASSERT_TRUE(UpdateUntil([&] { return engine->Calls() == 1; }));

        // Black is the program's: the controller plays for it all the same, and the search that this makes stale is dropped
        EXPECT_TRUE(control.PerformAction(MoveAction("c7c5")).has_value());
        engine->Open();
        for (int frame = 0; frame < 200; ++frame) {
            opponent.Update();
        }
        EXPECT_EQ(MoveCount(), 2u) << "the answer of the stale search is not played";
        EXPECT_EQ(StringMember(State(), "turn"), "white");
        EXPECT_EQ(ActionMoves().size(), MoveGenerator::GenerateLegal(model.GetPosition()).size()) << "the list of actions is the legal moves of the position, whoever plays for it";
    }

    TEST_F(ChessGameControlTest, ASetPositionWhileTheProgramThinksDropsTheAnswer) {
        const auto engine = eastl::make_shared<GatedEngine>(false);
        CountingFactory factory({engine});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("black"));
        Perform("e2e4");
        ASSERT_TRUE(UpdateUntil([&] { return engine->Calls() == 1; }));

        SetFen(kKiwipeteFen);  // the engine ignores its token and answers later
        engine->Open();
        ASSERT_TRUE(WaitUntil([&] { return engine->Answered() == 1; }));
        for (int frame = 0; frame < 200; ++frame) {
            opponent.Update();
        }
        EXPECT_EQ(MoveCount(), 0u) << "the answer belongs to the game that was replaced";
    }

}  // namespace BECore::Chess::Tests
