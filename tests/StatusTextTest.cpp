#include "ChessTestUtil.h"

#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/StatusText.h>

/// The words of the HUD: pure, they read a model and make text
namespace BECore::Chess::Tests {

    namespace {

        bool Play(ChessGameModel& model, StringView uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        void ExpectWords(const ChessGameModel& model, const char* turn, const char* status) {
            EXPECT_EQ(StatusText::Turn(model), turn);
            EXPECT_EQ(StatusText::Status(model), status);
        }

    }  // namespace

    TEST(StatusTextTest, TheOpeningIsWhitesMoveAndSaysNothingElse) {
        ChessGameModel model;
        ExpectWords(model, "White to move", "");
    }

    TEST(StatusTextTest, TheTurnFollowsTheMoves) {
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        ExpectWords(model, "Black to move", "");
        ASSERT_TRUE(Play(model, "e7e5"));
        ExpectWords(model, "White to move", "");
    }

    TEST(StatusTextTest, ACheckIsNamed) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen("4k3/4r3/8/8/8/8/8/4K3 w - - 0 1"));
        ASSERT_TRUE(model.IsInCheck());
        ExpectWords(model, "White to move", "Check");
    }

    TEST(StatusTextTest, AMateNamesTheWinner) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"));
        ASSERT_TRUE(Play(model, "a1a8"));
        ExpectWords(model, "White wins", "Checkmate");
    }

    TEST(StatusTextTest, ABlackMateNamesBlack) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen("r5k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1"));
        ASSERT_TRUE(Play(model, "a8a1"));
        ExpectWords(model, "Black wins", "Checkmate");
    }

    TEST(StatusTextTest, EveryDrawSaysWhy) {
        ChessGameModel model;

        model.SetPosition(PositionFromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
        ExpectWords(model, "Draw", "Stalemate");

        model.SetPosition(PositionFromFen("8/8/4k3/8/8/4K3/8/8 w - - 0 1"));
        ExpectWords(model, "Draw", "Draw: insufficient material");

        model.SetPosition(PositionFromFen("4k3/8/8/8/8/8/8/R3K3 w - - 100 80"));
        ExpectWords(model, "Draw", "Draw: fifty-move rule");

        model.Reset();
        for (int cycle = 0; cycle < 2; ++cycle) {
            for (const char* uci : {"g1f3", "g8f6", "f3g1", "f6g8"}) {
                ASSERT_TRUE(Play(model, uci)) << uci;
            }
        }
        ExpectWords(model, "Draw", "Draw: threefold repetition");
    }

    TEST(StatusTextTest, WhileTheComputerThinksTheTurnSaysSoInsteadOfWhoseMoveItIs) {
        ChessGameModel model;
        EXPECT_EQ(StatusText::Turn(model, true), "Computer is thinking");
        EXPECT_EQ(StatusText::Turn(model, false), "White to move");
        EXPECT_EQ(StatusText::Turn(model), StatusText::Turn(model, false)) << "without the argument the program is not thinking";
        ASSERT_TRUE(Play(model, "e2e4"));
        EXPECT_EQ(StatusText::Turn(model, true), "Computer is thinking") << "for either side";
        EXPECT_EQ(StatusText::Status(model), "") << "the status of the game says nothing about the search";
    }

    TEST(StatusTextTest, AFinishedGameIsNeverThinking) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"));
        ASSERT_TRUE(Play(model, "a1a8"));
        EXPECT_EQ(StatusText::Turn(model, true), "White wins");

        model.SetPosition(PositionFromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
        EXPECT_EQ(StatusText::Turn(model, true), "Draw");
    }

    TEST(StatusTextTest, AResetStartsOverInWords) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
        ASSERT_TRUE(model.IsGameOver());

        model.Reset();

        ExpectWords(model, "White to move", "");
    }

}  // namespace BECore::Chess::Tests
