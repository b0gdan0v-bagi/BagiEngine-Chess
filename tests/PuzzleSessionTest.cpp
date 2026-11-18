#include <Chess/Game/PuzzleSession.h>
#include <gtest/gtest.h>
#include <tests/ChessTestUtil.h>

/**
 * The judge of a puzzle over a bare ChessGameModel, with small hand-made puzzles. The model never draws a seed here, and no test sleeps: the program's reply
 * is waited for with the game time given to Update().
 */
namespace BECore::Chess::Tests {

    namespace {

        /// Black's setup h8g8, then the person's Ra1a7 and Rb2b8# after the forced-looking Kg8h8: a mate in 2 with a reply
        constexpr StringView kMateInTwo = "ladder\t7k/8/8/8/8/8/1R6/R6K b - - 0 1\th8g8 a1a7 g8h8 b2b8\t1200\tmateIn2\n";
        /// A mate in 1 with the one mate Ra1a8
        constexpr StringView kMateInOne = "back\t6k1/1p3ppp/8/8/8/8/8/R3K3 b - - 0 1\tb7b6 a1a8\t800\tmateIn1\n";
        /// A mate in 1 that Ra1a8 and Rd1d8 both give
        constexpr StringView kTwoMates = "twin\t6k1/2p2ppp/8/8/8/8/8/R2R2K1 b - - 0 1\tc7c6 a1a8\t900\tmateIn1\n";
        /// The solver is Black
        constexpr StringView kBlackSolves = "black\tr3k3/8/8/8/8/8/1P3PPP/6K1 w - - 0 1\tb2b3 a8a1\t1000\tmateIn1\n";
        /// A promotion that mates, as a queen or as a rook
        constexpr StringView kPromotes = "promo\t7k/P7/6K1/8/8/8/8/8 b - - 0 1\th8g8 a7a8q\t1100\tmateIn1 promotion\n";

        /// A power of two, so that kPuzzleReplySeconds is a whole number of frames in floating point too
        constexpr float kFrame = 0.125f;

        PuzzleSet SetOf(std::initializer_list<StringView> rows) {
            String tsv = "id\tfen\tmoves\trating\tthemes\n";
            for (const StringView row : rows) {
                tsv.append(row.data(), row.size());
            }
            Vector<String> problems;
            PuzzleSet set = PuzzleSet::Parse(StringView(tsv.data(), tsv.size()), &problems);
            EXPECT_TRUE(problems.empty());
            return set;
        }

        class PuzzleSessionTest : public ::testing::Test {
        protected:
            void Load(std::initializer_list<StringView> rows) {
                session.SetPuzzles(SetOf(rows));
            }

            /// Begins puzzle @p index and fails the test when it does not begin
            void Start(size_t index = 0) {
                ASSERT_TRUE(session.Begin(model, index));
            }

            /// Plays @p uci for whoever is to move, the way a click or perform_action does
            void Play(StringView uci) {
                const Optional<Move> move = MoveGenerator::ParseUci(model.GetPosition(), uci);
                ASSERT_TRUE(move.has_value()) << String(uci).c_str();
                ASSERT_TRUE(model.TryMove(*move).has_value()) << String(uci).c_str();
            }

            /// Frames of @p seconds in steps of kFrame
            void RunFrames(float seconds) {
                for (float done = 0.0f; done < seconds - kFrame / 2; done += kFrame) {
                    session.Update(model, kFrame);
                }
            }

            ChessGameModel model;
            PuzzleSession session;
        };

    }  // namespace

