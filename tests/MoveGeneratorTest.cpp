#include <Chess/Game/ChessBoard.h>
#include <Chess/Game/ChessTypes.h>
#include <Chess/Game/MoveGenerator.h>
#include <Common/Printers.h>
#include <EASTL/algorithm.h>

/**
 * Pseudo-legal move generation coverage for ChessBoard + MoveGenerator.
 *
 * MVP scope, same restrictions as the gameplay layer: no castling, no
 * en-passant, no pawn promotion, no check/checkmate. We only verify that
 * each piece kind reaches the squares allowed by basic movement rules and
 * is correctly blocked by friendly / enemy pieces.
 */
namespace BECore::Chess::Tests {

    // Compile-time checks: a regression breaks the build.
    static_assert(Square::IsInside(0, 0));
    static_assert(Square::IsInside(7, 7));
    static_assert(!Square::IsInside(-1, 0));
    static_assert(!Square::IsInside(0, 8));
    static_assert(Square{3, 4}.Index() == 4 * 8 + 3);
    static_assert(Square::FromIndex(35) == (Square{3, 4}));

    namespace {

        /// True if @p target appears anywhere in @p moves.
        bool Contains(const eastl::vector<Square>& moves, Square target) {
            return eastl::find(moves.begin(), moves.end(), target) != moves.end();
        }

    }  // namespace

    TEST(MoveGeneratorTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts above are the check";
    }

    TEST(MoveGeneratorTest, PawnFromStart) {
        ChessBoard board;
        board.ResetToStart();
        // e2 = file 4, rank 1.
        const auto moves = MoveGenerator::Generate(board, Square{4, 1});
        EXPECT_EQ(moves.size(), 2u);
        EXPECT_TRUE(Contains(moves, Square{4, 2}));
        EXPECT_TRUE(Contains(moves, Square{4, 3}));
    }

    TEST(MoveGeneratorTest, PawnBlocked) {
        ChessBoard board;
        board.Set(Square{4, 1}, Piece{PieceType::Pawn, PieceColor::White});
        board.Set(Square{4, 2}, Piece{PieceType::Pawn, PieceColor::Black});
        const auto moves = MoveGenerator::Generate(board, Square{4, 1});
        EXPECT_TRUE(moves.empty());
    }

    TEST(MoveGeneratorTest, PawnCapture) {
        ChessBoard board;
        board.Set(Square{4, 3}, Piece{PieceType::Pawn, PieceColor::White});
        board.Set(Square{3, 4}, Piece{PieceType::Pawn, PieceColor::Black});
        board.Set(Square{5, 4}, Piece{PieceType::Pawn, PieceColor::Black});
        const auto moves = MoveGenerator::Generate(board, Square{4, 3});
        EXPECT_TRUE(Contains(moves, Square{4, 4})) << "forward push";
        EXPECT_TRUE(Contains(moves, Square{3, 4})) << "diagonal capture left";
        EXPECT_TRUE(Contains(moves, Square{5, 4})) << "diagonal capture right";
        EXPECT_EQ(moves.size(), 3u) << "not 4: not on starting rank, no double push";
    }

    TEST(MoveGeneratorTest, KnightOpening) {
        ChessBoard board;
        board.ResetToStart();
        // b1 knight = file 1, rank 0. Should reach a3 and c3 only (d2 blocked by pawn).
        const auto moves = MoveGenerator::Generate(board, Square{1, 0});
        EXPECT_EQ(moves.size(), 2u);
        EXPECT_TRUE(Contains(moves, Square{0, 2}));
        EXPECT_TRUE(Contains(moves, Square{2, 2}));
    }

