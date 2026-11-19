#include <Chess/Engine/OwnEngine.h>
#include <Chess/Engine/SearchRequest.h>
#include <Chess/Engine/UciEngine.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/Puzzle.h>
#include <tests/ChessTestUtil.h>
#include <tests/ShippedPuzzles.h>
#include <tests/StockfishUtil.h>

/**
 * The shipped `assets/puzzles/puzzles.tsv` holds what the mode relies on (it parses whole, every line is a mate of the length its theme says) and the engines agree
 * with it: from the position after the setup move the best line mates in N. The searches have a fixed depth, never a time: a depth gives the same answer on every
 * run, a limit of time does not (tests/CLAUDE.md, "No test depends on machine load").
 */
namespace BECore::Chess::Tests {

    namespace {

        constexpr size_t kMinPuzzles = 100;
        constexpr size_t kMaxMateLength = 3;

        /// N of the theme `mateIn<N>` of @p puzzle, or 0 when it has none or several
        size_t MateLengthOf(const Puzzle& puzzle) {
            size_t found = 0;
            size_t count = 0;
            for (size_t length = 1; length <= kMaxMateLength; ++length) {
                const String theme = Format("mateIn{}", length);
                for (const String& own : puzzle.themes) {
                    if (own == theme) {
                        found = length;
                        ++count;
                    }
                }
            }
            return count == 1 ? found : 0;
        }

        /// The game of @p puzzle up to and including the setup move: what the person is shown
        ChessGameModel ModelAfterSetup(const Puzzle& puzzle) {
            ChessGameModel model;
            model.SetPosition(PositionFromFen(puzzle.startFen));
            EXPECT_TRUE(model.TryMove(puzzle.line.front()).has_value()) << puzzle.id.c_str();
            return model;
        }

        /// Fails the test unless @p engine finds, at @p depth, a mate in the length of the theme from the position after the setup, and its first move is the line's or
        /// another move that mates in that many moves (any mate counts, as on lichess)
        void ExpectEngineMates(IChessEngine& engine, const Puzzle& puzzle, int depth) {
            const size_t length = MateLengthOf(puzzle);
            ChessGameModel model = ModelAfterSetup(puzzle);
            const auto analysis = engine.Analyse(SearchRequest::FromModel(model, SearchLimits{0, depth}), CancellationToken());
            ASSERT_TRUE(analysis.has_value()) << puzzle.id.c_str() << ": " << Describe(analysis.error()).c_str();
            const AnalysisLine& best = analysis->Best();
            EXPECT_EQ(best.score.kind, AnalysisScoreKind::Mate) << puzzle.id.c_str() << " at depth " << depth;
            EXPECT_EQ(best.score.value, static_cast<int>(length)) << puzzle.id.c_str() << " at depth " << depth;
            if (best.move == puzzle.line[1]) {
                return;
            }
            ASSERT_TRUE(model.TryMove(best.move).has_value()) << puzzle.id.c_str() << ": " << best.move.ToUci().c_str();
            if (length == 1) {
                EXPECT_EQ(model.GetStatus(), GameStatus::Checkmate) << puzzle.id.c_str() << ": " << best.move.ToUci().c_str() << " is not the line's " << puzzle.line[1].ToUci().c_str();
            }
        }

        void ExpectEngineMatesEveryPuzzle(IChessEngine& engine, size_t length, int depth) {
            size_t checked = 0;
            const PuzzleSet& set = ShippedPuzzleSet();
            for (size_t index = 0; index < set.Size(); ++index) {
                const Puzzle& puzzle = set.At(index);
                if (MateLengthOf(puzzle) == length) {
                    ExpectEngineMates(engine, puzzle, depth);
                    ++checked;
                }
            }
            EXPECT_GT(checked, 0U) << "no puzzle of mate in " << length;
        }

    }  // namespace

    TEST(PuzzleShippedTest, TheTableParsesWithNoProblemAndHasEnoughPuzzles) {
        Vector<String> problems;
        const PuzzleSet set = PuzzleSet::Parse(ShippedPuzzlesTable(), &problems);

        for (const String& problem : problems) {
            ADD_FAILURE() << problem.c_str();
        }
        EXPECT_GE(set.Size(), kMinPuzzles);
    }

    TEST(PuzzleShippedTest, TheIdsAreUniqueAndTheRatingsNeverFall) {
        const PuzzleSet& set = ShippedPuzzleSet();

        for (size_t index = 0; index < set.Size(); ++index) {
            EXPECT_EQ(set.FindIndexById(set.At(index).id), index) << set.At(index).id.c_str();
            if (index > 0) {
                EXPECT_GE(set.At(index).rating, set.At(index - 1).rating) << set.At(index).id.c_str();
            }
        }
    }

    TEST(PuzzleShippedTest, EveryLineIsAMateOfTheLengthItsThemeSaysAndTheSolverIsOneOfTheColors) {
        const PuzzleSet& set = ShippedPuzzleSet();

        for (size_t index = 0; index < set.Size(); ++index) {
            const Puzzle& puzzle = set.At(index);
            const size_t length = MateLengthOf(puzzle);
            EXPECT_NE(length, 0U) << puzzle.id.c_str() << " has no single mateIn<N> theme";
            EXPECT_EQ(puzzle.SolverMoveCount(), length) << puzzle.id.c_str();
            EXPECT_TRUE(puzzle.SolverColor() == PieceColor::White || puzzle.SolverColor() == PieceColor::Black) << puzzle.id.c_str();

            ChessGameModel model;
            model.SetPosition(PositionFromFen(puzzle.startFen));
            for (size_t ply = 0; ply < puzzle.line.size(); ++ply) {
                ASSERT_TRUE(model.TryMove(puzzle.line[ply]).has_value()) << puzzle.id.c_str() << " ply " << ply;
                if (ply + 1 < puzzle.line.size()) {
                    EXPECT_EQ(model.GetStatus(), GameStatus::Ongoing) << puzzle.id.c_str() << " ends early at ply " << ply;
                }
            }
            EXPECT_EQ(model.GetStatus(), GameStatus::Checkmate) << puzzle.id.c_str();
            EXPECT_EQ(model.GetPosition().SideToMove(), Opposite(puzzle.SolverColor())) << puzzle.id.c_str() << ": the person's move mates, so the program is to move";
        }
    }

    // The own engine at the least depth that is green for a mate in N, 2N plies; one test per N
    // so that a slow one shows which
    TEST(PuzzleShippedTest, TheOwnEngineMatesInOneWhereTheTableSaysSo) {
        OwnEngine engine;
        ExpectEngineMatesEveryPuzzle(engine, 1, 2);
    }

    TEST(PuzzleShippedTest, TheOwnEngineMatesInTwoWhereTheTableSaysSo) {
        OwnEngine engine;
        ExpectEngineMatesEveryPuzzle(engine, 2, 4);
    }

    TEST(PuzzleShippedTest, TheOwnEngineMatesInThreeWhereTheTableSaysSo) {
        OwnEngine engine;
        ExpectEngineMatesEveryPuzzle(engine, 3, 6);
    }

    // Stockfish at 2N + 2 plies still scores some mates in 2 and 3 in centipawns; 3N + 4 gives every one as a mate
    TEST(PuzzleShippedTest, StockfishMatesEveryPuzzleToo) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        UciEngine engine(StockfishSettings(*stockfish));

        for (size_t length = 1; length <= kMaxMateLength; ++length) {
            ExpectEngineMatesEveryPuzzle(engine, length, static_cast<int>(3 * length + 4));
        }
    }

}  // namespace BECore::Chess::Tests
