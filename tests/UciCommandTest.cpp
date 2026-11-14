#include "ChessTestUtil.h"

#include <Chess/Game/Position.h>
#include <Chess/Uci/UciCommand.h>

/// The lines a GUI sends, read into commands: pure, no engine and no rules (a move is text here)
namespace BECore::Chess::Tests {

    namespace {

        using Uci::GoParams;
        using Uci::ParseCommand;
        using Uci::UciCommand;
        using Uci::UciCommandKind;

        constexpr const char* kMateInOneFen = "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1";

        UciCommand Parse(const char* line) {
            return ParseCommand(line);
        }

        void ExpectKind(const char* line, UciCommandKind kind) {
            EXPECT_EQ(Parse(line).kind, kind) << line;
        }

        /// Only the fields named by the caller may be set: the rest of `go` is empty
        void ExpectGo(const GoParams& go, Optional<int> depth, Optional<int> moveTime, Optional<int> wTime, Optional<int> bTime, Optional<int> wInc, Optional<int> bInc, Optional<int> movesToGo,
                      bool infinite) {
            EXPECT_EQ(go.depth, depth);
            EXPECT_EQ(go.moveTime, moveTime);
            EXPECT_EQ(go.wTime, wTime);
            EXPECT_EQ(go.bTime, bTime);
            EXPECT_EQ(go.wInc, wInc);
            EXPECT_EQ(go.bInc, bInc);
            EXPECT_EQ(go.movesToGo, movesToGo);
            EXPECT_EQ(go.infinite, infinite);
        }

        constexpr std::nullopt_t kNone = std::nullopt;

    }  // namespace

    TEST(UciCommandTest, EveryPlainCommandHasItsKind) {
        ExpectKind("uci", UciCommandKind::Uci);
        ExpectKind("isready", UciCommandKind::IsReady);
        ExpectKind("ucinewgame", UciCommandKind::NewGame);
        ExpectKind("stop", UciCommandKind::Stop);
        ExpectKind("ponderhit", UciCommandKind::PonderHit);
        ExpectKind("quit", UciCommandKind::Quit);
        ExpectKind("go", UciCommandKind::Go);
        ExpectKind("setoption name Depth value 4", UciCommandKind::SetOption);
        ExpectKind("position startpos", UciCommandKind::Position);
    }

    TEST(UciCommandTest, PositionStartposIsTheStartFenWithNoMoves) {
        const UciCommand command = Parse("position startpos");

        ASSERT_EQ(command.kind, UciCommandKind::Position);
        EXPECT_EQ(command.position.startFen, Position::kStartFen);
        EXPECT_TRUE(command.position.moves.empty());
    }

    TEST(UciCommandTest, PositionStartposKeepsTheMovesAsText) {
        const UciCommand command = Parse("position startpos moves e2e4 e7e5");

        ASSERT_EQ(command.kind, UciCommandKind::Position);
        EXPECT_EQ(command.position.startFen, Position::kStartFen);
        ASSERT_EQ(command.position.moves.size(), 2u);
        EXPECT_EQ(command.position.moves[0], "e2e4");
        EXPECT_EQ(command.position.moves[1], "e7e5");
    }

    TEST(UciCommandTest, TheParserDoesNotJudgeTheMoves) {
        const UciCommand command = Parse("position startpos moves e2e5 zzzz");

        ASSERT_EQ(command.kind, UciCommandKind::Position);
        ASSERT_EQ(command.position.moves.size(), 2u);
        EXPECT_EQ(command.position.moves[0], "e2e5");
        EXPECT_EQ(command.position.moves[1], "zzzz");
    }

    TEST(UciCommandTest, PositionFenTakesTheFieldsUpToMoves) {
        const UciCommand command = Parse("position fen 6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");

        ASSERT_EQ(command.kind, UciCommandKind::Position);
        EXPECT_EQ(command.position.startFen, kMateInOneFen);
        EXPECT_TRUE(command.position.moves.empty());
    }

