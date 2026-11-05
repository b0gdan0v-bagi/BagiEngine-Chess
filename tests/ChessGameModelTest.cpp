#include "ChessTestUtil.h"

#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>

/**
 * The match model: the click flow, moves, the end of the game and the piece handles the scene follows. It needs no engine
 * services, so it runs here the same way the game uses it.
 */
namespace BECore::Chess::Tests {

    namespace {

        bool Click(ChessGameModel& model, eastl::string_view square) {
            return model.OnSquareClicked(Sq(square));
        }

        /// Plays a move given in UCI through TryMove (so castling, en passant and under-promotion work too).
        bool Play(ChessGameModel& model, eastl::string_view uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        /// Plays "e2 e4"-style click pairs; each pair is a selection followed by a target.
        void ClickMoves(ChessGameModel& model, std::initializer_list<const char*> squares) {
            for (const char* square : squares) {
                Click(model, square);
            }
        }

        /// A handle for each piece of the opening position, registered at its start square (as the scene does, whatever the
        /// game has reached by then).
        class Handles {
        public:
            explicit Handles(ChessGameModel& model) {
                const Position opening = Position::Start();
                for (int sq = 0; sq < kSquareCount; ++sq) {
                    const Square square = Square::FromIndex(sq);
                    if (!opening.GetPieceAt(square).IsEmpty()) {
                        model.RegisterPiece(&_slots[static_cast<size_t>(sq)], square);
                    }
                }
            }

            /// The handle that started on @p square.
            [[nodiscard]] const void* At(eastl::string_view square) const {
                return AtIndex(SqIdx(square));
            }
            [[nodiscard]] const void* AtIndex(int sq) const {
                return &_slots[static_cast<size_t>(sq)];
            }

        private:
            eastl::array<char, kSquareCount> _slots{};
        };

        /// Each handle stands on a square that holds the piece it reports, and every piece of the position has exactly one handle
        void ExpectEveryPieceHasOneHandle(const ChessGameModel& model, const Handles& handles) {
            const Position opening = Position::Start();
            eastl::array<int, kSquareCount> handlesOnSquare{};
            for (int start = 0; start < kSquareCount; ++start) {
                if (opening.GetPieceAt(Square::FromIndex(start)).IsEmpty()) {
                    continue;
                }
                const void* handle = handles.AtIndex(start);
                ASSERT_TRUE(model.IsActive(handle)) << "the handle that started on square " << start;
                const Square at = model.GetSquareOf(handle);
                ASSERT_TRUE(at.IsInside());
                EXPECT_TRUE(model.GetPieceOf(handle) == model.GetPieceAt(at)) << "the handle that started on square " << start;
                ++handlesOnSquare[static_cast<size_t>(at.Index())];
            }
            for (int sq = 0; sq < kSquareCount; ++sq) {
                EXPECT_EQ(handlesOnSquare[static_cast<size_t>(sq)], model.GetPieceAt(Square::FromIndex(sq)).IsEmpty() ? 0 : 1) << "square " << sq;
            }
        }
    }  // namespace

    // ---- Click flow ----

    TEST(ChessGameModelTest, StartsOngoingWithWhiteToMove) {
        const ChessGameModel model;
        EXPECT_TRUE(model.GetTurn() == PieceColor::White);
        EXPECT_EQ(model.GetStatus(), GameStatus::Ongoing);
        EXPECT_FALSE(model.IsGameOver());
        EXPECT_FALSE(model.IsInCheck());
        EXPECT_FALSE(model.HasSelection());
        EXPECT_FALSE(model.GetWinner().has_value());
        EXPECT_EQ(model.GetPosition().ToFen(), eastl::string(Position::kStartFen));
    }

    TEST(ChessGameModelTest, SelectingAPieceShowsItsLegalMoves) {
        ChessGameModel model;
        EXPECT_FALSE(Click(model, "e2"));
        ASSERT_TRUE(model.HasSelection());
        EXPECT_EQ(model.GetSelected(), Sq("e2"));
        EXPECT_EQ(model.GetLegalMoves().size(), 2u);
    }

    TEST(ChessGameModelTest, SelectingTheOpponentsPieceOrAnEmptySquareDoesNothing) {
        ChessGameModel model;
        Click(model, "e7");
        EXPECT_FALSE(model.HasSelection());
        Click(model, "e4");
        EXPECT_FALSE(model.HasSelection());
    }

    TEST(ChessGameModelTest, ClickingALegalTargetPlaysTheMove) {
        ChessGameModel model;
        Click(model, "e2");
        EXPECT_TRUE(Click(model, "e4"));
        EXPECT_TRUE(model.GetTurn() == PieceColor::Black);
        EXPECT_FALSE(model.HasSelection());
        EXPECT_TRUE(model.GetPieceAt(Sq("e4")).type == PieceType::Pawn);
        EXPECT_TRUE(model.GetPieceAt(Sq("e2")).IsEmpty());
    }

    TEST(ChessGameModelTest, ClickingAnotherOwnPieceMovesTheSelection) {
        ChessGameModel model;
        Click(model, "e2");
        EXPECT_FALSE(Click(model, "g1"));
        ASSERT_TRUE(model.HasSelection());
        EXPECT_EQ(model.GetSelected(), Sq("g1"));
        EXPECT_EQ(model.GetLegalMoves().size(), 2u) << "the knight has f3 and h3";
    }

