#include <Chess/Game/PositionEditor.h>
#include <tests/ChessTestUtil.h>

namespace BECore::Chess::Tests {

    namespace {

        constexpr Piece kWhiteKing{PieceType::King, PieceColor::White};
        constexpr Piece kBlackKing{PieceType::King, PieceColor::Black};
        constexpr Piece kWhiteRook{PieceType::Rook, PieceColor::White};
        constexpr Piece kWhitePawn{PieceType::Pawn, PieceColor::White};
        constexpr Piece kBlackQueen{PieceType::Queen, PieceColor::Black};

        PositionEditor TwoKings() {
            PositionEditor editor;
            editor.Clear();
            EXPECT_TRUE(editor.Put(Sq("e1"), kWhiteKing).has_value());
            EXPECT_TRUE(editor.Put(Sq("e8"), kBlackKing).has_value());
            return editor;
        }

        // White pawns on the free squares of ranks 2 to 7 until White has kMaxPiecesPerSide pieces
        void FillWhite(PositionEditor& editor) {
            for (int index = 8; index < kSquareCount - 8 && editor.CountPieces(PieceColor::White) < kMaxPiecesPerSide; ++index) {
                if (editor.GetPieceAt(Square::FromIndex(index)).IsEmpty()) {
                    ASSERT_TRUE(editor.Put(Square::FromIndex(index), kWhitePawn).has_value());
                }
            }
        }

        // The fields a draft keeps: the pieces, the side and the rights; the en passant square and the clocks are not among them.
        String KeptFields(StringView fen) {
            size_t spaces = 0;
            for (size_t i = 0; i < fen.size(); ++i) {
                if (fen[i] == ' ' && ++spaces == 3) {
                    return String(fen.substr(0, i));
                }
            }
            return String(fen);
        }

        bool IsRefusedWith(const PositionEditor& editor, FenError expected) {
            const auto built = editor.Build();
            return !built.has_value() && built.error() == expected;
        }

    }  // namespace

    TEST(PositionEditorTest, TheEmptyBoardIsRefused) {
        PositionEditor editor;
        editor.Clear();
        EXPECT_TRUE(IsRefusedWith(editor, FenError::BadKingCount));
        EXPECT_EQ(editor.ToFen(), "8/8/8/8/8/8/8/8 w - - 0 1");
    }

    TEST(PositionEditorTest, TheStartPositionBuildsAndEqualsTheStart) {
        PositionEditor editor;
        editor.SetStart();
        const auto built = editor.Build();
        ASSERT_TRUE(built.has_value());
        EXPECT_EQ(built->ToFen(), Position::Start().ToFen());
        EXPECT_EQ(editor.ToFen(), String(Position::kStartFen));
    }

