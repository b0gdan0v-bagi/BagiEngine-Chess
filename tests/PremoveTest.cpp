#include "ChessTestUtil.h"

#include <Chess/Game/Premove.h>

/// The premove board and its targets: the geometry of a premove (own pieces block, the opponent's never, no check, no pins) and the board after queued premoves. Pure, nothing to start
namespace BECore::Chess::Tests {

    namespace {

        constexpr StringView kSparseFen = "4k3/8/8/8/8/8/8/R3K2R w - - 0 1";

        std::string NameOf(Square square) {
            return std::string{static_cast<char>('a' + square.file), static_cast<char>('1' + square.rank)};
        }

        /// The targets of a piece as a sorted list of names, so a test reads "e3 e4"
        std::vector<std::string> TargetsOf(const PremoveBoard& board, StringView from, PieceColor color) {
            std::vector<std::string> names;
            for (const Square square : board.Targets(Sq(from), color)) {
                names.push_back(NameOf(square));
            }
            std::sort(names.begin(), names.end());
            return names;
        }

        std::vector<std::string> Names(std::initializer_list<const char*> list) {
            std::vector<std::string> names(list.begin(), list.end());
            std::sort(names.begin(), names.end());
            return names;
        }

        Premove Pm(StringView from, StringView to) {
            return Premove{Sq(from), Sq(to)};
        }

        PremoveBoard BoardOf(StringView fen) {
            return PremoveBoard(PositionFromFen(fen));
        }

    }  // namespace

    TEST(PremoveTest, PremovesCompareByTheirSquares) {
        EXPECT_EQ(Pm("e2", "e4"), Pm("e2", "e4"));
        EXPECT_FALSE(Pm("e2", "e4") == Pm("e2", "e3"));
        EXPECT_FALSE(Pm("e2", "e4") == Pm("d2", "e4"));
    }

    TEST(PremoveTest, KnightJumpsOverOwnPiecesAndStopsOnlyOnFreeOrEnemySquares) {
        const PremoveBoard board = BoardOf(Position::kStartFen);
        EXPECT_EQ(TargetsOf(board, "g1", PieceColor::White), Names({"f3", "h3"}));
        EXPECT_EQ(TargetsOf(board, "b8", PieceColor::Black), Names({"a6", "c6"}));
    }

    TEST(PremoveTest, KnightMayTargetAnEnemyPiece) {
        const PremoveBoard board = BoardOf("4k3/8/3p4/8/4N3/8/8/4K3 w - - 0 1");
        EXPECT_EQ(TargetsOf(board, "e4", PieceColor::White), Names({"c3", "c5", "d2", "d6", "f2", "f6", "g3", "g5"}));
    }

    TEST(PremoveTest, RookAndBishopAreBlockedByAnOwnPieceButNotByAnEnemy) {
        const PremoveBoard start = BoardOf(Position::kStartFen);
        EXPECT_TRUE(TargetsOf(start, "a1", PieceColor::White).empty());
        EXPECT_TRUE(TargetsOf(start, "c1", PieceColor::White).empty());

        // The black pawn on a4 and the black bishop on e5 do not block the white pieces behind them
        const PremoveBoard board = BoardOf("4k3/8/8/4b3/p7/8/8/R1B1K3 w - - 0 1");
        const auto rook = TargetsOf(board, "a1", PieceColor::White);
        EXPECT_EQ(rook, Names({"a2", "a3", "a4", "a5", "a6", "a7", "a8", "b1"}));
        const auto bishop = TargetsOf(board, "c1", PieceColor::White);
        EXPECT_EQ(bishop, Names({"b2", "a3", "d2", "e3", "f4", "g5", "h6"}));
    }

    TEST(PremoveTest, RookStopsBeforeAnOwnPieceOnTheRay) {
        const PremoveBoard board = BoardOf(kSparseFen);
        EXPECT_EQ(TargetsOf(board, "a1", PieceColor::White), Names({"a2", "a3", "a4", "a5", "a6", "a7", "a8", "b1", "c1", "d1"}));
        EXPECT_EQ(TargetsOf(board, "h1", PieceColor::White), Names({"h2", "h3", "h4", "h5", "h6", "h7", "h8", "g1", "f1"}));
    }

    TEST(PremoveTest, QueenAndKingOnAnOpenBoard) {
        const PremoveBoard board = BoardOf("4k3/8/8/8/3Q4/8/8/4K3 w - - 0 1");
        EXPECT_EQ(TargetsOf(board, "d4", PieceColor::White).size(), 27U);
        // The king of the board: castling is no premove, its two-file step is no target
        EXPECT_EQ(TargetsOf(board, "e1", PieceColor::White), Names({"d1", "d2", "e2", "f1", "f2"}));
        const PremoveBoard castle = BoardOf(kSparseFen);
        EXPECT_EQ(TargetsOf(castle, "e1", PieceColor::White), Names({"d1", "d2", "e2", "f1", "f2"}));
    }