    TEST(ChessGameModelTest, ClickingAnIllegalSquareClearsTheSelection) {
        ChessGameModel model;
        Click(model, "e2");
        EXPECT_FALSE(Click(model, "e5"));
        EXPECT_FALSE(model.HasSelection());
        EXPECT_TRUE(model.GetTurn() == PieceColor::White);
    }

    TEST(ChessGameModelTest, ClicksOffTheBoardAreIgnored) {
        ChessGameModel model;
        EXPECT_FALSE(model.OnSquareClicked(Square{-1, 3}));
        EXPECT_FALSE(model.OnSquareClicked(Square{3, 8}));
        EXPECT_FALSE(model.HasSelection());
    }

    TEST(ChessGameModelTest, LegalMovesForListsTargetsOfAnySquare) {
        const ChessGameModel model;
        EXPECT_EQ(model.GetLegalMovesFor(Sq("b1")).size(), 2u);
        EXPECT_TRUE(model.GetLegalMovesFor(Sq("a1")).empty()) << "rook is boxed in";
        EXPECT_TRUE(model.GetLegalMovesFor(Sq("e4")).empty()) << "empty square";
        EXPECT_TRUE(model.GetLegalMovesFor(Square{9, 9}).empty());
    }

    TEST(ChessGameModelTest, APinnedPieceOffersNoMoveOffThePin) {
        ChessGameModel model;
        // After 1. e4 e5 2. Bb5 the bishop looks at the king on e8 through c6 and d7, and the pawn on d7 is the only piece between.
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(Play(model, "e7e5"));
        ASSERT_TRUE(Play(model, "f1b5"));
        EXPECT_FALSE(model.IsInCheck());
        EXPECT_TRUE(model.GetLegalMovesFor(Sq("d7")).empty()) << "d6 and d5 would leave the diagonal";
        EXPECT_FALSE(model.GetLegalMovesFor(Sq("c7")).empty()) << "other pawns are free";
    }

    // ---- Moves ----

    TEST(ChessGameModelTest, TryMoveRejectsIllegalMovesAndLeavesTheGameAlone) {
        ChessGameModel model;
        EXPECT_FALSE(model.TryMove(Mv("e2e5"))) << "a pawn cannot jump three squares";
        EXPECT_FALSE(model.TryMove(Mv("e7e5"))) << "not black's turn";
        EXPECT_FALSE(model.TryMove(Move{}));
        EXPECT_EQ(model.GetPosition().ToFen(), eastl::string(Position::kStartFen));
    }

    TEST(ChessGameModelTest, TryMoveSaysWhyItRefusedAMove) {
        ChessGameModel model;
        const auto illegal = model.TryMove(Mv("e2e5"));
        ASSERT_FALSE(illegal.has_value());
        EXPECT_EQ(illegal.error(), MoveError::IllegalMove);

        ClickMoves(model, {"f2", "f3", "e7", "e5", "g2", "g4", "d8", "h4"});
        ASSERT_TRUE(model.IsGameOver());
        const auto afterTheEnd = model.TryMove(Mv("e2e3"));
        ASSERT_FALSE(afterTheEnd.has_value());
        EXPECT_EQ(afterTheEnd.error(), MoveError::GameOver) << "a finished game says so even for a move that would be legal";
    }

    TEST(ChessGameModelTest, TryMoveNeedsNoSelection) {
        ChessGameModel model;
        EXPECT_TRUE(model.TryMove(Mv("g1f3")));
        EXPECT_TRUE(model.GetTurn() == PieceColor::Black);
    }

    TEST(ChessGameModelTest, PlayingAMoveClearsAPendingSelection) {
        ChessGameModel model;
        Click(model, "b1");
        ASSERT_TRUE(model.HasSelection());
        EXPECT_TRUE(model.TryMove(Mv("e2e4")));
        EXPECT_FALSE(model.HasSelection());
        EXPECT_TRUE(model.GetLegalMoves().empty());
    }

    TEST(ChessGameModelTest, ClearSelectionDropsTheSelectionAndItsTargetsWithoutMovingTheVersion) {
        ChessGameModel model;
        Click(model, "g1");
        ASSERT_TRUE(model.HasSelection());
        ASSERT_FALSE(model.GetLegalMoves().empty());
        const uint64_t version = model.GetVersion();

        model.ClearSelection();

        EXPECT_FALSE(model.HasSelection());
        EXPECT_TRUE(model.GetLegalMoves().empty());
        EXPECT_EQ(model.GetVersion(), version);
        model.ClearSelection();  // nothing selected: nothing to do
        EXPECT_FALSE(model.HasSelection());
        Click(model, "g1");
        EXPECT_TRUE(model.HasSelection()) << "a click selects again";
    }