    TEST(PositionEditorTest, OneKingEachIsAccepted) {
        const PositionEditor editor = TwoKings();
        const auto built = editor.Build();
        ASSERT_TRUE(built.has_value());
        EXPECT_EQ(built->ToFen(), "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
    }

    TEST(PositionEditorTest, ThePositionsTheFenRefusesAreRefusedWithTheRightReason) {
        PositionEditor pawnOnBackRank = TwoKings();
        ASSERT_TRUE(pawnOnBackRank.Put(Sq("a1"), kWhitePawn).has_value());
        EXPECT_TRUE(IsRefusedWith(pawnOnBackRank, FenError::PawnOnBackRank));
        pawnOnBackRank.Remove(Sq("a1"));
        ASSERT_TRUE(pawnOnBackRank.Put(Sq("a8"), kWhitePawn).has_value());
        EXPECT_TRUE(IsRefusedWith(pawnOnBackRank, FenError::PawnOnBackRank));

        PositionEditor twoKings = TwoKings();
        ASSERT_TRUE(twoKings.Put(Sq("a3"), kWhiteKing).has_value());
        EXPECT_TRUE(IsRefusedWith(twoKings, FenError::BadKingCount));

        PositionEditor sideWaitingInCheck = TwoKings();
        ASSERT_TRUE(sideWaitingInCheck.Put(Sq("e4"), kWhiteRook).has_value());
        EXPECT_TRUE(IsRefusedWith(sideWaitingInCheck, FenError::OpponentInCheck));
        sideWaitingInCheck.SetSideToMove(PieceColor::Black);
        EXPECT_TRUE(sideWaitingInCheck.Build().has_value());
    }

    TEST(PositionEditorTest, TheSeventeenthPieceOfAColorIsRefused) {
        PositionEditor editor = TwoKings();
        FillWhite(editor);
        ASSERT_EQ(editor.CountPieces(PieceColor::White), kMaxPiecesPerSide);

        const uint64_t revision = editor.GetRevision();
        const auto refused = editor.Put(Sq("h8"), Piece{PieceType::Queen, PieceColor::White});
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error(), EditRefusal::TooManyPieces);
        EXPECT_EQ(editor.GetLastRefusal(), EditRefusal::TooManyPieces);
        EXPECT_EQ(editor.GetRevision(), revision);
        EXPECT_TRUE(editor.GetPieceAt(Sq("h8")).IsEmpty());

        // A piece that replaces one of its own color is not a 17th, and the other color is counted on its own
        EXPECT_TRUE(editor.Put(Sq("a2"), Piece{PieceType::Queen, PieceColor::White}).has_value());
        EXPECT_EQ(editor.CountPieces(PieceColor::White), kMaxPiecesPerSide);
        EXPECT_TRUE(editor.Put(Sq("h8"), kBlackQueen).has_value());
        EXPECT_FALSE(editor.GetLastRefusal().has_value());
    }

