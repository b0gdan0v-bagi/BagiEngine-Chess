#include "ChessTestUtil.h"

#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/MoveList.h>

/**
 * BuildMoveList as a pure function: the numbers, White's and Black's move side by side, the ply of each move, the start from a FEN, and the list of a
 * real ChessGameModel. No engine services.
 */
namespace BECore::Chess::Tests {

    namespace {

        constexpr eastl::string_view kStartFen = Position::kStartFen;
        constexpr eastl::string_view kBlackToMoveFen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 7";
        constexpr eastl::string_view kItalianGameText = "1.e4 e5 2.Nf3 Nc6 3.Bc4 Bc5 4.c3 Nf6 5.d4 exd4 6.cxd4";
        const eastl::vector<eastl::string_view> kItalianGame = {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5", "c2c3", "g8f6", "d2d4", "e5d4", "c3d4"};

        /// The moves of a game from @p fen given in UCI; a spelling that is no legal move fails the test
        eastl::vector<Move> MovesOf(eastl::string_view fen, const eastl::vector<eastl::string_view>& line) {
            Position position = PositionFromFen(fen);
            eastl::vector<Move> moves;
            for (const eastl::string_view uci : line) {
                const std::optional<Move> move = MoveGenerator::ParseUci(position, uci);
                if (!move) {
                    ADD_FAILURE() << "Not a legal move: " << eastl::string(uci).c_str() << " in " << position.ToFen().c_str();
                    break;
                }
                position.DoMove(*move);
                moves.push_back(*move);
            }
            return moves;
        }

        /// The rows as a score sheet: "1.e4 e5 2.Nf3", "7...Nf6 8.Nc3" for a row with no White move
        std::string TextOf(const eastl::vector<MoveListRow>& rows) {
            std::string text;
            for (const MoveListRow& row : rows) {
                if (!text.empty()) {
                    text += ' ';
                }
                text += std::to_string(row.number);
                text += row.white.san.empty() ? "..." : ".";
                text += row.white.san.c_str();
                if (!row.black.san.empty()) {
                    text += row.white.san.empty() ? "" : " ";
                    text += row.black.san.c_str();
                }
            }
            return text;
        }

        std::string ListOf(eastl::string_view fen, const eastl::vector<eastl::string_view>& line) {
            return TextOf(BuildMoveList(fen, MovesOf(fen, line)));
        }

        /// The plies of the entries in the order of the rows, White before Black, the places with no move left out
        eastl::vector<int> PliesOf(const eastl::vector<MoveListRow>& rows) {
            eastl::vector<int> plies;
            for (const MoveListRow& row : rows) {
                for (const MoveListEntry* entry : {&row.white, &row.black}) {
                    if (entry->ply >= 0) {
                        plies.push_back(entry->ply);
                    }
                }
            }
            return plies;
        }

        void ExpectEveryPlyOnceInOrder(const eastl::vector<MoveListRow>& rows, size_t moveCount) {
            const eastl::vector<int> plies = PliesOf(rows);
            ASSERT_EQ(plies.size(), moveCount);
            for (size_t i = 0; i < plies.size(); ++i) {
                EXPECT_EQ(plies[i], static_cast<int>(i));
            }
        }

        bool Play(ChessGameModel& model, eastl::string_view uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

    }  // namespace

    // ---- The shape of the rows ----

    TEST(MoveListTest, NoMovesNoRows) {
        EXPECT_TRUE(BuildMoveList(kStartFen, {}).empty());
        EXPECT_TRUE(BuildMoveList("4k3/8/8/8/8/8/8/4K3 b - - 0 9", {}).empty());
    }

    TEST(MoveListTest, OneMoveIsOneRowWithBlackEmpty) {
        const eastl::vector<MoveListRow> rows = BuildMoveList(kStartFen, MovesOf(kStartFen, {"e2e4"}));
        ASSERT_EQ(rows.size(), 1u);
        EXPECT_EQ(rows[0].number, 1);
        EXPECT_EQ(rows[0].white.san, "e4");
        EXPECT_EQ(rows[0].white.ply, 0);
        EXPECT_TRUE(rows[0].black.san.empty());
        EXPECT_EQ(rows[0].black.ply, -1);
    }

    TEST(MoveListTest, FourPliesOfTheItalianGame) {
        const eastl::vector<MoveListRow> rows = BuildMoveList(kStartFen, MovesOf(kStartFen, {"e2e4", "e7e5", "g1f3", "b8c6"}));
        ASSERT_EQ(rows.size(), 2u);
        EXPECT_EQ(rows[0].number, 1);
        EXPECT_EQ(rows[0].white.san, "e4");
        EXPECT_EQ(rows[0].white.ply, 0);
        EXPECT_EQ(rows[0].black.san, "e5");
        EXPECT_EQ(rows[0].black.ply, 1);
        EXPECT_EQ(rows[1].number, 2);
        EXPECT_EQ(rows[1].white.san, "Nf3");
        EXPECT_EQ(rows[1].white.ply, 2);
        EXPECT_EQ(rows[1].black.san, "Nc6");
        EXPECT_EQ(rows[1].black.ply, 3);
    }

    TEST(MoveListTest, TheLastRowMayLackBlack) {
        EXPECT_EQ(ListOf(kStartFen, {"e2e4", "e7e5", "g1f3"}), "1.e4 e5 2.Nf3");
    }

    TEST(MoveListTest, ALongGameKeepsEveryPlyOnceInOrder) {
        const eastl::vector<MoveListRow> rows = BuildMoveList(kStartFen, MovesOf(kStartFen, kItalianGame));
        EXPECT_EQ(TextOf(rows), kItalianGameText.data());
        ExpectEveryPlyOnceInOrder(rows, kItalianGame.size());
    }

    // ---- A game from a FEN ----

    TEST(MoveListTest, BlackToMoveStartsWithAnEmptyWhiteAndKeepsTheFullmoveNumber) {
        const eastl::vector<MoveListRow> rows = BuildMoveList(kBlackToMoveFen, MovesOf(kBlackToMoveFen, {"g8f6", "b1c3", "e7e6"}));
        ASSERT_EQ(rows.size(), 2u);
        EXPECT_EQ(rows[0].number, 7);
        EXPECT_TRUE(rows[0].white.san.empty());
        EXPECT_EQ(rows[0].white.ply, -1);
        EXPECT_EQ(rows[0].black.san, "Nf6");
        EXPECT_EQ(rows[0].black.ply, 0);
        EXPECT_EQ(rows[1].number, 8);
        EXPECT_EQ(rows[1].white.san, "Nc3");
        EXPECT_EQ(rows[1].white.ply, 1);
        EXPECT_EQ(rows[1].black.san, "e6");
        EXPECT_EQ(rows[1].black.ply, 2);
        ExpectEveryPlyOnceInOrder(rows, 3);
    }

    TEST(MoveListTest, ABlackMoveAloneIsOneRow) {
        EXPECT_EQ(ListOf(kBlackToMoveFen, {"c7c5"}), "7...c5");
    }

    TEST(MoveListTest, WhiteToMoveStartsAtItsFullmoveNumber) {
        const char* fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 12";
        EXPECT_EQ(ListOf(fen, {"e2e4", "e7e5", "g1f3"}), "12.e4 e5 13.Nf3");
    }

    TEST(MoveListTest, AFenWithoutClocksCountsFromOne) {
        const char* fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
        EXPECT_EQ(ListOf(fen, {"d2d4", "d7d5"}), "1.d4 d5");
    }

    // ---- What the text says ----

    TEST(MoveListTest, CastlingAndPromotionInTheList) {
        const char* fen = "6k1/4P3/8/8/8/8/8/R3K2R w KQ - 0 1";
        EXPECT_EQ(ListOf(fen, {"e1g1", "g8h8", "e7e8q"}), "1.O-O Kh8 2.e8=Q+");
        EXPECT_EQ(ListOf(fen, {"e1c1", "g8g7", "e7e8n"}), "1.O-O-O Kg7 2.e8=N+");
    }

    TEST(MoveListTest, BlackCastlesBothWays) {
        const char* fen = "r3k2r/8/8/8/8/8/8/4K3 b kq - 0 1";
        EXPECT_EQ(ListOf(fen, {"e8g8"}), "1...O-O");
        EXPECT_EQ(ListOf(fen, {"e8c8"}), "1...O-O-O");
    }

    TEST(MoveListTest, AMateOnTheLastPlyEndsInHash) {
        const eastl::vector<MoveListRow> rows = BuildMoveList(kStartFen, MovesOf(kStartFen, {"f2f3", "e7e5", "g2g4", "d8h4"}));
        EXPECT_EQ(TextOf(rows), "1.f3 e5 2.g4 Qh4#");
        ASSERT_EQ(rows.size(), 2u);
        EXPECT_EQ(rows[1].black.san.back(), '#');
        EXPECT_EQ(rows[1].black.ply, 3);
    }

    TEST(MoveListTest, DisambiguationUsesThePositionBeforeTheMove) {
        const char* fen = "4k3/8/8/8/8/8/4K3/R6R w - - 0 1";
        EXPECT_EQ(ListOf(fen, {"a1d1"}), "1.Rad1");
    }

    // ---- The model ----

    TEST(MoveListTest, TheListOfAModelAfterClicks) {
        ChessGameModel model;
        for (const char* square : {"e2", "e4", "e7", "e5", "g1", "f3"}) {
            model.OnSquareClicked(Sq(square));
        }
        const eastl::vector<MoveListRow> rows = BuildMoveList(model.GetStartFen(), model.GetMoves());
        EXPECT_EQ(TextOf(rows), "1.e4 e5 2.Nf3");
        ExpectEveryPlyOnceInOrder(rows, model.GetMoves().size());
    }

    TEST(MoveListTest, TheListOfAModelAfterSetPosition) {
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        model.SetPosition(PositionFromFen(kBlackToMoveFen));
        EXPECT_TRUE(BuildMoveList(model.GetStartFen(), model.GetMoves()).empty());

        ASSERT_TRUE(Play(model, "g8f6"));
        ASSERT_TRUE(Play(model, "b1c3"));
        const eastl::vector<MoveListRow> rows = BuildMoveList(model.GetStartFen(), model.GetMoves());
        EXPECT_EQ(TextOf(rows), "7...Nf6 8.Nc3");
        ExpectEveryPlyOnceInOrder(rows, 2);
    }

    TEST(MoveListTest, TheListOfAModelThatPlayedTheItalianGame) {
        ChessGameModel model;
        for (const eastl::string_view uci : kItalianGame) {
            ASSERT_TRUE(Play(model, uci));
        }
        const eastl::vector<MoveListRow> rows = BuildMoveList(model.GetStartFen(), model.GetMoves());
        EXPECT_EQ(TextOf(rows), kItalianGameText.data());
        ExpectEveryPlyOnceInOrder(rows, model.GetMoves().size());
    }

    // ---- Input that is wrong ----

    TEST(MoveListTest, ABrokenFenGivesNothing) {
        const eastl::vector<Move> moves = MovesOf(kStartFen, {"e2e4", "e7e5"});
        EXPECT_TRUE(BuildMoveList("", moves).empty());
        EXPECT_TRUE(BuildMoveList("not a fen", moves).empty());
        EXPECT_TRUE(BuildMoveList("8/8/8/8/8/8/8/8 w - - 0 1", moves).empty());
    }

    TEST(MoveListTest, AnIllegalMoveStopsTheList) {
        eastl::vector<Move> moves = MovesOf(kStartFen, {"e2e4", "e7e5"});
        moves.push_back(Mv("e2e4"));  // nothing stands on e2 any more
        moves.push_back(Mv("g1f3"));
        const eastl::vector<MoveListRow> rows = BuildMoveList(kStartFen, moves);
        EXPECT_EQ(TextOf(rows), "1.e4 e5");
        ExpectEveryPlyOnceInOrder(rows, 2);
    }

    TEST(MoveListTest, TheNullMoveAndAFirstIllegalMoveGiveNoRows) {
        EXPECT_TRUE(BuildMoveList(kStartFen, {Move()}).empty());
        EXPECT_TRUE(BuildMoveList(kStartFen, {Mv("e2e5")}).empty());
    }

}  // namespace BECore::Chess::Tests
