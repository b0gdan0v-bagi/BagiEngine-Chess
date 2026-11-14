#include "ChessTestUtil.h"

#include <Chess/Engine/UciProtocol.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>

/// The UCI text: pure, the commands we send and the lines we read (bestmove, info). The texts are those of position_command in tools/stockfish_bridge.py
namespace BECore::Chess::Tests {

    namespace {

        constexpr SearchLimits kAnyLimits{100, 0};

        SearchRequest RequestOf(StringView fen, std::initializer_list<const char*> moves) {
            SearchRequest request;
            request.startFen = eastl::string(fen.data(), fen.size());
            for (const char* move : moves) {
                request.moves.push_back(Mv(move));
            }
            request.limits = kAnyLimits;
            return request;
        }

        void ExpectBestMove(StringView line, const char* move, bool isNone) {
            const auto parsed = UciProtocol::ParseBestMove(line);
            ASSERT_TRUE(parsed.has_value()) << eastl::string(line.data(), line.size()).c_str();
            EXPECT_EQ(parsed->move, move);
            EXPECT_EQ(parsed->IsNone(), isNone);
        }

        void ExpectNotBestMove(StringView line) {
            EXPECT_FALSE(UciProtocol::ParseBestMove(line).has_value()) << eastl::string(line.data(), line.size()).c_str();
        }

        Optional<UciProtocol::Info> ParsedInfo(StringView line) {
            return UciProtocol::ParseInfo(line);
        }

        void ExpectNotInfo(StringView line) {
            EXPECT_FALSE(UciProtocol::ParseInfo(line).has_value()) << eastl::string(line.data(), line.size()).c_str();
        }

        constexpr AnalysisScore Cp(int value) {
            return {AnalysisScoreKind::Centipawns, value};
        }

        constexpr AnalysisScore Mate(int moves) {
            return {AnalysisScoreKind::Mate, moves};
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

    TEST(UciProtocolTest, ARealStockfishInfoLineIsRead) {
        const auto info = ParsedInfo("info depth 18 seldepth 24 multipv 1 score cp 34 nodes 123 nps 4 hashfull 0 tbhits 0 time 5 pv e2e4 e7e5 g1f3");

        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->depth, 18);
        EXPECT_EQ(info->multiPv, 1);
        ASSERT_TRUE(info->score.has_value());
        EXPECT_EQ(*info->score, Cp(34));
        EXPECT_FALSE(info->lowerBound);
        EXPECT_FALSE(info->upperBound);
        EXPECT_EQ(info->firstPvMove, "e2e4");
    }

    TEST(UciProtocolTest, AMateScoreIsReadWithItsSign) {
        const auto win = ParsedInfo("info depth 5 score mate 3 nodes 10 pv e2e4");
        const auto loss = ParsedInfo("info depth 4 score mate -2 pv a1a2");

        ASSERT_TRUE(win.has_value() && win->score.has_value());
        EXPECT_EQ(*win->score, Mate(3));
        ASSERT_TRUE(loss.has_value() && loss->score.has_value());
        EXPECT_EQ(*loss->score, Mate(-2));
    }

    TEST(UciProtocolTest, ANegativeCentipawnScoreIsRead) {
        const auto info = ParsedInfo("info depth 9 score cp -153 pv d7d5");

        ASSERT_TRUE(info.has_value() && info->score.has_value());
        EXPECT_EQ(*info->score, Cp(-153));
    }

    TEST(UciProtocolTest, ABoundIsFlaggedAndKeepsItsScore) {
        const auto lower = ParsedInfo("info depth 12 score cp 80 lowerbound nodes 5 pv e2e4");
        const auto upper = ParsedInfo("info depth 12 score cp -10 upperbound pv e2e4");

        ASSERT_TRUE(lower.has_value() && lower->score.has_value());
        EXPECT_TRUE(lower->lowerBound);
        EXPECT_FALSE(lower->upperBound);
        EXPECT_EQ(*lower->score, Cp(80));
        ASSERT_TRUE(upper.has_value() && upper->score.has_value());
        EXPECT_TRUE(upper->upperBound);
        EXPECT_FALSE(upper->lowerBound);
        EXPECT_EQ(*upper->score, Cp(-10));
    }