    TEST(ChessGameModelTest, PawnReachingTheLastRankWaitsForAPieceOnClick) {
        ChessGameModel model;
        for (const char* uci : {"h2h4", "g7g5", "h4g5", "h7h6", "g5h6", "a7a6", "h6h7", "b7b6"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }
        ClickMoves(model, {"h7", "g8"});
        EXPECT_TRUE(model.HasPendingPromotion());
        EXPECT_TRUE(model.GetPieceAt(Sq("g8")).type == PieceType::Knight) << "nothing has moved";
        ASSERT_TRUE(model.ChoosePromotion(PieceType::Queen).has_value());
        EXPECT_TRUE(model.GetPieceAt(Sq("g8")).type == PieceType::Queen);
        EXPECT_TRUE(model.GetPieceAt(Sq("g8")).color == PieceColor::White);
        EXPECT_FALSE(model.HasPendingPromotion());
    }

    TEST(ChessGameModelTest, UnderPromotionIsAvailableThroughTryMove) {
        ChessGameModel model;
        for (const char* uci : {"h2h4", "g7g5", "h4g5", "h7h6", "g5h6", "a7a6", "h6h7", "b7b6"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }
        ASSERT_TRUE(Play(model, "h7g8n"));
        EXPECT_TRUE(model.GetPieceAt(Sq("g8")).type == PieceType::Knight);
    }

    // ---- The promotion picker ----

    namespace {

        constexpr eastl::string_view kPromotionFen = "8/P6k/8/8/8/8/8/K7 w - - 0 1";
        constexpr eastl::string_view kPromotingCaptureFen = "1r5k/P7/8/8/8/8/8/K7 w - - 0 1";
        constexpr eastl::string_view kMatingPromotionFen = "7k/P5pp/8/8/8/8/8/K7 w - - 0 1";
        constexpr eastl::string_view kBlackPromotionFen = "k7/8/8/8/8/8/p6K/8 b - - 0 1";

        /// A model on @p fen with the pawn selected and its promotion target clicked
        void StartPromotion(ChessGameModel& model, eastl::string_view fen, eastl::string_view from, eastl::string_view to) {
            model.SetPosition(PositionFromFen(fen));
            Click(model, from);
            Click(model, to);
        }

        /// The handle that stands on @p square now, or null
        const void* HandleOn(const ChessGameModel& model, const Handles& handles, eastl::string_view square) {
            for (int sq = 0; sq < kSquareCount; ++sq) {
                if (model.IsActive(handles.AtIndex(sq)) && model.GetSquareOf(handles.AtIndex(sq)) == Sq(square)) {
                    return handles.AtIndex(sq);
                }
            }
            return nullptr;
        }

    }  // namespace

    TEST(ChessGameModelTest, ClickOnAPromotionTargetIsPendingAndMovesNothing) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen(kPromotionFen));
        const uint64_t version = model.GetVersion();
        const eastl::string fen = model.GetPosition().ToFen();
        EXPECT_FALSE(model.HasPendingPromotion());

        EXPECT_FALSE(Click(model, "a7"));
        EXPECT_FALSE(model.HasPendingPromotion()) << "a selection alone is not a promotion";
        EXPECT_FALSE(Click(model, "a8"));

        EXPECT_TRUE(model.HasPendingPromotion());
        EXPECT_EQ(model.GetPromotionTarget(), Sq("a8"));
        EXPECT_EQ(model.GetVersion(), version);
        EXPECT_EQ(model.GetPosition().ToFen(), fen);
        EXPECT_TRUE(model.GetMoves().empty());
        EXPECT_TRUE(model.HasSelection());
        EXPECT_EQ(model.GetSelected(), Sq("a7"));
        EXPECT_TRUE(model.GetPieceAt(Sq("a7")).type == PieceType::Pawn);
    }

    TEST(ChessGameModelTest, EachChoiceOfThePickerPlaysItsPiece) {
        const eastl::array<std::pair<PieceType, const char*>, 4> choices{{
            {PieceType::Queen, "a7a8q"},
            {PieceType::Rook, "a7a8r"},
            {PieceType::Bishop, "a7a8b"},
            {PieceType::Knight, "a7a8n"},
        }};
        for (const auto& [piece, uci] : choices) {
            ChessGameModel model;
            const Handles handles(model);
            StartPromotion(model, kPromotionFen, "a7", "a8");
            const uint64_t version = model.GetVersion();
            ASSERT_TRUE(model.HasPendingPromotion()) << uci;

            const auto result = model.ChoosePromotion(piece);

            ASSERT_TRUE(result.has_value()) << uci;
            ASSERT_EQ(model.GetMoves().size(), 1u) << uci;
            EXPECT_EQ(model.GetMoves().back().ToUci(), uci);
            EXPECT_EQ(model.GetVersion(), version + 1) << uci;
            EXPECT_TRUE(model.GetPieceAt(Sq("a8")).type == piece) << uci;
            EXPECT_TRUE(model.GetPieceAt(Sq("a8")).color == PieceColor::White) << uci;
            EXPECT_TRUE(model.GetPieceAt(Sq("a7")).IsEmpty()) << uci;
            EXPECT_FALSE(model.HasPendingPromotion()) << uci;
            EXPECT_FALSE(model.HasSelection()) << uci;
            EXPECT_TRUE(model.GetTurn() == PieceColor::Black) << uci;

            const void* pawn = HandleOn(model, handles, "a8");
            ASSERT_NE(pawn, nullptr) << uci;
            EXPECT_TRUE(model.GetPieceOf(pawn).type == piece) << "the handle of the pawn reports the piece: " << uci;
        }
    }