    TEST(PremoveTest, AnOwnPieceIsNoTarget) {
        const PremoveBoard board = BoardOf(Position::kStartFen);
        for (const char* from : {"e1", "d1", "g1", "a1", "f1"}) {
            for (const Square target : board.Targets(Sq(from), PieceColor::White)) {
                EXPECT_FALSE(board.IsOwnPiece(target, PieceColor::White)) << from << " -> " << NameOf(target);
            }
        }
        EXPECT_TRUE(TargetsOf(board, "e1", PieceColor::White).empty());
    }

    TEST(PremoveTest, PawnStepsOneAndTwoFromItsStartRank) {
        const PremoveBoard board = BoardOf(Position::kStartFen);
        EXPECT_EQ(TargetsOf(board, "e2", PieceColor::White), Names({"d3", "e3", "e4", "f3"}));
        EXPECT_EQ(TargetsOf(board, "a2", PieceColor::White), Names({"a3", "a4", "b3"}));
        EXPECT_EQ(TargetsOf(board, "h7", PieceColor::Black), Names({"h6", "h5", "g6"}));
    }

    TEST(PremoveTest, PawnOffItsStartRankStepsOnce) {
        const PremoveBoard board = BoardOf("4k3/8/8/8/4P3/8/8/4K3 w - - 0 1");
        EXPECT_EQ(TargetsOf(board, "e4", PieceColor::White), Names({"d5", "e5", "f5"}));
    }

    TEST(PremoveTest, PawnDoubleStepNeedsBothSquaresFreeOfOwnPieces) {
        // An own knight on e3 stops both steps; an own bishop on e4 stops only the second
        const PremoveBoard blocked = BoardOf("4k3/8/8/8/8/4N3/4P3/4K3 w - - 0 1");
        EXPECT_EQ(TargetsOf(blocked, "e2", PieceColor::White), Names({"d3", "f3"}));
        const PremoveBoard far = BoardOf("4k3/8/8/8/4B3/8/4P3/4K3 w - - 0 1");
        EXPECT_EQ(TargetsOf(far, "e2", PieceColor::White), Names({"d3", "e3", "f3"}));
    }

    TEST(PremoveTest, PawnStepOntoAnEnemyPieceIsATargetBecauseItMayMoveAway) {
        const PremoveBoard board = BoardOf("4k3/8/8/8/8/4n3/4P3/4K3 w - - 0 1");
        EXPECT_EQ(TargetsOf(board, "e2", PieceColor::White), Names({"d3", "e3", "e4", "f3"}));
    }

    TEST(PremoveTest, PawnDiagonalsAreTargetsWithOrWithoutAPieceButNotOnAnOwnPiece) {
        const PremoveBoard board = BoardOf("4k3/8/8/3N4/4P3/8/8/4K3 w - - 0 1");
        // d5 holds an own knight, f5 is empty: still a target
        EXPECT_EQ(TargetsOf(board, "e4", PieceColor::White), Names({"e5", "f5"}));
    }

    TEST(PremoveTest, BlackPawnGoesDown) {
        const PremoveBoard board = BoardOf("4k3/4p3/8/8/8/8/8/4K3 b - - 0 1");
        EXPECT_EQ(TargetsOf(board, "e7", PieceColor::Black), Names({"d6", "e6", "e5", "f6"}));
    }

    TEST(PremoveTest, AnEmptyCellAndAnOpponentsPieceGiveNoTargets) {
        const PremoveBoard board = BoardOf(Position::kStartFen);
        EXPECT_TRUE(TargetsOf(board, "e4", PieceColor::White).empty());
        EXPECT_TRUE(TargetsOf(board, "e7", PieceColor::White).empty());
        EXPECT_TRUE(TargetsOf(board, "e2", PieceColor::Black).empty());
        EXPECT_TRUE(board.Targets(Square{-1, 3}, PieceColor::White).empty());
        EXPECT_TRUE(board.Targets(Square{8, 0}, PieceColor::White).empty());
    }

    TEST(PremoveTest, CheckAndPinsAreIgnored) {
        // The white rook on e2 is pinned to its king by the rook on e8 and still has its whole rank and file
        const PremoveBoard pinnedRook = BoardOf("4r1k1/8/8/8/8/8/4R3/4K3 w - - 0 1");
        EXPECT_EQ(TargetsOf(pinnedRook, "e2", PieceColor::White), Names({"a2", "b2", "c2", "d2", "f2", "g2", "h2", "e3", "e4", "e5", "e6", "e7", "e8"}));

        // A pinned knight has its eight jumps
        const PremoveBoard pinnedKnight = BoardOf("4r1k1/8/8/8/4N3/8/8/4K3 w - - 0 1");
        EXPECT_EQ(TargetsOf(pinnedKnight, "e4", PieceColor::White).size(), 8U);

        // The king is in check: a piece that does not answer the check and a king step onto an attacked square are targets;
        // the legality is for the turn of the premove
        const PremoveBoard check = BoardOf("4r1k1/8/8/8/8/8/R7/4K3 w - - 0 1");
        EXPECT_EQ(TargetsOf(check, "a2", PieceColor::White).size(), 14U);
        EXPECT_EQ(TargetsOf(check, "e1", PieceColor::White), Names({"d1", "d2", "e2", "f1", "f2"}));
    }