    TEST(UciProtocolTest, TheLineOfAMultiPvSearchSaysWhichLineItIs) {
        const auto second = ParsedInfo("info depth 10 multipv 2 score cp 12 pv d2d4 d7d5");

        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(second->multiPv, 2);
        EXPECT_EQ(second->firstPvMove, "d2d4");
    }

    TEST(UciProtocolTest, WithoutMultiPvTheLineIsTheFirstOne) {
        const auto info = ParsedInfo("info depth 3 score cp 5 pv e2e4");

        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->multiPv, 1);
    }

    TEST(UciProtocolTest, OnlyTheFirstMoveOfTheLineIsKept) {
        EXPECT_EQ(ParsedInfo("info depth 3 score cp 5 pv e7e8q a1a2 b2b3")->firstPvMove, "e7e8q");
        EXPECT_EQ(ParsedInfo("info depth 3 score cp 5 pv e2e4")->firstPvMove, "e2e4");
        EXPECT_TRUE(ParsedInfo("info depth 3 score cp 5 pv")->firstPvMove.empty());
        EXPECT_TRUE(ParsedInfo("info depth 3 score cp 5")->firstPvMove.empty());
    }

    TEST(UciProtocolTest, WhatFollowsThePvIsNotReadAsKeywords) {
        const auto info = ParsedInfo("info depth 3 pv e2e4 score cp 77 depth 99");

        ASSERT_TRUE(info.has_value());
        EXPECT_FALSE(info->score.has_value());
        EXPECT_EQ(info->depth, 3);
    }

    TEST(UciProtocolTest, AnInfoStringIsTextAndHasNoScore) {
        const auto info = ParsedInfo("info string score cp 999 depth 40 pv a1a2");

        ASSERT_TRUE(info.has_value());
        EXPECT_FALSE(info->score.has_value());
        EXPECT_EQ(info->depth, 0);
        EXPECT_TRUE(info->firstPvMove.empty());
        EXPECT_FALSE(ParsedInfo("info string")->score.has_value());
    }

    TEST(UciProtocolTest, TheFieldsBeforeAnInfoStringAreKept) {
        const auto info = ParsedInfo("info depth 7 score cp 3 string NNUE evaluation using something");

        ASSERT_TRUE(info.has_value() && info->score.has_value());
        EXPECT_EQ(info->depth, 7);
        EXPECT_EQ(*info->score, Cp(3));
    }

