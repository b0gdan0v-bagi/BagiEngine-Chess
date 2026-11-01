#include "ChessTestUtil.h"

#include <Chess/Game/MoveGenerator.h>
#include <EASTL/algorithm.h>

/**
 * Legal move generation: how each piece moves and is blocked, and what the rules of check, castling, en passant and
 * promotion add. The reference counts of whole move trees are in PerftTest.cpp.
 */
namespace BECore::Chess::Tests {

    namespace {

        /// Number of legal moves of the piece standing on @p from.
        size_t CountFrom(const Position& position, eastl::string_view from) {
            size_t count = 0;
            for (const Move move : MoveGenerator::GenerateLegal(position)) {
                if (move.From() == Sq(from)) {
                    ++count;
                }
            }
            return count;
        }

        bool Has(const Position& position, eastl::string_view uci) {
            return MoveGenerator::ParseUci(position, uci).has_value();
        }

    }  // namespace

    // ---- Piece movement ----

    TEST(MoveGeneratorTest, StartPositionHasTwentyMoves) {
        EXPECT_EQ(MoveGenerator::GenerateLegal(Position::Start()).size(), 20u);
    }

    TEST(MoveGeneratorTest, PawnFromStart) {
        const Position start = Position::Start();
        EXPECT_EQ(CountFrom(start, "e2"), 2u);
        EXPECT_TRUE(Has(start, "e2e3"));
        EXPECT_TRUE(Has(start, "e2e4"));
    }

    TEST(MoveGeneratorTest, PawnBlocked) {
        const Position position = PositionFromFen("4k3/8/8/8/8/4p3/4P3/4K3 w - - 0 1");
        EXPECT_EQ(CountFrom(position, "e2"), 0u);
    }

    TEST(MoveGeneratorTest, PawnCannotJumpOverAPiece) {
        const Position position = PositionFromFen("4k3/8/8/8/4p3/8/4P3/4K3 w - - 0 1");
        EXPECT_TRUE(Has(position, "e2e3"));
        EXPECT_FALSE(Has(position, "e2e4"));
    }

    TEST(MoveGeneratorTest, PawnCapture) {
        const Position position = PositionFromFen("4k3/8/8/3p1p2/4P3/8/8/4K3 w - - 0 1");
        EXPECT_TRUE(Has(position, "e4e5")) << "forward push";
        EXPECT_TRUE(Has(position, "e4d5")) << "diagonal capture left";
        EXPECT_TRUE(Has(position, "e4f5")) << "diagonal capture right";
        EXPECT_EQ(CountFrom(position, "e4"), 3u) << "not 4: not on the starting rank, no double push";
    }

    TEST(MoveGeneratorTest, BlackPawnsMoveDown) {
        const Position start = PositionFromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1");
        EXPECT_TRUE(Has(start, "e7e6"));
        EXPECT_TRUE(Has(start, "e7e5"));
        EXPECT_FALSE(Has(start, "e7e8q"));
    }

    TEST(MoveGeneratorTest, KnightOpening) {
        const Position start = Position::Start();
        EXPECT_EQ(CountFrom(start, "b1"), 2u);
        EXPECT_TRUE(Has(start, "b1a3"));
        EXPECT_TRUE(Has(start, "b1c3"));
    }

    TEST(MoveGeneratorTest, RookOpenAndBlocked) {
        EXPECT_EQ(CountFrom(PositionFromFen("7k/8/8/8/4R3/8/8/K7 w - - 0 1"), "e4"), 14u) << "lone rook";
        EXPECT_EQ(CountFrom(PositionFromFen("7k/8/4P3/8/4R3/8/8/K7 w - - 0 1"), "e4"), 11u) << "friendly pawn cuts the ray";
        EXPECT_EQ(CountFrom(PositionFromFen("7k/8/4p3/8/4R3/8/8/K7 w - - 0 1"), "e4"), 12u) << "enemy pawn can be taken, not passed";
    }

