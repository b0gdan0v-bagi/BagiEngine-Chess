#include "ChessTestUtil.h"

#include <Chess/Engine/Evaluation.h>

/// The static evaluation: pure functions of a position, nothing to start
namespace BECore::Chess::Tests {

    namespace {

        constexpr int kScoreSlack = 150;

        int EvaluateFen(StringView fen) {
            return Evaluate(PositionFromFen(fen));
        }

        // A position of the middlegame and one of the endgame in which a king moves to the same two squares
        constexpr StringView kMiddlegameKingHome = "r2qk2r/pppppppp/8/8/8/8/PPPPPPPP/R2QK2R w - - 0 1";
        constexpr StringView kMiddlegameKingCenter = "r2qk2r/pppppppp/8/8/4K3/8/PPPPPPPP/R2Q3R w - - 0 1";
        constexpr StringView kEndgameKingHome = "4k3/8/8/8/8/8/P7/4K3 w - - 0 1";
        constexpr StringView kEndgameKingCenter = "4k3/8/8/8/4K3/8/P7/8 w - - 0 1";

    }  // namespace

    TEST(EvaluationTest, TheStartPositionIsEvenForEitherSideToMove) {
        EXPECT_EQ(Evaluate(Position::Start()), 0);
        EXPECT_EQ(EvaluateFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1"), 0);
    }

    TEST(EvaluationTest, TheScoreIsFromTheSideToMove) {
        // White is a queen up
        const int whiteToMove = EvaluateFen("4k3/pppppppp/8/8/8/8/PPPPPPPP/3QK3 w - - 0 1");
        const int blackToMove = EvaluateFen("4k3/pppppppp/8/8/8/8/PPPPPPPP/3QK3 b - - 0 1");

        EXPECT_GT(whiteToMove, 0);
        EXPECT_LT(blackToMove, 0);
        EXPECT_EQ(blackToMove, -whiteToMove);
    }

    TEST(EvaluationTest, AQueenUpIsAboutAQueen) {
        const int score = EvaluateFen("4k3/pppppppp/8/8/8/8/PPPPPPPP/3QK3 w - - 0 1");

        EXPECT_GT(score, kQueenValue - kScoreSlack);
        EXPECT_LT(score, kQueenValue + kScoreSlack);
    }

    TEST(EvaluationTest, ThePiecesHaveTheirUsualOrder) {
        EXPECT_EQ(PieceValue(PieceType::None), 0);
        EXPECT_EQ(PieceValue(PieceType::Pawn), 100);
        EXPECT_EQ(PieceValue(PieceType::Knight), 320);
        EXPECT_EQ(PieceValue(PieceType::Bishop), 330);
        EXPECT_EQ(PieceValue(PieceType::Rook), 500);
        EXPECT_EQ(PieceValue(PieceType::Queen), 900);
        EXPECT_GT(PieceValue(PieceType::King), PieceValue(PieceType::Queen));
    }

    TEST(EvaluationTest, TheEvaluationDoesNotChangeWithTheColors) {
        const StringView fens[] = {
            Position::kStartFen,
            kKiwipeteFen,
            kPosition3Fen,
            kPosition4Fen,
            kPosition5Fen,
            kPosition6Fen,
            "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1",
            "4k3/pppppppp/8/8/8/8/PPPPPPPP/3QK3 w - - 0 1",
            "4k3/8/8/8/4K3/8/P7/8 b - - 0 1",
            "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3",
            "8/2k5/8/8/3N4/8/5B2/6K1 b - - 12 40",
        };
        ASSERT_GE(sizeof(fens) / sizeof(fens[0]), 8U);

        for (const StringView fen : fens) {
            const String mirrored = MirrorFen(fen);
            const String text(fen.data(), fen.size());
            EXPECT_EQ(EvaluateFen(fen), EvaluateFen(StringView(mirrored))) << text << " <-> " << mirrored;
        }
    }

    TEST(EvaluationTest, TheMirrorHelperIsItsOwnInverse) {
        // A check on the test's own tool: a symmetry test with a broken mirror would pass for the wrong reason
        const String once = MirrorFen(kKiwipeteFen);
        const String twice = MirrorFen(StringView(once));

        EXPECT_EQ(twice, String(kKiwipeteFen.data(), kKiwipeteFen.size()));
        EXPECT_EQ(MirrorFen("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3"), "rnbqkbnr/pppp1ppp/8/8/3Pp3/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 3");
    }

    TEST(EvaluationTest, AKnightOnTheRimIsWorthLessThanInTheCenter) {
        const int rim = EvaluateFen("4k3/8/8/8/N7/8/8/4K3 w - - 0 1");
        const int center = EvaluateFen("4k3/8/8/8/3N4/8/8/4K3 w - - 0 1");

        EXPECT_LT(rim, center);
    }

    TEST(EvaluationTest, APawnOnTheSeventhIsWorthMoreThanOnTheSecond) {
        const int seventh = EvaluateFen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
        const int second = EvaluateFen("4k3/8/8/8/8/8/P7/4K3 w - - 0 1");

        EXPECT_GT(seventh, second);
    }

    TEST(EvaluationTest, TheEndgameIsToldByTheMaterialLeft) {
        EXPECT_FALSE(IsEndgame(Position::Start()));
        EXPECT_FALSE(IsEndgame(PositionFromFen(kMiddlegameKingHome)));
        EXPECT_TRUE(IsEndgame(PositionFromFen(kEndgameKingHome)));
        EXPECT_TRUE(IsEndgame(PositionFromFen("4k3/8/8/8/8/8/8/4K3 w - - 0 1")));
        // A queen and a rook each is the limit
        EXPECT_TRUE(IsEndgame(PositionFromFen("3qr1k1/8/8/8/8/8/8/3QR1K1 w - - 0 1")));
        EXPECT_FALSE(IsEndgame(PositionFromFen("3qr1k1/8/8/8/8/8/8/1N1QR1K1 w - - 0 1")));
        // Pawns and kings are not counted
        EXPECT_TRUE(IsEndgame(PositionFromFen("4k3/pppppppp/8/8/8/8/PPPPPPPP/4K3 w - - 0 1")));
    }

    TEST(EvaluationTest, TheKingGoesToTheCenterInTheEndgameAndStaysHomeInTheMiddlegame) {
        // The same step of the white king: from e1 to e4
        const int middlegameStep = EvaluateFen(kMiddlegameKingCenter) - EvaluateFen(kMiddlegameKingHome);
        const int endgameStep = EvaluateFen(kEndgameKingCenter) - EvaluateFen(kEndgameKingHome);

        EXPECT_LT(middlegameStep, 0);
        EXPECT_GT(endgameStep, 0);
    }

    TEST(EvaluationTest, ASearchCanTellAMateFromAnEvaluation) {
        EXPECT_GT(MateIn(3), MateIn(5));
        EXPECT_GT(MateIn(0), MateIn(1));
        EXPECT_EQ(MateIn(0), kMateScore);

        EXPECT_TRUE(IsMateScore(MateIn(3)));
        EXPECT_TRUE(IsMateScore(-MateIn(3)));
        EXPECT_TRUE(IsMateScore(MateIn(kMaxMatePly)));
        EXPECT_TRUE(IsMateScore(-MateIn(kMaxMatePly)));

        EXPECT_FALSE(IsMateScore(kDrawScore));
        EXPECT_FALSE(IsMateScore(kQueenValue));
        EXPECT_FALSE(IsMateScore(-kQueenValue));
        EXPECT_FALSE(IsMateScore(MateIn(kMaxMatePly + 1)));
        EXPECT_FALSE(IsMateScore(-MateIn(kMaxMatePly + 1)));
    }

    TEST(EvaluationTest, NoEvaluationLooksLikeAMate) {
        // The most one side can have: all the pieces of a start position against a lone king
        const int score = EvaluateFen("4k3/8/8/8/8/8/PPPPPPPP/RNBQKBNR w KQ - 0 1");

        EXPECT_FALSE(IsMateScore(score));
        EXPECT_FALSE(IsMateScore(-score));
    }

}  // namespace BECore::Chess::Tests
