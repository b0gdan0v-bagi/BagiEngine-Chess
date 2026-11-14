#include "ChessTestUtil.h"
#include "TestEngines.h"

#include <BECore/Control/Json.h>
#include <Chess/ChessGameControl.h>
#include <Chess/Engine/OwnEngine.h>
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

            ChessGameControlTest() : opponent(model), control(model, opponent, analyst) {
#if CHESS_NO_STOCKFISH
                // The cases below are a world of doubles in which "stockfish" is only a name that a test factory answers (the opponent starts with it, as it does on the desktop). A build without
                // Stockfish starts with the own engine, so the fixture picks the name first, through a factory that makes any double. What a real game of that
                // build does is the cases that say `WithoutStockfish`
                opponent.SetEngineFactory([](OpponentEngine, const OpponentLevel&) -> MadeEngine { return eastl::make_shared<InstantEngine>(); });
                [[maybe_unused]] const auto picked = opponent.SetEngine(OpponentEngine::Stockfish);
                // The factory stays: the default one would refuse the name Stockfish at every pick that names it, also one that changes nothing. A case that needs another sets its own
#endif
            }

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

            /// What a game loop does with the analysis too: the frame of ChessBoardComponent, until @p done
            template <typename Condition>
            bool FrameUntil(Condition done) {
                return WaitUntil([&] {
                    opponent.Update();
                    analyst.Update(model, opponent.GetEngine());
                    return done();
                });
            }

            /// A few frames, no waiting for anything
            void Frames(int count = 100) {
                for (int frame = 0; frame < count; ++frame) {
                    opponent.Update();
                    analyst.Update(model, opponent.GetEngine());
                }
            }

            /// The "analysis" object of the state
            Json AnalysisOfState() {
                const Json state = State();
                const Json* found = FindJsonMember(state, "analysis");
                EXPECT_NE(found, nullptr);
                return found ? *found : Json();
            }

            /// A member of the "analysis" object, copied (null when it is not there)
            Json AnalysisMember(const char* key) {
                const Json analysis = AnalysisOfState();
                const Json* found = FindJsonMember(analysis, key);
                return found ? *found : Json();
            }

            /// {"analysis": {..}}; an empty @p evaluation or @p hint leaves the member out
            static Json AnalysisRequest(std::optional<bool> evaluation, std::optional<bool> hint) {
                Json analysis = Json::object();
                if (evaluation) {
                    analysis["evaluation"] = *evaluation;
                }
                if (hint) {
                    analysis["hint"] = *hint;
                }
                Json request = Json::object();
                request["analysis"] = std::move(analysis);
                return request;
            }

            /// set_state with the analysis; the test fails when it is refused
            void AnalyseWith(std::optional<bool> evaluation, std::optional<bool> hint = std::nullopt) {
                const auto result = control.SetState(AnalysisRequest(evaluation, hint));
                ASSERT_TRUE(result.has_value()) << result.error().message.c_str();
            }

            /// The state of the analysis as one string, to see that a refused call changed nothing
            std::string AnalysisSnapshot() {
                return AnalysisOfState().dump() + " v" + std::to_string(control.GetStateVersion());
            }

            ChessGameModel model;
            OpponentController opponent;
            PositionAnalyst analyst;
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
        const Json first = State();
        const Json second = State();
        EXPECT_EQ(control.GetStateVersion(), before);
        EXPECT_EQ(first.dump(), second.dump()) << "reading the state twice gives the same state";
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
        // Not the fixture's controller: it has picked a name for the doubles. A new one is what a game starts with
        ChessGameModel freshModel;
        OpponentController freshOpponent(freshModel);
        PositionAnalyst freshAnalyst;
        ChessGameControl freshControl(freshModel, freshOpponent, freshAnalyst);
        const auto freshState = freshControl.GetState();
        ASSERT_TRUE(freshState.has_value());
        const Json* freshOpponentState = FindJsonMember(*freshState, "opponent");
        ASSERT_NE(freshOpponentState, nullptr);
        const Json& computer = *freshOpponentState;

        ASSERT_TRUE(computer.is_object());
        const Json* plays = FindJsonMember(computer, "plays");
        ASSERT_NE(plays, nullptr);
        EXPECT_TRUE(plays->is_null()) << "two people";
        EXPECT_EQ(StringMember(computer, "level"), "medium");
        EXPECT_EQ(StringMember(computer, "engine"), kStockfishAvailable ? "stockfish" : "own") << "the engine a game starts with: the other one is a pick of the person";
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
        SKIP_WITHOUT_WORKER_THREADS();
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
        SKIP_WITHOUT_WORKER_THREADS();
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
        SKIP_WITHOUT_WORKER_THREADS();
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

    // ---- The analysis ----

    namespace {

        constexpr AnalysisScore kPawnUp{AnalysisScoreKind::Centipawns, 35};

        eastl::shared_ptr<AnalysingEngine> Analyser(AnalysisScore score = kPawnUp, std::optional<Move> move = std::nullopt) {
            return eastl::make_shared<AnalysingEngine>(score, move);
        }

    }  // namespace

    TEST_F(ChessGameControlTest, TheAnalysisOfANewGameIsOffAndEmpty) {
        const Json analysis = AnalysisOfState();

        ASSERT_TRUE(analysis.is_object());
        EXPECT_EQ(analysis.size(), 9u) << "evaluationOn, hintRequested, thinking, evaluation, hint, lines, ratingsOn, ratings, error";
        EXPECT_EQ(GetJsonBool(analysis, "evaluationOn"), false) << "the evaluation costs a process and CPU: it is off until asked for";
        EXPECT_EQ(GetJsonBool(analysis, "ratingsOn"), false) << "so does the rating of the moves";
        const Json* ratings = FindJsonMember(analysis, "ratings");
        ASSERT_NE(ratings, nullptr);
        EXPECT_TRUE(ratings->is_array());
        EXPECT_TRUE(ratings->empty()) << "no move, no entry";
        EXPECT_EQ(GetJsonBool(analysis, "hintRequested"), false);
        EXPECT_EQ(GetJsonBool(analysis, "thinking"), false);
        for (const char* key : {"evaluation", "hint", "error"}) {
            const Json* member = FindJsonMember(analysis, key);
            ASSERT_NE(member, nullptr) << key;
            EXPECT_TRUE(member->is_null()) << key;
        }
    }

    TEST_F(ChessGameControlTest, TheLinesAreEmptyWhileOffAndNothingIsSearchedForThem) {
        const auto engine = Analyser();
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        const Json lines = AnalysisMember("lines");
        ASSERT_TRUE(lines.is_array());
        EXPECT_TRUE(lines.empty());
        Perform("e2e4");
        Frames();
        EXPECT_TRUE(AnalysisMember("lines").empty()) << "asking for nothing publishes nothing";
        EXPECT_EQ(engine->Calls(), 0);
    }

    TEST_F(ChessGameControlTest, TheLinesAreBestFirstAndFromWhitesView) {
        const auto engine = Analyser(kPawnUp, Mv("e7e5"));
        engine->SetSecondLine({AnalysisScoreKind::Mate, 3}, 5);
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        AnalyseWith(true);
        EXPECT_TRUE(AnalysisMember("lines").empty()) << "not computed yet";
        Perform("e2e4");  // Black to move: both scores are the side to move's, so White sees them turned
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("lines").empty(); }));

        const Json lines = AnalysisMember("lines");
        ASSERT_EQ(lines.size(), 2u);
        EXPECT_EQ(engine->LastRequest().lines, 2) << "the engine is asked for two lines";
        EXPECT_EQ(StringMember(lines[0], "move"), "e7e5");
        EXPECT_EQ(StringMember(lines[0], "text"), "-0.35");
        EXPECT_EQ(GetJsonInt(*FindJsonMember(lines[0], "score"), "value"), -35);
        EXPECT_EQ(StringMember(*FindJsonMember(lines[0], "score"), "kind"), "centipawns");
        EXPECT_EQ(GetJsonInt(lines[0], "depth"), 7);
        EXPECT_NE(StringMember(lines[1], "move"), "e7e5") << "a different move";
        EXPECT_EQ(StringMember(lines[1], "text"), "#-3");
        EXPECT_EQ(StringMember(*FindJsonMember(lines[1], "score"), "kind"), "mate");
        EXPECT_EQ(GetJsonInt(*FindJsonMember(lines[1], "score"), "value"), -3);
        EXPECT_EQ(GetJsonInt(lines[1], "depth"), 5);

        // the first line is what the evaluation and the hint say (the hint's text stays the side to move's)
        EXPECT_EQ(StringMember(AnalysisMember("evaluation"), "text"), "-0.35");
        EXPECT_TRUE(AnalysisMember("hint").is_null()) << "no hint was asked for";
    }

    TEST_F(ChessGameControlTest, ALineAfterAHintAloneIsPublishedAndItsFirstIsTheHint) {
        const auto engine = Analyser(kPawnUp, Mv("d2d4"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        AnalyseWith(std::nullopt, true);
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("hint").is_null(); }));

        const Json lines = AnalysisMember("lines");
        ASSERT_EQ(lines.size(), 2u);
        EXPECT_EQ(StringMember(lines[0], "move"), StringMember(AnalysisMember("hint"), "move"));
        EXPECT_TRUE(AnalysisMember("evaluation").is_null());

        AnalyseWith(std::nullopt, false);
        EXPECT_TRUE(AnalysisMember("lines").empty()) << "dropped with the hint, though the answer is still held";
    }

    TEST_F(ChessGameControlTest, ThePositionWithOneLegalMoveHasOneLine) {
        const auto engine = Analyser();
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        SetFen("7k/8/8/8/8/8/r7/7K w - - 0 1");  // the rook covers g2 and h2: Kg1 is the only legal move
        ASSERT_EQ(ActionMoves().size(), 1u);
        AnalyseWith(true);
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("lines").empty(); }));
        EXPECT_EQ(AnalysisMember("lines").size(), 1u) << "the engine is asked for two, a second legal move does not exist";
        EXPECT_EQ(engine->LastRequest().lines, 2);
    }

    TEST_F(ChessGameControlTest, TheLinesGoWithAMoveAndWithAChangeOfTheEngine) {
        const auto engine = Analyser();
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());
        AnalyseWith(true);
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("lines").empty(); }));

        Perform("e2e4");
        EXPECT_TRUE(AnalysisMember("lines").empty()) << "at once, with no frame between";
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("lines").empty(); }));

        SetOpponent(EngineRequest("own"));
        EXPECT_TRUE(AnalysisMember("lines").empty()) << "the old engine's answer goes at once";
    }

    TEST_F(ChessGameControlTest, NothingIsAnalysedUntilAskedFor) {
        const auto engine = Analyser();
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        Perform("e2e4");
        Frames();
        EXPECT_EQ(factory.Calls(), 0);
        EXPECT_EQ(engine->Calls(), 0);
    }

    TEST_F(ChessGameControlTest, TheEvaluationIsSwitchedOnAndReadFromWhitesView) {
        const auto engine = Analyser(kPawnUp);
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        const uint64_t before = control.GetStateVersion();
        AnalyseWith(true);
        EXPECT_GT(control.GetStateVersion(), before) << "a switch moves the version";
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "evaluationOn"), true);
        EXPECT_TRUE(AnalysisMember("evaluation").is_null()) << "not computed yet";

        const uint64_t switched = control.GetStateVersion();
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("evaluation").is_null(); }));
        EXPECT_GT(control.GetStateVersion(), switched) << "a result that arrives moves the version";

        const Json evaluation = AnalysisMember("evaluation");
        EXPECT_EQ(StringMember(evaluation, "text"), "+0.35");
        EXPECT_EQ(GetJsonInt(evaluation, "depth"), 7);
        const Json* score = FindJsonMember(evaluation, "score");
        ASSERT_NE(score, nullptr);
        EXPECT_EQ(StringMember(*score, "kind"), "centipawns");
        EXPECT_EQ(GetJsonInt(*score, "value"), 35);
        EXPECT_TRUE(AnalysisMember("hint").is_null()) << "no hint was asked for";
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "thinking"), false);

        // The engine says +35 for the side to move; with Black to move that is 35 for Black, so -35 for White
        Perform("e2e4");
        EXPECT_TRUE(AnalysisMember("evaluation").is_null()) << "the answer belonged to the other position";
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("evaluation").is_null(); }));
        const Json black = AnalysisMember("evaluation");
        EXPECT_EQ(StringMember(black, "text"), "-0.35");
        EXPECT_EQ(GetJsonInt(*FindJsonMember(black, "score"), "value"), -35);

        AnalyseWith(false);
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "evaluationOn"), false);
        EXPECT_TRUE(AnalysisMember("evaluation").is_null()) << "off shows nothing";
    }

    TEST_F(ChessGameControlTest, AMateIsReadFromWhitesViewToo) {
        const auto engine = Analyser({AnalysisScoreKind::Mate, 2});
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        Perform("e2e4");  // Black to move, and the engine says Black mates in 2
        AnalyseWith(true);
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("evaluation").is_null(); }));

        const Json evaluation = AnalysisMember("evaluation");
        EXPECT_EQ(StringMember(evaluation, "text"), "#-2");
        const Json* score = FindJsonMember(evaluation, "score");
        ASSERT_NE(score, nullptr);
        EXPECT_EQ(StringMember(*score, "kind"), "mate");
        EXPECT_EQ(GetJsonInt(*score, "value"), -2) << "White is mated in 2";
    }

    TEST_F(ChessGameControlTest, AHintIsAskedForAndReadBack) {
        const auto engine = Analyser(kPawnUp, Mv("d2d4"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        const uint64_t before = control.GetStateVersion();
        AnalyseWith(std::nullopt, true);
        EXPECT_GT(control.GetStateVersion(), before) << "a hint asked moves the version";
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "hintRequested"), true);
        EXPECT_TRUE(AnalysisMember("hint").is_null()) << "null until the answer for this very position has come";

        const uint64_t asked = control.GetStateVersion();
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("hint").is_null(); }));
        EXPECT_GT(control.GetStateVersion(), asked) << "a hint that arrives moves the version";

        const Json hint = AnalysisMember("hint");
        EXPECT_EQ(StringMember(hint, "move"), "d2d4");
        EXPECT_EQ(StringMember(hint, "text"), "+0.35");
        EXPECT_EQ(GetJsonInt(hint, "depth"), 7);
        EXPECT_TRUE(AnalysisMember("evaluation").is_null()) << "the evaluation is off: the search had a score, but it is not shown";
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "evaluationOn"), false);

        const eastl::vector<eastl::string> moves = ActionMoves();
        EXPECT_NE(eastl::find(moves.begin(), moves.end(), "d2d4"), moves.end()) << "the hint is one of the listed actions";
    }

    TEST_F(ChessGameControlTest, AHintAndTheEvaluationAreOneSearch) {
        const auto engine = Analyser(kPawnUp, Mv("d2d4"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        AnalyseWith(true, true);
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("hint").is_null() && !AnalysisMember("evaluation").is_null(); }));
        Frames();
        EXPECT_EQ(engine->Calls(), 1);
    }

    TEST_F(ChessGameControlTest, AMoveDropsTheHintAndTheVersionMovesWithIt) {
        const auto engine = Analyser(kPawnUp, Mv("d2d4"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());
        AnalyseWith(std::nullopt, true);
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("hint").is_null(); }));

        const uint64_t before = control.GetStateVersion();
        Perform("e2e4");

        EXPECT_GT(control.GetStateVersion(), before);
        EXPECT_TRUE(AnalysisMember("hint").is_null()) << "at once, with no frame between";
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "hintRequested"), false);
        const int calls = engine->Calls();
        Frames();
        EXPECT_EQ(engine->Calls(), calls) << "the hint is not asked for again by itself";
        EXPECT_TRUE(AnalysisMember("hint").is_null());
    }

    TEST_F(ChessGameControlTest, ResetAndSetPositionDropTheHint) {
        const auto engine = Analyser(kPawnUp, Mv("d2d4"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        AnalyseWith(std::nullopt, true);
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("hint").is_null(); }));
        ASSERT_TRUE(control.Reset(Json::object()).has_value());
        EXPECT_TRUE(AnalysisMember("hint").is_null());
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "hintRequested"), false);

        AnalyseWith(std::nullopt, true);
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("hint").is_null(); }));
        SetFen(kMateInOneFen);
        EXPECT_TRUE(AnalysisMember("hint").is_null());
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "hintRequested"), false);
    }

    TEST_F(ChessGameControlTest, HintFalseDropsTheHint) {
        const auto engine = Analyser(kPawnUp, Mv("d2d4"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());
        AnalyseWith(std::nullopt, true);
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("hint").is_null(); }));

        const uint64_t before = control.GetStateVersion();
        AnalyseWith(std::nullopt, false);

        EXPECT_TRUE(AnalysisMember("hint").is_null());
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "hintRequested"), false);
        EXPECT_GT(control.GetStateVersion(), before);
    }

    TEST_F(ChessGameControlTest, AHintOnTheProgramsTurnIsNotYourTurnAndChangesNothing) {
        CountingFactory opponents({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(opponents.AsFactory());
        const auto engine = Analyser();
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        SetOpponent(OpponentRequest("black"));  // the person has White
        const std::string before = AnalysisSnapshot();
        Perform("e2e4");  // Black, the program's, is to move
        const std::string moved = AnalysisSnapshot();

        // evaluation: true next to the refused hint is not applied either
        EXPECT_EQ(SetStateError(AnalysisRequest(true, true)), "not_your_turn");
        EXPECT_EQ(AnalysisSnapshot(), moved);
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "evaluationOn"), false);
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "hintRequested"), false);
        Frames();
        EXPECT_EQ(factory.Calls(), 0) << "the engine is never asked";
        EXPECT_EQ(engine->Calls(), 0);
        EXPECT_NE(before, moved);
    }

    TEST_F(ChessGameControlTest, EitherSideMayAskForAHintInAGameOfTwoPeople) {
        const auto engine = Analyser(kPawnUp, Mv("e7e5"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        Perform("e2e4");  // Black to move
        AnalyseWith(std::nullopt, true);
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("hint").is_null(); }));
        EXPECT_EQ(StringMember(AnalysisMember("hint"), "move"), "e7e5");
    }

    TEST_F(ChessGameControlTest, AHintForThePersonsTurnIsAnsweredWhileTheProgramPlaysTheOtherSide) {
        CountingFactory opponents({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(opponents.AsFactory());
        const auto engine = Analyser(kPawnUp, Mv("d2d4"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        SetOpponent(OpponentRequest("black"));
        AnalyseWith(std::nullopt, true);  // White, the person's, is to move
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("hint").is_null(); }));
        EXPECT_EQ(StringMember(AnalysisMember("hint"), "move"), "d2d4");
    }

    TEST_F(ChessGameControlTest, AHintOnAFinishedGameIsGameOver) {
        const auto engine = Analyser();
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());
        SetFen(kMateInOneFen);
        Perform("a1a8");
        ASSERT_TRUE(model.IsGameOver());

        const std::string before = AnalysisSnapshot();
        EXPECT_EQ(SetStateError(AnalysisRequest(true, true)), "game_over");
        EXPECT_EQ(AnalysisSnapshot(), before) << "nothing changed, the evaluation switch of the same call included";
        Frames();
        EXPECT_EQ(factory.Calls(), 0);

        // The evaluation alone may be switched on for a finished game; it analyses nothing
        AnalyseWith(true);
        Frames();
        EXPECT_EQ(factory.Calls(), 0);
        EXPECT_TRUE(AnalysisMember("evaluation").is_null());
    }

    TEST_F(ChessGameControlTest, AnalysisCannotBeCombinedWithAFenOrAnOpponent) {
        const auto engine = Analyser();
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());
        const std::string before = AnalysisSnapshot();
        const std::string fenBefore = StringMember(State(), "fen");

        Json withFen = AnalysisRequest(true, true);
        withFen["fen"] = MakeJsonString(kMateInOneFen);
        EXPECT_EQ(SetStateError(withFen), "invalid_params");

        Json withOpponent = AnalysisRequest(true, std::nullopt);
        withOpponent["opponent"] = Json::object();
        EXPECT_EQ(SetStateError(withOpponent), "invalid_params");

        EXPECT_EQ(AnalysisSnapshot(), before);
        EXPECT_EQ(StringMember(State(), "fen"), fenBefore) << "the position is not replaced";
    }

    TEST_F(ChessGameControlTest, BadAnalysisRequestsAreInvalidParamsAndChangeNothing) {
        const auto engine = Analyser();
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());
        const std::string before = AnalysisSnapshot();

        const auto request = [](const Json& analysis) {
            Json state = Json::object();
            state["analysis"] = analysis;
            return state;
        };
        Json wrongHint = Json::object();
        wrongHint["evaluation"] = true;
        wrongHint["hint"] = "yes";
        Json wrongEvaluation = Json::object();
        wrongEvaluation["evaluation"] = 1;
        Json nullEvaluation = Json::object();
        nullEvaluation["evaluation"] = Json();
        Json unknown = Json::object();
        unknown["evaluation"] = true;
        unknown["hnt"] = true;
        Json wrongRatings = Json::object();
        wrongRatings["ratings"] = 1;
        Json nullRatings = Json::object();
        nullRatings["ratings"] = Json();
        Json unknownNextToRatings = Json::object();
        unknownNextToRatings["ratings"] = true;
        unknownNextToRatings["rating"] = true;

        for (const Json& analysis : {wrongHint, wrongEvaluation, nullEvaluation, unknown, wrongRatings, nullRatings, unknownNextToRatings, Json(5), Json("on"), Json(), Json::array(), Json(true)}) {
            EXPECT_EQ(SetStateError(request(analysis)), "invalid_params") << analysis.dump();
            EXPECT_EQ(AnalysisSnapshot(), before) << analysis.dump();
        }
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "evaluationOn"), false) << "the valid half of a refused call is not applied";
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "ratingsOn"), false) << "nor the ratings next to an unknown member";
        Frames();
        EXPECT_EQ(factory.Calls(), 0);
    }

    TEST_F(ChessGameControlTest, AnEmptyAnalysisObjectChangesNothing) {
        const std::string before = AnalysisSnapshot();
        const auto result = control.SetState(AnalysisRequest(std::nullopt, std::nullopt));
        EXPECT_TRUE(result.has_value());
        EXPECT_EQ(AnalysisSnapshot(), before);
    }

    TEST_F(ChessGameControlTest, TheAnalysisIsThinkingWhileItsEngineSearches) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = eastl::make_shared<AnalysingEngine>(kPawnUp, std::nullopt, 7);
        engine->Gate(true);
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        AnalyseWith(true);
        ASSERT_TRUE(FrameUntil([&] { return engine->Calls() == 1; }));
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "thinking"), true);
        EXPECT_TRUE(AnalysisMember("evaluation").is_null());

        engine->Open();
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("evaluation").is_null(); }));
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "thinking"), false);
    }

    TEST_F(ChessGameControlTest, AFailingEngineShowsAsAnErrorWithItsKind) {
        const auto engine = Analyser();
        engine->FailWith(EngineError{EngineErrorKind::ProcessDied, "the engine died"});
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        AnalyseWith(true);
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("error").is_null(); }));
        const Json error = AnalysisMember("error");
        EXPECT_EQ(StringMember(error, "kind"), "process_died");
        EXPECT_EQ(StringMember(error, "detail"), "the engine died");
        EXPECT_TRUE(AnalysisMember("evaluation").is_null());
        EXPECT_TRUE(OpponentMember("error").is_null()) << "the error of the analysis is not the opponent's";
        Frames();
        EXPECT_EQ(engine->Calls(), 1) << "one error, not a try per frame";

        AnalyseWith(false);  // else the engine is asked about the next position and fails again, which is the right thing
        Perform("e2e4");
        Frames(1);
        EXPECT_TRUE(AnalysisMember("error").is_null()) << "an error goes with the next position";
    }

    TEST_F(ChessGameControlTest, AFactoryThatCannotMakeTheEngineIsAnError) {
        CountingAnalysisFactory factory(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish"});
        analyst.SetEngineFactory(factory.AsFactory());

        AnalyseWith(std::nullopt, true);
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("error").is_null(); }));
        EXPECT_EQ(StringMember(AnalysisMember("error"), "kind"), "launch_failed");
        EXPECT_TRUE(AnalysisMember("hint").is_null());
        EXPECT_EQ(factory.Calls(), 1);
    }

    TEST_F(ChessGameControlTest, ChangingTheEngineDropsTheOldEnginesAnswerAtOnce) {
        const auto stockfish = Analyser({AnalysisScoreKind::Centipawns, 35});
        const auto own = Analyser({AnalysisScoreKind::Centipawns, 80});
        CountingAnalysisFactory factory{stockfish, own};
        analyst.SetEngineFactory(factory.AsFactory());
        AnalyseWith(true);
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("evaluation").is_null(); }));
        ASSERT_EQ(StringMember(AnalysisMember("evaluation"), "text"), "+0.35");

        // Nobody plays, so the pick needs no engine for the opponent; no frame runs between the pick and the read
        SetOpponent(EngineRequest("own"));

        EXPECT_TRUE(AnalysisMember("evaluation").is_null()) << "the answer of the other engine is not shown for the same position";
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("evaluation").is_null(); }));
        EXPECT_EQ(StringMember(AnalysisMember("evaluation"), "text"), "+0.80");
        ASSERT_EQ(factory.Calls(), 2);
        EXPECT_EQ(factory.Engines()[1], "own");
    }

    TEST_F(ChessGameControlTest, AnErrorOfTheOldEngineGoesWithThePick) {
        const auto stockfish = Analyser();
        stockfish->FailWith(EngineError{EngineErrorKind::Timeout, "slow"});
        CountingAnalysisFactory factory{stockfish, Analyser({AnalysisScoreKind::Centipawns, 80})};
        analyst.SetEngineFactory(factory.AsFactory());
        AnalyseWith(true);
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("error").is_null(); }));

        SetOpponent(EngineRequest("own"));
        EXPECT_TRUE(AnalysisMember("error").is_null());
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("evaluation").is_null(); }));
    }

    TEST_F(ChessGameControlTest, TheAnalysisDoesNotChangeTheListOfActions) {
        const auto engine = Analyser(kPawnUp, Mv("d2d4"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());
        const eastl::vector<eastl::string> before = ActionMoves();

        AnalyseWith(true, true);
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("hint").is_null(); }));
        EXPECT_EQ(ActionMoves(), before);
        EXPECT_EQ(Actions().size(), 20u);
    }

    TEST_F(ChessGameControlTest, TheVersionNeverGoesBackWhileTheAnalysisRuns) {
        const auto engine = Analyser(kPawnUp, Mv("d2d4"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        uint64_t last = control.GetStateVersion();
        const auto expectNoDrop = [&] {
            const uint64_t now = control.GetStateVersion();
            EXPECT_GE(now, last);
            last = now;
        };
        AnalyseWith(true, true);
        expectNoDrop();
        ASSERT_TRUE(FrameUntil([&] {
            expectNoDrop();
            return !AnalysisMember("hint").is_null();
        }));
        Perform("e2e4");
        expectNoDrop();
        AnalyseWith(false, false);
        expectNoDrop();
        ASSERT_TRUE(control.Reset(Json::object()).has_value());
        expectNoDrop();
    }

    TEST_F(ChessGameControlTest, SetStateNamesAnalysisAmongTheNeededMembers) {
        const auto result = control.SetState(Json::object());
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(ErrorCodeOf(result.error()), "invalid_params");
        EXPECT_NE(std::string(result.error().message.c_str()).find("analysis"), std::string::npos);
    }

    // ---- Two programs (plays "both", the seats) ----

    namespace {

        /// {"engine": .., "level": ..}; a null member is left out
        Json SeatObject(const char* engine, const char* level = nullptr) {
            Json seat = Json::object();
            if (engine) {
                seat["engine"] = engine;
            }
            if (level) {
                seat["level"] = level;
            }
            return seat;
        }

        /// {"opponent": {"plays": .., "white": .., "black": ..}}; a null JSON seat is left out
        Json SeatsRequest(const Json& plays, const Json& white = Json(), const Json& black = Json()) {
            Json opponent = Json::object();
            opponent["plays"] = plays;
            if (!white.is_null()) {
                opponent["white"] = white;
            }
            if (!black.is_null()) {
                opponent["black"] = black;
            }
            return OpponentState(opponent);
        }

        /// A factory that makes @p engine for the own engine and cannot make Stockfish (as a machine with none)
        struct OwnOnlyFactory {
            eastl::shared_ptr<IChessEngine> engine;
            eastl::vector<eastl::string> asked;  ///< "engine/level" of each call

            EngineFactory AsFactory() {
                return [this](OpponentEngine kind, const OpponentLevel& level) -> MadeEngine {
                    const eastl::string_view id = OpponentEngineId(kind);
                    asked.push_back(Format("{}/{}", id, level.id));
                    if (kind == OpponentEngine::Stockfish) {
                        return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
                    }
                    return engine;
                };
            }
        };

    }  // namespace

    TEST_F(ChessGameControlTest, BothSeatsAreSetAndReadBackWithAnEngineAndALevelEach) {
        CountingFactory factory({eastl::make_shared<InstantEngine>(), eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        const uint64_t version = control.GetStateVersion();

        SetOpponent(SeatsRequest(Json("both"), SeatObject("own", "easy"), SeatObject("stockfish", "hard")));

        const Json computer = OpponentOfState();
        EXPECT_EQ(StringMember(computer, "plays"), "both");
        const Json* seats = FindJsonMember(computer, "seats");
        ASSERT_NE(seats, nullptr);
        const Json* white = FindJsonMember(*seats, "white");
        const Json* black = FindJsonMember(*seats, "black");
        ASSERT_NE(white, nullptr);
        ASSERT_NE(black, nullptr);
        EXPECT_EQ(StringMember(*white, "engine"), "own");
        EXPECT_EQ(StringMember(*white, "level"), "easy");
        EXPECT_EQ(StringMember(*black, "engine"), "stockfish");
        EXPECT_EQ(StringMember(*black, "level"), "hard");
        EXPECT_EQ(StringMember(computer, "engine"), "own") << "the single-seat members are White's seat while two programs play";
        EXPECT_EQ(StringMember(computer, "level"), "easy");
        ASSERT_EQ(factory.Calls(), 2) << "an engine object per seat";
        EXPECT_EQ(factory.Engines()[0], "own");
        EXPECT_EQ(factory.Levels()[0], "easy");
        EXPECT_EQ(factory.Engines()[1], "stockfish");
        EXPECT_EQ(factory.Levels()[1], "hard");
        EXPECT_TRUE(opponent.IsWatching());
        EXPECT_GT(control.GetStateVersion(), version);
        EXPECT_TRUE(OpponentMember("error").is_null());
    }

    TEST_F(ChessGameControlTest, TheSeatsAreAlwaysInTheStateAlsoWhenNobodyPlays) {
        const Json computer = OpponentOfState();
        const Json* seats = FindJsonMember(computer, "seats");
        ASSERT_NE(seats, nullptr);
        ASSERT_TRUE(seats->is_object());
        EXPECT_EQ(seats->size(), 2u);
        for (const char* color : {"white", "black"}) {
            const Json* seat = FindJsonMember(*seats, color);
            ASSERT_NE(seat, nullptr) << color;
            EXPECT_EQ(StringMember(*seat, "engine"), "stockfish") << color;
            EXPECT_EQ(StringMember(*seat, "level"), "medium") << color;
            EXPECT_EQ(seat->size(), 2u) << color;
        }
    }

    TEST_F(ChessGameControlTest, TheOldShapesKeepTheirMeaningWithSeats) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());

        SetOpponent(OpponentRequest(nullptr, "hard"));  // a level alone while nobody plays: both seats
        const Json computer = OpponentOfState();
        const Json* seats = FindJsonMember(computer, "seats");
        ASSERT_NE(seats, nullptr);
        EXPECT_EQ(StringMember(*FindJsonMember(*seats, "white"), "level"), "hard");
        EXPECT_EQ(StringMember(*FindJsonMember(*seats, "black"), "level"), "hard");
        EXPECT_EQ(factory.Calls(), 0);

        SetOpponent(OpponentRequest("black", nullptr, "own"));
        EXPECT_EQ(StringMember(OpponentOfState(), "plays"), "black");
        EXPECT_EQ(StringMember(OpponentOfState(), "engine"), "own");
        EXPECT_EQ(StringMember(OpponentOfState(), "level"), "hard");
        EXPECT_TRUE(opponent.PlaysFor(PieceColor::Black));
        EXPECT_FALSE(opponent.PlaysFor(PieceColor::White));
        EXPECT_EQ(factory.Calls(), 1);

        SetOpponent(OpponentRequest(nullptr));
        EXPECT_TRUE(OpponentMember("plays").is_null());
        EXPECT_FALSE(opponent.PlaysFor(PieceColor::Black));
    }

    TEST_F(ChessGameControlTest, TheTopLevelMembersAreForBothSeatsAndASeatObjectOverridesOne) {
        Json opponentObject = Json::object();
        opponentObject["level"] = "easy";
        opponentObject["engine"] = "own";
        opponentObject["black"] = SeatObject(nullptr, "max");
        SetOpponent(OpponentState(opponentObject));

        const Json computer = OpponentOfState();
        const Json* seats = FindJsonMember(computer, "seats");
        ASSERT_NE(seats, nullptr);
        const Json* white = FindJsonMember(*seats, "white");
        const Json* black = FindJsonMember(*seats, "black");
        EXPECT_EQ(StringMember(*white, "level"), "easy");
        EXPECT_EQ(StringMember(*white, "engine"), "own");
        EXPECT_EQ(StringMember(*black, "level"), "max") << "the seat object comes after the top-level member";
        EXPECT_EQ(StringMember(*black, "engine"), "own");
        EXPECT_TRUE(OpponentMember("plays").is_null()) << "no 'plays': the sides are kept, and nobody played";
    }

    TEST_F(ChessGameControlTest, AMissingMemberIsUnchangedPerSeat) {
        CountingFactory factory({eastl::make_shared<InstantEngine>(), eastl::make_shared<InstantEngine>(), eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(SeatsRequest(Json("both"), SeatObject("own", "easy"), SeatObject("own", "medium")));
        ASSERT_EQ(factory.Calls(), 2);

        Json onlyWhite = Json::object();
        onlyWhite["white"] = SeatObject(nullptr, "hard");
        SetOpponent(OpponentState(onlyWhite));

        const Json computer = OpponentOfState();
        EXPECT_EQ(StringMember(computer, "plays"), "both") << "no 'plays': the sides are kept";
        const Json* seats = FindJsonMember(computer, "seats");
        EXPECT_EQ(StringMember(*FindJsonMember(*seats, "white"), "level"), "hard");
        EXPECT_EQ(StringMember(*FindJsonMember(*seats, "white"), "engine"), "own");
        EXPECT_EQ(StringMember(*FindJsonMember(*seats, "black"), "level"), "medium");
        EXPECT_EQ(StringMember(*FindJsonMember(*seats, "black"), "engine"), "own");
        ASSERT_EQ(factory.Calls(), 3) << "only the seat that changed gets a new engine";
        EXPECT_EQ(factory.Levels()[2], "hard");
    }

    TEST_F(ChessGameControlTest, ABothPickThenOneSideIsATakeoverAndNullEndsIt) {
        CountingFactory factory({eastl::make_shared<InstantEngine>(), eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("both"));
        ASSERT_EQ(factory.Calls(), 2);

        SetOpponent(OpponentRequest("white"));
        EXPECT_EQ(StringMember(OpponentOfState(), "plays"), "white") << "the person has Black";
        EXPECT_TRUE(opponent.PlaysFor(PieceColor::White));
        EXPECT_FALSE(opponent.PlaysFor(PieceColor::Black));
        EXPECT_EQ(factory.Calls(), 2) << "White's seat keeps its engine";

        SetOpponent(OpponentRequest("both"));
        EXPECT_EQ(StringMember(OpponentOfState(), "plays"), "both");
        EXPECT_EQ(factory.Calls(), 3) << "only Black's seat is new";

        SetOpponent(OpponentRequest(nullptr));
        EXPECT_TRUE(OpponentMember("plays").is_null());
        EXPECT_FALSE(opponent.IsWatching());
        EXPECT_FALSE(opponent.PlaysFor(PieceColor::White));
        EXPECT_FALSE(opponent.PlaysFor(PieceColor::Black));
    }

    TEST_F(ChessGameControlTest, ABadSeatPickIsInvalidParamsAndChangesNothing) {
        CountingFactory factory({eastl::make_shared<InstantEngine>(), eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(SeatsRequest(Json("both"), SeatObject("own", "easy"), SeatObject("own", "hard")));
        const Json before = State();
        const uint64_t version = control.GetStateVersion();
        const int calls = factory.Calls();

        Json bad[] = {
            SeatsRequest(Json("Both")),
            SeatsRequest(Json("")),
            SeatsRequest(Json("BOTH")),
            SeatsRequest(Json("both ")),
            SeatsRequest(Json(3)),
            SeatsRequest(Json(true)),
            SeatsRequest(Json::object()),
            SeatsRequest(Json("both"), Json(3)),
            SeatsRequest(Json("both"), Json("own")),
            SeatsRequest(Json("both"), Json::array()),
            SeatsRequest(Json("both"), SeatObject(nullptr, "nope")),
            SeatsRequest(Json("both"), SeatObject("Own")),
            SeatsRequest(Json("both"), SeatObject("other")),
            SeatsRequest(Json("both"), SeatObject("own"), SeatObject(nullptr, "Easy")),
        };
        Json unknownMember = SeatObject("own");
        unknownMember["x"] = 1;
        Json nullLevel = SeatObject("own");
        nullLevel["level"] = Json();
        Json numberEngine = SeatObject(nullptr, "easy");
        numberEngine["engine"] = 7;
        for (const Json& request : bad) {
            EXPECT_EQ(SetStateError(request), "invalid_params") << DumpJson(request).c_str();
        }
        for (const Json& seat : {unknownMember, nullLevel, numberEngine}) {
            EXPECT_EQ(SetStateError(SeatsRequest(Json("both"), seat)), "invalid_params") << DumpJson(seat).c_str();
            EXPECT_EQ(SetStateError(SeatsRequest(Json("both"), Json(), seat)), "invalid_params") << DumpJson(seat).c_str();
        }

        // A bad seat next to a good top-level member or a FEN refuses the whole call
        Json badWithFen = SeatsRequest(Json("white"), SeatObject(nullptr, "nope"));
        badWithFen["fen"] = MakeJsonString(kMateInOneFen);
        EXPECT_EQ(SetStateError(badWithFen), "invalid_params");
        Json badWithLevel = SeatsRequest(Json("white"), Json(), SeatObject("x"));
        badWithLevel["opponent"]["level"] = "max";
        EXPECT_EQ(SetStateError(badWithLevel), "invalid_params");

        EXPECT_EQ(State(), before);
        EXPECT_EQ(control.GetStateVersion(), version);
        EXPECT_EQ(factory.Calls(), calls) << "a refused call asks for no engine";
    }

    TEST_F(ChessGameControlTest, ASeatWhoseEngineIsMissingRefusesTheWholePickEvenWhenTheOtherSeatIsFine) {
        OwnOnlyFactory factory{eastl::make_shared<InstantEngine>(), {}};
        opponent.SetEngineFactory(factory.AsFactory());
        Perform("e2e4");
        const std::string fenBefore = StringMember(State(), "fen");

        Json request = SeatsRequest(Json("both"), SeatObject("own"), SeatObject("stockfish"));
        request["fen"] = MakeJsonString(kMateInOneFen);
        const auto result = control.SetState(request);

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(ErrorCodeOf(result.error()), "engine_unavailable");
        EXPECT_NE(std::string(result.error().message.c_str()).find("no Stockfish in the test"), std::string::npos);
        EXPECT_EQ(StringMember(State(), "fen"), fenBefore) << "the position is not replaced";
        const Json computer = OpponentOfState();
        EXPECT_TRUE(FindJsonMember(computer, "plays")->is_null()) << "the new choice of White's seat was fine and is not applied either";
        EXPECT_FALSE(opponent.PlaysFor(PieceColor::White));
        const Json* seats = FindJsonMember(computer, "seats");
        EXPECT_EQ(StringMember(*FindJsonMember(*seats, "white"), "engine"), "stockfish") << "the choices of a refused pick are not saved";
        EXPECT_EQ(StringMember(*FindJsonMember(*seats, "black"), "engine"), "stockfish");
        EXPECT_EQ(StringMember(*FindJsonMember(computer, "error"), "kind"), "launch_failed");
        EXPECT_EQ(factory.asked.size(), 2u);
    }

    TEST_F(ChessGameControlTest, ARefusedSeatKeepsTheRunningSearchAndTheSides) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto gate = eastl::make_shared<GatedEngine>(true);
        OwnOnlyFactory factory{gate, {}};
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("black", nullptr, "own"));
        Perform("e2e4");
        ASSERT_TRUE(UpdateUntil([&] { return GetJsonBool(OpponentOfState(), "thinking") == true; }));
        ASSERT_EQ(factory.asked.size(), 1u);

        // Black's seat is unchanged and keeps its search; White's new seat asks for Stockfish and fails
        Json request = SeatsRequest(Json("both"), SeatObject("stockfish"), SeatObject("own"));
        request["opponent"]["black"]["level"] = "medium";
        const std::string fenBefore = StringMember(State(), "fen");
        request["fen"] = MakeJsonString(kMateInOneFen);

        EXPECT_EQ(SetStateError(request), "engine_unavailable");
        const Json computer = OpponentOfState();
        EXPECT_EQ(StringMember(computer, "plays"), "black");
        EXPECT_EQ(StringMember(computer, "engine"), "own");
        EXPECT_EQ(GetJsonBool(computer, "thinking"), true) << "the running search is not cancelled";
        EXPECT_FALSE(gate->WasCancelled());
        EXPECT_EQ(StringMember(State(), "fen"), fenBefore);
        EXPECT_EQ(StringMember(*FindJsonMember(computer, "error"), "kind"), "launch_failed");

        gate->Open();
        ASSERT_TRUE(UpdateUntil([&] { return MoveCount() == 2; })) << "the program still answers";
    }

    TEST_F(ChessGameControlTest, AFenAndBothInOneCallApplyTheOpponentFirst) {
        CountingFactory factory({eastl::make_shared<InstantEngine>(), eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());

        Json request = SeatsRequest(Json("both"));
        request["fen"] = MakeJsonString(kMateInOneFen);
        SetOpponent(request);

        EXPECT_EQ(StringMember(State(), "fen"), std::string(kMateInOneFen.data(), kMateInOneFen.size()));
        EXPECT_EQ(StringMember(OpponentOfState(), "plays"), "both");
        EXPECT_EQ(factory.Calls(), 2);
    }

    TEST_F(ChessGameControlTest, ResetKeepsBothSeats) {
        CountingFactory factory({eastl::make_shared<InstantEngine>(), eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(SeatsRequest(Json("both"), SeatObject("own", "easy"), SeatObject("stockfish", "max")));
        const Json seatsBefore = *FindJsonMember(OpponentOfState(), "seats");

        ASSERT_TRUE(control.Reset(Json::object()).has_value());

        EXPECT_EQ(StringMember(OpponentOfState(), "plays"), "both");
        EXPECT_EQ(*FindJsonMember(OpponentOfState(), "seats"), seatsBefore);
        EXPECT_EQ(factory.Calls(), 2) << "no new engines";
    }

    TEST_F(ChessGameControlTest, TwoProgramsPlayEachOtherAndTheMovesAlternate) {
        CountingFactory factory({eastl::make_shared<InstantEngine>(), eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("both"));

        ASSERT_TRUE(UpdateUntil([&] { return MoveCount() >= 6; }));

        const Json state = State();
        EXPECT_TRUE(FindJsonMember(OpponentOfState(), "error")->is_null());
        EXPECT_EQ(model.GetMoves().size() % 2, GetJsonString(state, "turn") == eastl::string("white") ? 0u : 1u) << "the turn follows the number of moves";

        // The pieces that moved are White's, Black's, White's...: the moves replayed on a model of our own
        ChessGameModel replay;
        for (const Move& played : model.GetMoves()) {
            EXPECT_EQ(replay.GetPosition().GetPieceAt(played.From()).color, replay.GetTurn()) << played.ToUci().c_str();
            ASSERT_TRUE(replay.TryMove(played).has_value()) << played.ToUci().c_str();
        }
    }

    TEST_F(ChessGameControlTest, ThinkingIsTrueWhileEitherSeatSearches) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto whiteGate = eastl::make_shared<GatedEngine>(true);
        const auto blackGate = eastl::make_shared<GatedEngine>(true);
        CountingFactory factory({whiteGate, blackGate});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("both"));

        ASSERT_TRUE(UpdateUntil([&] { return GetJsonBool(OpponentOfState(), "thinking") == true; })) << "White's seat searches";
        EXPECT_EQ(whiteGate->Calls(), 1);
        EXPECT_EQ(blackGate->Calls(), 0);
        whiteGate->Open();
        ASSERT_TRUE(UpdateUntil([&] { return blackGate->Calls() == 1; })) << "then Black's";
        EXPECT_EQ(GetJsonBool(OpponentOfState(), "thinking"), true);
    }

    TEST_F(ChessGameControlTest, PerformActionIsNotBlockedWhileTwoProgramsPlay) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto whiteGate = eastl::make_shared<GatedEngine>(false);  // ignores its token: only the version tells that its answer is stale
        const auto blackGate = eastl::make_shared<GatedEngine>(true);
        CountingFactory factory({whiteGate, blackGate});
        opponent.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("both"));
        ASSERT_TRUE(UpdateUntil([&] { return whiteGate->Calls() == 1; }));

        const Json outcome = Perform("e2e4");  // a controller plays for the program's side; the search it makes stale is dropped

        EXPECT_EQ(StringMember(outcome, "move"), "e2e4");
        EXPECT_EQ(MoveCount(), 1u);
        ASSERT_TRUE(UpdateUntil([&] { return blackGate->Calls() == 1; })) << "Black's program searches the position after the controller's move";
        whiteGate->Open();
        ASSERT_TRUE(UpdateUntil([&] { return whiteGate->Answered() == 1; }));
        for (int frame = 0; frame < 200; ++frame) {
            opponent.Update();
        }
        EXPECT_EQ(MoveCount(), 1u) << "White's stale answer is not played";
        EXPECT_TRUE(FindJsonMember(OpponentOfState(), "error")->is_null()) << "and it is no error either";
        EXPECT_EQ(blackGate->Calls(), 1);
        EXPECT_EQ(ActionMoves().empty(), false) << "the list of actions is unchanged by the programs";
    }

    TEST_F(ChessGameControlTest, AHintWhileTwoProgramsPlayIsNotYourTurnOnEitherTurnAndChangesNothing) {
        CountingFactory opponents({eastl::make_shared<InstantEngine>(), eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(opponents.AsFactory());
        const auto engine = Analyser();
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("both"));  // no frame: nothing plays by itself

        for (const char* move : {static_cast<const char*>(nullptr), "e2e4"}) {  // White to move, then Black
            if (move) {
                Perform(move);
            }
            const std::string before = AnalysisSnapshot();
            EXPECT_EQ(SetStateError(AnalysisRequest(true, true)), "not_your_turn") << "turn " << StringMember(State(), "turn");
            EXPECT_EQ(AnalysisSnapshot(), before);
            EXPECT_EQ(GetJsonBool(AnalysisOfState(), "evaluationOn"), false) << "the evaluation switch of that call is not applied either";
        }
        EXPECT_EQ(factory.Calls(), 0);

        // The evaluation alone is allowed, and a hint is again possible once the programs are gone
        AnalyseWith(true);
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "evaluationOn"), true);
        SetOpponent(OpponentRequest(nullptr));
        AnalyseWith(std::nullopt, true);
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "hintRequested"), true);
    }

    TEST_F(ChessGameControlTest, ATakeoverGivesTheHintBackOnThePersonsTurn) {
        CountingFactory opponents({eastl::make_shared<InstantEngine>(), eastl::make_shared<InstantEngine>()});
        opponent.SetEngineFactory(opponents.AsFactory());
        const auto engine = Analyser(kPawnUp, Mv("e7e5"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());
        SetOpponent(OpponentRequest("both"));
        Perform("e2e4");  // Black to move

        SetOpponent(OpponentRequest("white"));  // the person takes Black
        AnalyseWith(std::nullopt, true);
        ASSERT_TRUE(FrameUntil([&] { return !AnalysisMember("hint").is_null(); }));
        EXPECT_EQ(StringMember(AnalysisMember("hint"), "move"), "e7e5");
    }

    // ---- The ratings of the moves (analysis.ratingsOn, analysis.ratings) ----

    namespace {

        /// {"analysis": {"ratings": @p on}}
        Json RatingsRequest(bool on) {
            Json analysis = Json::object();
            analysis["ratings"] = on;
            Json request = Json::object();
            request["analysis"] = std::move(analysis);
            return request;
        }

        /// The ratings array has @p count entries and none is null
        bool AllRated(const Json& ratings, size_t count) {
            if (!ratings.is_array() || ratings.size() != count) {
                return false;
            }
            for (size_t index = 0; index < count; ++index) {
                if (ratings[index].is_null()) {
                    return false;
                }
            }
            return true;
        }

        /// The reasons of the ratings, in order ("<null>" for a move not rated)
        eastl::vector<std::string> ReasonsOfRatings(const Json& ratings) {
            eastl::vector<std::string> reasons;
            for (size_t index = 0; index < ratings.size(); ++index) {
                reasons.push_back(ratings[index].is_null() ? std::string("<null>") : StringMember(ratings[index], "reason"));
            }
            return reasons;
        }

        /// The real OwnEngine that counts its analyses (the default factory's engine, with a count), for a test that must know a search really ran
        class CountingOwnEngine final : public IChessEngine {
        public:
            [[nodiscard]] std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken& token) override {
                return _own.FindMove(request, token);
            }

            [[nodiscard]] std::expected<Analysis, EngineError> Analyse(const SearchRequest& request, const CancellationToken& token) override {
                _analyses.fetch_add(1);
                return _own.Analyse(request, token);
            }

            [[nodiscard]] int Analyses() const {
                return _analyses.load();
            }

        private:
            OwnEngine _own;
            std::atomic<int> _analyses{0};
        };

    }  // namespace

    TEST_F(ChessGameControlTest, TheRatingsAreOffAndNothingIsSearchedForThemUntilSwitchedOn) {
        const auto engine = Analyser(kPawnUp, Mv("e2e4"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        Perform("e2e4");
        Frames();

        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "ratingsOn"), false);
        const Json ratings = AnalysisMember("ratings");
        ASSERT_TRUE(ratings.is_array());
        ASSERT_EQ(ratings.size(), 1u) << "one entry per move of moves";
        EXPECT_TRUE(ratings[0].is_null());
        EXPECT_EQ(factory.Calls(), 0);
    }

    TEST_F(ChessGameControlTest, TheRatingsHaveOneEntryPerMoveInTheOrderOfMovesWithTheBestMoveBrilliant) {
        const auto engine = Analyser(kPawnUp, Mv("e2e4"));  // line 1 is e2e4 for every request, a second line 25 centipawns lower
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());

        ASSERT_TRUE(control.SetState(RatingsRequest(true)).has_value());
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "ratingsOn"), true);
        Perform("e2e4");
        Perform("e7e5");
        EXPECT_EQ(AnalysisMember("ratings").size(), 2u) << "the list has the length of moves from the first read";
        ASSERT_TRUE(FrameUntil([&] { return AllRated(AnalysisMember("ratings"), 2); })) << AnalysisOfState().dump().c_str();

        const Json ratings = AnalysisMember("ratings");
        EXPECT_EQ(ReasonsOfRatings(ratings), (eastl::vector<std::string>{"brilliant", "not_best"}));
        EXPECT_EQ(GetJsonBool(ratings[0], "brilliant"), true);
        EXPECT_EQ(GetJsonBool(ratings[1], "brilliant"), false);
        EXPECT_EQ(ratings[0].size(), 2u) << "brilliant and reason";
        EXPECT_EQ(MoveCount(), 2u);
        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Engines()[0], "own");
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "thinking"), false) << "the analysis is off, so is its search";
        EXPECT_TRUE(AnalysisMember("evaluation").is_null());
        EXPECT_TRUE(AnalysisMember("lines").empty()) << "the rater's lines are not published as the analysis's";
    }

    TEST_F(ChessGameControlTest, AResetDropsTheRatingsAndKeepsTheSwitch) {
        const auto engine = Analyser(kPawnUp, Mv("e2e4"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(control.SetState(RatingsRequest(true)).has_value());
        Perform("e2e4");
        ASSERT_TRUE(FrameUntil([&] { return AllRated(AnalysisMember("ratings"), 1); }));

        ASSERT_TRUE(control.Reset(Json::object()).has_value());

        EXPECT_TRUE(AnalysisMember("ratings").empty());
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "ratingsOn"), true) << "the switch is the person's, a new game keeps it (as the evaluation's)";
        Frames();  // the rater sees the empty game and lets go of the old ratings
        Perform("e2e4");
        EXPECT_TRUE(AnalysisMember("ratings")[0].is_null()) << "not rated until the new game's own search has answered";
        ASSERT_TRUE(FrameUntil([&] { return AllRated(AnalysisMember("ratings"), 1); })) << "the next game is rated from its first move";
        EXPECT_EQ(ReasonsOfRatings(AnalysisMember("ratings")), (eastl::vector<std::string>{"brilliant"}));
    }

    TEST_F(ChessGameControlTest, AFenDropsTheRatingsAndSwitchingOffKeepsThem) {
        const auto engine = Analyser(kPawnUp, Mv("e2e4"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(control.SetState(RatingsRequest(true)).has_value());
        Perform("e2e4");
        ASSERT_TRUE(FrameUntil([&] { return AllRated(AnalysisMember("ratings"), 1); }));

        ASSERT_TRUE(control.SetState(RatingsRequest(false)).has_value());
        Frames();
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "ratingsOn"), false);
        EXPECT_TRUE(AllRated(AnalysisMember("ratings"), 1)) << "off stops the work and keeps what is rated";

        SetFen(kMateInOneFen);
        EXPECT_TRUE(AnalysisMember("ratings").empty()) << "a position set from outside is not the same game";
    }

    TEST_F(ChessGameControlTest, TheRatingsCanBeSwitchedOnForAFinishedGame) {
        const auto engine = Analyser(kPawnUp, Mv("a1a8"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());
        SetFen(kMateInOneFen);
        Perform("a1a8");
        ASSERT_EQ(StringMember(State(), "status"), "checkmate");

        ASSERT_TRUE(control.SetState(RatingsRequest(true)).has_value()) << "no refusal: the moves of a finished game are rated like any";

        ASSERT_TRUE(FrameUntil([&] { return AllRated(AnalysisMember("ratings"), 1); }));
        EXPECT_EQ(ReasonsOfRatings(AnalysisMember("ratings")), (eastl::vector<std::string>{"brilliant"}));
    }

    TEST_F(ChessGameControlTest, ARefusedHintDoesNotApplyTheRatingsSwitchOfTheSameCall) {
        const auto engine = Analyser(kPawnUp, Mv("a1a8"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());
        SetFen(kMateInOneFen);
        Perform("a1a8");
        ASSERT_EQ(StringMember(State(), "status"), "checkmate");
        const std::string before = AnalysisSnapshot();

        Json analysis = Json::object();
        analysis["hint"] = true;
        analysis["ratings"] = true;
        Json request = Json::object();
        request["analysis"] = analysis;
        EXPECT_EQ(SetStateError(request), "game_over");

        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "ratingsOn"), false) << "the ratings switch of a refused call is not applied either";
        EXPECT_EQ(AnalysisSnapshot(), before);
        Frames();
        EXPECT_EQ(factory.Calls(), 0);
    }

    TEST_F(ChessGameControlTest, RatingsWithTheRealOwnEngineAreTheSameWhenTheGameIsPlayedTwice) {
        // The real OwnEngine at depth 5 (no process) behind a counting wrapper; pinned in MoveRaterTest: not_best, brilliant, not_best, brilliant
        const auto engine = eastl::make_shared<CountingOwnEngine>();
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(control.SetState(RatingsRequest(true)).has_value());
        const auto playTheGame = [&] {
            for (const char* uci : {"e2e4", "e7e5", "g1f3", "b8c6"}) {
                Perform(uci);
            }
            EXPECT_TRUE(FrameUntil([&] { return AllRated(AnalysisMember("ratings"), 4); })) << AnalysisOfState().dump().c_str();
            return ReasonsOfRatings(AnalysisMember("ratings"));
        };

        const eastl::vector<std::string> first = playTheGame();
        const int searchesOfTheFirstGame = engine->Analyses();
        EXPECT_EQ(searchesOfTheFirstGame, 4) << "one search per move";

        ASSERT_TRUE(control.Reset(Json::object()).has_value());
        Frames(1);  // the rater sees the empty game and lets go of the old ratings, so the second game is rated from nothing
        ASSERT_TRUE(AnalysisMember("ratings").empty());
        const eastl::vector<std::string> second = playTheGame();

        EXPECT_EQ(first, (eastl::vector<std::string>{"not_best", "brilliant", "not_best", "brilliant"}));
        EXPECT_EQ(engine->Analyses(), 2 * searchesOfTheFirstGame) << "the second game was really searched again, not read from the old ratings";
        EXPECT_EQ(second, first) << "a fixed depth: the same game, the same ratings";
        EXPECT_TRUE(AnalysisMember("error").is_null());
    }

    TEST_F(ChessGameControlTest, ABadRatingsRequestIsInvalidParamsAndChangesNothing) {
        const auto engine = Analyser();
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());
        Perform("e2e4");
        const std::string before = AnalysisSnapshot();
        const std::string fenBefore = StringMember(State(), "fen");

        Json one = Json::object();
        one["ratings"] = 1;
        Json text = Json::object();
        text["ratings"] = "true";
        Json unknownNext = Json::object();
        unknownNext["ratings"] = true;
        unknownNext["ratingsOn"] = true;
        for (const Json& analysis : {one, text, unknownNext}) {
            Json request = Json::object();
            request["analysis"] = analysis;
            EXPECT_EQ(SetStateError(request), "invalid_params") << analysis.dump();
            EXPECT_EQ(AnalysisSnapshot(), before) << analysis.dump();
        }

        // Combined with a fen or an opponent: refused whole, the fen is not loaded and the switch is not applied
        Json combined = FenState(kMateInOneFen);
        combined["analysis"] = RatingsRequest(true)["analysis"];
        EXPECT_EQ(SetStateError(combined), "invalid_params");
        Json withOpponent = OpponentRequest("black");
        withOpponent["analysis"] = RatingsRequest(true)["analysis"];
        EXPECT_EQ(SetStateError(withOpponent), "invalid_params");

        EXPECT_EQ(AnalysisSnapshot(), before);
        EXPECT_EQ(StringMember(State(), "fen"), fenBefore);
        EXPECT_EQ(GetJsonBool(AnalysisOfState(), "ratingsOn"), false);
        Frames();
        EXPECT_EQ(factory.Calls(), 0);
    }

    TEST_F(ChessGameControlTest, TheStateVersionGrowsWithASwitchAndWithARatingThatArrives) {
        const auto engine = Analyser(kPawnUp, Mv("e2e4"));
        CountingAnalysisFactory factory{engine};
        analyst.SetEngineFactory(factory.AsFactory());
        Perform("e2e4");
        const uint64_t before = control.GetStateVersion();

        ASSERT_TRUE(control.SetState(RatingsRequest(true)).has_value());
        const uint64_t switched = control.GetStateVersion();
        EXPECT_GT(switched, before);
        uint64_t last = switched;
        ASSERT_TRUE(FrameUntil([&] {
            EXPECT_GE(control.GetStateVersion(), last) << "never goes back";
            last = control.GetStateVersion();
            return AllRated(AnalysisMember("ratings"), 1);
        }));
        EXPECT_GT(control.GetStateVersion(), switched) << "the rating that arrives is a change a client can wait for";

        const uint64_t rated = control.GetStateVersion();
        Frames();
        EXPECT_EQ(control.GetStateVersion(), rated) << "nothing left to rate, the version stays";
    }
