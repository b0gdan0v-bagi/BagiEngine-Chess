#include "ChessTestUtil.h"
#include "StockfishUtil.h"

#include <Chess/Engine/Analysis.h>
#include <Chess/Engine/OwnEngine.h>
#include <Chess/Engine/SearchRequest.h>
#include <Chess/Engine/UciEngine.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>

/// UciEngine against a real Stockfish on positions with one clear answer, among them the ones where our request and our validation could go wrong:
/// a game with a history, an underpromotion, castling and en passant. Every case skips itself when there is no Stockfish (StockfishUtil.h).
/// The searches have a fixed depth, not a time: a depth gives the same answer on every run, a move time does not. Each position was checked
/// against Stockfish 19 (the key move is the one move with a mate score, the only legal move, or, for the history pair and the stalemate trap, a move that
/// stays clear of the others by a wide margin or is named below) and run several times
namespace BECore::Chess::Tests {

    namespace {

        constexpr SearchLimits kDepth10{0, 10};

        // A mate in two with one key move (Nf6+ gxf6 Bxf7#), and the same position with the colors exchanged (Black to move)
        constexpr eastl::string_view kMateInTwoFen = "r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2bK2R w KQkq - 1 1";
        constexpr eastl::string_view kMirroredMateInTwoFen = "r2Bk2r/ppp2ppp/3p4/2bNp3/2Pnn1b1/3P4/PP2NPPP/R2QKB1R b KQkq - 1 1";

        // White has a king and a queen against a king: Qb6 stalemates, Qa6 or Qa1 mates
        constexpr eastl::string_view kStalemateTrapFen = "k7/2K5/5Q2/8/8/8/8/8 w - - 0 1";

        // The pawn on c7 promotes with c8=N mate; c8=Q, c8=R and c8=B leave Black without a move and without check: stalemate
        constexpr eastl::string_view kUnderpromotionFen = "b7/kpP5/pR1B4/P7/8/8/8/6K1 w - - 0 1";
        constexpr eastl::string_view kMirroredUnderpromotionFen = "6k1/8/8/8/p7/Pr1b4/KPp5/B7 b - - 0 1";

        // Castling kingside gives mate: the king leaves the first rank and the rook on f1 checks along it. The plain rook moves cannot (the king is in the way)
        // and the king's other steps are covered by the black rook on a2
        constexpr eastl::string_view kCastlingMateFen = "8/8/8/4B3/1N6/8/r7/1k2K2R w K - 0 1";
        constexpr eastl::string_view kMirroredCastlingMateFen = "1K2k2r/R7/8/1n6/4b3/8/8/8 b k - 0 1";

        // A black pawn has just played d7-d5 and checks the king on c4; the only legal move is the pawn on c5 taking it en passant
        constexpr eastl::string_view kEnPassantFen = "6k1/6b1/2p5/p1Pp4/2K5/r7/8/8 w - d6 0 1";
        constexpr eastl::string_view kMirroredEnPassantFen = "8/8/R7/2k5/P1pP4/2P5/6B1/6K1 b - d3 0 1";

        // Black has a queen and a rook for a rook and shuffles the queen; the white rook on h2 can take the black one on h8, which loses by
        // about a rook less than anything else. The bare FEN has White to move with the king on b1
        constexpr eastl::string_view kShuffleStartFen = "7r/6k1/2q5/8/8/8/7R/K7 b - - 0 1";
        constexpr eastl::string_view kShuffleBareFen = "7r/6k1/2q5/8/8/8/7R/1K6 w - - 0 1";

        // Analysis: a mate in one for each color (Ra8# and Ra1#), a mate in two (kMateInTwoFen above), a side that every move of which loses to a
        // mate in one, a rook that takes a queen nothing guards, and a position where White has a lone king against a queen and a rook
        constexpr eastl::string_view kMateInOneFen = "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1";
        constexpr eastl::string_view kMirroredMateInOneFen = "r5k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1";
        constexpr eastl::string_view kMatedInOneFen = "kbK5/pp6/RP6/8/8/8/8/8 b - - 0 1";
        constexpr eastl::string_view kHangingQueenFen = "4k3/8/8/3q4/8/8/8/3RK3 w - - 0 1";
        constexpr eastl::string_view kLostFen = "4k3/8/8/8/8/2q5/1r6/6K1 w - - 0 1";