    TEST(UciProtocolTest, AMateInZeroIsNoScore) {
        const auto info = ParsedInfo("info depth 0 score mate 0");

        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->depth, 0);
        EXPECT_FALSE(info->score.has_value());
    }

    TEST(UciProtocolTest, ALineWithoutAScoreHasNone) {
        const auto info = ParsedInfo("info depth 4 currmove e2e4 currmovenumber 1");

        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->depth, 4);
        EXPECT_FALSE(info->score.has_value());
        EXPECT_TRUE(ParsedInfo("info")->firstPvMove.empty());
        EXPECT_FALSE(ParsedInfo("info")->score.has_value());
    }

    TEST(UciProtocolTest, ATruncatedOrBrokenScoreIsNoScoreAndNeverACrash) {
        for (const char* line : {"info depth 3 score", "info depth 3 score cp", "info depth 3 score mate", "info score cp abc", "info score cp 1x", "info score mate -",
                                 "info score cp 99999999999999999999", "info score foo 12", "info score cp lowerbound", "info score cp --5", "info score cp 3.5"}) {
            const auto info = ParsedInfo(line);
            ASSERT_TRUE(info.has_value()) << line;
            EXPECT_FALSE(info->score.has_value()) << line;
        }
    }

    TEST(UciProtocolTest, AKeywordAfterAMissingValueIsStillSeen) {
        const auto info = ParsedInfo("info score cp lowerbound pv e2e4");

        ASSERT_TRUE(info.has_value());
        EXPECT_FALSE(info->score.has_value());
        EXPECT_TRUE(info->lowerBound);
        EXPECT_EQ(info->firstPvMove, "e2e4");
    }

    TEST(UciProtocolTest, ABrokenDepthOrMultiPvKeepsTheDefault) {
        const auto info = ParsedInfo("info depth x multipv -3 score cp 1");

        ASSERT_TRUE(info.has_value());
        EXPECT_EQ(info->depth, 0);
        EXPECT_EQ(info->multiPv, 1);
        ASSERT_TRUE(info->score.has_value());
        EXPECT_EQ(*info->score, Cp(1));
        EXPECT_EQ(ParsedInfo("info depth")->depth, 0);
        EXPECT_EQ(ParsedInfo("info multipv")->multiPv, 1);
        EXPECT_EQ(ParsedInfo("info depth -4 multipv 0")->depth, 0);
    }

    TEST(UciProtocolTest, UnknownTokensAreSkipped) {
        const auto info = ParsedInfo("info wdl 100 900 0 depth 6 foo bar score cp 20 baz pv e2e4");

        ASSERT_TRUE(info.has_value() && info->score.has_value());
        EXPECT_EQ(info->depth, 6);
        EXPECT_EQ(*info->score, Cp(20));
        EXPECT_EQ(info->firstPvMove, "e2e4");
    }

    TEST(UciProtocolTest, AnInfoLineKeepsItsFieldsWithAnEndOfLineAndSpaces) {
        const auto info = ParsedInfo("  info  depth 8   score  cp  15  pv  e2e4\r\n");

        ASSERT_TRUE(info.has_value() && info->score.has_value());
        EXPECT_EQ(info->depth, 8);
        EXPECT_EQ(*info->score, Cp(15));
        EXPECT_EQ(info->firstPvMove, "e2e4");
        EXPECT_EQ(*ParsedInfo("info depth 1 score mate 2\r")->score, Mate(2));
        EXPECT_TRUE(ParsedInfo("info depth 1 score cp 5 upperbound\r")->upperBound);
    }

    TEST(UciProtocolTest, OtherLinesAreNotInfo) {
        ExpectNotInfo("bestmove e2e4");
        ExpectNotInfo("bestmove e2e4 ponder e7e5");
        ExpectNotInfo("uciok");
        ExpectNotInfo("readyok");
        ExpectNotInfo("");
        ExpectNotInfo("\r");
        ExpectNotInfo("   ");
        ExpectNotInfo("information depth 1");
        ExpectNotInfo("Info depth 1");
        ExpectNotInfo("score cp 10");
        ExpectNotInfo("option name Skill Level type spin default 20 min 0 max 20");
    }

    TEST(UciProtocolTest, AnInfoLineIsNotABestMove) {
        const StringView line = "info depth 1 score cp 10 pv e2e4";

        EXPECT_TRUE(UciProtocol::ParseInfo(line).has_value());
        EXPECT_FALSE(UciProtocol::ParseBestMove(line).has_value());
    }

    // -- LineCollector: the lines of a search for N lines ------------------------------------------------------------------------

    namespace {

        UciProtocol::LineCollector Collect(int lineCount, std::initializer_list<const char*> lines) {
            UciProtocol::LineCollector collector(lineCount);
            for (const char* line : lines) {
                collector.Offer(line);
            }
            return collector;
        }

        /// A few lines of a Stockfish transcript with `MultiPV 3`: three lines per depth, each depth after the one before
        constexpr const char* kThreeLineTranscript[] = {
            "info string NNUE evaluation using nn-1111111111.nnue",
            "info depth 1 seldepth 1 multipv 1 score cp 18 nodes 20 nps 20000 hashfull 0 tbhits 0 time 1 pv e2e4",
            "info depth 1 seldepth 1 multipv 2 score cp 12 nodes 20 nps 20000 hashfull 0 tbhits 0 time 1 pv d2d4",
            "info depth 1 seldepth 1 multipv 3 score cp 5 nodes 20 nps 20000 hashfull 0 tbhits 0 time 1 pv g1f3",
            "info depth 2 seldepth 2 multipv 1 score cp 32 nodes 90 nps 90000 hashfull 0 tbhits 0 time 1 pv e2e4 e7e5",
            "info depth 2 seldepth 2 multipv 2 score cp 20 nodes 90 nps 90000 hashfull 0 tbhits 0 time 1 pv d2d4 d7d5",
            "info depth 2 seldepth 2 multipv 3 score cp 11 nodes 90 nps 90000 hashfull 0 tbhits 0 time 1 pv g1f3 d7d5",
        };

        UciProtocol::LineCollector CollectTranscript(int lineCount, size_t offered = std::size(kThreeLineTranscript)) {
            UciProtocol::LineCollector collector(lineCount);
            for (size_t at = 0; at < offered; ++at) {
                collector.Offer(kThreeLineTranscript[at]);
            }
            return collector;
        }

    }  // namespace

    TEST(UciProtocolTest, TheCollectorKeepsTheLastDepthOfEachOfThreeLines) {
        const eastl::vector<UciProtocol::Info> lines = CollectTranscript(3).Lines();

        ASSERT_EQ(lines.size(), 3u);
        EXPECT_EQ(lines[0].multiPv, 1);
        EXPECT_EQ(lines[0].firstPvMove, "e2e4");
        EXPECT_EQ(lines[0].score, Cp(32));
        EXPECT_EQ(lines[0].depth, 2);
        EXPECT_EQ(lines[1].multiPv, 2);
        EXPECT_EQ(lines[1].firstPvMove, "d2d4");
        EXPECT_EQ(lines[1].score, Cp(20));
        EXPECT_EQ(lines[1].depth, 2);
        EXPECT_EQ(lines[2].multiPv, 3);
        EXPECT_EQ(lines[2].firstPvMove, "g1f3");
        EXPECT_EQ(lines[2].score, Cp(11));
        EXPECT_EQ(lines[2].depth, 2);
    }

    TEST(UciProtocolTest, TheCollectorOfOneLineTakesOnlyIndexOne) {
        const eastl::vector<UciProtocol::Info> lines = CollectTranscript(1).Lines();

        ASSERT_EQ(lines.size(), 1u);
        EXPECT_EQ(lines[0].multiPv, 1);
        EXPECT_EQ(lines[0].firstPvMove, "e2e4");
        EXPECT_EQ(lines[0].score, Cp(32));
    }

    TEST(UciProtocolTest, TheCollectorOfOneLineReadsALineWithoutMultiPvAsTheFirst) {
        // A search for one line sends no `multipv` word at all
        const eastl::vector<UciProtocol::Info> lines = Collect(1, {"info depth 1 score cp 10 pv e2e4", "info depth 2 score cp 14 pv e2e4 e7e5"}).Lines();

        ASSERT_EQ(lines.size(), 1u);
        EXPECT_EQ(lines[0].multiPv, 1);
        EXPECT_EQ(lines[0].score, Cp(14));
        EXPECT_EQ(lines[0].depth, 2);
    }

    TEST(UciProtocolTest, TheCollectorOfTwoLinesLeavesIndexThreeOut) {
        const eastl::vector<UciProtocol::Info> lines = CollectTranscript(2).Lines();

        ASSERT_EQ(lines.size(), 2u);
        EXPECT_EQ(lines[0].firstPvMove, "e2e4");
        EXPECT_EQ(lines[1].firstPvMove, "d2d4");
    }

    TEST(UciProtocolTest, ACollectorForNoLinesActsAsOneForOne) {
        EXPECT_EQ(CollectTranscript(0).Lines().size(), 1u);
        EXPECT_EQ(CollectTranscript(-4).Lines().size(), 1u);
    }

    TEST(UciProtocolTest, TheCollectorWithNothingOfferedHasNoLines) {
        EXPECT_TRUE(UciProtocol::LineCollector(3).Lines().empty());
    }

    TEST(UciProtocolTest, ABoundIsKeptOnlyForAnIndexThatNeverHadAnExactScore) {
        const eastl::vector<UciProtocol::Info> lines = Collect(3,
                                                               {
                                                                   "info depth 3 multipv 1 score cp 30 pv e2e4",
                                                                   "info depth 4 multipv 1 score cp 80 lowerbound pv e2e4",  // later, but only a bound: the exact one stays
                                                                   "info depth 3 multipv 2 score cp 20 upperbound pv d2d4",
                                                                   "info depth 4 multipv 2 score cp 25 upperbound pv d2d4",  // no exact one for index 2: the latest bound
                                                                   "info depth 3 multipv 3 score cp 10 pv g1f3",
                                                               })
                                                           .Lines();

        ASSERT_EQ(lines.size(), 3u);
        EXPECT_EQ(lines[0].score, Cp(30));
        EXPECT_FALSE(lines[0].lowerBound);
        EXPECT_EQ(lines[1].score, Cp(25));
        EXPECT_TRUE(lines[1].upperBound);
        EXPECT_EQ(lines[1].depth, 4);
        EXPECT_EQ(lines[2].score, Cp(10));
    }

    TEST(UciProtocolTest, ALaterExactScoreReplacesABoundAndAnEarlierExactOne) {
        const eastl::vector<UciProtocol::Info> lines = Collect(2,
                                                               {
                                                                   "info depth 3 multipv 2 score cp 20 lowerbound pv d2d4",
                                                                   "info depth 3 multipv 2 score cp 22 pv d2d4",
                                                                   "info depth 4 multipv 2 score cp 24 pv d2d4",
                                                                   "info depth 5 multipv 2 score cp 50 upperbound pv d2d4",
                                                               })
                                                           .Lines();

        ASSERT_EQ(lines.size(), 1u) << "index 1 never came";
        EXPECT_EQ(lines[0].multiPv, 2);
        EXPECT_EQ(lines[0].score, Cp(24));
        EXPECT_EQ(lines[0].depth, 4);
        EXPECT_FALSE(lines[0].upperBound);
    }

    TEST(UciProtocolTest, TheCollectorIgnoresWhatIsNotAScoredInfoLine) {
        const eastl::vector<UciProtocol::Info> lines = Collect(2,
                                                               {
                                                                   "info depth 4 multipv 1 score cp 30 pv e2e4",
                                                                   "info depth 5 multipv 1 pv e2e4",               // no score
                                                                   "info depth 5 multipv 1 score cp",              // a truncated one
                                                                   "info string multipv 1 score cp 999 pv a2a3",   // text
                                                                   "info depth 5 multipv 2 score mate 0 pv d2d4",  // no usable score
                                                                   "info currmove d2d4 currmovenumber 2",          // no score
                                                                   "bestmove e2e4 ponder e7e5",                    // not info
                                                                   "readyok",
                                                                   "",
                                                               })
                                                           .Lines();

        ASSERT_EQ(lines.size(), 1u);
        EXPECT_EQ(lines[0].depth, 4);
        EXPECT_EQ(lines[0].score, Cp(30));
        EXPECT_EQ(lines[0].firstPvMove, "e2e4");
    }

    TEST(UciProtocolTest, WhenTheLastDepthIsCutShortAnIndexKeepsTheDepthBefore) {
        // A search stopped by its time: depth 2 reached the first two lines only
        const eastl::vector<UciProtocol::Info> lines = CollectTranscript(3, 6).Lines();

        ASSERT_EQ(lines.size(), 3u);
        EXPECT_EQ(lines[0].depth, 2);
        EXPECT_EQ(lines[1].depth, 2);
        EXPECT_EQ(lines[2].depth, 1);
        EXPECT_EQ(lines[2].score, Cp(5));
        EXPECT_EQ(lines[2].firstPvMove, "g1f3");
    }

    TEST(UciProtocolTest, AMissingIndexLeavesTheOthersInOrder) {
        const eastl::vector<UciProtocol::Info> lines = Collect(3,
                                                               {
                                                                   "info depth 2 multipv 3 score cp 11 pv g1f3",
                                                                   "info depth 2 multipv 1 score cp 32 pv e2e4",
                                                               })
                                                           .Lines();

        ASSERT_EQ(lines.size(), 2u);
        EXPECT_EQ(lines[0].multiPv, 1);
        EXPECT_EQ(lines[1].multiPv, 3);
        EXPECT_EQ(lines[1].firstPvMove, "g1f3");
    }

    TEST(UciProtocolTest, TheCollectorReadsLinesWithAnEndOfLineLeftOnThem) {
        const eastl::vector<UciProtocol::Info> lines = Collect(2,
                                                               {
                                                                   "info depth 2 multipv 1 score cp 32 pv e2e4\r",
                                                                   "info depth 2 multipv 2 score mate -2 pv d2d4\r\n",
                                                               })
                                                           .Lines();

        ASSERT_EQ(lines.size(), 2u);
        EXPECT_EQ(lines[0].firstPvMove, "e2e4");
        EXPECT_EQ(lines[1].firstPvMove, "d2d4");
        EXPECT_EQ(lines[1].score, Mate(-2));
    }

    TEST(UciProtocolTest, AMultiPvIndexBelowOneIsReadAsTheMainLine) {
        // ParseInfo keeps the default 1 for a zero or negative index, so such a line is the main line
        const eastl::vector<UciProtocol::Info> lines = Collect(2, {"info depth 2 multipv 0 score cp 7 pv e2e4"}).Lines();

        ASSERT_EQ(lines.size(), 1u);
        EXPECT_EQ(lines[0].multiPv, 1);
    }

    TEST(UciProtocolTest, TrimLineTakesSpacesAndLineEndsFromBothEnds) {
        EXPECT_EQ(UciProtocol::TrimLine("uciok"), StringView("uciok"));
        EXPECT_EQ(UciProtocol::TrimLine("uciok\n"), StringView("uciok"));
        EXPECT_EQ(UciProtocol::TrimLine("uciok\r\n"), StringView("uciok"));
        EXPECT_EQ(UciProtocol::TrimLine("  readyok  "), StringView("readyok"));
        EXPECT_EQ(UciProtocol::TrimLine(" \r\n bestmove e2e4 \r\n "), StringView("bestmove e2e4"));
    }

    TEST(UciProtocolTest, TrimLineLeavesTheInsideAlone) {
        EXPECT_EQ(UciProtocol::TrimLine("a  b"), StringView("a  b"));
        EXPECT_EQ(UciProtocol::TrimLine(" a \r\n b "), StringView("a \r\n b"));
    }

    TEST(UciProtocolTest, TrimLineOfNothingIsEmpty) {
        EXPECT_TRUE(UciProtocol::TrimLine("").empty());
        EXPECT_TRUE(UciProtocol::TrimLine("   ").empty());
        EXPECT_TRUE(UciProtocol::TrimLine("\r\n").empty());
        EXPECT_TRUE(UciProtocol::TrimLine(" \r\n \r\n").empty());
    }

}  // namespace BECore::Chess::Tests
