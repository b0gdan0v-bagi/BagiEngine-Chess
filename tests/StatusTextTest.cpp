#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/EditorText.h>
#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/StatusText.h>
#include <tests/ChessTestUtil.h>

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

    TEST(StatusTextTest, AResignationNamesTheWinnerAndWhoResigned) {
        ChessGameModel white;
        ASSERT_TRUE(white.Resign(PieceColor::White).has_value());
        ExpectWords(white, "Black wins", "White resigned");

        ChessGameModel black;
        ASSERT_TRUE(Play(black, "e2e4"));
        ASSERT_TRUE(black.Resign(PieceColor::Black).has_value());
        ExpectWords(black, "White wins", "Black resigned");
        EXPECT_EQ(StatusText::Turn(black, true), "White wins") << "a finished game is never thinking";
    }

    TEST(StatusTextTest, AnAgreedDrawIsADrawWithNoWinner) {
        ChessGameModel model;
        ASSERT_TRUE(model.AgreeDraw().has_value());
        ExpectWords(model, "Draw", "Draw agreed");
    }

    TEST(StatusTextTest, TheViewingLineStillWinsOverTheEndOfAResignedGame) {
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(Play(model, "e7e5"));
        ASSERT_TRUE(model.Resign(PieceColor::White).has_value());
        ASSERT_TRUE(model.ShowMoves(1));
        ExpectWords(model, "Black wins", "At move 1 - tap board");
        ASSERT_TRUE(model.EndViewing());
        EXPECT_EQ(StatusText::Status(model), "White resigned");
    }

    TEST(StatusTextTest, ADeclinedDrawHasItsOwnWords) {
        EXPECT_EQ(StatusText::DrawDeclined(), "Draw declined");
    }

    TEST(StatusTextTest, TheMenuAsksBeforeItEndsTheGame) {
        EXPECT_EQ(StatusText::ResignQuestion(), "Resign?");
        EXPECT_EQ(StatusText::DrawQuestion(), "Agree a draw?");
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

    TEST(StatusTextTest, AViewSaysWhichMoveIsShownAndHowToGetBackAndTheTurnStaysTheGames) {
        ChessGameModel model;
        EXPECT_EQ(StatusText::Viewing(model), "");
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(Play(model, "d7d5"));
        ASSERT_TRUE(Play(model, "f1b5"));
        ExpectWords(model, "Black to move", "Check");

        ASSERT_TRUE(model.ShowMoves(0));
        ExpectWords(model, "Black to move", "At the start - tap board");
        ASSERT_TRUE(model.ShowMoves(1));
        EXPECT_EQ(StatusText::Status(model), "At move 1 - tap board");
        ASSERT_TRUE(model.ShowMoves(2));
        EXPECT_EQ(StatusText::Status(model), "At move 1 - tap board") << "White's and Black's move of a number say the same";
        EXPECT_EQ(StatusText::Viewing(model), StatusText::Status(model));

        ASSERT_TRUE(model.EndViewing());
        ExpectWords(model, "Black to move", "Check");
        EXPECT_EQ(StatusText::Viewing(model), "");
    }

    TEST(StatusTextTest, AViewOfAGameFromAFenCountsFromItsFullmoveNumberAndAFinishedGameIsViewedToo) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen("6k1/5ppp/8/8/8/8/5PPP/R5K1 b - - 0 41"));
        ASSERT_TRUE(Play(model, "g8h8"));
        ASSERT_TRUE(Play(model, "a1a8"));
        ASSERT_TRUE(model.IsGameOver());

        ASSERT_TRUE(model.ShowMoves(1));
        EXPECT_EQ(StatusText::Status(model), "At move 41 - tap board") << "the first move is Black's of the number of the FEN";
        EXPECT_EQ(StatusText::Turn(model), "White wins");
        ASSERT_TRUE(model.ShowMoves(0));
        EXPECT_EQ(StatusText::Status(model), "At the start - tap board") << "the start of a game from a FEN is its position, whatever its number";
        ASSERT_TRUE(model.EndViewing());
        EXPECT_EQ(StatusText::Status(model), "Checkmate");
    }

    TEST(StatusTextTest, TheEditorsLineIsTheMessageThenTheLastRefusalThenEditing) {
        PositionEditor editor;
        EXPECT_EQ(StatusText::Editing(editor, ""), "Editing");

        EXPECT_EQ(StatusText::Editing(editor, "Needs one king each"), "Needs one king each") << "the reason of a refused Play wins";

        editor.SetStart();
        ASSERT_FALSE(editor.Put(Sq("e3"), Piece{PieceType::Pawn, PieceColor::White}).has_value()) << "the 17th piece of a color";
        ASSERT_TRUE(editor.GetLastRefusal().has_value());
        EXPECT_EQ(StatusText::Editing(editor, ""), EditorText::Refusal(EditRefusal::TooManyPieces));

        editor.Clear();
        EXPECT_EQ(StatusText::Editing(editor, ""), "Editing") << "the next edit that works takes the refusal away";
    }

    TEST(StatusTextTest, ThePuzzleTurnNamesEveryPhase) {
        EXPECT_EQ(StatusText::PuzzleTurn(PuzzlePhase::YourMove), "Your move");
        EXPECT_EQ(StatusText::PuzzleTurn(PuzzlePhase::Solved), "Solved");
        for (const PuzzlePhase phase : EnumUtils<PuzzlePhase>::Values()) {
            EXPECT_FALSE(StatusText::PuzzleTurn(phase).empty()) << static_cast<int>(phase);
        }
        EXPECT_NE(StatusText::PuzzleTurn(PuzzlePhase::Replying), StatusText::PuzzleTurn(PuzzlePhase::YourMove)) << "the person cannot move while the reply is due";
    }

    TEST(StatusTextTest, ThePuzzleStatusIsEmptyUnlessTheLastMoveWasWrong) {
        EXPECT_EQ(StatusText::PuzzleStatus(false), "");
        EXPECT_EQ(StatusText::PuzzleStatus(true), "Wrong move");
    }

    TEST(StatusTextTest, ThePuzzleNumberCountsFromOneAndTheRatingIsTheNumber) {
        EXPECT_EQ(StatusText::PuzzleNumber(0, 130), "Puzzle 1 of 130");
        EXPECT_EQ(StatusText::PuzzleNumber(11, 130), "Puzzle 12 of 130");
        EXPECT_EQ(StatusText::PuzzleNumber(9998, 9999), "Puzzle 9999 of 9999");
        EXPECT_EQ(StatusText::PuzzleRating(1450), "Rating 1450");
        EXPECT_EQ(StatusText::PuzzleRating(999), "Rating 999");
    }

    TEST(StatusTextTest, ThePuzzleProgressAddsTheMistakesOnlyWhenThereAreSome) {
        EXPECT_EQ(StatusText::PuzzleProgress(PuzzlePhase::YourMove, false, 0), "Your move");
        EXPECT_EQ(StatusText::PuzzleProgress(PuzzlePhase::Replying, false, 0), StatusText::PuzzleTurn(PuzzlePhase::Replying));
        EXPECT_EQ(StatusText::PuzzleProgress(PuzzlePhase::YourMove, true, 1), "Wrong move, 1 mistake");
        EXPECT_EQ(StatusText::PuzzleProgress(PuzzlePhase::YourMove, false, 2), "Your move, 2 mistakes");
        EXPECT_EQ(StatusText::PuzzleProgress(PuzzlePhase::Solved, false, 3), "Solved, 3 mistakes");
        EXPECT_EQ(StatusText::PuzzleProgress(PuzzlePhase::Solved, false, 0), "Solved");
    }

    TEST(StatusTextTest, TheOneButtonOfStartAndNextSaysWhichItIs) {
        EXPECT_EQ(StatusText::PuzzleNextWord(false), "Start");
        EXPECT_EQ(StatusText::PuzzleNextWord(true), "Next");
        EXPECT_EQ(StatusText::NoPuzzles(), "No puzzles");
    }

    TEST(StatusTextTest, EveryEditorLineFitsTheHud) {
        PositionEditor editor;
        EXPECT_LE(StatusText::Editing(editor, "").size(), EditorText::kMaxRefusalChars);
        for (const FenError error : EnumUtils<FenError>::Values()) {
            EXPECT_LE(StatusText::Editing(editor, EditorText::Refusal(error)).size(), EditorText::kMaxRefusalChars);
        }
    }

}  // namespace BECore::Chess::Tests