    TEST(ChessGameModelTest, PromotingCaptureWaitsForAPieceToo) {
        const eastl::array<std::pair<PieceType, const char*>, 4> choices{{
            {PieceType::Queen, "a7b8q"},
            {PieceType::Rook, "a7b8r"},
            {PieceType::Bishop, "a7b8b"},
            {PieceType::Knight, "a7b8n"},
        }};
        for (const auto& [piece, uci] : choices) {
            ChessGameModel model;
            const Handles handles(model);
            StartPromotion(model, kPromotingCaptureFen, "a7", "b8");
            ASSERT_TRUE(model.HasPendingPromotion()) << uci;
            EXPECT_EQ(model.GetPromotionTarget(), Sq("b8"));
            EXPECT_TRUE(model.GetPieceAt(Sq("b8")).type == PieceType::Rook) << "the rook is still there";

            ASSERT_TRUE(model.ChoosePromotion(piece).has_value()) << uci;

            EXPECT_EQ(model.GetMoves().back().ToUci(), uci);
            EXPECT_TRUE(model.GetPieceAt(Sq("b8")).type == piece) << uci;
            EXPECT_TRUE(model.GetPieceAt(Sq("b8")).color == PieceColor::White) << uci;
        }
    }

    TEST(ChessGameModelTest, ChoiceWithNothingPendingIsIllegalAndChangesNothing) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen(kPromotionFen));
        const uint64_t version = model.GetVersion();
        const eastl::string fen = model.GetPosition().ToFen();