#if CHESS_NO_STOCKFISH

    // ---- Without Stockfish (CHESS_NO_STOCKFISH): the default factory of a real controller ----

    namespace {

        /// A control adapter over a controller of its own, with the default factory and the default engine of this build
        struct RealDefaults {
            RealDefaults() : opponent(model), control(model, opponent, analyst) {}

            Json OpponentState() const {
                const auto state = control.GetState();
                const Json* found = state ? FindJsonMember(*state, "opponent") : nullptr;
                return found != nullptr ? *found : Json();
            }

            ChessGameModel model;
            OpponentController opponent;
            PositionAnalyst analyst;
            ChessGameControl control;
        };

    }  // namespace

    TEST_F(ChessGameControlTest, WithoutStockfishAGameStartsWithTheOwnEngineAndStockfishIsEngineUnavailableAlsoForNobody) {
        RealDefaults game;
        EXPECT_EQ(StringMember(game.OpponentState(), "engine"), "own");
        const uint64_t version = game.control.GetStateVersion();

        const auto result = game.control.SetState(EngineRequest(Json("stockfish")));

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(ErrorCodeOf(result.error()), "engine_unavailable");
        EXPECT_FALSE(std::string(result.error().message.c_str()).empty()) << "the sentence of the refusal";
        const Json computer = game.OpponentState();
        const Json* plays = FindJsonMember(computer, "plays");
        const Json* error = FindJsonMember(computer, "error");
        ASSERT_NE(plays, nullptr);
        ASSERT_NE(error, nullptr);
        EXPECT_TRUE(plays->is_null());
        EXPECT_EQ(StringMember(computer, "engine"), "own") << "nothing is changed";
        EXPECT_EQ(StringMember(*error, "kind"), "unsupported");
        EXPECT_GT(game.control.GetStateVersion(), version) << "the error that appeared is a change a client can see";
    }

    TEST_F(ChessGameControlTest, WithoutStockfishAPickOfStockfishWithASideIsRefusedAsAWholeAndThePositionStays) {
        RealDefaults game;
        Json request = OpponentRequest("black", nullptr, "stockfish");
        request["fen"] = MakeJsonString(kMateInOneFen);

        const auto result = game.control.SetState(request);

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(ErrorCodeOf(result.error()), "engine_unavailable");
        const auto state = game.control.GetState();
        ASSERT_TRUE(state.has_value());
        EXPECT_EQ(StringMember(*state, "fen"), std::string(Position::kStartFen.data(), Position::kStartFen.size())) << "the position is not replaced";
        const Json computer = game.OpponentState();
        const Json* plays = FindJsonMember(computer, "plays");
        ASSERT_NE(plays, nullptr);
        EXPECT_TRUE(plays->is_null());
        EXPECT_EQ(StringMember(computer, "engine"), "own");

        // The seat objects are asked in the same way, also the one nobody plays
        Json seats = OpponentRequest(nullptr);
        seats["opponent"]["black"] = Json::object();
        seats["opponent"]["black"]["engine"] = "stockfish";
        const auto seatResult = game.control.SetState(seats);
        ASSERT_FALSE(seatResult.has_value());
        EXPECT_EQ(ErrorCodeOf(seatResult.error()), "engine_unavailable");
        const Json afterSeats = game.OpponentState();
        const Json* seatsState = FindJsonMember(afterSeats, "seats");
        ASSERT_NE(seatsState, nullptr);
        const Json* blackSeat = FindJsonMember(*seatsState, "black");
        ASSERT_NE(blackSeat, nullptr);
        EXPECT_EQ(StringMember(*blackSeat, "engine"), "own");
    }

    TEST_F(ChessGameControlTest, WithoutStockfishASideAloneWorksAndTheOwnEnginePlaysIt) {
        RealDefaults game;

        const auto result = game.control.SetState(OpponentRequest("black", "easy"));

        ASSERT_TRUE(result.has_value()) << result.error().message.c_str();
        const Json computer = game.OpponentState();
        EXPECT_EQ(StringMember(computer, "plays"), "black");
        EXPECT_EQ(StringMember(computer, "engine"), "own");
        const Json* error = FindJsonMember(computer, "error");
        ASSERT_NE(error, nullptr);
        EXPECT_TRUE(error->is_null());
    }

#endif  // CHESS_NO_STOCKFISH

}  // namespace BECore::Chess::Tests