    TEST_F(PuzzleSessionTest, BeginPlaysTheSetupAndTheFirstMoveIsTheFollowing) {
        Load({kMateInOne});
        const uint64_t before = session.GetRevision();

        Start();

        EXPECT_TRUE(session.IsActiveFor(model));
        ASSERT_EQ(model.GetMoves().size(), 1U);
        EXPECT_EQ(model.GetMoves()[0].ToUci(), "b7b6");
        EXPECT_EQ(model.GetStartFen(), "6k1/1p3ppp/8/8/8/8/8/R3K3 b - - 0 1");
        EXPECT_EQ(model.GetTurn(), PieceColor::White);
        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::YourMove);
        EXPECT_EQ(session.GetMistakes(), 0U);
        EXPECT_FALSE(session.WasWrong());
        EXPECT_EQ(session.GetStep(model), 0U);
        EXPECT_EQ(session.GetSteps(), 1U);
        ASSERT_TRUE(session.GetIndex().has_value());
        EXPECT_EQ(*session.GetIndex(), 0U);
        ASSERT_NE(session.GetCurrent(), nullptr);
        EXPECT_EQ(session.GetCurrent()->id, "back");
        EXPECT_GT(session.GetRevision(), before);
    }

    TEST_F(PuzzleSessionTest, AnIndexOutsideTheSetChangesNothing) {
        Load({kMateInOne});
        Play("e2e4");
        const uint64_t version = model.GetVersion();
        const uint64_t revision = session.GetRevision();

        EXPECT_FALSE(session.Begin(model, 1));

        EXPECT_EQ(model.GetVersion(), version);
        EXPECT_EQ(session.GetRevision(), revision);
        EXPECT_FALSE(session.IsActiveFor(model));
        EXPECT_FALSE(session.GetIndex().has_value());
    }

    TEST_F(PuzzleSessionTest, WithNoPuzzleBegunNothingIsActiveAndUpdateDoesNothing) {
        Load({kMateInOne});

        EXPECT_FALSE(session.IsActiveFor(model));
        EXPECT_FALSE(session.Update(model, 1.0f));
        EXPECT_EQ(session.GetSteps(), 0U);
        EXPECT_EQ(session.GetCurrent(), nullptr);
    }

    TEST_F(PuzzleSessionTest, TheRightMoveOfAMateInOneSolvesIt) {
        Load({kMateInOne});
        Start();

        Play("a1a8");
        EXPECT_TRUE(session.IsActiveFor(model));
        EXPECT_TRUE(session.Update(model, 0.0f));

        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::Solved);
        EXPECT_EQ(session.GetStep(model), 1U);
        EXPECT_EQ(session.GetMistakes(), 0U);
        EXPECT_FALSE(session.WasWrong());
        EXPECT_TRUE(model.IsGameOver());
        EXPECT_FALSE(session.Update(model, 10.0f));
        EXPECT_EQ(model.GetMoves().size(), 2U);
    }

    TEST_F(PuzzleSessionTest, ARightMoveIsAnsweredAfterTheReplyDelayOfGameTime) {
        Load({kMateInTwo});
        Start();

        Play("a1a7");
        EXPECT_TRUE(session.Update(model, kFrame));
        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::Replying);
        EXPECT_EQ(session.GetStep(model), 1U);
        EXPECT_EQ(model.GetMoves().size(), 2U);

        RunFrames(kPuzzleReplySeconds - 2 * kFrame);
        EXPECT_EQ(model.GetMoves().size(), 2U);
        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::Replying);

        RunFrames(2 * kFrame);
        ASSERT_EQ(model.GetMoves().size(), 3U);
        EXPECT_EQ(model.GetMoves()[2].ToUci(), "g8h8");
        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::YourMove);
        EXPECT_EQ(session.GetStep(model), 1U);
        EXPECT_EQ(session.GetMistakes(), 0U);

        Play("b2b8");
        EXPECT_TRUE(session.Update(model, kFrame));
        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::Solved);
        EXPECT_EQ(session.GetStep(model), 2U);
        EXPECT_EQ(session.GetSteps(), 2U);
    }

    TEST_F(PuzzleSessionTest, FramesOfZeroNeverPlayTheReply) {
        Load({kMateInTwo});
        Start();
        Play("a1a7");

        for (int frame = 0; frame < 100; ++frame) {
            session.Update(model, 0.0f);
        }

        EXPECT_EQ(model.GetMoves().size(), 2U);
        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::Replying);
    }

    TEST_F(PuzzleSessionTest, ALongFrameThatSeesTheMoveDoesNotAnswerItAtOnce) {
        Load({kMateInTwo});
        Start();
        Play("a1a7");

        EXPECT_TRUE(session.Update(model, 10.0f));
        EXPECT_EQ(model.GetMoves().size(), 2U);

        EXPECT_FALSE(session.Update(model, 0.0f));
        EXPECT_EQ(model.GetMoves().size(), 2U);

        EXPECT_TRUE(session.Update(model, kPuzzleReplySeconds));
        EXPECT_EQ(model.GetMoves().size(), 3U);
    }

    TEST_F(PuzzleSessionTest, AWrongMoveIsTakenBackAndCounted) {
        Load({kMateInOne});
        Start();
        const uint64_t revision = session.GetRevision();

        Play("a1a2");
        EXPECT_TRUE(session.IsActiveFor(model));
        EXPECT_TRUE(session.Update(model, kFrame));

        ASSERT_EQ(model.GetMoves().size(), 1U);
        EXPECT_EQ(model.GetTurn(), PieceColor::White);
        EXPECT_TRUE(session.IsActiveFor(model));
        EXPECT_EQ(session.GetMistakes(), 1U);
        EXPECT_TRUE(session.WasWrong());
        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::YourMove);
        EXPECT_EQ(session.GetStep(model), 0U);
        EXPECT_GT(session.GetRevision(), revision);

        EXPECT_FALSE(session.Update(model, kFrame));
        EXPECT_EQ(session.GetMistakes(), 1U);
        EXPECT_TRUE(session.WasWrong());
    }

    TEST_F(PuzzleSessionTest, WasWrongClearsWithThePersonsNextMove) {
        Load({kMateInOne});
        Start();
        Play("a1a2");
        session.Update(model, kFrame);
        ASSERT_TRUE(session.WasWrong());

        Play("a1a8");
        session.Update(model, kFrame);

        EXPECT_FALSE(session.WasWrong());
        EXPECT_EQ(session.GetMistakes(), 1U);
        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::Solved);
    }

    TEST_F(PuzzleSessionTest, TwoWrongMovesAreTwoMistakes) {
        Load({kMateInOne});
        Start();

        Play("a1a2");
        session.Update(model, kFrame);
        Play("e1e2");
        session.Update(model, kFrame);

        EXPECT_EQ(session.GetMistakes(), 2U);
        EXPECT_EQ(model.GetMoves().size(), 1U);
    }

    TEST_F(PuzzleSessionTest, ACheckThatIsNotMateIsWrong) {
        Load({kMateInTwo});
        Start();

        Play("a1a8");
        session.Update(model, kFrame);

        EXPECT_EQ(session.GetMistakes(), 1U);
        EXPECT_EQ(model.GetMoves().size(), 1U);
        EXPECT_FALSE(model.IsGameOver());
    }

    TEST_F(PuzzleSessionTest, AnotherMateSolvesAtOnce) {
        Load({kTwoMates});
        Start();

        Play("d1d8");
        session.Update(model, kFrame);

        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::Solved);
        EXPECT_EQ(session.GetMistakes(), 0U);
        EXPECT_FALSE(session.WasWrong());
        EXPECT_EQ(session.GetStep(model), 1U);
        EXPECT_TRUE(session.IsActiveFor(model));
        EXPECT_EQ(model.GetStatus(), GameStatus::Checkmate);
    }

    TEST_F(PuzzleSessionTest, TheLastMateOfALongerLineStopsTheReplies) {
        Load({kMateInTwo});
        Start();

        Play("a1a7");
        session.Update(model, kFrame);
        session.Update(model, kPuzzleReplySeconds);
        Play("b2b8");
        session.Update(model, kFrame);

        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::Solved);
        EXPECT_FALSE(session.Update(model, 10.0f));
    }

    TEST_F(PuzzleSessionTest, APromotionOfTheLineAndAnotherMatingOneSolveAndAWeakOneIsWrong) {
        Load({kPromotes});
        Start();

        Play("a7a8n");
        session.Update(model, kFrame);
        EXPECT_EQ(session.GetMistakes(), 1U);
        EXPECT_EQ(model.GetMoves().size(), 1U);

        Play("a7a8r");
        session.Update(model, kFrame);
        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::Solved);
        EXPECT_EQ(session.GetMistakes(), 1U);

        ASSERT_TRUE(session.Retry(model));
        Play("a7a8q");
        session.Update(model, kFrame);
        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::Solved);
        EXPECT_EQ(session.GetMistakes(), 0U);
    }

    TEST_F(PuzzleSessionTest, ABlackSolverPlaysTheSideAfterTheSetup) {
        Load({kBlackSolves});
        Start();

        EXPECT_EQ(model.GetTurn(), PieceColor::Black);
        EXPECT_EQ(session.GetCurrent()->SolverColor(), PieceColor::Black);

        Play("a8a1");
        session.Update(model, kFrame);

        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::Solved);
    }

    TEST_F(PuzzleSessionTest, RetryBeginsTheSamePuzzleWithNoMistakes) {
        Load({kMateInOne, kMateInTwo});
        Start(1);
        Play("a1a8");
        session.Update(model, kFrame);
        ASSERT_EQ(session.GetMistakes(), 1U);
        ASSERT_TRUE(session.WasWrong());

        EXPECT_TRUE(session.Retry(model));

        EXPECT_EQ(*session.GetIndex(), 1U);
        EXPECT_EQ(session.GetMistakes(), 0U);
        EXPECT_FALSE(session.WasWrong());
        EXPECT_EQ(model.GetMoves().size(), 1U);
        EXPECT_EQ(model.GetMoves()[0].ToUci(), "h8g8");
        EXPECT_TRUE(session.IsActiveFor(model));
    }

    TEST_F(PuzzleSessionTest, RetryOfASolvedPuzzleStartsItAgain) {
        Load({kMateInOne});
        Start();
        Play("a1a8");
        session.Update(model, kFrame);
        ASSERT_TRUE(model.IsGameOver());

        EXPECT_TRUE(session.Retry(model));

        EXPECT_FALSE(model.IsGameOver());
        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::YourMove);
    }

    TEST_F(PuzzleSessionTest, RetryWithNoPuzzleOnDoesNothing) {
        Load({kMateInOne});
        Start();
        model.Reset();
        const uint64_t version = model.GetVersion();

        EXPECT_FALSE(session.Retry(model));

        EXPECT_EQ(model.GetVersion(), version);
    }

    TEST_F(PuzzleSessionTest, NextGoesOnAndWrapsAfterTheLast) {
        Load({kMateInOne, kMateInTwo, kBlackSolves});
        Start();

        EXPECT_TRUE(session.Next(model));
        EXPECT_EQ(*session.GetIndex(), 1U);
        EXPECT_TRUE(session.Next(model));
        EXPECT_EQ(*session.GetIndex(), 2U);
        EXPECT_TRUE(session.Next(model));
        EXPECT_EQ(*session.GetIndex(), 0U);
        EXPECT_TRUE(session.IsActiveFor(model));
        EXPECT_EQ(session.GetMistakes(), 0U);
    }

    TEST_F(PuzzleSessionTest, NextWithNoPuzzleOnBeginsTheFirstOne) {
        Load({kMateInOne, kMateInTwo});
        Start(1);
        model.Reset();

        EXPECT_TRUE(session.Next(model));

        EXPECT_EQ(*session.GetIndex(), 0U);
    }

    TEST_F(PuzzleSessionTest, NextOfAnEmptySetIsFalse) {
        EXPECT_FALSE(session.Next(model));
        EXPECT_FALSE(session.Begin(model, 0));
    }

    TEST_F(PuzzleSessionTest, ResetEndsThePuzzleAtOnce) {
        Load({kMateInOne});
        Start();

        model.Reset();

        EXPECT_FALSE(session.IsActiveFor(model));
        EXPECT_FALSE(session.Update(model, 1.0f));
    }

    TEST_F(PuzzleSessionTest, APositionOfAnotherFenEndsThePuzzle) {
        Load({kMateInOne});
        Start();

        model.SetPosition(PositionFromFen("6k1/1p3ppp/8/8/8/8/8/R3K3 w - - 0 1"));

        EXPECT_FALSE(session.IsActiveFor(model));
    }

    TEST_F(PuzzleSessionTest, ThePuzzlesOwnFenWithNoSetupMoveIsNotAPuzzle) {
        Load({kMateInOne});
        Start();

        model.SetPosition(PositionFromFen("6k1/1p3ppp/8/8/8/8/8/R3K3 b - - 0 1"));

        EXPECT_FALSE(session.IsActiveFor(model));
    }

    TEST_F(PuzzleSessionTest, ATakebackUnderTheSetupEndsThePuzzle) {
        Load({kMateInOne});
        Start();

        EXPECT_EQ(model.TakeBack(1), 1U);

        EXPECT_FALSE(session.IsActiveFor(model));
        EXPECT_FALSE(session.Update(model, 1.0f));
    }

    TEST_F(PuzzleSessionTest, AMoveOfTheProgramsSideThatTheLineDoesNotHaveEndsThePuzzle) {
        Load({kMateInTwo});
        Start();
        Play("a1a7");
        session.Update(model, kFrame);

        Play("g8f8");

        EXPECT_FALSE(session.IsActiveFor(model));
        EXPECT_FALSE(session.Update(model, 1.0f));
        EXPECT_EQ(model.GetMoves().size(), 3U);
        EXPECT_EQ(session.GetMistakes(), 0U);
    }

    TEST_F(PuzzleSessionTest, TheLinesOwnReplyPlayedByHandIsTakenAsTheReply) {
        Load({kMateInTwo});
        Start();
        Play("a1a7");
        session.Update(model, kFrame);

        Play("g8h8");

        EXPECT_TRUE(session.IsActiveFor(model));
        EXPECT_FALSE(session.Update(model, 10.0f));
        EXPECT_EQ(model.GetMoves().size(), 3U);
        EXPECT_EQ(session.GetMistakes(), 0U);
        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::YourMove);
    }

    TEST_F(PuzzleSessionTest, AWrongMoveNotYetJudgedKeepsThePuzzleOnButASecondMoveOnTopEndsIt) {
        Load({kMateInTwo});
        Start();

        Play("a1a2");
        EXPECT_TRUE(session.IsActiveFor(model));
        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::YourMove);

        Play("g8f8");
        EXPECT_FALSE(session.IsActiveFor(model));
    }

    TEST_F(PuzzleSessionTest, AMoveDoneWhileTheReplyIsDueIsJudgedByTheSameRules) {
        Load({kMateInTwo});
        Start();
        Play("a1a7");
        session.Update(model, kFrame);
        session.Update(model, kPuzzleReplySeconds);
        ASSERT_EQ(model.GetMoves().size(), 3U);

        Play("a7a2");
        EXPECT_TRUE(session.IsActiveFor(model));
        EXPECT_TRUE(session.Update(model, kFrame));

        EXPECT_EQ(model.GetMoves().size(), 3U);
        EXPECT_EQ(session.GetMistakes(), 1U);
    }

    TEST_F(PuzzleSessionTest, EndForgetsThePuzzleAndLeavesTheGame) {
        Load({kMateInOne});
        Start();
        const uint64_t revision = session.GetRevision();

        session.End();

        EXPECT_GT(session.GetRevision(), revision);
        EXPECT_FALSE(session.IsActiveFor(model));
        EXPECT_EQ(session.GetCurrent(), nullptr);
        EXPECT_FALSE(session.GetIndex().has_value());
        EXPECT_EQ(model.GetMoves().size(), 1U);

        const uint64_t afterEnd = session.GetRevision();
        session.End();
        EXPECT_EQ(session.GetRevision(), afterEnd);
    }

    TEST_F(PuzzleSessionTest, SetPuzzlesEndsThePuzzleThatWasOn) {
        Load({kMateInOne});
        Start();

        Load({kMateInTwo});

        EXPECT_FALSE(session.IsActiveFor(model));
        EXPECT_EQ(session.GetPuzzles().Size(), 1U);
    }

    TEST_F(PuzzleSessionTest, TheRevisionNeverGoesBack) {
        Load({kMateInTwo});
        Vector<uint64_t> seen{session.GetRevision()};

        Start();
        seen.push_back(session.GetRevision());
        Play("a1a2");
        session.Update(model, kFrame);
        seen.push_back(session.GetRevision());
        Play("a1a7");
        session.Update(model, kFrame);
        seen.push_back(session.GetRevision());
        session.Update(model, kPuzzleReplySeconds);
        seen.push_back(session.GetRevision());
        EXPECT_TRUE(session.Retry(model));
        seen.push_back(session.GetRevision());
        session.End();
        seen.push_back(session.GetRevision());

        for (size_t i = 1; i < seen.size(); ++i) {
            EXPECT_GT(seen[i], seen[i - 1]) << i;
        }
    }

    TEST_F(PuzzleSessionTest, TheRevisionStaysWhileNothingHappens) {
        Load({kMateInTwo});
        Start();
        const uint64_t revision = session.GetRevision();

        session.Update(model, kFrame);
        session.Update(model, kFrame);

        EXPECT_EQ(session.GetRevision(), revision);
    }

    TEST_F(PuzzleSessionTest, TheSessionNeverPlaysForThePerson) {
        Load({kMateInOne});
        Start();

        RunFrames(5.0f);

        EXPECT_EQ(model.GetMoves().size(), 1U);
        EXPECT_EQ(session.GetPhase(model), PuzzlePhase::YourMove);
    }

}  // namespace BECore::Chess::Tests