    TEST(MoveGeneratorTest, RookOpenAndBlocked) {
        ChessBoard board;
        board.Set(Square{4, 4}, Piece{PieceType::Rook, PieceColor::White});
        // Lone rook in the middle of an empty board reaches 14 squares (7 + 7).
        const auto moves = MoveGenerator::Generate(board, Square{4, 4});
        EXPECT_EQ(moves.size(), 14u);

        // Block one direction with a friendly piece: that ray is fully cut.
        board.Set(Square{4, 6}, Piece{PieceType::Pawn, PieceColor::White});
        const auto blocked = MoveGenerator::Generate(board, Square{4, 4});
        EXPECT_FALSE(Contains(blocked, Square{4, 6}));
        EXPECT_FALSE(Contains(blocked, Square{4, 7}));
        EXPECT_TRUE(Contains(blocked, Square{4, 5}));

        // Replace with an enemy piece: rook can capture it but not pass through.
        board.Set(Square{4, 6}, Piece{PieceType::Pawn, PieceColor::Black});
        const auto enemyBlock = MoveGenerator::Generate(board, Square{4, 4});
        EXPECT_TRUE(Contains(enemyBlock, Square{4, 6}));
        EXPECT_FALSE(Contains(enemyBlock, Square{4, 7}));
    }

    TEST(MoveGeneratorTest, BishopDiagonals) {
        ChessBoard board;
        board.Set(Square{3, 3}, Piece{PieceType::Bishop, PieceColor::White});
        const auto moves = MoveGenerator::Generate(board, Square{3, 3});
        // From d4 a lone bishop reaches 13 squares.
        EXPECT_EQ(moves.size(), 13u);
        EXPECT_TRUE(Contains(moves, Square{0, 0}));
        EXPECT_TRUE(Contains(moves, Square{6, 0}));
        EXPECT_TRUE(Contains(moves, Square{0, 6}));
        EXPECT_TRUE(Contains(moves, Square{7, 7}));
        // No straight moves.
        EXPECT_FALSE(Contains(moves, Square{3, 5}));
        EXPECT_FALSE(Contains(moves, Square{5, 3}));
    }

    TEST(MoveGeneratorTest, QueenCombination) {
        ChessBoard board;
        board.Set(Square{3, 3}, Piece{PieceType::Queen, PieceColor::White});
        const auto moves = MoveGenerator::Generate(board, Square{3, 3});
        // Lone queen on d4 reaches 27 squares (rook 14 + bishop 13).
        EXPECT_EQ(moves.size(), 27u);
    }

    TEST(MoveGeneratorTest, KingNeighbors) {
        ChessBoard board;
        board.Set(Square{4, 4}, Piece{PieceType::King, PieceColor::White});
        const auto moves = MoveGenerator::Generate(board, Square{4, 4});
        EXPECT_EQ(moves.size(), 8u);

        // Corner: king reaches 3 squares.
        ChessBoard corner;
        corner.Set(Square{0, 0}, Piece{PieceType::King, PieceColor::White});
        const auto cornerMoves = MoveGenerator::Generate(corner, Square{0, 0});
        EXPECT_EQ(cornerMoves.size(), 3u);
        EXPECT_TRUE(Contains(cornerMoves, Square{1, 0}));
        EXPECT_TRUE(Contains(cornerMoves, Square{0, 1}));
        EXPECT_TRUE(Contains(cornerMoves, Square{1, 1}));
    }

    TEST(MoveGeneratorTest, StartingPositionMoveCounts) {
        ChessBoard board;
        board.ResetToStart();
        // Sum of all White pseudo-legal moves at the opening position is 20.
        size_t total = 0;
        for (int rank = 0; rank < 2; ++rank) {
            for (int file = 0; file < 8; ++file) {
                total += MoveGenerator::Generate(board, Square{static_cast<int8_t>(file), static_cast<int8_t>(rank)}).size();
            }
        }
        EXPECT_EQ(total, 20u);
    }

    TEST(MoveGeneratorTest, CaptureRemovesPiece) {
        ChessBoard board;
        board.Set(Square{4, 3}, Piece{PieceType::Pawn, PieceColor::White});
        board.Set(Square{3, 4}, Piece{PieceType::Pawn, PieceColor::Black});
        ASSERT_FALSE(board.Get(Square{3, 4}).IsEmpty());
        board.Move(Square{4, 3}, Square{3, 4});
        EXPECT_TRUE(board.Get(Square{4, 3}).IsEmpty());
        const Piece captured = board.Get(Square{3, 4});
        EXPECT_TRUE(captured.type == PieceType::Pawn);
        EXPECT_TRUE(captured.color == PieceColor::White);
    }

}  // namespace BECore::Chess::Tests
