#include <BECore/Time/Time.h>
#include <Chess/Components/MoveRows.h>
#include <tests/ChessUiHarness.h>

/// The game and its puzzle: begin, next, retry and leave; what a puzzle takes from the opponent, the board's orientation, the takeback, resigning, draw offers and the save. The judge itself
/// is PuzzleSessionTest's; here the scene runs the frames (ChessBoardComponent::OnUpdate) with game time given by hand, never a sleep.
namespace BECore::Chess::Tests {

    namespace {

        /// White solves: Black's setup h8g8, then Ra1a7, the reply g8h8 and Rb2b8#
        constexpr StringView kMateInTwo = "ladder\t7k/8/8/8/8/8/1R6/R6K b - - 0 1\th8g8 a1a7 g8h8 b2b8\t1200\tmateIn2\n";
        /// Black solves: White's setup b2b3, then Ra8a1#
        constexpr StringView kBlackSolves = "black\tr3k3/8/8/8/8/8/1P3PPP/6K1 w - - 0 1\tb2b3 a8a1\t1000\tmateIn1\n";

        constexpr int kFrameLimit = 40;  ///< frames of a quarter second: far more than the reply needs

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

        class ChessGamePuzzleTest : public ChessUiTest {
        protected:
            ChessGame& Game() const {
                ChessGame* game = ChessGame::GetActive();
                EXPECT_NE(game, nullptr);
                return *game;
            }

            void Load() {
                Game().SetPuzzles(SetOf({kMateInTwo, kBlackSolves}));
            }

            /// One frame of the scene with a quarter second of game time
            void Frame() {
                CoreManager::GetTime().Advance(0.25);
                _manager->UpdateAll();
            }

            void Frames(int count) {
                for (int frame = 0; frame < count; ++frame) {
                    Frame();
                }
            }

            bool FramesUntilMoves(size_t count) {
                for (int frame = 0; frame < kFrameLimit; ++frame) {
                    if (Model().GetMoves().size() == count) {
                        return true;
                    }
                    Frame();
                }
                return Model().GetMoves().size() == count;
            }
        };

    }  // namespace

    TEST_F(ChessGamePuzzleTest, NoPuzzleIsOnUntilOneIsBegun) {
        Load();
        EXPECT_FALSE(Game().IsPuzzleOn());
        EXPECT_FALSE(Game().RetryPuzzle());
        EXPECT_FALSE(Game().LeavePuzzle());
        EXPECT_FALSE(Game().BeginPuzzle(2)) << "the set has two";
        EXPECT_FALSE(Game().IsPuzzleOn());
        EXPECT_EQ(ChessGame::GetActivePuzzle(), &Game().GetPuzzle());
        EXPECT_TRUE(Model().GetMoves().empty());
    }

    TEST_F(ChessGamePuzzleTest, BeginNextRetryAndLeave) {
        Load();

        ASSERT_TRUE(Game().BeginPuzzle(0));
        EXPECT_TRUE(Game().IsPuzzleOn());
        ASSERT_EQ(Model().GetMoves().size(), 1u);

        ASSERT_TRUE(Game().NextPuzzle());
        EXPECT_EQ(*Game().GetPuzzle().GetIndex(), 1u);
        EXPECT_TRUE(Game().NextPuzzle()) << "the first one after the last";
        EXPECT_EQ(*Game().GetPuzzle().GetIndex(), 0u);

        Settle();
        ClickMove("a1", "a7");
        Frame();
        ASSERT_EQ(Model().GetMoves().size(), 2u);
        ASSERT_TRUE(Game().RetryPuzzle());
        EXPECT_EQ(Model().GetMoves().size(), 1u) << "the same puzzle from its setup";
        EXPECT_EQ(Game().GetPuzzle().GetMistakes(), 0u);

        ASSERT_TRUE(Game().LeavePuzzle());
        EXPECT_FALSE(Game().IsPuzzleOn());
        EXPECT_TRUE(Model().GetMoves().empty()) << "a plain Reset()";
        EXPECT_EQ(Model().GetStartFen(), String(Position::kStartFen.data(), Position::kStartFen.size()));
        EXPECT_FALSE(Game().LeavePuzzle());
    }

    TEST_F(ChessGamePuzzleTest, NextWithNoPuzzleOnBeginsTheFirst) {
        Load();
        ASSERT_TRUE(Game().NextPuzzle());
        EXPECT_EQ(*Game().GetPuzzle().GetIndex(), 0u);
    }

    TEST_F(ChessGamePuzzleTest, AResetOrAMoveOutOfTheLineEndsItAtOnce) {
        Load();
        ASSERT_TRUE(Game().BeginPuzzle(0));
        Game().Reset();
        EXPECT_FALSE(Game().IsPuzzleOn()) << "no frame between";

        ASSERT_TRUE(Game().BeginPuzzle(0));
        Model().SetPosition(Position::Start());
        EXPECT_FALSE(Game().IsPuzzleOn());
    }

    TEST_F(ChessGamePuzzleTest, AMissingPuzzleFileGivesNoPuzzles) {
        const PuzzleSet set = ChessGame::LoadPuzzles("assets/puzzles/not-there.tsv");
        EXPECT_TRUE(set.IsEmpty());
        Game().SetPuzzles(set);
        EXPECT_FALSE(Game().NextPuzzle());
        EXPECT_FALSE(Game().BeginPuzzle(0));
    }