    TEST(MoveGeneratorTest, BishopDiagonals) {
        const Position position = PositionFromFen("k7/8/8/8/3B4/8/8/7K w - - 0 1");
        EXPECT_EQ(CountFrom(position, "d4"), 13u);
        EXPECT_TRUE(Has(position, "d4a1"));
        EXPECT_TRUE(Has(position, "d4g7"));
        EXPECT_FALSE(Has(position, "d4d5")) << "no straight moves";
    }

    TEST(MoveGeneratorTest, QueenCombinesRookAndBishop) {
        EXPECT_EQ(CountFrom(PositionFromFen("k7/8/8/8/3Q4/8/8/7K w - - 0 1"), "d4"), 27u);
    }

    TEST(MoveGeneratorTest, KingNeighbors) {
        EXPECT_EQ(CountFrom(PositionFromFen("k7/8/8/8/4K3/8/8/8 w - - 0 1"), "e4"), 8u);
        EXPECT_EQ(CountFrom(PositionFromFen("7k/8/8/8/8/8/8/K7 w - - 0 1"), "a1"), 3u) << "corner";
    }

    // ---- Check ----

    TEST(MoveGeneratorTest, InCheckOnlyEvasionsAreLegal) {
        // The rook on e8 checks the king on e1 down the open e-file; the rook on a5 and the bishop on c3 can block it.
        const Position position = PositionFromFen("4r1k1/8/8/R7/8/2B5/8/4K3 w - - 0 1");
        ASSERT_TRUE(position.IsInCheck());
        EXPECT_TRUE(Has(position, "c3e5")) << "block with the bishop";
        EXPECT_TRUE(Has(position, "a5e5")) << "block with the rook";
        EXPECT_TRUE(Has(position, "e1d1")) << "king steps aside";
        EXPECT_FALSE(Has(position, "e1e2")) << "king stays on the checking file";
        EXPECT_FALSE(Has(position, "a5a8")) << "does not answer the check";
        EXPECT_FALSE(Has(position, "c3b4")) << "does not answer the check";
    }

    TEST(MoveGeneratorTest, CapturingTheCheckerIsAnEvasion) {
        // The knight on f3 checks the king on e1 and cannot be blocked; the rook on f1 can take it.
        const Position position = PositionFromFen("7k/8/8/8/8/5n2/8/4KR2 w - - 0 1");
        ASSERT_TRUE(position.IsInCheck());
        EXPECT_TRUE(Has(position, "f1f3")) << "take the knight";
        EXPECT_TRUE(Has(position, "e1e2"));
        EXPECT_FALSE(Has(position, "e1d2")) << "the knight covers d2";
        EXPECT_FALSE(Has(position, "f1f2")) << "does not answer the check";
    }

    TEST(MoveGeneratorTest, DoubleCheckAllowsOnlyKingMoves) {
        // Rook e8 and knight f3 both attack e1; the bishop on b4 could block the rook, but a double check leaves only the king.
        const Position position = PositionFromFen("4r2k/8/8/8/1B6/5n2/8/R3K3 w - - 0 1");
        ASSERT_EQ(PopCount(position.Checkers()), 2);
        const MoveList moves = MoveGenerator::GenerateLegal(position);
        EXPECT_FALSE(moves.empty());
        for (const Move move : moves) {
            EXPECT_EQ(move.FromIndex(), SqIdx("e1")) << move.ToUci().c_str();
        }
    }

    TEST(MoveGeneratorTest, PinnedPieceCannotBlockASideCheck) {
        // The rook on e8 checks along the e-file; the knight on c3 could block on e2 or e4, but the bishop on a5 pins it to the king.
        const Position position = PositionFromFen("4r2k/8/8/b7/8/2N5/8/4K3 w - - 0 1");
        ASSERT_TRUE(position.IsInCheck());
        EXPECT_EQ(CountFrom(position, "c3"), 0u) << "pinned to the king by the bishop, so it cannot answer the check";
    }

    // ---- Special moves ----