        constexpr eastl::string_view kMateFen = "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1";
        constexpr eastl::string_view kStalemateFen = "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1";

        /// A request for the position @p fen, no moves behind it
        SearchRequest RequestFor(eastl::string_view fen, SearchLimits limits = kDepth10) {
            SearchRequest request;
            request.startFen = eastl::string(fen.data(), fen.size());
            request.limits = limits;
            return request;
        }

        /// The answer of a fresh UciEngine over the Stockfish at @p path
        std::expected<Move, EngineError> Ask(const eastl::string& path, const SearchRequest& request) {
            UciEngine engine(StockfishSettings(path));
            return engine.FindMove(request, CancellationToken());
        }

        /// The analysis of a fresh UciEngine over the Stockfish at @p path
        std::expected<Analysis, EngineError> Analyse(const eastl::string& path, const SearchRequest& request) {
            UciEngine engine(StockfishSettings(path));
            return engine.Analyse(request, CancellationToken());
        }

        /// The best line of the analysis, or a failure that says why not
        std::optional<AnalysisLine> BestLine(const eastl::string& path, eastl::string_view fen, SearchLimits limits = kDepth10) {
            const auto result = Analyse(path, RequestFor(fen, limits));
            if (!result) {
                ADD_FAILURE() << Describe(result.error()).c_str();
                return std::nullopt;
            }
            return result->Best();
        }

        bool Play(ChessGameModel& model, Move move) {
            return model.TryMove(move).has_value();
        }

        bool PlayUci(ChessGameModel& model, eastl::string_view uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && Play(model, *move);
        }

        ChessGameModel ModelAt(eastl::string_view fen) {
            ChessGameModel model;
            model.SetPosition(PositionFromFen(fen));
            return model;
        }

        /// The move Stockfish gives for @p request, or a failure that says why not
        std::optional<Move> Answer(const eastl::string& path, const SearchRequest& request) {
            const auto result = Ask(path, request);
            if (!result) {
                ADD_FAILURE() << Describe(result.error()).c_str();
                return std::nullopt;
            }
            return *result;
        }

        /// Stockfish answers with @p expected, and the game accepts the move and ends in @p status
        void ExpectMoveAndStatus(const eastl::string& path, eastl::string_view fen, Move expected, GameStatus status) {
            const auto answer = Answer(path, RequestFor(fen));
            ASSERT_TRUE(answer.has_value());
            EXPECT_EQ(*answer, expected) << answer->ToUci().c_str() << " instead of " << expected.ToUci().c_str();
            EXPECT_EQ(answer->Kind(), expected.Kind());
            ChessGameModel model = ModelAt(fen);
            ASSERT_TRUE(Play(model, *answer)) << answer->ToUci().c_str();
            EXPECT_EQ(model.GetStatus(), status);
        }

    }  // namespace

    TEST(StockfishPositionTest, AMateInTwoGetsItsKeyMove) {
        SKIP_WITHOUT_STOCKFISH(stockfish);

        const auto answer = Answer(*stockfish, RequestFor(kMateInTwoFen));

        ASSERT_TRUE(answer.has_value());
        EXPECT_EQ(answer->ToUci(), "d5f6");
    }

    TEST(StockfishPositionTest, TheMirroredMateInTwoGetsTheMirroredKeyMove) {
        SKIP_WITHOUT_STOCKFISH(stockfish);

        const auto answer = Answer(*stockfish, RequestFor(kMirroredMateInTwoFen));

        ASSERT_TRUE(answer.has_value());
        EXPECT_EQ(answer->ToUci(), "d4f3");
    }

    TEST(StockfishPositionTest, TheKeyMoveOfTheMateInTwoIsPlayedAndTheMateFollows) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        ChessGameModel model = ModelAt(kMateInTwoFen);
        ASSERT_TRUE(PlayUci(model, "d5f6"));
        ASSERT_TRUE(PlayUci(model, "g7f6"));

        // The model carries the moves, so the request is the one the window would send
        const auto answer = Answer(*stockfish, SearchRequest::FromModel(model, kDepth10));