    TEST_F(ChessGamePuzzleTest, TheBoardIsTurnedForTheSolverAndBackAfterLeaving) {
        Load();
        EXPECT_EQ(Game().BoardBottomColor(), PieceColor::White);

        ASSERT_TRUE(Game().BeginPuzzle(1));
        EXPECT_EQ(Game().BoardBottomColor(), PieceColor::Black);
        Settle();
        EXPECT_EQ(ChessBoardComponent::GetActive()->GetBottomColor(), PieceColor::Black);

        ASSERT_TRUE(Game().LeavePuzzle());
        EXPECT_EQ(Game().BoardBottomColor(), PieceColor::White);
    }

    TEST_F(ChessGamePuzzleTest, TheSolverMovesWhateverTheOpponentPlaysAndTheReplyIsTheProgramsOnly) {
        Load();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::White).has_value()) << "the program has the color the solver has";
        ASSERT_TRUE(Game().BeginPuzzle(0));
        Settle();
        EXPECT_FALSE(Game().ProgramToMoveNow());
        EXPECT_EQ(Game().BoardBottomColor(), PieceColor::White);

        ClickMove("a1", "a7");
        Frame();
        ASSERT_EQ(Model().GetMoves().size(), 2u) << "the board took the person's move";
        EXPECT_TRUE(Game().ProgramToMoveNow()) << "the reply is the line's";
        ClickMove("b2", "b3");
        EXPECT_EQ(Model().GetMoves().size(), 2u) << "no click plays the reply, and no premove is queued";
        EXPECT_TRUE(Model().GetPremoves().empty());

        ASSERT_TRUE(FramesUntilMoves(3));
        EXPECT_EQ(Model().GetMoves()[2].ToUci(), "g8h8");
        EXPECT_FALSE(Game().ProgramToMoveNow());
    }

    TEST_F(ChessGamePuzzleTest, TheJudgeWaitsWhileAPositionIsEditedAndRepliesAfterTheDraftIsCancelled) {
        Load();
        ASSERT_TRUE(Game().BeginPuzzle(0));
        Settle();
        ClickMove("a1", "a7");
        Frame();
        ASSERT_EQ(Model().GetMoves().size(), 2u);

        Game().BeginEditing(EditStart::Current);
        Frames(8);
        EXPECT_TRUE(Game().IsEditing()) << "the reply would have ended the draft";
        EXPECT_EQ(Model().GetMoves().size(), 2u);
        EXPECT_TRUE(Game().IsPuzzleOn());

        Game().CancelEditing();
        ASSERT_TRUE(FramesUntilMoves(3));
        EXPECT_EQ(Model().GetMoves()[2].ToUci(), "g8h8");
        EXPECT_TRUE(Game().IsPuzzleOn());
    }

    TEST_F(ChessGamePuzzleTest, AWrongMoveIsNotTakenBackWhileAPositionIsEdited) {
        Load();
        ASSERT_TRUE(Game().BeginPuzzle(0));
        Settle();
        ClickMove("b2", "b3");
        ASSERT_EQ(Model().GetMoves().size(), 2u);

        Game().BeginEditing(EditStart::Current);
        Frames(2);
        EXPECT_TRUE(Game().IsEditing());
        EXPECT_EQ(Model().GetMoves().size(), 2u) << "the takeback would have ended the draft";

        Game().CancelEditing();
        Frame();
        EXPECT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_TRUE(Game().GetPuzzle().WasWrong());
    }

    TEST_F(ChessGamePuzzleTest, ASearchInFlightWhenThePuzzleBeginsNeverPlaysIntoItNorAfterIt) {
        SKIP_WITHOUT_WORKER_THREADS();
        Load();
        const auto engine = New<GatedEngine>();
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; })) << "the program is searching";

        ASSERT_TRUE(Game().BeginPuzzle(0));
        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return engine->Answered() == 1; }));
        Frames(10);
        EXPECT_TRUE(Game().IsPuzzleOn()) << "its answer was not played into the puzzle";
        EXPECT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(engine->Calls(), 1) << "and the program asked nothing for the puzzle's position";

        ASSERT_TRUE(Game().LeavePuzzle());
        Frames(10);
        EXPECT_TRUE(Model().GetMoves().empty()) << "the answer is stale after the reset: White is to move";
        EXPECT_EQ(engine->Calls(), 1);
    }

    TEST_F(ChessGamePuzzleTest, TakeBackResignAndDrawAreOffWhileOneIsOnAndBackAfter) {
        Load();
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        EXPECT_EQ(TakeBackPliesNow(&Model()), 1u);
        EXPECT_TRUE(Game().ResigningNow().has_value());
        EXPECT_TRUE(Game().CanOfferDraw());

        ASSERT_TRUE(Game().BeginPuzzle(0));
        EXPECT_EQ(TakeBackPliesNow(&Model()), 0u);
        EXPECT_FALSE(Game().ResigningNow().has_value());
        EXPECT_FALSE(Game().CanOfferDraw());
        const auto offered = Game().OfferDraw();
        ASSERT_FALSE(offered.has_value());
        EXPECT_EQ(offered.error(), DrawOfferRefusal::Puzzle);

        ASSERT_TRUE(Game().LeavePuzzle());
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        EXPECT_EQ(TakeBackPliesNow(&Model()), 1u);
        EXPECT_TRUE(Game().ResigningNow().has_value());
        EXPECT_TRUE(Game().CanOfferDraw());
    }

    TEST_F(ChessGamePuzzleTest, TheOpponentPickIsLeftAsItWas) {
        Load();
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        const uint64_t revision = Opponent().GetRevision();

        ASSERT_TRUE(Game().BeginPuzzle(0));
        Frames(4);

        EXPECT_EQ(Opponent().GetRevision(), revision);
        EXPECT_EQ(Opponent().GetPlays(), PieceColor::Black);
        ASSERT_TRUE(Game().LeavePuzzle());
        EXPECT_EQ(Opponent().GetPlays(), PieceColor::Black);
    }

}  // namespace BECore::Chess::Tests