    TEST(UciCommandTest, PositionFenWithMovesStopsTheFenAtTheKeyword) {
        const UciCommand command = Parse("position fen 6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1 moves a1a8");

        ASSERT_EQ(command.kind, UciCommandKind::Position);
        EXPECT_EQ(command.position.startFen, kMateInOneFen);
        ASSERT_EQ(command.position.moves.size(), 1u);
        EXPECT_EQ(command.position.moves[0], "a1a8");
    }

    TEST(UciCommandTest, AFenWithFewerFieldsIsPassedOnAsItIs) {
        const UciCommand command = Parse("position fen 6k1/5ppp/8/8/8/8/5PPP/R5K1 w moves a1a8");

        ASSERT_EQ(command.kind, UciCommandKind::Position);
        EXPECT_EQ(command.position.startFen, "6k1/5ppp/8/8/8/8/5PPP/R5K1 w");
        EXPECT_EQ(command.position.moves.size(), 1u);
    }

    TEST(UciCommandTest, AFenWithNoFieldsIsEmpty) {
        const UciCommand command = Parse("position fen");

        ASSERT_EQ(command.kind, UciCommandKind::Position);
        EXPECT_TRUE(command.position.startFen.empty());
    }

    TEST(UciCommandTest, PositionWithNeitherStartposNorFenIsUnknown) {
        ExpectKind("position", UciCommandKind::Unknown);
        ExpectKind("position e2e4", UciCommandKind::Unknown);
        ExpectKind("position moves e2e4", UciCommandKind::Unknown);
    }

    TEST(UciCommandTest, ABareGoHasNoLimits) {
        const UciCommand command = Parse("go");

        ASSERT_EQ(command.kind, UciCommandKind::Go);
        ExpectGo(command.go, kNone, kNone, kNone, kNone, kNone, kNone, kNone, false);
    }

    TEST(UciCommandTest, GoDepthAndMoveTime) {
        ExpectGo(Parse("go depth 6").go, 6, kNone, kNone, kNone, kNone, kNone, kNone, false);
        ExpectGo(Parse("go movetime 500").go, kNone, 500, kNone, kNone, kNone, kNone, kNone, false);
        ExpectGo(Parse("go depth 4 movetime 100").go, 4, 100, kNone, kNone, kNone, kNone, kNone, false);
    }

    TEST(UciCommandTest, GoReadsTheClocks) {
        ExpectGo(Parse("go wtime 60000 btime 59000 winc 1000 binc 1100 movestogo 30").go, kNone, kNone, 60000, 59000, 1000, 1100, 30, false);
    }

    TEST(UciCommandTest, GoInfiniteAndPonderAreInfinite) {
        ExpectGo(Parse("go infinite").go, kNone, kNone, kNone, kNone, kNone, kNone, kNone, true);
        ExpectGo(Parse("go ponder wtime 1000 btime 2000").go, kNone, kNone, 1000, 2000, kNone, kNone, kNone, true);
    }

    TEST(UciCommandTest, ANumberThatIsMissingOrBadLeavesItsFieldEmpty) {
        ExpectGo(Parse("go depth").go, kNone, kNone, kNone, kNone, kNone, kNone, kNone, false);
        ExpectGo(Parse("go depth x").go, kNone, kNone, kNone, kNone, kNone, kNone, kNone, false);
        ExpectGo(Parse("go depth 4x").go, kNone, kNone, kNone, kNone, kNone, kNone, kNone, false);
        ExpectGo(Parse("go depth 99999999999").go, kNone, kNone, kNone, kNone, kNone, kNone, kNone, false);
    }

    TEST(UciCommandTest, AKeywordAfterAMissingNumberIsStillSeen) {
        ExpectGo(Parse("go depth wtime 5").go, kNone, kNone, 5, kNone, kNone, kNone, kNone, false);
        ExpectGo(Parse("go movetime infinite").go, kNone, kNone, kNone, kNone, kNone, kNone, kNone, true);
    }