    TEST(MoveGeneratorTest, PromotionMakesFourMovesQueenFirst) {
        const Position position = PositionFromFen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
        MoveList promotions;
        for (const Move move : MoveGenerator::GenerateLegal(position)) {
            if (move.From() == Sq("a7")) {
                promotions.push_back(move);
            }
        }
        ASSERT_EQ(promotions.size(), 4u);
        EXPECT_TRUE(promotions[0].PromotionType() == PieceType::Queen);
        EXPECT_TRUE(promotions[1].PromotionType() == PieceType::Rook);
        EXPECT_TRUE(promotions[2].PromotionType() == PieceType::Bishop);
        EXPECT_TRUE(promotions[3].PromotionType() == PieceType::Knight);
        for (const Move move : promotions) {
            EXPECT_TRUE(move.IsPromotion());
        }
    }

    TEST(MoveGeneratorTest, PromotionByCaptureAlsoMakesFourMoves) {
        const Position position = PositionFromFen("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1");
        EXPECT_EQ(CountFrom(position, "a7"), 8u) << "four pushes to a8 and four captures on b8";
    }

    TEST(MoveGeneratorTest, BlackPromotesOnTheFirstRank) {
        const Position position = PositionFromFen("4k3/8/8/8/8/8/p7/4K3 b - - 0 1");
        EXPECT_TRUE(Has(position, "a2a1q"));
        EXPECT_TRUE(Has(position, "a2a1n"));
    }

    TEST(MoveGeneratorTest, EnPassantIsGenerated) {
        const Position position = PositionFromFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2");
        const auto move = MoveGenerator::ParseUci(position, "e5d6");
        ASSERT_TRUE(move.has_value());
        EXPECT_TRUE(move->Kind() == MoveKind::EnPassant);
    }

    TEST(MoveGeneratorTest, EnPassantCanAnswerACheckByTheDoubleSteppedPawn) {
        // The pawn that just moved to d4 gives check to the king on c5; taking it en passant is a legal answer.
        const Position position = PositionFromFen("8/8/8/2k5/3Pp3/8/8/4K3 b - d3 0 1");
        ASSERT_TRUE(position.IsInCheck());
        EXPECT_TRUE(Has(position, "e4d3"));
    }

    TEST(MoveGeneratorTest, EnPassantIsNotAnAnswerToAnotherPiecesCheck) {
        // A knight checks the king; the double-stepped pawn is not the checker, so taking it would not help.
        const Position position = PositionFromFen("4k3/8/8/3pP3/8/5n2/8/4K3 w - d6 0 2");
        ASSERT_TRUE(position.IsInCheck());
        EXPECT_EQ(position.EnPassantSquare(), SqIdx("d6"));
        EXPECT_FALSE(Has(position, "e5d6"));
    }

    TEST(MoveGeneratorTest, CastlingBothWays) {
        const Position position = PositionFromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        const auto kingSide = MoveGenerator::ParseUci(position, "e1g1");
        const auto queenSide = MoveGenerator::ParseUci(position, "e1c1");
        ASSERT_TRUE(kingSide.has_value());
        ASSERT_TRUE(queenSide.has_value());
        EXPECT_TRUE(kingSide->Kind() == MoveKind::Castling);
        EXPECT_TRUE(queenSide->Kind() == MoveKind::Castling);
    }

    TEST(MoveGeneratorTest, CastlingNeedsTheRightsAndEmptySquares) {
        EXPECT_FALSE(Has(PositionFromFen("r3k2r/8/8/8/8/8/8/R3K2R w kq - 0 1"), "e1g1")) << "no rights";
        EXPECT_FALSE(Has(PositionFromFen("r3k2r/8/8/8/8/8/8/R3KB1R w KQkq - 0 1"), "e1g1")) << "bishop on f1";
        EXPECT_FALSE(Has(PositionFromFen("r3k2r/8/8/8/8/8/8/RN2K2R w KQkq - 0 1"), "e1c1")) << "knight on b1 even though b1 is not crossed";
        EXPECT_TRUE(Has(PositionFromFen("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1"), "e8g8"));
        EXPECT_TRUE(Has(PositionFromFen("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1"), "e8c8"));
    }