        for (const PieceType piece : {PieceType::Queen, PieceType::Knight, PieceType::Pawn}) {
            const auto result = model.ChoosePromotion(piece);
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error(), MoveError::IllegalMove);
        }
        Click(model, "a7");  // selected, no target yet
        const auto selected = model.ChoosePromotion(PieceType::Queen);
        ASSERT_FALSE(selected.has_value());
        EXPECT_EQ(selected.error(), MoveError::IllegalMove);
        EXPECT_TRUE(model.HasSelection());

        EXPECT_EQ(model.GetVersion(), version);
        EXPECT_EQ(model.GetPosition().ToFen(), fen);
    }

    TEST(ChessGameModelTest, ChoiceOfAPieceThatIsNotAPromotionIsIllegalAndKeepsThePending) {
        ChessGameModel model;
        StartPromotion(model, kPromotionFen, "a7", "a8");
        const uint64_t version = model.GetVersion();

        for (const PieceType piece : {PieceType::None, PieceType::Pawn, PieceType::King}) {
            const auto result = model.ChoosePromotion(piece);
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error(), MoveError::IllegalMove);
        }

        EXPECT_TRUE(model.HasPendingPromotion());
        EXPECT_EQ(model.GetVersion(), version);
        EXPECT_TRUE(model.GetMoves().empty());
        EXPECT_TRUE(model.ChoosePromotion(PieceType::Queen).has_value()) << "the choice still stands";
    }

    TEST(ChessGameModelTest, ClickOnAnotherSquareCancelsThePromotion) {
        for (const char* other : {"a7", "h7", "e4", "a1"}) {
            ChessGameModel model;
            StartPromotion(model, kPromotionFen, "a7", "a8");
            ASSERT_TRUE(model.HasPendingPromotion());
            const uint64_t version = model.GetVersion();

            EXPECT_FALSE(Click(model, other));

            EXPECT_FALSE(model.HasPendingPromotion()) << other;
            EXPECT_TRUE(model.HasSelection()) << "the selection stays: " << other;
            EXPECT_EQ(model.GetSelected(), Sq("a7")) << other;
            EXPECT_EQ(model.GetVersion(), version) << other;
            const auto result = model.ChoosePromotion(PieceType::Queen);
            ASSERT_FALSE(result.has_value()) << other;
            EXPECT_EQ(result.error(), MoveError::IllegalMove);
            EXPECT_TRUE(model.GetMoves().empty());
        }
    }

    TEST(ChessGameModelTest, ClickOnTheTargetAgainCancelsAndAnotherOpensIt) {
        ChessGameModel model;
        StartPromotion(model, kPromotionFen, "a7", "a8");
        Click(model, "a8");
        EXPECT_FALSE(model.HasPendingPromotion()) << "the second click on the target is a click on a square";
        Click(model, "a8");
        EXPECT_TRUE(model.HasPendingPromotion()) << "the pawn is still selected, so the target opens the choice again";
    }

    TEST(ChessGameModelTest, CancelPromotionKeepsTheSelection) {
        ChessGameModel model;
        StartPromotion(model, kPromotionFen, "a7", "a8");
        const uint64_t version = model.GetVersion();

        model.CancelPromotion();

        EXPECT_FALSE(model.HasPendingPromotion());
        EXPECT_TRUE(model.HasSelection());
        EXPECT_EQ(model.GetSelected(), Sq("a7"));
        EXPECT_FALSE(model.GetLegalMoves().empty());
        EXPECT_EQ(model.GetVersion(), version);
        model.CancelPromotion();  // nothing pending: nothing to do
        EXPECT_TRUE(model.HasSelection());
        Click(model, "a8");
        EXPECT_TRUE(model.HasPendingPromotion()) << "the same target opens the choice again";
    }

    TEST(ChessGameModelTest, ResetSetPositionClearSelectionAndAMoveClearThePending) {
        {
            ChessGameModel model;
            StartPromotion(model, kPromotionFen, "a7", "a8");
            model.Reset();
            EXPECT_FALSE(model.HasPendingPromotion()) << "Reset";
            EXPECT_FALSE(model.HasSelection());
        }
        {
            ChessGameModel model;
            StartPromotion(model, kPromotionFen, "a7", "a8");
            model.SetPosition(PositionFromFen(kPromotingCaptureFen));
            EXPECT_FALSE(model.HasPendingPromotion()) << "SetPosition";
        }
        {
            ChessGameModel model;
            StartPromotion(model, kPromotionFen, "a7", "a8");
            model.ClearSelection();
            EXPECT_FALSE(model.HasPendingPromotion()) << "ClearSelection";
            EXPECT_FALSE(model.HasSelection());
        }
        {
            ChessGameModel model;
            StartPromotion(model, kPromotionFen, "a7", "a8");
            ASSERT_TRUE(Play(model, "a1b1"));
            EXPECT_FALSE(model.HasPendingPromotion()) << "a move of another piece";
            EXPECT_FALSE(model.ChoosePromotion(PieceType::Queen).has_value());
            EXPECT_TRUE(model.GetPieceAt(Sq("a7")).type == PieceType::Pawn);
        }
        {
            ChessGameModel model;
            StartPromotion(model, kPromotionFen, "a7", "a8");
            ASSERT_TRUE(Play(model, "a7a8r"));
            EXPECT_FALSE(model.HasPendingPromotion()) << "the same pawn promoted by TryMove";
        }
    }

    TEST(ChessGameModelTest, PromotionThatMatesEndsTheGame) {
        ChessGameModel model;
        StartPromotion(model, kMatingPromotionFen, "a7", "a8");
        ASSERT_TRUE(model.HasPendingPromotion());

        ASSERT_TRUE(model.ChoosePromotion(PieceType::Queen).has_value());

        EXPECT_EQ(model.GetStatus(), GameStatus::Checkmate);
        EXPECT_TRUE(model.IsGameOver());
        ASSERT_TRUE(model.GetWinner().has_value());
        EXPECT_TRUE(*model.GetWinner() == PieceColor::White);
        EXPECT_FALSE(model.HasPendingPromotion());

        const uint64_t version = model.GetVersion();
        const auto after = model.ChoosePromotion(PieceType::Rook);
        ASSERT_FALSE(after.has_value());
        EXPECT_EQ(after.error(), MoveError::GameOver);
        EXPECT_EQ(model.GetVersion(), version);
        EXPECT_EQ(model.GetMoves().size(), 1u);
    }

    TEST(ChessGameModelTest, ABlackPromotionWorks) {
        ChessGameModel model;
        const Handles handles(model);
        StartPromotion(model, kBlackPromotionFen, "a2", "a1");
        ASSERT_TRUE(model.HasPendingPromotion());
        EXPECT_EQ(model.GetPromotionTarget(), Sq("a1"));

        ASSERT_TRUE(model.ChoosePromotion(PieceType::Knight).has_value());

        EXPECT_EQ(model.GetMoves().back().ToUci(), "a2a1n");
        EXPECT_TRUE(model.GetPieceAt(Sq("a1")).type == PieceType::Knight);
        EXPECT_TRUE(model.GetPieceAt(Sq("a1")).color == PieceColor::Black);
        const void* pawn = HandleOn(model, handles, "a1");
        ASSERT_NE(pawn, nullptr);
        EXPECT_TRUE(model.GetPieceOf(pawn).type == PieceType::Knight);
        EXPECT_TRUE(model.GetPieceOf(pawn).color == PieceColor::Black);
    }

    TEST(ChessGameModelTest, MovesThatDoNotPromoteNeverSetThePending) {
        ChessGameModel model;
        ClickMoves(model, {"e2", "e4"});
        EXPECT_FALSE(model.HasPendingPromotion());
        EXPECT_EQ(model.GetMoves().size(), 1u);

        // A click on a square the pawn cannot go to is no promotion either
        ChessGameModel other;
        other.SetPosition(PositionFromFen(kPromotionFen));
        ClickMoves(other, {"a7", "a6"});
        EXPECT_FALSE(other.HasPendingPromotion()) << "a7a6 is not a move";
        ClickMoves(other, {"a1", "b1"});
        EXPECT_FALSE(other.HasPendingPromotion());
        EXPECT_EQ(other.GetMoves().size(), 1u) << "the king moved by a plain click pair";
    }

    TEST(ChessGameModelTest, NoPendingPromotionOnceTheGameIsOver) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));  // stalemate for Black
        ASSERT_TRUE(model.IsGameOver());
        Click(model, "h8");
        Click(model, "h7");
        EXPECT_FALSE(model.HasPendingPromotion());
        const auto result = model.ChoosePromotion(PieceType::Queen);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), MoveError::GameOver);
    }

    // ---- End of the game ----

    TEST(ChessGameModelTest, FoolsMateEndsTheGameWhenPlayedByClicks) {
        ChessGameModel model;
        ClickMoves(model, {"f2", "f3", "e7", "e5", "g2", "g4", "d8", "h4"});

        EXPECT_EQ(model.GetStatus(), GameStatus::Checkmate);
        EXPECT_TRUE(model.IsGameOver());
        EXPECT_TRUE(model.IsInCheck());
        ASSERT_TRUE(model.GetWinner().has_value());
        EXPECT_TRUE(*model.GetWinner() == PieceColor::Black);
        EXPECT_EQ(model.GetKingSquare(PieceColor::White), Sq("e1"));
    }

    TEST(ChessGameModelTest, ScholarsMateEndsTheGameForWhite) {
        ChessGameModel model;
        for (const char* uci : {"e2e4", "e7e5", "f1c4", "b8c6", "d1h5", "g8f6", "h5f7"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }
        EXPECT_EQ(model.GetStatus(), GameStatus::Checkmate);
        ASSERT_TRUE(model.GetWinner().has_value());
        EXPECT_TRUE(*model.GetWinner() == PieceColor::White);
    }

    TEST(ChessGameModelTest, CheckIsReportedWithoutEndingTheGame) {
        ChessGameModel model;
        for (const char* uci : {"e2e4", "f7f5", "d1h5"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }
        EXPECT_TRUE(model.IsInCheck());
        EXPECT_EQ(model.GetStatus(), GameStatus::Ongoing);
        // The queen on h5 checks along h5-g6-f7-e8, and g7-g6 is the only way to answer: the king has no safe square.
        EXPECT_EQ(model.GetLegalMovesFor(Sq("g7")).size(), 1u);
        EXPECT_TRUE(model.GetLegalMovesFor(Sq("a7")).empty());
    }

    TEST(ChessGameModelTest, NoMoveIsAcceptedAfterTheGameEnds) {
        ChessGameModel model;
        ClickMoves(model, {"f2", "f3", "e7", "e5", "g2", "g4", "d8", "h4"});
        ASSERT_TRUE(model.IsGameOver());

        EXPECT_FALSE(Click(model, "e2"));
        EXPECT_FALSE(model.HasSelection());
        EXPECT_FALSE(model.TryMove(Mv("e2e3")));
        EXPECT_EQ(model.GetStatus(), GameStatus::Checkmate);
    }

    TEST(ChessGameModelTest, ResetStartsANewGame) {
        ChessGameModel model;
        ClickMoves(model, {"f2", "f3", "e7", "e5", "g2", "g4", "d8", "h4"});
        ASSERT_TRUE(model.IsGameOver());

        model.Reset();
        EXPECT_EQ(model.GetStatus(), GameStatus::Ongoing);
        EXPECT_FALSE(model.IsInCheck());
        EXPECT_TRUE(model.GetTurn() == PieceColor::White);
        EXPECT_EQ(model.GetPosition().ToFen(), eastl::string(Position::kStartFen));
        EXPECT_TRUE(Play(model, "e2e4"));
    }

    TEST(ChessGameModelTest, ThreefoldRepetitionDrawsTheGame) {
        ChessGameModel model;
        for (int cycle = 0; cycle < 2; ++cycle) {
            ASSERT_FALSE(model.IsGameOver()) << "cycle " << cycle;
            for (const char* uci : {"g1f3", "g8f6", "f3g1", "f6g8"}) {
                ASSERT_TRUE(Play(model, uci)) << uci;
            }
        }
        EXPECT_EQ(model.GetStatus(), GameStatus::DrawThreefold);
        EXPECT_FALSE(model.GetWinner().has_value());
    }

    // ---- History and version ----

    TEST(ChessGameModelTest, PlayedMovesAreKeptInOrder) {
        ChessGameModel model;
        EXPECT_TRUE(model.GetMoves().empty());
        EXPECT_EQ(model.GetStartFen(), eastl::string(Position::kStartFen));

        for (const char* uci : {"e2e4", "e7e5", "g1f3"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }
        ASSERT_EQ(model.GetMoves().size(), 3u);
        EXPECT_EQ(model.GetMoves()[0], Mv("e2e4"));
        EXPECT_EQ(model.GetMoves()[2], Mv("g1f3"));

        EXPECT_FALSE(model.TryMove(Mv("e2e5")).has_value());
        EXPECT_EQ(model.GetMoves().size(), 3u) << "a refused move is not history";
    }

    TEST(ChessGameModelTest, VersionGrowsWithEveryChangeAndNeverWithASelection) {
        ChessGameModel model;
        uint64_t version = model.GetVersion();

        Click(model, "e2");
        EXPECT_EQ(model.GetVersion(), version) << "selecting moves nothing";

        EXPECT_FALSE(model.TryMove(Mv("e2e5")).has_value());
        EXPECT_EQ(model.GetVersion(), version) << "a refused move changes nothing";

        ASSERT_TRUE(Play(model, "e2e4"));
        EXPECT_GT(model.GetVersion(), version);
        version = model.GetVersion();

        model.Reset();
        EXPECT_GT(model.GetVersion(), version) << "a new game is a change, even from the position it already had";
        version = model.GetVersion();

        model.SetPosition(PositionFromFen(kKiwipeteFen));
        EXPECT_GT(model.GetVersion(), version);
    }

    TEST(ChessGameModelTest, ClickedMovesCountAsMovesToo) {
        ChessGameModel model;
        const uint64_t version = model.GetVersion();
        ClickMoves(model, {"e2", "e4"});
        EXPECT_EQ(model.GetMoves().size(), 1u);
        EXPECT_GT(model.GetVersion(), version);
    }

    TEST(ChessGameModelTest, ResetForgetsTheMoves) {
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        model.Reset();
        EXPECT_TRUE(model.GetMoves().empty());
    }

    // ---- Setting a position ----

    TEST(ChessGameModelTest, SetPositionStartsAGameFromThatPosition) {
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        Click(model, "e7");

        model.SetPosition(PositionFromFen(kKiwipeteFen));

        EXPECT_EQ(model.GetPosition().ToFen(), eastl::string(kKiwipeteFen));
        EXPECT_EQ(model.GetStartFen(), eastl::string(kKiwipeteFen));
        EXPECT_TRUE(model.GetMoves().empty());
        EXPECT_FALSE(model.HasSelection());
        EXPECT_EQ(model.GetStatus(), GameStatus::Ongoing);
        EXPECT_TRUE(Play(model, "e1g1")) << "the castling rights of the FEN are in force";
        ASSERT_EQ(model.GetMoves().size(), 1u);
    }

    TEST(ChessGameModelTest, SetPositionOfAFinishedGameStartsItFinished) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"));
        EXPECT_EQ(model.GetStatus(), GameStatus::Ongoing);
        ASSERT_TRUE(Play(model, "a1a8"));
        EXPECT_EQ(model.GetStatus(), GameStatus::Checkmate);

        model.SetPosition(PositionFromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
        EXPECT_EQ(model.GetStatus(), GameStatus::Stalemate);
        EXPECT_TRUE(model.IsGameOver());
        EXPECT_FALSE(model.TryMove(Mv("h8g8")).has_value());

        model.Reset();
        EXPECT_EQ(model.GetStatus(), GameStatus::Ongoing);
    }

    TEST(ChessGameModelTest, SetPositionDealsEveryPieceToAHandle) {
        ChessGameModel model;
        const Handles handles(model);
        model.SetPosition(PositionFromFen(kKiwipeteFen));
        ExpectEveryPieceHasOneHandle(model, handles);
    }

    TEST(ChessGameModelTest, PiecesThatRegisterAfterTheGameMovedStandWhereThePositionHasThem) {
        // A client that acts before the first frame: the scene registers its pieces after e2e4 e7e5 g1f3 were played
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(Play(model, "e7e5"));
        ASSERT_TRUE(Play(model, "g1f3"));
        const Handles handles(model);

        ExpectEveryPieceHasOneHandle(model, handles);
        EXPECT_EQ(model.GetSquareOf(handles.At("a1")), Sq("a1"));
        EXPECT_TRUE(model.GetPieceAt(Sq("e2")).IsEmpty()) << "e2 is empty after e2e4";
        EXPECT_TRUE(model.GetPieceAt(Sq("f3")).type == PieceType::Knight);

        // The game goes on from there and a reset brings every handle home
        ASSERT_TRUE(Play(model, "b8c6"));
        ExpectEveryPieceHasOneHandle(model, handles);
        model.Reset();
        for (int sq = 0; sq < 16; ++sq) {
            EXPECT_EQ(model.GetSquareOf(handles.AtIndex(sq)), Square::FromIndex(sq)) << "white handle " << sq;
        }
        EXPECT_TRUE(model.GetPieceOf(handles.At("e2")) == model.GetPieceAt(Sq("e2")));
    }

    TEST(ChessGameModelTest, PiecesThatRegisterIntoASetPositionAreDealtToIt) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen(kKiwipeteFen));
        const Handles handles(model);
        ExpectEveryPieceHasOneHandle(model, handles);
    }

    TEST(ChessGameModelTest, SetPositionTakesTheHandlesWithoutAPieceOffTheBoard) {
        ChessGameModel model;
        const Handles handles(model);
        const Position opening = Position::Start();
        model.SetPosition(PositionFromFen("4k3/8/8/8/8/8/8/4K3 w - - 0 1"));

        int kings = 0;
        int others = 0;
        for (int start = 0; start < kSquareCount; ++start) {
            if (opening.GetPieceAt(Square::FromIndex(start)).IsEmpty()) {
                continue;
            }
            const void* handle = handles.AtIndex(start);
            if (!model.IsActive(handle)) {
                EXPECT_FALSE(model.GetSquareOf(handle).IsInside());
            } else if (model.GetPieceOf(handle).type == PieceType::King) {
                ++kings;
            } else {
                ++others;
            }
        }
        EXPECT_EQ(kings, 2);
        EXPECT_EQ(others, 0);
    }

    TEST(ChessGameModelTest, ResetBringsTheHandlesBackAfterASetPosition) {
        ChessGameModel model;
        const Handles handles(model);
        model.SetPosition(PositionFromFen("4k3/8/8/8/8/8/8/4K3 w - - 0 1"));
        model.Reset();

        for (const char* name : {"a1", "e1", "h1", "a2", "d7", "e8", "h8"}) {
            EXPECT_TRUE(model.IsActive(handles.At(name))) << name;
            EXPECT_EQ(model.GetSquareOf(handles.At(name)), Sq(name)) << name;
        }
        EXPECT_TRUE(model.GetPieceOf(handles.At("d1")).type == PieceType::Queen);
    }

    // ---- Piece handles ----

    TEST(ChessGameModelTest, HandlesStartOnTheirSquares) {
        ChessGameModel model;
        const Handles handles(model);
        EXPECT_EQ(model.GetSquareOf(handles.At("e1")), Sq("e1"));
        EXPECT_TRUE(model.GetPieceOf(handles.At("e1")).type == PieceType::King);
        EXPECT_TRUE(model.GetPieceOf(handles.At("d8")).color == PieceColor::Black);
        EXPECT_TRUE(model.IsActive(handles.At("a2")));
    }

    TEST(ChessGameModelTest, HandleFollowsItsPieceAndTheCapturedOneLeaves) {
        ChessGameModel model;
        const Handles handles(model);
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(Play(model, "d7d5"));
        ASSERT_TRUE(Play(model, "e4d5"));

        EXPECT_EQ(model.GetSquareOf(handles.At("e2")), Sq("d5"));
        EXPECT_FALSE(model.IsActive(handles.At("d7")));
        EXPECT_FALSE(model.GetSquareOf(handles.At("d7")).IsInside());
        EXPECT_TRUE(model.GetPieceOf(handles.At("d7")).IsEmpty());
    }

    TEST(ChessGameModelTest, CastlingMovesTheRookHandleToo) {
        ChessGameModel model;
        const Handles handles(model);
        for (const char* uci : {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5", "e1g1"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }
        EXPECT_EQ(model.GetSquareOf(handles.At("e1")), Sq("g1"));
        EXPECT_EQ(model.GetSquareOf(handles.At("h1")), Sq("f1"));
        EXPECT_TRUE(model.IsActive(handles.At("h1")));
    }

    TEST(ChessGameModelTest, QueenSideCastlingMovesTheRookHandleToo) {
        ChessGameModel model;
        const Handles handles(model);
        for (const char* uci : {"d2d4", "d7d5", "b1c3", "b8c6", "c1f4", "c8f5", "d1d2", "d8d7", "e1c1"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }
        EXPECT_EQ(model.GetSquareOf(handles.At("e1")), Sq("c1"));
        EXPECT_EQ(model.GetSquareOf(handles.At("a1")), Sq("d1"));
    }

    TEST(ChessGameModelTest, EnPassantRemovesThePassedPawnHandle) {
        ChessGameModel model;
        const Handles handles(model);
        for (const char* uci : {"e2e4", "a7a6", "e4e5", "d7d5", "e5d6"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }
        EXPECT_EQ(model.GetSquareOf(handles.At("e2")), Sq("d6"));
        EXPECT_FALSE(model.IsActive(handles.At("d7"))) << "the pawn on d5 was taken although the capture landed on d6";
        EXPECT_TRUE(model.GetPieceAt(Sq("d5")).IsEmpty());
    }

    TEST(ChessGameModelTest, PromotionChangesTheHandleType) {
        ChessGameModel model;
        const Handles handles(model);
        for (const char* uci : {"h2h4", "g7g5", "h4g5", "h7h6", "g5h6", "a7a6", "h6h7", "b7b6", "h7g8q"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }
        EXPECT_EQ(model.GetSquareOf(handles.At("h2")), Sq("g8"));
        const Piece promoted = model.GetPieceOf(handles.At("h2"));
        EXPECT_TRUE(promoted.type == PieceType::Queen);
        EXPECT_TRUE(promoted.color == PieceColor::White);
        EXPECT_FALSE(model.IsActive(handles.At("g8"))) << "the knight that stood on g8 was captured";
    }

    TEST(ChessGameModelTest, ResetSendsHandlesBackAsTheyStarted) {
        ChessGameModel model;
        const Handles handles(model);
        for (const char* uci : {"h2h4", "g7g5", "h4g5", "h7h6", "g5h6", "a7a6", "h6h7", "b7b6", "h7g8q"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }
        model.Reset();

        EXPECT_EQ(model.GetSquareOf(handles.At("h2")), Sq("h2"));
        EXPECT_TRUE(model.GetPieceOf(handles.At("h2")).type == PieceType::Pawn) << "the promotion is undone";
        EXPECT_TRUE(model.IsActive(handles.At("g8")));
        EXPECT_EQ(model.GetSquareOf(handles.At("g8")), Sq("g8"));
    }

    TEST(ChessGameModelTest, UnregisteredHandlesAreUnknown) {
        ChessGameModel model;
        const Handles handles(model);
        model.UnregisterPiece(handles.At("a2"));
        EXPECT_FALSE(model.IsActive(handles.At("a2")));
        EXPECT_FALSE(model.GetSquareOf(handles.At("a2")).IsInside());
    }

}  // namespace BECore::Chess::Tests
