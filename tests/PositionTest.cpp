#include "ChessTestUtil.h"

namespace BECore::Chess::Tests {

    namespace {

        bool IsRejectedWith(StringView fen, FenError expected) {
            const auto position = Position::FromFen(fen);
            return !position.has_value() && position.error() == expected;
        }

    }  // namespace

    // ---- FEN ----

    TEST(PositionTest, StartPositionRoundTripsThroughFen) {
        const Position start = Position::Start();
        EXPECT_EQ(start.ToFen(), eastl::string(Position::kStartFen));
    }

    TEST(PositionTest, StandardTestPositionsRoundTripThroughFen) {
        for (const auto fen : {kKiwipeteFen, kPosition3Fen, kPosition4Fen, kPosition5Fen, kPosition6Fen}) {
            EXPECT_EQ(PositionFromFen(fen).ToFen(), eastl::string(fen));
        }
    }

    TEST(PositionTest, ClocksDefaultWhenLeftOut) {
        const Position position = PositionFromFen("4k3/8/8/8/8/8/8/4K3 w - -");
        EXPECT_EQ(position.HalfmoveClock(), 0);
        EXPECT_EQ(position.FullmoveNumber(), 1);
        EXPECT_EQ(position.ToFen(), "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    }

    TEST(PositionTest, ClocksAreRead) {
        const Position position = PositionFromFen("4k3/8/8/8/8/8/8/4K3 b - - 37 52");
        EXPECT_EQ(position.HalfmoveClock(), 37);
        EXPECT_EQ(position.FullmoveNumber(), 52);
        EXPECT_TRUE(position.SideToMove() == PieceColor::Black);
    }

    TEST(PositionTest, FenRejectsWrongFieldCount) {
        EXPECT_TRUE(IsRejectedWith("", FenError::WrongFieldCount));
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8/4K3 w -", FenError::WrongFieldCount));
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8/4K3 w - - 0 1 extra", FenError::WrongFieldCount));
    }

    TEST(PositionTest, FenRejectsBadPlacement) {
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8 w - - 0 1", FenError::BadPiecePlacement)) << "seven ranks";
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8/4K4 w - - 0 1", FenError::BadPiecePlacement)) << "nine files";
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8/4K2 w - - 0 1", FenError::BadPiecePlacement)) << "seven files";
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8/4X3 w - - 0 1", FenError::BadPiecePlacement)) << "unknown piece letter";
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8/4K3/8 w - - 0 1", FenError::BadPiecePlacement)) << "nine ranks";
    }

    TEST(PositionTest, FenRejectsMissingOrExtraKings) {
        EXPECT_TRUE(IsRejectedWith("8/8/8/8/8/8/8/4K3 w - - 0 1", FenError::BadKingCount));
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8/3KK3 w - - 0 1", FenError::BadKingCount));
    }

    TEST(PositionTest, FenRejectsPawnsOnTheBackRanks) {
        EXPECT_TRUE(IsRejectedWith("4k2P/8/8/8/8/8/8/4K3 w - - 0 1", FenError::PawnOnBackRank));
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8/p3K3 w - - 0 1", FenError::PawnOnBackRank));
    }

    TEST(PositionTest, FenRejectsMoreThanSixteenPiecesOfOneSide) {
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/N7/PPPPPPPP/NNNNKNNN w - - 0 1", FenError::TooManyPieces)) << "seventeen";
        EXPECT_TRUE(Position::FromFen("4k3/8/8/8/8/8/PPPPPPPP/NNNNKNNN w - - 0 1").has_value()) << "sixteen is the most";
    }

    TEST(PositionTest, FenRejectsBadSideToMove) {
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8/4K3 x - - 0 1", FenError::BadSideToMove));
    }

    TEST(PositionTest, FenRejectsCastlingWithoutKingAndRookInPlace) {
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8/4K3 w K - 0 1", FenError::BadCastling)) << "no rook";
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8/R3K2R w Kk - 0 1", FenError::BadCastling)) << "black has no rook";
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8/R3K2R w KK - 0 1", FenError::BadCastling)) << "repeated right";
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8/R3K2R w X - 0 1", FenError::BadCastling)) << "unknown letter";
    }

    TEST(PositionTest, FenRejectsBadEnPassantSquare) {
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/3pP3/8/8/8/4K3 w - d3 0 2", FenError::BadEnPassant)) << "wrong rank for the side to move";
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/4P3/8/8/8/4K3 w - d6 0 2", FenError::BadEnPassant)) << "no pawn that could have pushed";
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/3pP3/8/8/8/4K3 w - z6 0 2", FenError::BadEnPassant));
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/3pP3/8/8/8/4K3 w - d 0 2", FenError::BadEnPassant));
    }

    TEST(PositionTest, FenRejectsBadClocks) {
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8/4K3 w - - x 1", FenError::BadClock));
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8/4K3 w - - 0 0", FenError::BadClock));
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/8/4K3 w - - -1 1", FenError::BadClock));
    }

    TEST(PositionTest, FenRejectsAPositionWhereTheSideNotToMoveIsInCheck) {
        EXPECT_TRUE(IsRejectedWith("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1", FenError::OpponentInCheck));
    }

    TEST(PositionTest, EnPassantSquareIsKeptOnlyWhenACaptureExists) {
        const Position capturable = PositionFromFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2");
        EXPECT_EQ(capturable.EnPassantSquare(), SqIdx("d6"));
        EXPECT_EQ(capturable.ToFen(), "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2");

        const Position nobody = PositionFromFen("4k3/8/8/3p4/8/8/8/4K3 w - d6 0 2");
        EXPECT_EQ(nobody.EnPassantSquare(), kNoSquare);
        EXPECT_EQ(nobody.ToFen(), "4k3/8/8/3p4/8/8/8/4K3 w - - 0 2");
    }

    // ---- Queries ----

    TEST(PositionTest, StartPositionPieces) {
        const Position start = Position::Start();
        EXPECT_EQ(PopCount(start.Pieces()), 32);
        EXPECT_EQ(PopCount(start.Pieces(PieceColor::White)), 16);
        EXPECT_EQ(PopCount(start.Pieces(PieceType::Pawn)), 16);
        EXPECT_EQ(start.KingSquare(PieceColor::White), SqIdx("e1"));
        EXPECT_EQ(start.KingSquare(PieceColor::Black), SqIdx("e8"));
        EXPECT_EQ(start.CastlingRights(), kAllCastlingRights);
        EXPECT_FALSE(start.IsInCheck());
        EXPECT_EQ(start.Pinned(), 0ULL);
    }

    TEST(PositionTest, PieceAtAgreesWithTheBitboards) {
        const Position position = PositionFromFen(kKiwipeteFen);
        for (int sq = 0; sq < kSquareCount; ++sq) {
            const Piece piece = position.PieceAt(sq);
            const bool occupied = (position.Pieces() & SquareBB(sq)) != 0;
            ASSERT_EQ(occupied, !piece.IsEmpty()) << "square " << sq;
            if (occupied) {
                ASSERT_NE(position.Pieces(piece.color, piece.type) & SquareBB(sq), 0ULL) << "square " << sq;
            }
        }
    }

    TEST(PositionTest, GetPieceAtIgnoresSquaresOffTheBoard) {
        const Position start = Position::Start();
        EXPECT_TRUE(start.GetPieceAt(Square{-1, 0}).IsEmpty());
        EXPECT_TRUE(start.GetPieceAt(Square{0, 8}).IsEmpty());
        EXPECT_TRUE(start.GetPieceAt(Square{4, 0}).type == PieceType::King);
    }

    TEST(PositionTest, AttackersToCollectsBothColors) {
        // d2 is attacked by the white bishop on c1, the white king on e1 and the black knight on f3, but not by the rook on e4.
        const Position position = PositionFromFen("7k/8/8/8/4R3/5n2/8/2B1K3 w - - 0 1");
        const Bitboard attackers = position.AttackersTo(SqIdx("d2"), position.Pieces());
        EXPECT_NE(attackers & SquareBB(SqIdx("f3")), 0ULL) << "knight";
        EXPECT_NE(attackers & SquareBB(SqIdx("c1")), 0ULL) << "bishop";
        EXPECT_NE(attackers & SquareBB(SqIdx("e1")), 0ULL) << "king";
        EXPECT_EQ(attackers & SquareBB(SqIdx("e4")), 0ULL) << "the rook does not reach d2";
    }

    TEST(PositionTest, CheckersAreTheEnemyPiecesAttackingTheKing) {
        const Position single = PositionFromFen("4k3/8/8/8/8/8/4r3/4K3 w - - 0 1");
        EXPECT_TRUE(single.IsInCheck());
        EXPECT_EQ(single.Checkers(), SquareBB(SqIdx("e2")));

        const Position doubleCheck = PositionFromFen("4k3/8/8/8/8/5n2/4r3/4K3 w - - 0 1");
        EXPECT_EQ(PopCount(doubleCheck.Checkers()), 2);
    }

    TEST(PositionTest, AnOnlyBlockerIsPinnedButTwoBlockersAreNot) {
        const Position pinned = PositionFromFen("4k3/4r3/8/8/8/8/4B3/4K3 w - - 0 1");
        EXPECT_EQ(pinned.Pinned(), SquareBB(SqIdx("e2")));

        const Position twoBlockers = PositionFromFen("4k3/4r3/8/8/4N3/8/4B3/4K3 w - - 0 1");
        EXPECT_EQ(twoBlockers.Pinned(), 0ULL);
    }

    TEST(PositionTest, PinnedIsComputedForTheSideToMove) {
        // The black bishop on e7 stands alone between the white rook and the black king.
        const Position blackToMove = PositionFromFen("4k3/4b3/8/8/8/8/4R3/4K3 b - - 0 1");
        EXPECT_EQ(blackToMove.Pinned(), SquareBB(SqIdx("e7")));

        const Position whiteToMove = PositionFromFen("4k3/4b3/8/8/8/8/4R3/4K3 w - - 0 1");
        EXPECT_EQ(whiteToMove.Pinned(), 0ULL);
    }

    // ---- DoMove / UndoMove ----

    TEST(PositionTest, QuietMoveUpdatesTheBoardAndTheClocks) {
        Position position = Position::Start();
        position.DoMove(Mv("g1f3"));
        EXPECT_EQ(position.ToFen(), "rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b KQkq - 1 1");
        position.DoMove(Mv("g8f6"));
        EXPECT_EQ(position.ToFen(), "rnbqkb1r/pppppppp/5n2/8/8/5N2/PPPPPPPP/RNBQKB1R w KQkq - 2 2");
    }

    TEST(PositionTest, PawnMoveResetsTheHalfmoveClock) {
        Position position = PositionFromFen("4k3/8/8/8/8/8/4P3/4K3 w - - 9 40");
        position.DoMove(Mv("e2e3"));
        EXPECT_EQ(position.HalfmoveClock(), 0);
    }

    TEST(PositionTest, DoubleStepSetsEnPassantOnlyWhenCapturable) {
        Position lonely = PositionFromFen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
        lonely.DoMove(Mv("e2e4"));
        EXPECT_EQ(lonely.EnPassantSquare(), kNoSquare);
        EXPECT_EQ(lonely.ToFen(), "4k3/8/8/8/4P3/8/8/4K3 b - - 0 1");

        Position watched = PositionFromFen("4k3/8/8/8/3p4/8/4P3/4K3 w - - 0 1");
        watched.DoMove(Mv("e2e4"));
        EXPECT_EQ(watched.EnPassantSquare(), SqIdx("e3"));
        EXPECT_EQ(watched.ToFen(), "4k3/8/8/8/3pP3/8/8/4K3 b - e3 0 1");
    }

    TEST(PositionTest, EnPassantCaptureRemovesThePassedPawn) {
        Position position = PositionFromFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2");
        position.DoMove(EnPassantMove("e5d6"));
        EXPECT_EQ(position.ToFen(), "4k3/8/3P4/8/8/8/8/4K3 b - - 0 2");
        EXPECT_TRUE(position.PieceAt(SqIdx("d5")).IsEmpty());
    }

    TEST(PositionTest, CaptureResetsTheClockAndRemovesThePiece) {
        Position position = PositionFromFen("4k3/8/8/3p4/8/8/8/3RK3 w - - 12 30");
        position.DoMove(Mv("d1d5"));
        EXPECT_EQ(position.HalfmoveClock(), 0);
        EXPECT_EQ(position.ToFen(), "4k3/8/8/3R4/8/8/8/4K3 b - - 0 30");
    }

    TEST(PositionTest, CastlingMovesTheRookAndDropsTheRights) {
        const StringView fen = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
        Position position = PositionFromFen(fen);

        position.DoMove(CastlingMove("e1g1"));
        EXPECT_EQ(position.ToFen(), "r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1");
        position.DoMove(CastlingMove("e8c8"));
        EXPECT_EQ(position.ToFen(), "2kr3r/8/8/8/8/8/8/R4RK1 w - - 2 2");

        position.UndoMove();
        position.UndoMove();
        EXPECT_EQ(position.ToFen(), eastl::string(fen));
    }

    TEST(PositionTest, MovingARookOrKingLosesOnlyTheRelatedRights) {
        Position position = PositionFromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        position.DoMove(Mv("h1h2"));
        EXPECT_EQ(position.CastlingRights(), kWhiteQueenSide | kBlackKingSide | kBlackQueenSide);
        position.DoMove(Mv("e8d8"));
        EXPECT_EQ(position.CastlingRights(), kWhiteQueenSide);
    }

    TEST(PositionTest, CapturingARookAtHomeTakesItsRightAway) {
        Position position = PositionFromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        position.DoMove(Mv("a1a8"));
        EXPECT_EQ(position.CastlingRights(), kWhiteKingSide | kBlackKingSide);
    }

    TEST(PositionTest, PromotionReplacesThePawnAndUndoRestoresIt) {
        const StringView fen = "4k3/P7/8/8/8/8/8/4K3 w - - 0 1";
        Position position = PositionFromFen(fen);
        position.DoMove(Mv("a7a8q"));
        EXPECT_EQ(position.ToFen(), "Q3k3/8/8/8/8/8/8/4K3 b - - 0 1");
        position.UndoMove();
        EXPECT_EQ(position.ToFen(), eastl::string(fen));
    }

    TEST(PositionTest, UnderPromotionAndPromotionByCapture) {
        Position position = PositionFromFen("1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1");
        position.DoMove(Mv("a7b8n"));
        EXPECT_EQ(position.ToFen(), "1N2k3/8/8/8/8/8/8/4K3 b - - 0 1");
        position.UndoMove();
        EXPECT_EQ(position.ToFen(), "1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1");
    }

    TEST(PositionTest, UndoRestoresEverythingAfterALongLine) {
        Position position = PositionFromFen(kKiwipeteFen);
        const eastl::string before = position.ToFen();
        const ZobristKey keyBefore = position.Key();
        const Bitboard checkersBefore = position.Checkers();

        position.DoMove(CastlingMove("e1g1"));
        position.DoMove(Mv("b4c3"));
        position.DoMove(Mv("d5e6"));
        position.DoMove(Mv("e8f8"));
        EXPECT_NE(position.ToFen(), before);

        for (int i = 0; i < 4; ++i) {
            position.UndoMove();
        }
        EXPECT_EQ(position.ToFen(), before);
        EXPECT_EQ(position.Key(), keyBefore);
        EXPECT_EQ(position.Checkers(), checkersBefore);
        EXPECT_FALSE(position.CanUndo());
    }

    TEST(PositionTest, UndoWithNothingToUndoChangesNothing) {
        Position position = Position::Start();
        position.UndoMove();
        EXPECT_EQ(position.ToFen(), eastl::string(Position::kStartFen));
    }

    TEST(PositionTest, IncrementalKeyEqualsTheKeyOfTheSameFen) {
        Position position = PositionFromFen(kKiwipeteFen);
        const Move line[] = {CastlingMove("e1g1"), Mv("b4c3"), Mv("d5e6"), Mv("e8f8"), Mv("e6f7")};
        for (const Move move : line) {
            position.DoMove(move);
            EXPECT_EQ(position.Key(), PositionFromFen(position.ToFen()).Key()) << "after " << move.ToUci().c_str();
        }
    }

    TEST(PositionTest, KeyDependsOnSideToMoveCastlingAndEnPassant) {
        const Position white = PositionFromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        const Position black = PositionFromFen("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1");
        const Position fewerRights = PositionFromFen("r3k2r/8/8/8/8/8/8/R3K2R w Kkq - 0 1");
        EXPECT_NE(white.Key(), black.Key());
        EXPECT_NE(white.Key(), fewerRights.Key());

        const Position withEp = PositionFromFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2");
        const Position withoutEp = PositionFromFen("4k3/8/8/3pP3/8/8/8/4K3 w - - 0 2");
        EXPECT_NE(withEp.Key(), withoutEp.Key());
    }

    // ---- Repetition ----

    TEST(PositionTest, KnightShuffleRepeatsThePosition) {
        Position position = Position::Start();
        EXPECT_EQ(position.RepetitionCount(), 1);

        const Move shuffle[] = {Mv("g1f3"), Mv("g8f6"), Mv("f3g1"), Mv("f6g8")};
        for (const Move move : shuffle) {
            position.DoMove(move);
        }
        EXPECT_EQ(position.RepetitionCount(), 2);
        for (const Move move : shuffle) {
            position.DoMove(move);
        }
        EXPECT_EQ(position.RepetitionCount(), 3);
    }

    TEST(PositionTest, IrreversibleMoveEndsTheRepetitionWindow) {
        Position position = Position::Start();
        for (const Move move : {Mv("g1f3"), Mv("g8f6"), Mv("f3g1"), Mv("f6g8")}) {
            position.DoMove(move);
        }
        position.DoMove(Mv("e2e4"));
        position.DoMove(Mv("e7e5"));
        EXPECT_EQ(position.RepetitionCount(), 1);
    }

    // ---- Insufficient material ----

    TEST(PositionTest, InsufficientMaterial) {
        EXPECT_TRUE(PositionFromFen("4k3/8/8/8/8/8/8/4K3 w - - 0 1").IsInsufficientMaterial()) << "K v K";
        EXPECT_TRUE(PositionFromFen("4k3/8/8/8/8/8/8/3NK3 w - - 0 1").IsInsufficientMaterial()) << "K+N v K";
        EXPECT_TRUE(PositionFromFen("4k3/8/8/8/8/8/8/2B1K3 w - - 0 1").IsInsufficientMaterial()) << "K+B v K";
        EXPECT_TRUE(PositionFromFen("4kb2/8/8/8/8/8/8/2B1K3 w - - 0 1").IsInsufficientMaterial()) << "bishops on the same color";
    }

    TEST(PositionTest, MaterialThatCanStillMate) {
        EXPECT_FALSE(PositionFromFen("2b1k3/8/8/8/8/8/8/2B1K3 w - - 0 1").IsInsufficientMaterial()) << "bishops on opposite colors";
        EXPECT_FALSE(PositionFromFen("4k3/8/8/8/8/8/8/2NNK3 w - - 0 1").IsInsufficientMaterial()) << "two knights";
        EXPECT_FALSE(PositionFromFen("4kn2/8/8/8/8/8/8/3NK3 w - - 0 1").IsInsufficientMaterial()) << "knight each";
        EXPECT_FALSE(PositionFromFen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1").IsInsufficientMaterial()) << "a pawn can promote";
        EXPECT_FALSE(PositionFromFen("4k3/8/8/8/8/8/8/3RK3 w - - 0 1").IsInsufficientMaterial()) << "a rook";
        EXPECT_FALSE(PositionFromFen("4k3/8/8/8/8/8/8/3QK3 w - - 0 1").IsInsufficientMaterial()) << "a queen";
        EXPECT_FALSE(PositionFromFen("4k3/8/8/8/8/8/8/2NBK3 w - - 0 1").IsInsufficientMaterial()) << "knight and bishop";
    }

    // ---- IsLegal ----

    TEST(PositionTest, KingMayNotStepOntoAnAttackedSquare) {
        const Position position = PositionFromFen("4k3/8/8/8/8/8/4r3/3K4 w - - 0 1");
        EXPECT_FALSE(position.IsLegal(Mv("d1d2"))) << "d2 is on the rook's rank";
        EXPECT_TRUE(position.IsLegal(Mv("d1c1")));
    }

    TEST(PositionTest, KingMayNotStayOnTheLineOfTheSliderThatChecksIt) {
        // The king moving away along the checking rook's file is still attacked once it has left e1.
        const Position position = PositionFromFen("4r2k/8/8/8/8/8/8/4K3 w - - 0 1");
        EXPECT_FALSE(position.IsLegal(Mv("e1e2")));
        EXPECT_TRUE(position.IsLegal(Mv("e1d1")));
    }

    TEST(PositionTest, PinnedPieceMovesOnlyAlongThePin) {
        const Position position = PositionFromFen("4k3/4r3/8/8/8/8/4R3/4K3 w - - 0 1");
        EXPECT_TRUE(position.IsLegal(Mv("e2e5"))) << "stays on the e-file";
        EXPECT_TRUE(position.IsLegal(Mv("e2e7"))) << "captures the pinner";
        EXPECT_FALSE(position.IsLegal(Mv("e2d2"))) << "leaves the file";

        const Position diagonal = PositionFromFen("7k/8/8/8/8/2b5/3N4/4K3 w - - 0 1");
        EXPECT_FALSE(diagonal.IsLegal(Mv("d2b3"))) << "a pinned knight cannot move at all";
    }

    TEST(PositionTest, CastlingNeedsASafePath) {
        const Position safe = PositionFromFen("4k3/8/8/8/8/8/8/4K2R w K - 0 1");
        EXPECT_TRUE(safe.IsLegal(CastlingMove("e1g1")));

        const Position crossed = PositionFromFen("4kr2/8/8/8/8/8/8/4K2R w K - 0 1");
        EXPECT_FALSE(crossed.IsLegal(CastlingMove("e1g1"))) << "f1 is attacked by the rook on f8";

        const Position landing = PositionFromFen("4k1r1/8/8/8/8/8/8/4K2R w K - 0 1");
        EXPECT_FALSE(landing.IsLegal(CastlingMove("e1g1"))) << "g1 is attacked";
    }

    TEST(PositionTest, QueenSideCastlingMayPassAnAttackedRookSquare) {
        // Only the squares the king crosses matter: b1 may be attacked.
        const Position position = PositionFromFen("1r2k3/8/8/8/8/8/8/R3K3 w Q - 0 1");
        EXPECT_TRUE(position.IsLegal(CastlingMove("e1c1")));
    }

    TEST(PositionTest, EnPassantThatOpensTheRankToTheKingIsIllegal) {
        // Both pawns leave the fifth rank at once and the rook on h5 would see the king on a5.
        const Position position = PositionFromFen("8/8/8/KpP4r/8/8/8/7k w - b6 0 1");
        EXPECT_FALSE(position.IsLegal(EnPassantMove("c5b6")));
    }

    TEST(PositionTest, EnPassantThatIsHarmlessIsLegal) {
        const Position position = PositionFromFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2");
        EXPECT_TRUE(position.IsLegal(EnPassantMove("e5d6")));
    }

}  // namespace BECore::Chess::Tests