    TEST(PremoveTest, ApplyLiftsThePieceAndEmptiesTheCell) {
        PremoveBoard board = BoardOf(Position::kStartFen);
        board.Apply(Pm("e2", "e4"));
        EXPECT_TRUE(board.GetPieceAt(Sq("e2")).IsEmpty());
        EXPECT_EQ(board.GetPieceAt(Sq("e4")).type, PieceType::Pawn);
        EXPECT_FALSE(board.IsOwnPiece(Sq("e2"), PieceColor::White));
        EXPECT_TRUE(board.IsOwnPiece(Sq("e4"), PieceColor::White));
    }

    TEST(PremoveTest, TheSecondPremoveMovesThePieceTheFirstOneMoved) {
        PremoveBoard board = BoardOf(Position::kStartFen);
        EXPECT_TRUE(TargetsOf(board, "e3", PieceColor::White).empty());
        board.Apply(Pm("e2", "e4"));

        EXPECT_EQ(TargetsOf(board, "e4", PieceColor::White), Names({"d5", "e5", "f5"}));
        // The bishop's diagonal is open now
        EXPECT_EQ(TargetsOf(board, "f1", PieceColor::White), Names({"a6", "b5", "c4", "d3", "e2"}));

        board.Apply(Pm("e4", "e5"));
        EXPECT_TRUE(board.GetPieceAt(Sq("e4")).IsEmpty());
        EXPECT_EQ(board.GetPieceAt(Sq("e5")).type, PieceType::Pawn);
    }

    TEST(PremoveTest, ACapturingPremoveRemovesTheEnemyPiece) {
        PremoveBoard board = BoardOf("4k3/8/8/3p4/8/8/8/3RK3 w - - 0 1");
        board.Apply(Pm("d1", "d5"));
        EXPECT_EQ(board.GetPieceAt(Sq("d5")).type, PieceType::Rook);
        EXPECT_EQ(board.GetPieceAt(Sq("d5")).color, PieceColor::White);
        EXPECT_FALSE(board.IsOwnPiece(Sq("d5"), PieceColor::Black));

        // The black pawn is gone, so nothing of Black's blocks or is hit there any more: the rook goes on to d8
        EXPECT_EQ(TargetsOf(board, "d5", PieceColor::White).size(), 14U);
    }

    TEST(PremoveTest, APawnThatReachesTheLastRankBecomesAQueen) {
        PremoveBoard white = BoardOf("4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
        white.Apply(Pm("a7", "a8"));
        EXPECT_EQ(white.GetPieceAt(Sq("a8")).type, PieceType::Queen);
        EXPECT_EQ(white.GetPieceAt(Sq("a8")).color, PieceColor::White);
        EXPECT_FALSE(TargetsOf(white, "a8", PieceColor::White).empty());

        PremoveBoard black = BoardOf("4k3/8/8/8/8/8/p7/4K3 b - - 0 1");
        black.Apply(Pm("a2", "b1"));
        EXPECT_EQ(black.GetPieceAt(Sq("b1")).type, PieceType::Queen);
        EXPECT_EQ(black.GetPieceAt(Sq("b1")).color, PieceColor::Black);
    }

    TEST(PremoveTest, ApplyOfAnEmptyCellOrOutsideTheBoardChangesNothing) {
        PremoveBoard board = BoardOf(Position::kStartFen);
        board.Apply(Pm("e4", "e5"));
        board.Apply(Premove{Sq("e2"), Square{9, 9}});
        board.Apply(Premove{Square{-1, 0}, Sq("e4")});
        EXPECT_EQ(board.GetPieceAt(Sq("e2")).type, PieceType::Pawn);
        EXPECT_TRUE(board.GetPieceAt(Sq("e4")).IsEmpty());
        EXPECT_TRUE(board.GetPieceAt(Sq("e5")).IsEmpty());
    }

    TEST(PremoveTest, TheBoardStartsFromThePositionNotFromTheRules) {
        // Black to move in the position, and White's premoves are still those of White
        const PremoveBoard board = BoardOf("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
        EXPECT_EQ(TargetsOf(board, "e4", PieceColor::White), Names({"d5", "e5", "f5"}));
        EXPECT_EQ(TargetsOf(board, "e7", PieceColor::Black), Names({"d6", "e6", "e5", "f6"}));
    }

}  // namespace BECore::Chess::Tests
