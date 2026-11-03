#include "ChessTestUtil.h"

#include <Chess/Engine/UciProtocol.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>

/// The UCI text: pure, the commands we send and the one line we read. The texts are those of position_command in tools/stockfish_bridge.py
namespace BECore::Chess::Tests {

    namespace {

        constexpr SearchLimits kAnyLimits{100, 0};

        SearchRequest RequestOf(eastl::string_view fen, std::initializer_list<const char*> moves) {
            SearchRequest request;
            request.startFen = eastl::string(fen.data(), fen.size());
            for (const char* move : moves) {
                request.moves.push_back(Mv(move));
            }
            request.limits = kAnyLimits;
            return request;
        }

        void ExpectBestMove(eastl::string_view line, const char* move, bool isNone) {
            const auto parsed = UciProtocol::ParseBestMove(line);
            ASSERT_TRUE(parsed.has_value()) << eastl::string(line.data(), line.size()).c_str();
            EXPECT_EQ(parsed->move, move);
            EXPECT_EQ(parsed->IsNone(), isNone);
        }

        void ExpectNotBestMove(eastl::string_view line) {
            EXPECT_FALSE(UciProtocol::ParseBestMove(line).has_value()) << eastl::string(line.data(), line.size()).c_str();
        }

    }  // namespace

