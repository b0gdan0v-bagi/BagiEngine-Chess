#include "ChessTestUtil.h"

/**
 * Game status: checkmate, stalemate and the draws that end a game by themselves (EvaluateStatus), on positions given by FEN
 * and on lines played from the start.
 */
namespace BECore::Chess::Tests {

    namespace {

        GameStatus StatusOf(eastl::string_view fen) {
            return EvaluateStatus(PositionFromFen(fen));
        }

    }  // namespace

    // ---- Checkmate ----

    TEST(RulesTest, FoolsMate) {
        EXPECT_EQ(StatusOf("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3"), GameStatus::Checkmate);
    }

    TEST(RulesTest, ScholarsMate) {
        EXPECT_EQ(StatusOf("r1bqkb1r/pppp1Qpp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4"), GameStatus::Checkmate);
    }

    TEST(RulesTest, BackRankMate) {
        EXPECT_EQ(StatusOf("R5k1/5ppp/8/8/8/8/8/4K3 b - - 0 1"), GameStatus::Checkmate);
    }

    TEST(RulesTest, SmotheredMate) {
        EXPECT_EQ(StatusOf("6rk/5Npp/8/8/8/8/8/4K3 b - - 0 1"), GameStatus::Checkmate);
    }

    TEST(RulesTest, QueenMateProtectedByABishop) {
        // The queen on h7 is protected by the bishop on b1, so the king cannot take it, and its other squares hold its own men.
        EXPECT_EQ(StatusOf("6rk/6pQ/8/8/8/8/8/1B2K3 b - - 0 1"), GameStatus::Checkmate);
    }

    TEST(RulesTest, CheckThatCanBeBlockedIsNotMate) {
        // The back-rank mate again, but the rook on d7 can step to d8 in front of the check.
        EXPECT_EQ(StatusOf("R5k1/3r1ppp/8/8/8/8/8/4K3 b - - 0 1"), GameStatus::Ongoing);
    }

    TEST(RulesTest, CheckThatCanBeCapturedIsNotMate) {
        // The back-rank mate again, but the rook on a2 can take the checking rook on a8.
        EXPECT_EQ(StatusOf("R5k1/5ppp/8/8/8/8/r7/4K3 b - - 0 1"), GameStatus::Ongoing);
    }

    TEST(RulesTest, CheckThatTheKingCanEscapeIsNotMate) {
        EXPECT_EQ(StatusOf("R5k1/5pp1/8/8/8/8/8/4K3 b - - 0 1"), GameStatus::Ongoing);
    }

    TEST(RulesTest, MateOnTheLastPlyOfTheSeventyFiveMoveRuleStaysMate) {
        EXPECT_EQ(StatusOf("R5k1/5ppp/8/8/8/8/8/4K3 b - - 150 80"), GameStatus::Checkmate);
    }

    // ---- Stalemate ----

    TEST(RulesTest, ClassicStalemate) {
        EXPECT_EQ(StatusOf("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"), GameStatus::Stalemate);
    }

    TEST(RulesTest, KingBoxedInByAPawnAndTheEnemyKingIsStalemate) {
        // The pawn on a7 covers b8 and is protected by the king on b6, which covers b7: not in check, no square left.
        EXPECT_EQ(StatusOf("k7/P7/1K6/8/8/8/8/8 b - - 0 1"), GameStatus::Stalemate);
    }

    // ---- Ongoing ----

    TEST(RulesTest, StartPositionIsOngoing) {
        EXPECT_EQ(EvaluateStatus(Position::Start()), GameStatus::Ongoing);
    }

    TEST(RulesTest, CheckAloneDoesNotEndTheGame) {
        EXPECT_EQ(StatusOf("4r1k1/8/8/R7/8/2B5/8/4K3 w - - 0 1"), GameStatus::Ongoing);
    }

    // ---- Draws ----

    TEST(RulesTest, DeadPositionIsADraw) {
        EXPECT_EQ(StatusOf("4k3/8/8/8/8/8/8/4K3 w - - 0 1"), GameStatus::DrawInsufficientMaterial);
        EXPECT_EQ(StatusOf("4k3/8/8/8/8/8/8/3NK3 w - - 0 1"), GameStatus::DrawInsufficientMaterial);
        EXPECT_EQ(StatusOf("4kb2/8/8/8/8/8/8/2B1K3 w - - 0 1"), GameStatus::DrawInsufficientMaterial);
    }

    TEST(RulesTest, MaterialThatCanMateKeepsTheGameGoing) {
        EXPECT_EQ(StatusOf("4k3/8/8/8/8/8/8/3RK3 w - - 0 1"), GameStatus::Ongoing);
        EXPECT_EQ(StatusOf("2b1k3/8/8/8/8/8/8/2B1K3 w - - 0 1"), GameStatus::Ongoing);
    }

    TEST(RulesTest, SeventyFiveMoveRuleEndsTheGame) {
        EXPECT_EQ(StatusOf("4k3/8/8/8/8/8/8/R3K3 w - - 149 100"), GameStatus::Ongoing);
        EXPECT_EQ(StatusOf("4k3/8/8/8/8/8/8/R3K3 w - - 150 100"), GameStatus::DrawSeventyFiveMoves);
    }

    TEST(RulesTest, FivefoldRepetitionEndsTheGame) {
        Position position = Position::Start();
        const Move cycle[] = {Mv("g1f3"), Mv("g8f6"), Mv("f3g1"), Mv("f6g8")};

        // Each full cycle brings the start position back once more: it has occurred 2, 3, 4 and then 5 times.
        for (int occurrence = 2; occurrence <= 5; ++occurrence) {
            for (const Move move : cycle) {
                position.DoMove(move);
            }
            ASSERT_EQ(position.RepetitionCount(), occurrence);
            EXPECT_EQ(EvaluateStatus(position), occurrence < 5 ? GameStatus::Ongoing : GameStatus::DrawFivefold) << "occurrence " << occurrence;
        }
    }

}  // namespace BECore::Chess::Tests