    TEST(MoveGeneratorTest, CastlingIsNotAllowedOutOfCheckOrThroughCheck) {
        EXPECT_FALSE(Has(PositionFromFen("2k1r3/8/8/8/8/8/8/R3K2R w KQ - 0 1"), "e1g1")) << "king in check";
        EXPECT_FALSE(Has(PositionFromFen("2k2r2/8/8/8/8/8/8/R3K2R w KQ - 0 1"), "e1g1")) << "f1 attacked";
        EXPECT_FALSE(Has(PositionFromFen("2kr4/8/8/8/8/8/8/R3K2R w KQ - 0 1"), "e1c1")) << "d1 attacked";
        EXPECT_TRUE(Has(PositionFromFen("2kr4/8/8/8/8/8/8/R3K2R w KQ - 0 1"), "e1g1")) << "the other side is fine";
    }

    // ---- Checkmate and stalemate give no moves ----

    TEST(MoveGeneratorTest, CheckmateAndStalemateHaveNoLegalMoves) {
        const Position mate = PositionFromFen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
        EXPECT_TRUE(mate.IsInCheck());
        EXPECT_TRUE(MoveGenerator::GenerateLegal(mate).empty());

        const Position stalemate = PositionFromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        EXPECT_FALSE(stalemate.IsInCheck());
        EXPECT_TRUE(MoveGenerator::GenerateLegal(stalemate).empty());
    }

    // ---- UCI ----

    TEST(MoveGeneratorTest, ParseUciFindsOnlyLegalMoves) {
        const Position start = Position::Start();
        const auto move = MoveGenerator::ParseUci(start, "g1f3");
        ASSERT_TRUE(move.has_value());
        EXPECT_EQ(move->ToUci(), "g1f3");

        EXPECT_FALSE(MoveGenerator::ParseUci(start, "e2e5").has_value()) << "a pawn cannot advance three squares";
        EXPECT_FALSE(MoveGenerator::ParseUci(start, "e7e5").has_value()) << "not black's turn";
        EXPECT_FALSE(MoveGenerator::ParseUci(start, "").has_value());
        EXPECT_FALSE(MoveGenerator::ParseUci(start, "nonsense").has_value());
    }

    TEST(MoveGeneratorTest, ParseUciDistinguishesPromotionPieces) {
        const Position position = PositionFromFen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
        const auto knight = MoveGenerator::ParseUci(position, "a7a8n");
        ASSERT_TRUE(knight.has_value());
        EXPECT_TRUE(knight->PromotionType() == PieceType::Knight);
        EXPECT_FALSE(MoveGenerator::ParseUci(position, "a7a8").has_value()) << "a promotion needs its piece letter";
        EXPECT_FALSE(MoveGenerator::ParseUci(position, "a7a8k").has_value());
    }

    TEST(MoveGeneratorTest, EveryGeneratedMoveRoundTripsThroughUci) {
        for (const auto fen : {Position::kStartFen, kKiwipeteFen, kPosition3Fen, kPosition4Fen}) {
            const Position position = PositionFromFen(fen);
            for (const Move move : MoveGenerator::GenerateLegal(position)) {
                const auto parsed = MoveGenerator::ParseUci(position, eastl::string_view(move.ToUci().c_str()));
                ASSERT_TRUE(parsed.has_value()) << move.ToUci().c_str();
                EXPECT_TRUE(*parsed == move) << move.ToUci().c_str();
            }
        }
    }

    TEST(MoveGeneratorTest, NoGeneratedMoveIsNull) {
        for (const Move move : MoveGenerator::GenerateLegal(PositionFromFen(kKiwipeteFen))) {
            EXPECT_FALSE(move.IsNull());
        }
    }

}  // namespace BECore::Chess::Tests