    TEST(UciProtocolTest, ThePositionCommandOfTheOpeningHasNoMoves) {
        const SearchRequest request = RequestOf(Position::kStartFen, {});

        EXPECT_EQ(UciProtocol::PositionCommand(request), "position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    }

    TEST(UciProtocolTest, ThePositionCommandListsTheMovesAfterTheFen) {
        const SearchRequest request = RequestOf(Position::kStartFen, {"e2e4", "e7e5", "g1f3"});

        EXPECT_EQ(UciProtocol::PositionCommand(request), "position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 moves e2e4 e7e5 g1f3");
    }

    TEST(UciProtocolTest, ThePositionCommandOfOneMove) {
        const SearchRequest request = RequestOf(Position::kStartFen, {"e2e4"});

        EXPECT_EQ(UciProtocol::PositionCommand(request), "position fen rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 moves e2e4");
    }

    TEST(UciProtocolTest, APromotionKeepsItsLetter) {
        const SearchRequest request = RequestOf("7k/4P3/8/8/8/8/8/K7 w - - 0 1", {"e7e8q"});

        EXPECT_EQ(UciProtocol::PositionCommand(request), "position fen 7k/4P3/8/8/8/8/8/K7 w - - 0 1 moves e7e8q");
        EXPECT_EQ(UciProtocol::PositionCommand(RequestOf("7k/4P3/8/8/8/8/8/K7 w - - 0 1", {"e7e8n"})), "position fen 7k/4P3/8/8/8/8/8/K7 w - - 0 1 moves e7e8n");
    }

    TEST(UciProtocolTest, CastlingIsTheKingsMove) {
        SearchRequest request = RequestOf("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", {});
        request.moves.push_back(CastlingMove("e1g1"));
        request.moves.push_back(CastlingMove("e8c8"));

        EXPECT_EQ(UciProtocol::PositionCommand(request), "position fen r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1 moves e1g1 e8c8");
    }

    TEST(UciProtocolTest, ThePositionCommandOfAModelIsItsStartAndItsMoves) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"));
        const auto move = MoveGenerator::ParseUci(model.GetPosition(), "g1h1");
        ASSERT_TRUE(move.has_value());
        ASSERT_TRUE(model.TryMove(*move).has_value());

        EXPECT_EQ(UciProtocol::PositionCommand(SearchRequest::FromModel(model, kAnyLimits)), "position fen 6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1 moves g1h1");
    }

    TEST(UciProtocolTest, TheGoCommandNamesTheLimitsGiven) {
        EXPECT_EQ(UciProtocol::GoCommand(SearchLimits{100, 0}), "go movetime 100");
        EXPECT_EQ(UciProtocol::GoCommand(SearchLimits{0, 8}), "go depth 8");
        EXPECT_EQ(UciProtocol::GoCommand(SearchLimits{100, 8}), "go depth 8 movetime 100");
    }

    TEST(UciProtocolTest, TheSetOptionCommandKeepsASpaceInTheName) {
        EXPECT_EQ(UciProtocol::SetOptionCommand("Skill Level", "5"), "setoption name Skill Level value 5");
        EXPECT_EQ(UciProtocol::SetOptionCommand("UCI_Elo", "1500"), "setoption name UCI_Elo value 1500");
        EXPECT_EQ(UciProtocol::SetOptionCommand("Debug Log File", "/tmp/a b.txt"), "setoption name Debug Log File value /tmp/a b.txt");
    }

    TEST(UciProtocolTest, ABestMoveIsRead) {
        ExpectBestMove("bestmove e2e4", "e2e4", false);
        ExpectBestMove("bestmove e7e8q", "e7e8q", false);
        ExpectBestMove("bestmove e1g1", "e1g1", false);
    }

    TEST(UciProtocolTest, ThePonderMoveIsLeftOut) {
        ExpectBestMove("bestmove e2e4 ponder e7e5", "e2e4", false);
        ExpectBestMove("bestmove e2e4 ponder", "e2e4", false);
    }

    TEST(UciProtocolTest, NoMoveIsNamedNone) {
        ExpectBestMove("bestmove (none)", "(none)", true);
        ExpectBestMove("bestmove 0000", "0000", true);
        ExpectBestMove("bestmove (none) ponder e7e5", "(none)", true);
    }

    TEST(UciProtocolTest, ABareBestMoveHasNoMove) {
        ExpectBestMove("bestmove", "", true);
        ExpectBestMove("bestmove   ", "", true);
    }

    TEST(UciProtocolTest, AnEndOfLineLeftOnTheLineDoesNoHarm) {
        ExpectBestMove("bestmove e2e4\r", "e2e4", false);
        ExpectBestMove("bestmove e2e4\r\n", "e2e4", false);
        ExpectBestMove("bestmove e2e4 ponder e7e5\r", "e2e4", false);
        ExpectBestMove("bestmove\r", "", true);
        ExpectBestMove("bestmove (none)\r\n", "(none)", true);
    }

    TEST(UciProtocolTest, SpacesAroundAndBetweenTheWordsAreAllowed) {
        ExpectBestMove("  bestmove e2e4  ", "e2e4", false);
        ExpectBestMove("bestmove    e2e4", "e2e4", false);
    }

    TEST(UciProtocolTest, OtherLinesAreNotABestMove) {
        ExpectNotBestMove("info depth 1 seldepth 1 score cp 10 nodes 20 pv e2e4");
        ExpectNotBestMove("info string bestmove e2e4");
        ExpectNotBestMove("uciok");
        ExpectNotBestMove("readyok");
        ExpectNotBestMove("");
        ExpectNotBestMove("\r");
        ExpectNotBestMove("   ");
        ExpectNotBestMove("e2e4");
        ExpectNotBestMove("bestmoves e2e4");
        ExpectNotBestMove("bestmovee2e4");
        ExpectNotBestMove("Bestmove e2e4");
    }

    TEST(UciProtocolTest, TrimLineTakesSpacesAndLineEndsFromBothEnds) {
        EXPECT_EQ(UciProtocol::TrimLine("uciok"), eastl::string_view("uciok"));
        EXPECT_EQ(UciProtocol::TrimLine("uciok\n"), eastl::string_view("uciok"));
        EXPECT_EQ(UciProtocol::TrimLine("uciok\r\n"), eastl::string_view("uciok"));
        EXPECT_EQ(UciProtocol::TrimLine("  readyok  "), eastl::string_view("readyok"));
        EXPECT_EQ(UciProtocol::TrimLine(" \r\n bestmove e2e4 \r\n "), eastl::string_view("bestmove e2e4"));
    }

    TEST(UciProtocolTest, TrimLineLeavesTheInsideAlone) {
        EXPECT_EQ(UciProtocol::TrimLine("a  b"), eastl::string_view("a  b"));
        EXPECT_EQ(UciProtocol::TrimLine(" a \r\n b "), eastl::string_view("a \r\n b"));
    }

    TEST(UciProtocolTest, TrimLineOfNothingIsEmpty) {
        EXPECT_TRUE(UciProtocol::TrimLine("").empty());
        EXPECT_TRUE(UciProtocol::TrimLine("   ").empty());
        EXPECT_TRUE(UciProtocol::TrimLine("\r\n").empty());
        EXPECT_TRUE(UciProtocol::TrimLine(" \r\n \r\n").empty());
    }

}  // namespace BECore::Chess::Tests