    TEST(PositionEditorTest, ASquareOffTheBoardIsRefused) {
        PositionEditor editor = TwoKings();
        const auto refused = editor.Put(Square{8, 0}, kWhitePawn);
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error(), EditRefusal::OffBoard);
        EXPECT_FALSE(editor.Remove(Square{-1, 3}));
        EXPECT_EQ(editor.GetLastRefusal(), EditRefusal::OffBoard);
        EXPECT_FALSE(editor.Apply(Square{0, 8}));
        EXPECT_EQ(editor.GetLastRefusal(), EditRefusal::OffBoard);
    }

    TEST(PositionEditorTest, ACastlingRightGoesOffWhenItsRookLeavesAndStaysOff) {
        PositionEditor editor;
        editor.SetStart();
        ASSERT_TRUE(editor.GetCastling(CastlingRight::WhiteKingSide));
        editor.Remove(Sq("h1"));
        EXPECT_FALSE(editor.GetCastling(CastlingRight::WhiteKingSide));
        EXPECT_TRUE(editor.GetCastling(CastlingRight::WhiteQueenSide));
        EXPECT_NE(editor.ToFen().find(" Qkq "), String::npos);

        ASSERT_TRUE(editor.Put(Sq("h1"), kWhiteRook).has_value());
        EXPECT_TRUE(editor.CanCastle(CastlingRight::WhiteKingSide));
        EXPECT_FALSE(editor.GetCastling(CastlingRight::WhiteKingSide)) << "a flag never comes back by itself";
        editor.SetCastling(CastlingRight::WhiteKingSide, true);
        EXPECT_TRUE(editor.GetCastling(CastlingRight::WhiteKingSide));
    }

    TEST(PositionEditorTest, ACastlingRightGoesOffWhenTheKingLeavesOrAWrongPieceArrives) {
        PositionEditor editor;
        editor.SetStart();
        editor.Remove(Sq("e8"));
        EXPECT_FALSE(editor.GetCastling(CastlingRight::BlackKingSide));
        EXPECT_FALSE(editor.GetCastling(CastlingRight::BlackQueenSide));
        EXPECT_TRUE(editor.GetCastling(CastlingRight::WhiteKingSide));

        ASSERT_TRUE(editor.Put(Sq("a1"), Piece{PieceType::Rook, PieceColor::Black}).has_value());
        EXPECT_FALSE(editor.GetCastling(CastlingRight::WhiteQueenSide));
        EXPECT_FALSE(editor.CanCastle(CastlingRight::WhiteQueenSide));
    }

    TEST(PositionEditorTest, SettingAnUnavailableRightIsIgnored) {
        PositionEditor editor = TwoKings();
        const uint64_t revision = editor.GetRevision();
        editor.SetCastling(CastlingRight::WhiteKingSide, true);
        EXPECT_FALSE(editor.GetCastling(CastlingRight::WhiteKingSide));
        EXPECT_EQ(editor.GetRevision(), revision);

        ASSERT_TRUE(editor.Put(Sq("h1"), kWhiteRook).has_value());
        editor.SetCastling(CastlingRight::WhiteKingSide, true);
        EXPECT_TRUE(editor.GetCastling(CastlingRight::WhiteKingSide));
        const auto built = editor.Build();
        ASSERT_TRUE(built.has_value());
        EXPECT_EQ(built->CastlingRights(), kWhiteKingSide);
        editor.SetCastling(CastlingRight::WhiteKingSide, false);
        EXPECT_EQ(editor.Build()->CastlingRights(), 0);
    }

    TEST(PositionEditorTest, LoadRoundTripsThePerftPositionsApartFromEnPassantAndClocks) {
        for (const StringView fen : {Position::kStartFen, kKiwipeteFen, kPosition3Fen, kPosition4Fen, kPosition5Fen, kPosition6Fen}) {
            const Position position = PositionFromFen(fen);
            PositionEditor editor;
            editor.Load(position);
            EXPECT_EQ(KeptFields(editor.ToFen()), KeptFields(position.ToFen())) << String(fen).c_str();
            const auto built = editor.Build();
            ASSERT_TRUE(built.has_value()) << String(fen).c_str();
            EXPECT_EQ(built->Key(), position.Key()) << String(fen).c_str();
        }
    }

    TEST(PositionEditorTest, LoadDropsTheEnPassantSquareAndTheClocks) {
        const Position position = PositionFromFen("rnbqkbnr/ppp1pppp/8/8/3pP3/5N2/PPPP1PPP/RNBQKB1R b KQkq e3 7 12");
        ASSERT_NE(position.EnPassantSquare(), kNoSquare);
        PositionEditor editor;
        editor.Load(position);
        EXPECT_EQ(editor.ToFen(), "rnbqkbnr/ppp1pppp/8/8/3pP3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 0 1");
    }

    TEST(PositionEditorTest, LoadFenOfARefusedTextLeavesTheDraft) {
        PositionEditor editor = TwoKings();
        editor.SetSideToMove(PieceColor::Black);
        const String before = editor.ToFen();
        const uint64_t revision = editor.GetRevision();

        for (const StringView text : {"", "not a fen", "8/8/8/8/8/8/8/8 w - - 0 1", "4k3/8/8/8/8/8/8/4K3 x - - 0 1"}) {
            EXPECT_FALSE(editor.LoadFen(text).has_value()) << String(text).c_str();
            EXPECT_EQ(editor.ToFen(), before);
            EXPECT_EQ(editor.GetRevision(), revision);
        }
        ASSERT_TRUE(editor.LoadFen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1").has_value());
        EXPECT_EQ(editor.GetPieceAt(Sq("e2")), kWhitePawn);
    }

    TEST(PositionEditorTest, ApplyTogglesThePickedPiece) {
        PositionEditor editor = TwoKings();
        editor.SetTool(EditorTool{PieceType::Queen, PieceColor::Black});
        EXPECT_TRUE(editor.Apply(Sq("d4")));
        EXPECT_EQ(editor.GetPieceAt(Sq("d4")), kBlackQueen);
        EXPECT_TRUE(editor.Apply(Sq("d4"))) << "the same piece again takes it off";
        EXPECT_TRUE(editor.GetPieceAt(Sq("d4")).IsEmpty());

        // Another piece replaces what stands there
        EXPECT_TRUE(editor.Apply(Sq("d4")));
        editor.SetTool(EditorTool{PieceType::Rook, PieceColor::White});
        EXPECT_TRUE(editor.Apply(Sq("d4")));
        EXPECT_EQ(editor.GetPieceAt(Sq("d4")), kWhiteRook);
    }

    TEST(PositionEditorTest, TheEraserTakesAPieceOffAndChangesNothingOnAnEmptySquare) {
        PositionEditor editor = TwoKings();
        editor.SetTool(EditorTool{PieceType::None, PieceColor::White});
        const uint64_t revision = editor.GetRevision();
        EXPECT_FALSE(editor.Apply(Sq("d4")));
        EXPECT_EQ(editor.GetRevision(), revision);
        EXPECT_TRUE(editor.Apply(Sq("e1")));
        EXPECT_TRUE(editor.GetPieceAt(Sq("e1")).IsEmpty());
    }

    TEST(PositionEditorTest, ARefusedApplyChangesNothing) {
        PositionEditor editor = TwoKings();
        FillWhite(editor);
        editor.SetTool(EditorTool{PieceType::Queen, PieceColor::White});
        const String before = editor.ToFen();
        EXPECT_FALSE(editor.Apply(Sq("h8")));
        EXPECT_EQ(editor.GetLastRefusal(), EditRefusal::TooManyPieces);
        EXPECT_EQ(editor.ToFen(), before);
    }

    TEST(PositionEditorTest, TheRevisionGrowsOnEveryChangeAndNeverWithoutOne) {
        PositionEditor editor = TwoKings();
        uint64_t revision = editor.GetRevision();
        const auto grows = [&] {
            const bool grew = editor.GetRevision() > revision;
            revision = editor.GetRevision();
            return grew;
        };
        editor.SetSideToMove(PieceColor::Black);
        EXPECT_TRUE(grows());
        editor.SetSideToMove(PieceColor::Black);
        EXPECT_FALSE(grows());
        editor.SetTool(EditorTool{PieceType::Knight, PieceColor::Black});
        EXPECT_TRUE(grows());
        editor.SetTool(EditorTool{PieceType::Knight, PieceColor::Black});
        EXPECT_FALSE(grows());
        EXPECT_TRUE(editor.Put(Sq("b1"), kWhitePawn).has_value());
        EXPECT_TRUE(grows());
        EXPECT_TRUE(editor.Put(Sq("b1"), kWhitePawn).has_value());
        EXPECT_FALSE(grows());
        editor.Clear();
        EXPECT_TRUE(grows());
    }

    TEST(PositionEditorTest, TheToolTableIsOneListOfIds) {
        const Span<const PieceType> pieces = EditorToolPieces();
        ASSERT_EQ(pieces.size(), 6U);
        EXPECT_EQ(pieces.front(), PieceType::King);
        for (const PieceType type : pieces) {
            EXPECT_FALSE(EditorToolId(type).empty());
            EXPECT_EQ(FindEditorTool(EditorToolId(type)), type);
        }
        EXPECT_EQ(EditorToolId(PieceType::None), "erase");
        EXPECT_EQ(FindEditorTool("erase"), PieceType::None);
        EXPECT_FALSE(FindEditorTool("Queen").has_value());
        EXPECT_FALSE(FindEditorTool("").has_value());
        EXPECT_FALSE(FindEditorTool("queens").has_value());
    }

    TEST(PositionEditorTest, TheEraserEqualsTheEraserOfAnyColor) {
        EXPECT_TRUE((EditorTool{PieceType::None, PieceColor::White} == EditorTool{PieceType::None, PieceColor::Black}));
        EXPECT_FALSE((EditorTool{PieceType::Pawn, PieceColor::White} == EditorTool{PieceType::Pawn, PieceColor::Black}));
    }

}  // namespace BECore::Chess::Tests
