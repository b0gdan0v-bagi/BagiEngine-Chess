#include "ChessTestUtil.h"

#include <Chess/Engine/SearchRequest.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>

/// The request an engine gets: the start FEN and the moves of a game, kept apart from the model that goes on changing
namespace BECore::Chess::Tests {

    namespace {

        constexpr SearchLimits kMoveTime100{100, 0};
        constexpr eastl::string_view kCastlingAndPromotionFen = "4k2r/1P6/8/8/8/8/8/R3K2R w KQk - 0 1";

        bool Play(ChessGameModel& model, eastl::string_view uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        eastl::string SpellMoves(const SearchRequest& request) {
            eastl::string text;
            for (const Move& move : request.moves) {
                text.append(text.empty() ? "" : " ");
                text.append(move.ToUci());
            }
            return text;
        }

        /// What every IChessEngine has to be able to do with a request, in the plainest way: play the first legal move of the final position
        class FirstLegalMoveEngine final : public IChessEngine {
        public:
            std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken& token) override {
                if (token.IsCancelled()) {
                    return std::unexpected(EngineError{EngineErrorKind::Cancelled, "cancelled before the search"});
                }
                const auto position = request.FinalPosition();
                if (!position) {
                    return std::unexpected(position.error());
                }
                const MoveList moves = MoveGenerator::GenerateLegal(*position);
                if (moves.empty()) {
                    return std::unexpected(EngineError{EngineErrorKind::NoMove, "the game is over"});
                }
                return moves.front();
            }
        };

        void ExpectBadRequest(const std::expected<Position, EngineError>& result) {
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().kind, EngineErrorKind::BadRequest);
            EXPECT_FALSE(result.error().detail.empty());
        }