    TEST(UciCommandTest, ANegativeNumberIsReadAsGiven) {
        ExpectGo(Parse("go wtime -5 btime 100").go, kNone, kNone, -5, 100, kNone, kNone, kNone, false);
        ExpectGo(Parse("go depth -1").go, -1, kNone, kNone, kNone, kNone, kNone, kNone, false);
    }

    TEST(UciCommandTest, UnknownTokensOfGoAreSkippedAndMoveNothing) {
        ExpectGo(Parse("go searchmoves e2e4 depth 3").go, 3, kNone, kNone, kNone, kNone, kNone, kNone, false);
        ExpectGo(Parse("go nodes 1000").go, kNone, kNone, kNone, kNone, kNone, kNone, kNone, false);
        ExpectGo(Parse("go depth 3 nodes 1000 wtime 7").go, 3, kNone, 7, kNone, kNone, kNone, kNone, false);
        ExpectGo(Parse("go mate 3 movetime 20").go, kNone, 20, kNone, kNone, kNone, kNone, kNone, false);
    }

    TEST(UciCommandTest, SetOptionSplitsAtNameAndValue) {
        const UciCommand command = Parse("setoption name Depth value 4");

        ASSERT_EQ(command.kind, UciCommandKind::SetOption);
        EXPECT_EQ(command.setOption.name, "Depth");
        EXPECT_EQ(command.setOption.value, "4");
    }

    TEST(UciCommandTest, AnOptionNameMayHoldSpaces) {
        const UciCommand command = Parse("setoption name Blunder Percent value 5");

        EXPECT_EQ(command.setOption.name, "Blunder Percent");
        EXPECT_EQ(command.setOption.value, "5");
    }

    TEST(UciCommandTest, TheValueIsTheRestOfTheLine) {
        const UciCommand command = Parse("setoption name Level value very  hard");

        EXPECT_EQ(command.setOption.name, "Level");
        EXPECT_EQ(command.setOption.value, "very hard");
    }

    TEST(UciCommandTest, AMissingValueIsEmpty) {
        const UciCommand command = Parse("setoption name Clear Hash");

        ASSERT_EQ(command.kind, UciCommandKind::SetOption);
        EXPECT_EQ(command.setOption.name, "Clear Hash");
        EXPECT_TRUE(command.setOption.value.empty());
    }

    TEST(UciCommandTest, ASetOptionWithoutANameHasAnEmptyName) {
        const UciCommand command = Parse("setoption");

        ASSERT_EQ(command.kind, UciCommandKind::SetOption);
        EXPECT_TRUE(command.setOption.name.empty());
        EXPECT_TRUE(command.setOption.value.empty());
    }

    TEST(UciCommandTest, SpacesAtTheEndsAndACarriageReturnAreIgnored) {
        ExpectKind("   uci", UciCommandKind::Uci);
        ExpectKind("uci\r", UciCommandKind::Uci);
        ExpectKind("  isready  \r\n", UciCommandKind::IsReady);
        ExpectGo(Parse("go   depth   3\r").go, 3, kNone, kNone, kNone, kNone, kNone, kNone, false);
    }

    TEST(UciCommandTest, AnythingElseIsUnknown) {
        ExpectKind("", UciCommandKind::Unknown);
        ExpectKind("   ", UciCommandKind::Unknown);
        ExpectKind("\r", UciCommandKind::Unknown);
        ExpectKind("foo bar", UciCommandKind::Unknown);
        ExpectKind("debug on", UciCommandKind::Unknown);
        ExpectKind("UCI", UciCommandKind::Unknown);
    }

    TEST(UciCommandTest, OnlyTheFirstWordNamesTheCommand) {
        ExpectKind("foo uci", UciCommandKind::Unknown);
        ExpectKind("uci extra words", UciCommandKind::Uci);
        ExpectKind("quit now", UciCommandKind::Quit);
    }

}  // namespace BECore::Chess::Tests
