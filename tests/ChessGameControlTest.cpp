#include "ChessTestUtil.h"

#include <Chess/ChessGameControl.h>
#include <Chess/Game/MoveGenerator.h>
#include <EASTL/algorithm.h>

/**
 * Coverage of ChessGameControl: the state, the actions and the ways to act, on a model that the test owns.
 * The adapter needs no scene, only the model.
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

        class ChessGameControlTest : public ::testing::Test {
        protected:
            ChessGameControlTest() : control(model) {}

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

            ChessGameModel model;
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

}  // namespace BECore::Chess::Tests