        void ExpectBadRequest(const std::expected<void, EngineError>& result) {
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().kind, EngineErrorKind::BadRequest);
            EXPECT_FALSE(result.error().detail.empty());
        }

    }  // namespace

    TEST(SearchRequestTest, AFreshModelGivesTheOpeningAndNoMoves) {
        const ChessGameModel model;
        const SearchRequest request = SearchRequest::FromModel(model, kMoveTime100);

        EXPECT_EQ(request.startFen, eastl::string(Position::kStartFen));
        EXPECT_TRUE(request.moves.empty());
        EXPECT_EQ(request.limits.moveTimeMs, 100);
        EXPECT_EQ(request.limits.depth, 0);
    }

    TEST(SearchRequestTest, AModelGivesItsStartFenAndItsMovesWithCastlingAndPromotionSpelledRight) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen(kCastlingAndPromotionFen));
        ASSERT_TRUE(Play(model, "e1c1"));  // white castles long
        ASSERT_TRUE(Play(model, "e8g8"));  // black castles short
        ASSERT_TRUE(Play(model, "b7b8q"));

        const SearchRequest request = SearchRequest::FromModel(model, SearchLimits{0, 8});

        EXPECT_EQ(request.startFen, eastl::string(kCastlingAndPromotionFen));
        EXPECT_EQ(SpellMoves(request), "e1c1 e8g8 b7b8q");
        EXPECT_EQ(request.limits.moveTimeMs, 0);
        EXPECT_EQ(request.limits.depth, 8);
        ASSERT_EQ(request.moves.size(), model.GetMoves().size());
        for (size_t index = 0; index < request.moves.size(); ++index) {
            EXPECT_TRUE(request.moves[index] == model.GetMoves()[index]) << "move " << index;
        }
    }

    TEST(SearchRequestTest, AnUnderpromotionKeepsItsLetter) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen("8/P6k/8/8/8/8/8/K7 w - - 0 1"));
        ASSERT_TRUE(Play(model, "a7a8n"));

        EXPECT_EQ(SpellMoves(SearchRequest::FromModel(model, kMoveTime100)), "a7a8n");
    }

    TEST(SearchRequestTest, TheRequestIsACopyThatTheModelDoesNotChange) {
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        const SearchRequest request = SearchRequest::FromModel(model, kMoveTime100);

        ASSERT_TRUE(Play(model, "e7e5"));
        model.SetPosition(PositionFromFen(kKiwipeteFen));

        EXPECT_EQ(SpellMoves(request), "e2e4");
        EXPECT_EQ(request.startFen, eastl::string(Position::kStartFen));
    }

    TEST(SearchRequestTest, APositionGivesItsFenAndNoMoves) {
        const Position position = PositionFromFen(kKiwipeteFen);
        const SearchRequest request = SearchRequest::FromPosition(position, kMoveTime100);

        EXPECT_EQ(request.startFen, position.ToFen());
        EXPECT_TRUE(request.moves.empty());
        EXPECT_EQ(request.limits.moveTimeMs, 100);

        const auto finalPosition = request.FinalPosition();
        ASSERT_TRUE(finalPosition.has_value());
        EXPECT_EQ(finalPosition->Key(), position.Key());
        EXPECT_EQ(finalPosition->ToFen(), position.ToFen());
    }

    TEST(SearchRequestTest, APositionLosesItsRepetitionHistoryAModelKeepsIt) {
        ChessGameModel model;
        for (const char* move : {"g1f3", "g8f6", "f3g1", "f6g8"}) {
            ASSERT_TRUE(Play(model, move));
        }
        ASSERT_EQ(model.GetPosition().RepetitionCount(), 2);

        const auto fromModel = SearchRequest::FromModel(model, kMoveTime100).FinalPosition();
        const auto fromPosition = SearchRequest::FromPosition(model.GetPosition(), kMoveTime100).FinalPosition();

        ASSERT_TRUE(fromModel.has_value());
        ASSERT_TRUE(fromPosition.has_value());
        EXPECT_EQ(fromModel->Key(), fromPosition->Key());
        EXPECT_EQ(fromModel->RepetitionCount(), 2);
        EXPECT_EQ(fromPosition->RepetitionCount(), 1);
    }

    TEST(SearchRequestTest, TheFinalPositionIsTheOneOfTheModel) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen(kCastlingAndPromotionFen));
        for (const char* move : {"e1c1", "e8g8", "b7b8q", "g8g7"}) {
            ASSERT_TRUE(Play(model, move)) << move;
        }

        const auto finalPosition = SearchRequest::FromModel(model, kMoveTime100).FinalPosition();

        ASSERT_TRUE(finalPosition.has_value());
        EXPECT_EQ(finalPosition->Key(), model.GetPosition().Key());
        EXPECT_EQ(finalPosition->ToFen(), model.GetPosition().ToFen());
        EXPECT_EQ(finalPosition->SideToMove(), model.GetTurn());
    }

    TEST(SearchRequestTest, TheFinalPositionOfAGameThatEndedInARepetitionHasTheRepetitions) {
        ChessGameModel model;
        for (int round = 0; round < 2; ++round) {
            for (const char* move : {"g1f3", "g8f6", "f3g1", "f6g8"}) {
                ASSERT_TRUE(Play(model, move));
            }
        }
        ASSERT_EQ(model.GetStatus(), GameStatus::DrawThreefold);

        const auto finalPosition = SearchRequest::FromModel(model, kMoveTime100).FinalPosition();

        ASSERT_TRUE(finalPosition.has_value());
        EXPECT_EQ(finalPosition->RepetitionCount(), 3);
        EXPECT_EQ(finalPosition->Key(), model.GetPosition().Key());
    }

    TEST(SearchRequestTest, AHandMadeMoveWithoutItsKindIsPlayedAsCastling) {
        // Mv() makes a normal move; the king's two steps are castling, which the position knows from the legal moves
        SearchRequest request;
        request.startFen = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
        request.moves.push_back(Mv("e1g1"));
        request.limits = kMoveTime100;

        const auto finalPosition = request.FinalPosition();

        ASSERT_TRUE(finalPosition.has_value());
        EXPECT_EQ(finalPosition->ToFen(), "r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1");
    }

    TEST(SearchRequestTest, AMoveThatIsNotLegalIsABadRequest) {
        SearchRequest request = SearchRequest::FromPosition(Position::Start(), kMoveTime100);
        request.moves.push_back(Mv("e2e5"));

        const auto finalPosition = request.FinalPosition();

        ASSERT_NO_FATAL_FAILURE(ExpectBadRequest(finalPosition));
        EXPECT_NE(finalPosition.error().detail.find("e2e5"), eastl::string::npos);
    }

    TEST(SearchRequestTest, AMoveOutOfTurnIsABadRequest) {
        SearchRequest request = SearchRequest::FromPosition(Position::Start(), kMoveTime100);
        request.moves.push_back(Mv("e7e5"));  // legal for Black, but White is to move

        ExpectBadRequest(request.FinalPosition());
    }

    TEST(SearchRequestTest, AnIllegalMoveLaterInTheListIsFoundToo) {
        SearchRequest request = SearchRequest::FromPosition(Position::Start(), kMoveTime100);
        request.moves.push_back(Mv("e2e4"));
        request.moves.push_back(Mv("e7e5"));
        request.moves.push_back(Mv("e4e5"));  // the pawn is blocked

        const auto finalPosition = request.FinalPosition();

        ASSERT_NO_FATAL_FAILURE(ExpectBadRequest(finalPosition));
        EXPECT_NE(finalPosition.error().detail.find("move 3"), eastl::string::npos);
    }

    TEST(SearchRequestTest, TheNullMoveIsABadRequest) {
        SearchRequest request = SearchRequest::FromPosition(Position::Start(), kMoveTime100);
        request.moves.push_back(Move());

        ExpectBadRequest(request.FinalPosition());
    }

    TEST(SearchRequestTest, ABrokenFenIsABadRequest) {
        for (const char* fen : {"", "not a fen", "8/8/8/8/8/8/8/8 w - - 0 1", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1"}) {
            SearchRequest request;
            request.startFen = fen;
            request.limits = kMoveTime100;

            ExpectBadRequest(request.FinalPosition());
        }
    }

    TEST(SearchRequestTest, ValidLimitsPass) {
        for (const SearchLimits limits : {SearchLimits{100, 0}, SearchLimits{0, 8}, SearchLimits{100, 8}, SearchLimits{1, 1}}) {
            SearchRequest request;
            request.limits = limits;

            EXPECT_TRUE(request.Validate().has_value()) << limits.moveTimeMs << " ms, depth " << limits.depth;
        }
    }

    TEST(SearchRequestTest, LimitsThatAskForNothingAreRejected) {
        SearchRequest request;
        request.limits = SearchLimits{0, 0};

        ExpectBadRequest(request.Validate());
    }

    TEST(SearchRequestTest, NegativeLimitsAreRejected) {
        for (const SearchLimits limits : {SearchLimits{-1, 0}, SearchLimits{0, -1}, SearchLimits{-100, 8}, SearchLimits{100, -8}, SearchLimits{-1, -1}}) {
            SearchRequest request;
            request.limits = limits;

            ExpectBadRequest(request.Validate());
        }
    }

    TEST(SearchRequestTest, TheDefaultRequestHasNoLimits) {
        const SearchRequest request;

        ExpectBadRequest(request.Validate());
    }

    TEST(SearchRequestTest, BothFactoriesAskForOneLine) {
        const ChessGameModel model;

        EXPECT_EQ(SearchRequest::FromModel(model, kMoveTime100).lines, 1);
        EXPECT_EQ(SearchRequest::FromPosition(Position::Start(), kMoveTime100).lines, 1);
        EXPECT_EQ(SearchRequest().lines, 1);
    }

    TEST(SearchRequestTest, TheNumberOfLinesIsOneToTheMostAnalysisLines) {
        for (const int lines : {1, 2, kMaxAnalysisLines}) {
            SearchRequest request = SearchRequest::FromPosition(Position::Start(), kMoveTime100);
            request.lines = lines;

            EXPECT_TRUE(request.Validate().has_value()) << lines;
        }
        for (const int lines : {0, -1, kMaxAnalysisLines + 1}) {
            SearchRequest request = SearchRequest::FromPosition(Position::Start(), kMoveTime100);
            request.lines = lines;

            const auto valid = request.Validate();
            ASSERT_NO_FATAL_FAILURE(ExpectBadRequest(valid)) << lines;
            EXPECT_NE(valid.error().detail.find(Format("{}", lines).c_str()), eastl::string::npos) << "the sentence names the number";
        }
    }

    TEST(SearchRequestTest, AnEngineAnswersARequestThroughTheInterface) {
        FirstLegalMoveEngine implementation;
        IChessEngine& engine = implementation;
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));

        const auto move = engine.FindMove(SearchRequest::FromModel(model, kMoveTime100), CancellationToken());

        ASSERT_TRUE(move.has_value());
        const MoveList legal = MoveGenerator::GenerateLegal(model.GetPosition());
        EXPECT_NE(std::ranges::find(legal, *move), legal.end());
    }

    TEST(SearchRequestTest, AnEngineSeesTheCancellationAndTheBadRequest) {
        FirstLegalMoveEngine implementation;
        IChessEngine& engine = implementation;
        const SearchRequest request = SearchRequest::FromPosition(Position::Start(), kMoveTime100);

        CancellationToken token = CancellationToken::Create();
        token.Cancel();
        const auto cancelled = engine.FindMove(request, token);
        ASSERT_FALSE(cancelled.has_value());
        EXPECT_EQ(cancelled.error().kind, EngineErrorKind::Cancelled);

        SearchRequest broken = request;
        broken.startFen = "not a fen";
        const auto refused = engine.FindMove(broken, CancellationToken());
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().kind, EngineErrorKind::BadRequest);
    }

}  // namespace BECore::Chess::Tests