        ASSERT_TRUE(answer.has_value());
        EXPECT_EQ(answer->ToUci(), "c4f7");
        ASSERT_TRUE(Play(model, *answer));
        EXPECT_EQ(model.GetStatus(), GameStatus::Checkmate);
    }

    TEST(StockfishPositionTest, AWinningSideDoesNotStalemateTheOpponent) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        ChessGameModel trap = ModelAt(kStalemateTrapFen);
        ASSERT_TRUE(PlayUci(trap, "f6b6"));
        ASSERT_EQ(trap.GetStatus(), GameStatus::Stalemate) << "the position has lost its trap";

        const auto answer = Answer(*stockfish, RequestFor(kStalemateTrapFen));

        ASSERT_TRUE(answer.has_value());
        EXPECT_NE(answer->ToUci(), "f6b6");
        ChessGameModel model = ModelAt(kStalemateTrapFen);
        ASSERT_TRUE(Play(model, *answer)) << answer->ToUci().c_str();
        EXPECT_NE(model.GetStatus(), GameStatus::Stalemate);
        EXPECT_EQ(model.GetStatus(), GameStatus::Checkmate) << "a mate in one is on the board";
    }

    TEST(StockfishPositionTest, AGameWithItsHistoryReachesStockfishAndTheLostSideTakesTheRepetition) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        // The black queen shuffles c6-c5-c6, the white king a1-b1-a1: after seven plies the position with the white king on b1 has come twice, and
        // Kb1-a1 would bring the position the game began with for the third time, a draw; every other move of White loses (Rxh8 least)
        ChessGameModel model = ModelAt(kShuffleStartFen);
        for (const char* move : {"c6c5", "a1b1", "c5c6", "b1a1", "c6c5", "a1b1", "c5c6"}) {
            ASSERT_TRUE(PlayUci(model, move)) << move;
        }
        ASSERT_EQ(model.GetStatus(), GameStatus::Ongoing);
        ASSERT_EQ(model.GetPosition().Key(), PositionFromFen(kShuffleBareFen).Key());
        ASSERT_EQ(model.GetPosition().RepetitionCount(), 2);

        const auto answer = Answer(*stockfish, SearchRequest::FromModel(model, kDepth10));

        ASSERT_TRUE(answer.has_value());
        EXPECT_EQ(answer->ToUci(), "b1a1");
        ASSERT_TRUE(Play(model, *answer));
        EXPECT_EQ(model.GetStatus(), GameStatus::DrawThreefold);
    }

    TEST(StockfishPositionTest, TheSameFenWithoutItsHistoryGetsAnotherMove) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        // The position of the case above as a bare FEN: nothing is repeated, the draw is not on offer and Stockfish takes the rook (-496 against -885
        // for the next move at depth 10, a gap that does not close at depth 16)
        const auto answer = Answer(*stockfish, RequestFor(kShuffleBareFen));

        ASSERT_TRUE(answer.has_value());
        EXPECT_EQ(answer->ToUci(), "h2h8");
    }

    TEST(StockfishPositionTest, AGameOfTwentyPliesWithRepeatedPositionsIsAnsweredWithALegalMove) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        // Three knight and queen shuffles, so that three positions have come twice and none three times
        ChessGameModel model;
        for (const char* move : {"e2e4", "e7e5", "g1f3", "g8f6", "f3g1", "f6g8", "d2d3", "d7d6", "g1f3", "g8f6", "f3g1", "f6g8", "c2c3", "c7c6", "d1c2", "d8c7", "c2d1", "c7d8", "g1f3", "g8f6"}) {
            ASSERT_TRUE(PlayUci(model, move)) << move;
        }
        ASSERT_EQ(model.GetStatus(), GameStatus::Ongoing);
        ASSERT_EQ(model.GetMoves().size(), 20u);

        const auto answer = Answer(*stockfish, SearchRequest::FromModel(model, kDepth10));

        ASSERT_TRUE(answer.has_value());
        EXPECT_TRUE(Play(model, *answer)) << answer->ToUci().c_str();
    }

    TEST(StockfishPositionTest, AnUnderpromotionThatMatesComesBackWithItsLetter) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        // The position is only a mate by the knight: the other three pieces stalemate
        for (const char* piece : {"q", "r", "b"}) {
            ChessGameModel other = ModelAt(kUnderpromotionFen);
            ASSERT_TRUE(PlayUci(other, Format("c7c8{}", piece))) << piece;
            ASSERT_EQ(other.GetStatus(), GameStatus::Stalemate) << piece;
        }

        const auto answer = Answer(*stockfish, RequestFor(kUnderpromotionFen));

        ASSERT_TRUE(answer.has_value());
        EXPECT_EQ(answer->ToUci(), "c7c8n");
        EXPECT_TRUE(answer->IsPromotion());
        ChessGameModel model = ModelAt(kUnderpromotionFen);
        ASSERT_TRUE(Play(model, *answer));
        EXPECT_EQ(model.GetStatus(), GameStatus::Checkmate);
    }

    TEST(StockfishPositionTest, TheMirroredUnderpromotionComesBackWithItsLetterToo) {
        SKIP_WITHOUT_STOCKFISH(stockfish);

        ExpectMoveAndStatus(*stockfish, kMirroredUnderpromotionFen, Mv("c2c1n"), GameStatus::Checkmate);
    }

    TEST(StockfishPositionTest, CastlingIsTheMoveThatMates) {
        SKIP_WITHOUT_STOCKFISH(stockfish);

        ExpectMoveAndStatus(*stockfish, kCastlingMateFen, CastlingMove("e1g1"), GameStatus::Checkmate);
    }

    TEST(StockfishPositionTest, BlackCastlesToMateToo) {
        SKIP_WITHOUT_STOCKFISH(stockfish);

        ExpectMoveAndStatus(*stockfish, kMirroredCastlingMateFen, CastlingMove("e8g8"), GameStatus::Checkmate);
    }

    TEST(StockfishPositionTest, AnEnPassantCaptureThatIsTheOnlyMoveComesBackAsEnPassant) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        const MoveList legal = MoveGenerator::GenerateLegal(PositionFromFen(kEnPassantFen));
        ASSERT_EQ(legal.size(), 1u) << "the position has more than the one move";

        ExpectMoveAndStatus(*stockfish, kEnPassantFen, EnPassantMove("c5d6"), GameStatus::Ongoing);
    }

    TEST(StockfishPositionTest, BlackTakesEnPassantToo) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        const MoveList legal = MoveGenerator::GenerateLegal(PositionFromFen(kMirroredEnPassantFen));
        ASSERT_EQ(legal.size(), 1u) << "the position has more than the one move";

        ExpectMoveAndStatus(*stockfish, kMirroredEnPassantFen, EnPassantMove("c4d3"), GameStatus::Ongoing);
    }

    TEST(StockfishPositionTest, AMatedGameHasNoMoveToAskFor) {
        SKIP_WITHOUT_STOCKFISH(stockfish);

        const auto result = Ask(*stockfish, RequestFor(kMateFen));

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().kind, EngineErrorKind::NoMove) << result.error().detail.c_str();
    }

    TEST(StockfishPositionTest, AStalematedGameHasNoMoveToAskFor) {
        SKIP_WITHOUT_STOCKFISH(stockfish);

        const auto result = Ask(*stockfish, RequestFor(kStalemateFen));

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().kind, EngineErrorKind::NoMove) << result.error().detail.c_str();
    }

    TEST(StockfishPositionTest, AnalysisOfAMateInOneIsTheMatingMoveAndMateOne) {
        SKIP_WITHOUT_STOCKFISH(stockfish);

        const auto line = BestLine(*stockfish, kMateInOneFen);

        ASSERT_TRUE(line.has_value());
        EXPECT_EQ(line->move.ToUci(), "a1a8");
        EXPECT_EQ(line->score, (AnalysisScore{AnalysisScoreKind::Mate, 1})) << ScoreText(line->score).c_str();
        EXPECT_GE(line->depth, 1);
    }

    TEST(StockfishPositionTest, AnalysisOfTheMirroredMateInOneWithBlackToMoveIsMateOneToo) {
        SKIP_WITHOUT_STOCKFISH(stockfish);

        const auto line = BestLine(*stockfish, kMirroredMateInOneFen);

        ASSERT_TRUE(line.has_value());
        EXPECT_EQ(line->move.ToUci(), "a8a1");
        EXPECT_EQ(line->score, (AnalysisScore{AnalysisScoreKind::Mate, 1})) << "positive for the side to move, Black here: " << ScoreText(line->score).c_str();
    }

    TEST(StockfishPositionTest, AnalysisOfAHangingQueenIsTheCaptureAndWellAbovePlusThree) {
        SKIP_WITHOUT_STOCKFISH(stockfish);

        const auto line = BestLine(*stockfish, kHangingQueenFen);

        ASSERT_TRUE(line.has_value());
        EXPECT_EQ(line->move.ToUci(), "d1d5");
        EXPECT_EQ(line->score.kind, AnalysisScoreKind::Centipawns);
        EXPECT_GT(line->score.value, 300) << ScoreText(line->score).c_str();
    }

    TEST(StockfishPositionTest, AnalysisOfTheStartPositionIsWithinEightyCentipawnsOfZero) {
        SKIP_WITHOUT_STOCKFISH(stockfish);

        const auto line = BestLine(*stockfish, Position::kStartFen);

        ASSERT_TRUE(line.has_value());
        EXPECT_EQ(line->score.kind, AnalysisScoreKind::Centipawns);
        EXPECT_GE(line->score.value, -80) << ScoreText(line->score).c_str();
        EXPECT_LE(line->score.value, 80) << ScoreText(line->score).c_str();
    }

    TEST(StockfishPositionTest, AnalysisOfALostPositionIsNegativeForTheSideToMove) {
        SKIP_WITHOUT_STOCKFISH(stockfish);

        const auto line = BestLine(*stockfish, kLostFen);

        ASSERT_TRUE(line.has_value());
        EXPECT_LT(line->score.value, 0) << "from the side to move, White here: " << ScoreText(line->score).c_str();
    }

    // -- Several lines (MultiPV) ------------------------------------------------------------------------------------------------

    TEST(StockfishPositionTest, ThreeLinesFromTheStartAreThreeLegalMovesWithScoresThatDoNotRise) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        SearchRequest request = RequestFor(Position::kStartFen);
        request.lines = 3;

        const auto result = Analyse(*stockfish, request);

        ASSERT_TRUE(result.has_value()) << Describe(result.error()).c_str();
        ASSERT_EQ(result->LineCount(), 3u);
        const Position start = PositionFromFen(Position::kStartFen);
        for (size_t at = 0; at < result->lines.size(); ++at) {
            const AnalysisLine& line = result->lines[at];
            EXPECT_TRUE(MoveGenerator::ParseUci(start, line.move.ToUci()).has_value()) << line.move.ToUci().c_str();
            EXPECT_EQ(line.score.kind, AnalysisScoreKind::Centipawns) << ScoreText(line.score).c_str();
            EXPECT_GE(line.depth, 1);
            for (size_t before = 0; before < at; ++before) {
                EXPECT_NE(line.move, result->lines[before].move) << "line " << at + 1 << " repeats line " << before + 1;
            }
            if (at > 0) {
                EXPECT_LE(line.score.value, result->lines[at - 1].score.value) << "line " << at + 1 << " " << ScoreText(line.score).c_str();
            }
        }
    }

    TEST(StockfishPositionTest, WithAHangingQueenLineOneTakesItAndLineTwoIsWorseByAClearMargin) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        SearchRequest request = RequestFor(kHangingQueenFen);
        request.lines = 2;

        const auto result = Analyse(*stockfish, request);

        ASSERT_TRUE(result.has_value()) << Describe(result.error()).c_str();
        ASSERT_NE(result->Second(), nullptr);
        EXPECT_EQ(result->Best().move.ToUci(), "d1d5");
        EXPECT_EQ(result->Best().score.kind, AnalysisScoreKind::Centipawns);
        EXPECT_EQ(result->Second()->score.kind, AnalysisScoreKind::Centipawns) << ScoreText(result->Second()->score).c_str();
        // Both from the side to move, White: not taking the queen leaves the rook to it, about a queen and a rook of difference
        EXPECT_GT(result->Best().score.value - result->Second()->score.value, 300) << ScoreText(result->Best().score).c_str() << " against " << ScoreText(result->Second()->score).c_str();
    }

    TEST(StockfishPositionTest, APositionWithTwoLegalMovesGivesTwoLinesForFourAsked) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        // The white king on a1 is checked by the bishop on d4; it steps to a2 or b1 and nothing else is legal
        constexpr eastl::string_view kTwoMovesFen = "7k/p7/8/8/3b4/8/7P/K7 w - - 0 1";
        ASSERT_EQ(MoveGenerator::GenerateLegal(PositionFromFen(kTwoMovesFen)).size(), 2u);
        SearchRequest request = RequestFor(kTwoMovesFen);
        request.lines = 4;

        const auto result = Analyse(*stockfish, request);

        ASSERT_TRUE(result.has_value()) << Describe(result.error()).c_str();
        ASSERT_EQ(result->LineCount(), 2u);
        EXPECT_NE(result->Best().move, result->Second()->move);
    }

    TEST(StockfishPositionTest, ARealEngineTakesMultiPVAndThenOneLineAgainOnOneProcess) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        UciEngine engine(StockfishSettings(*stockfish));
        SearchRequest request = RequestFor(kMateInOneFen);
        request.lines = 3;

        const auto many = engine.Analyse(request, CancellationToken());
        request.lines = 1;
        const auto one = engine.Analyse(request, CancellationToken());
        const auto played = engine.FindMove(request, CancellationToken());

        ASSERT_TRUE(many.has_value()) << Describe(many.error()).c_str();
        ASSERT_TRUE(one.has_value()) << Describe(one.error()).c_str();
        ASSERT_TRUE(played.has_value()) << Describe(played.error()).c_str();
        EXPECT_EQ(many->Best().move.ToUci(), "a1a8");
        EXPECT_EQ(one->LineCount(), 1u);
        EXPECT_EQ(one->Best().move.ToUci(), "a1a8");
        EXPECT_EQ(played->ToUci(), "a1a8");
    }

    TEST(StockfishPositionTest, AnalysisOfAMatedGameHasNoLineToGive) {
        SKIP_WITHOUT_STOCKFISH(stockfish);

        const auto result = Analyse(*stockfish, RequestFor(kMateFen));

        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().kind, EngineErrorKind::NoMove) << result.error().detail.c_str();
    }

    TEST(StockfishPositionTest, TheOwnEngineAndStockfishAgreeOnTheMateInAnalysis) {
        SKIP_WITHOUT_STOCKFISH(stockfish);
        OwnEngine own;
        // Mates for the side to move in one and in two, for each color, and a loss to a mate in one: the sign, the kind and the number of moves are the same
        struct Case {
            eastl::string_view fen;
            int ownDepth;
            int mateMoves;
            const char* key;
        };
        const Case cases[] = {
            {kMateInOneFen, 2, 1, "a1a8"},
            {kMirroredMateInOneFen, 2, 1, "a8a1"},
            {kMateInTwoFen, 4, 2, "d5f6"},
            {kMatedInOneFen, 4, -1, nullptr},
        };

        for (const Case& test : cases) {
            const auto fromStockfish = Analyse(*stockfish, RequestFor(test.fen));
            const auto fromOwn = own.Analyse(RequestFor(test.fen, SearchLimits{0, test.ownDepth}), CancellationToken());
            ASSERT_TRUE(fromStockfish.has_value()) << test.fen.data() << ": " << Describe(fromStockfish.error()).c_str();
            ASSERT_TRUE(fromOwn.has_value()) << test.fen.data() << ": " << Describe(fromOwn.error()).c_str();

            const AnalysisScore expected{AnalysisScoreKind::Mate, test.mateMoves};
            EXPECT_EQ(fromStockfish->Best().score, expected) << test.fen.data() << " (Stockfish): " << ScoreText(fromStockfish->Best().score).c_str();
            EXPECT_EQ(fromOwn->Best().score, expected) << test.fen.data() << " (own): " << ScoreText(fromOwn->Best().score).c_str();
            if (test.key != nullptr) {
                EXPECT_EQ(fromStockfish->Best().move.ToUci(), eastl::string(test.key)) << test.fen.data();
                EXPECT_EQ(fromOwn->Best().move.ToUci(), eastl::string(test.key)) << test.fen.data();
            }
        }
    }

}  // namespace BECore::Chess::Tests
