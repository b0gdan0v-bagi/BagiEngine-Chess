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
        model.SetPosition(PositionFromFen("8/P6k/8/8/8/8/8/K7 w - - 100 80"));  // a pawn on the 7th with a free way, but 50 moves went by without a capture or a pawn move
        ASSERT_EQ(model.GetStatus(), GameStatus::DrawFiftyMoves);
        ASSERT_TRUE(model.IsGameOver());
        Click(model, "a7");
        Click(model, "a8");
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

    // ---- Taking moves back ----

    namespace {

        void PlayAll(ChessGameModel& model, const eastl::vector<const char*>& moves, size_t count) {
            for (size_t i = 0; i < count; ++i) {
                ASSERT_TRUE(Play(model, moves[i])) << "move " << i << ": " << moves[i];
            }
        }

        /// Every handle that started on a square of the opening stands where, and is what, the other model's handle says
        void ExpectSameHandles(const ChessGameModel& left, const Handles& leftHandles, const ChessGameModel& right, const Handles& rightHandles) {
            const Position opening = Position::Start();
            for (int sq = 0; sq < kSquareCount; ++sq) {
                if (opening.GetPieceAt(Square::FromIndex(sq)).IsEmpty()) {
                    continue;
                }
                const void* leftHandle = leftHandles.AtIndex(sq);
                const void* rightHandle = rightHandles.AtIndex(sq);
                EXPECT_EQ(left.IsActive(leftHandle), right.IsActive(rightHandle)) << "the handle that started on square " << sq;
                EXPECT_EQ(left.GetSquareOf(leftHandle), right.GetSquareOf(rightHandle)) << "the handle that started on square " << sq;
                EXPECT_TRUE(left.GetPieceOf(leftHandle) == right.GetPieceOf(rightHandle)) << "the handle that started on square " << sq;
            }
        }

        /// The handles are consistent with the board, also when pieces are gone: each active handle stands on a square that holds
        /// the piece it reports, no two share a square, and the active handles are exactly the pieces of the position
        void ExpectHandlesMatchTheBoard(const ChessGameModel& model, const Handles& handles) {
            const Position opening = Position::Start();
            eastl::array<int, kSquareCount> handlesOnSquare{};
            for (int start = 0; start < kSquareCount; ++start) {
                const void* handle = handles.AtIndex(start);
                if (opening.GetPieceAt(Square::FromIndex(start)).IsEmpty() || !model.IsActive(handle)) {
                    continue;
                }
                const Square at = model.GetSquareOf(handle);
                ASSERT_TRUE(at.IsInside());
                EXPECT_TRUE(model.GetPieceOf(handle) == model.GetPieceAt(at)) << "the handle that started on square " << start;
                ++handlesOnSquare[static_cast<size_t>(at.Index())];
            }
            for (int sq = 0; sq < kSquareCount; ++sq) {
                EXPECT_EQ(handlesOnSquare[static_cast<size_t>(sq)], model.GetPieceAt(Square::FromIndex(sq)).IsEmpty() ? 0 : 1) << "square " << sq;
            }
        }

        /// What a game is, as far as the rules and the scene can tell: both models must agree on all of it
        void ExpectSameGame(const ChessGameModel& left, const Handles& leftHandles, const ChessGameModel& right, const Handles& rightHandles) {
            EXPECT_EQ(left.GetPosition().ToFen(), right.GetPosition().ToFen());
            EXPECT_EQ(left.GetPosition().RepetitionCount(), right.GetPosition().RepetitionCount());
            EXPECT_EQ(left.GetStartFen(), right.GetStartFen());
            EXPECT_TRUE(left.GetMoves() == right.GetMoves());
            EXPECT_EQ(left.GetStatus(), right.GetStatus());
            EXPECT_TRUE(left.GetTurn() == right.GetTurn());
            EXPECT_TRUE(left.GetWinner() == right.GetWinner());
            ExpectSameHandles(left, leftHandles, right, rightHandles);
        }

        /// For every k, TakeBack(k) on a model that played all of @p moves equals a model that played the first moves.size() - k.
        /// An empty @p startFen is the opening; else the game begins from that FEN through SetPosition().
        void ExpectTakeBackMatchesAFreshModel(eastl::string_view startFen, const eastl::vector<const char*>& moves) {
            for (size_t back = 1; back <= moves.size(); ++back) {
                SCOPED_TRACE(testing::Message() << "taken back: " << back);
                ChessGameModel taken;
                const Handles takenHandles(taken);
                ChessGameModel fresh;
                const Handles freshHandles(fresh);
                if (!startFen.empty()) {
                    taken.SetPosition(PositionFromFen(startFen));
                    fresh.SetPosition(PositionFromFen(startFen));
                }
                PlayAll(taken, moves, moves.size());
                PlayAll(fresh, moves, moves.size() - back);
                ASSERT_EQ(taken.TakeBack(back), back);

                ExpectSameGame(taken, takenHandles, fresh, freshHandles);
                ExpectHandlesMatchTheBoard(taken, takenHandles);
            }
        }

    }  // namespace

    TEST(ChessGameModelTest, TakeBackOneMoveGivesTheOpeningBack) {
        ChessGameModel model;
        ASSERT_TRUE(Play(model, "e2e4"));
        const uint64_t version = model.GetVersion();

        EXPECT_EQ(model.TakeBack(), 1u);

        EXPECT_EQ(model.GetPosition().ToFen(), eastl::string(Position::kStartFen));
        EXPECT_TRUE(model.GetMoves().empty());
        EXPECT_EQ(model.GetVersion(), version + 1);
        EXPECT_TRUE(model.GetTurn() == PieceColor::White);
        EXPECT_EQ(model.GetStartFen(), eastl::string(Position::kStartFen));
    }

    TEST(ChessGameModelTest, TakeBackOfNothingChangesNothingAndKeepsTheVersion) {
        ChessGameModel model;
        uint64_t version = model.GetVersion();
        EXPECT_EQ(model.TakeBack(), 0u) << "no moves";
        EXPECT_EQ(model.TakeBack(5), 0u);
        EXPECT_EQ(model.GetVersion(), version);
        EXPECT_EQ(model.GetPosition().ToFen(), eastl::string(Position::kStartFen));

        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(Play(model, "e7e5"));
        Click(model, "g1");
        version = model.GetVersion();
        EXPECT_EQ(model.TakeBack(0), 0u);
        EXPECT_EQ(model.GetVersion(), version);
        EXPECT_EQ(model.GetMoves().size(), 2u);
        EXPECT_TRUE(model.HasSelection()) << "a takeback of nothing leaves the selection alone";
    }

    TEST(ChessGameModelTest, TakeBackTwoEqualsAGameOfTwoMovesLess) {
        const eastl::vector<const char*> moves = {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4"};
        ChessGameModel model;
        PlayAll(model, moves, moves.size());
        EXPECT_EQ(model.TakeBack(2), 2u);

        ChessGameModel fresh;
        PlayAll(fresh, moves, 3);
        EXPECT_EQ(model.GetPosition().ToFen(), fresh.GetPosition().ToFen());
        EXPECT_TRUE(model.GetMoves() == fresh.GetMoves());
        EXPECT_EQ(model.GetStatus(), fresh.GetStatus());
        EXPECT_TRUE(model.GetTurn() == fresh.GetTurn());
        EXPECT_TRUE(model.GetTurn() == PieceColor::Black);
    }

    TEST(ChessGameModelTest, TakeBackMoreThanThereAreTakesAll) {
        ChessGameModel model;
        for (const char* uci : {"e2e4", "e7e5", "g1f3"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }
        EXPECT_EQ(model.TakeBack(100), 3u);
        EXPECT_TRUE(model.GetMoves().empty());
        EXPECT_EQ(model.GetPosition().ToFen(), eastl::string(Position::kStartFen));
        EXPECT_EQ(model.TakeBack(100), 0u);
    }

    TEST(ChessGameModelTest, TakeBackMovesTheVersionOncePerCallAndNeverBack) {
        ChessGameModel model;
        for (const char* uci : {"e2e4", "e7e5", "g1f3", "b8c6"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }
        uint64_t version = model.GetVersion();

        EXPECT_EQ(model.TakeBack(3), 3u);
        EXPECT_EQ(model.GetVersion(), version + 1) << "three plies are one step";
        version = model.GetVersion();

        EXPECT_EQ(model.TakeBack(1), 1u);
        EXPECT_EQ(model.GetVersion(), version + 1);
        version = model.GetVersion();

        EXPECT_EQ(model.TakeBack(1), 0u);
        EXPECT_EQ(model.GetVersion(), version);

        ASSERT_TRUE(Play(model, "d2d4"));
        EXPECT_EQ(model.GetVersion(), version + 1) << "a move after a takeback is a step as before";
    }

    TEST(ChessGameModelTest, TakeBackPutsAFinishedGameOnAgain) {
        ChessGameModel model;
        ClickMoves(model, {"f2", "f3", "e7", "e5", "g2", "g4", "d8", "h4"});
        ASSERT_EQ(model.GetStatus(), GameStatus::Checkmate);
        ASSERT_TRUE(model.GetWinner().has_value());

        EXPECT_EQ(model.TakeBack(), 1u);
        EXPECT_EQ(model.GetStatus(), GameStatus::Ongoing);
        EXPECT_FALSE(model.IsGameOver());
        EXPECT_FALSE(model.GetWinner().has_value());
        EXPECT_FALSE(model.IsInCheck());
        EXPECT_TRUE(Play(model, "a7a6")) << "a move is accepted again";
    }

    TEST(ChessGameModelTest, TakeBackPutsAStalematedGameOnAgain) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen("7k/8/5K2/8/8/8/8/6Q1 w - - 0 1"));
        ASSERT_TRUE(Play(model, "g1g6"));
        ASSERT_EQ(model.GetStatus(), GameStatus::Stalemate);
        ASSERT_FALSE(model.TryMove(Mv("h8g8")).has_value());

        EXPECT_EQ(model.TakeBack(), 1u);
        EXPECT_EQ(model.GetStatus(), GameStatus::Ongoing);
        EXPECT_FALSE(model.GetWinner().has_value());
        EXPECT_TRUE(Play(model, "g1g2"));
    }

    TEST(ChessGameModelTest, TakeBackFromAThreefoldDrawKeepsTheRepetitionCountRight) {
        const eastl::vector<const char*> cycle = {"g1f3", "g8f6", "f3g1", "f6g8"};
        ChessGameModel model;
        for (int round = 0; round < 2; ++round) {
            PlayAll(model, cycle, cycle.size());
        }
        ASSERT_EQ(model.GetStatus(), GameStatus::DrawThreefold);

        // One ply back: that position has occurred twice, so the game is on, and the same move ends it again.
        EXPECT_EQ(model.TakeBack(), 1u);
        EXPECT_EQ(model.GetStatus(), GameStatus::Ongoing);
        EXPECT_EQ(model.GetPosition().RepetitionCount(), 2) << "the position after seven plies is the one after three";
        ASSERT_TRUE(Play(model, "f6g8"));
        EXPECT_EQ(model.GetStatus(), GameStatus::DrawThreefold);

        // A whole round back: the opening has occurred twice, and the round played again draws on its last ply, not before.
        EXPECT_EQ(model.TakeBack(4), 4u);
        EXPECT_EQ(model.GetStatus(), GameStatus::Ongoing);
        EXPECT_EQ(model.GetPosition().RepetitionCount(), 2);
        for (size_t i = 0; i < cycle.size(); ++i) {
            EXPECT_EQ(model.GetStatus(), GameStatus::Ongoing) << "before ply " << i;
            ASSERT_TRUE(Play(model, cycle[i])) << cycle[i];
        }
        EXPECT_EQ(model.GetStatus(), GameStatus::DrawThreefold);
    }

    TEST(ChessGameModelTest, TakeBackGoesNoFurtherThanASetPosition) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen(kKiwipeteFen));
        ASSERT_TRUE(Play(model, "e1g1"));
        ASSERT_TRUE(Play(model, "e8c8"));

        EXPECT_EQ(model.TakeBack(10), 2u);
        EXPECT_EQ(model.GetPosition().ToFen(), eastl::string(kKiwipeteFen));
        EXPECT_EQ(model.GetStartFen(), eastl::string(kKiwipeteFen));
        EXPECT_TRUE(model.GetMoves().empty());
        EXPECT_EQ(model.TakeBack(), 0u);
        EXPECT_EQ(model.GetPosition().ToFen(), eastl::string(kKiwipeteFen));
    }

    TEST(ChessGameModelTest, TakeBackOfAGameThatBeganFinishedChangesNothing) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
        ASSERT_EQ(model.GetStatus(), GameStatus::Stalemate);
        const uint64_t version = model.GetVersion();

        EXPECT_EQ(model.TakeBack(), 0u);
        EXPECT_EQ(model.GetStatus(), GameStatus::Stalemate);
        EXPECT_EQ(model.GetVersion(), version);
    }

    TEST(ChessGameModelTest, TakeBackDropsTheSelectionAndAPendingPromotion) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen("7k/P7/8/8/8/8/8/K7 w - - 0 1"));
        ASSERT_TRUE(Play(model, "a1b1"));
        Click(model, "h8");
        ASSERT_TRUE(model.HasSelection());
        EXPECT_EQ(model.TakeBack(), 1u);
        EXPECT_FALSE(model.HasSelection());
        EXPECT_TRUE(model.GetLegalMoves().empty());

        ASSERT_TRUE(Play(model, "a1b1"));
        ASSERT_TRUE(Play(model, "h8g8"));
        Click(model, "a7");
        Click(model, "a8");
        ASSERT_TRUE(model.HasPendingPromotion());
        EXPECT_EQ(model.TakeBack(), 1u);
        EXPECT_FALSE(model.HasPendingPromotion());
        EXPECT_FALSE(model.HasSelection());
    }

    TEST(ChessGameModelTest, TakeBackLeavesTheHandlesRightAfterCapturesAndKingSideCastlings) {
        ExpectTakeBackMatchesAFreshModel("", {"e2e4", "d7d5", "e4d5", "g8f6", "g1f3", "f6d5", "f1c4", "c8e6", "e1g1", "g7g6", "d2d3", "f8g7", "c4d5", "e6d5", "b1c3", "e8g8"});
    }

    TEST(ChessGameModelTest, TakeBackLeavesTheHandlesRightAfterQueenSideCastlingsAndCaptures) {
        ExpectTakeBackMatchesAFreshModel("", {"d2d4", "d7d5", "b1c3", "b8c6", "c1f4", "c8f5", "d1d2", "d8d7", "e1c1", "e8c8", "f4c7", "d7c7"});
    }

    TEST(ChessGameModelTest, TakeBackLeavesTheHandlesRightAfterEnPassant) {
        ExpectTakeBackMatchesAFreshModel("", {"e2e4", "a7a6", "e4e5", "d7d5", "e5d6", "c7d6"});
    }

    TEST(ChessGameModelTest, TakeBackBringsThePassedPawnBackOnItsSquare) {
        ChessGameModel model;
        const Handles handles(model);
        for (const char* uci : {"e2e4", "a7a6", "e4e5", "d7d5", "e5d6"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }
        ASSERT_FALSE(model.IsActive(handles.At("d7")));

        EXPECT_EQ(model.TakeBack(), 1u);
        EXPECT_TRUE(model.IsActive(handles.At("d7")));
        EXPECT_EQ(model.GetSquareOf(handles.At("d7")), Sq("d5"));
        EXPECT_EQ(model.GetSquareOf(handles.At("e2")), Sq("e5"));
        EXPECT_TRUE(Play(model, "e5d6")) << "the en passant capture is there to play again";
        EXPECT_FALSE(model.IsActive(handles.At("d7")));
    }

    TEST(ChessGameModelTest, TakeBackLeavesTheHandlesRightAfterAPromotionWithACapture) {
        ExpectTakeBackMatchesAFreshModel("", {"h2h4", "g7g5", "h4g5", "h7h6", "g5h6", "a7a6", "h6h7", "b7b6", "h7g8q"});
    }

    TEST(ChessGameModelTest, TakeBackTurnsAPromotedPieceBackIntoAPawnAndReturnsTheCapturedKnight) {
        ChessGameModel model;
        const Handles handles(model);
        for (const char* uci : {"h2h4", "g7g5", "h4g5", "h7h6", "g5h6", "a7a6", "h6h7", "b7b6", "h7g8q"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }
        ASSERT_TRUE(model.GetPieceOf(handles.At("h2")).type == PieceType::Queen);

        EXPECT_EQ(model.TakeBack(), 1u);
        EXPECT_EQ(model.GetSquareOf(handles.At("h2")), Sq("h7"));
        EXPECT_TRUE(model.GetPieceOf(handles.At("h2")).type == PieceType::Pawn);
        EXPECT_TRUE(model.IsActive(handles.At("g8")));
        EXPECT_EQ(model.GetSquareOf(handles.At("g8")), Sq("g8"));
        ExpectHandlesMatchTheBoard(model, handles);

        // The promotion can be made again, as another piece
        ASSERT_TRUE(Play(model, "h7g8n"));
        EXPECT_TRUE(model.GetPieceOf(handles.At("h2")).type == PieceType::Knight);
    }

    TEST(ChessGameModelTest, TakeBackLeavesTheHandlesOfASetPositionGameRight) {
        // Kiwipete: a capture, both castlings, a capture by a pawn and one by the queen
        ExpectTakeBackMatchesAFreshModel(kKiwipeteFen, {"e5f7", "e8g8", "e1c1", "h3g2", "f3g2"});
    }

    TEST(ChessGameModelTest, TakeBackLeavesTheHandlesRightAfterAPromotionWithoutACapture) {
        ExpectTakeBackMatchesAFreshModel("7k/P7/8/8/8/8/8/K7 w - - 0 1", {"a7a8q", "h8g7", "a8a2"});
    }

    TEST(ChessGameModelTest, TakeBackOfASetPositionGameKeepsEveryPieceOfThePositionOnAHandle) {
        ChessGameModel model;
        const Handles handles(model);
        model.SetPosition(PositionFromFen(kKiwipeteFen));
        for (const char* uci : {"e5f7", "e8g8", "e1c1"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }
        EXPECT_EQ(model.TakeBack(3), 3u);
        ExpectEveryPieceHasOneHandle(model, handles);
    }

    TEST(ChessGameModelTest, AMoveAfterATakeBackIsLegalAndTheHandlesFollowIt) {
        ChessGameModel model;
        const Handles handles(model);
        for (const char* uci : {"e2e4", "e7e5", "g1f3"}) {
            ASSERT_TRUE(Play(model, uci)) << uci;
        }
        ASSERT_EQ(model.TakeBack(2), 2u);

        ASSERT_TRUE(Play(model, "d7d5"));
        ASSERT_TRUE(Play(model, "e4d5"));
        EXPECT_FALSE(model.IsActive(handles.At("d7")));
        EXPECT_EQ(model.GetSquareOf(handles.At("e2")), Sq("d5"));
        EXPECT_EQ(model.GetSquareOf(handles.At("e7")), Sq("e7")) << "the pawn that was moved and taken back stands at home";
        ExpectHandlesMatchTheBoard(model, handles);
        ASSERT_EQ(model.GetMoves().size(), 3u);
        EXPECT_EQ(model.GetMoves()[1], Mv("d7d5"));
    }

    TEST(ChessGameModelTest, TakeBackOneMoveAtATimeIsAFreshModelEveryTime) {
        const eastl::vector<const char*> moves = {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5"};
        ChessGameModel model;
        const Handles handles(model);
        for (size_t i = 0; i < moves.size(); ++i) {
            SCOPED_TRACE(testing::Message() << "move " << i);
            ASSERT_TRUE(Play(model, moves[i]));
            ASSERT_EQ(model.TakeBack(), 1u);

            ChessGameModel fresh;
            const Handles freshHandles(fresh);
            PlayAll(fresh, moves, i);
            ExpectSameGame(model, handles, fresh, freshHandles);

            ASSERT_TRUE(Play(model, moves[i])) << "the move can be played again";
        }
        EXPECT_EQ(model.GetMoves().size(), moves.size());
    }

    // ---- Premoves ----

    namespace {

        bool PremoveClick(ChessGameModel& model, eastl::string_view square, PieceColor premover = PieceColor::White) {
            return model.OnPremoveClicked(premover, Sq(square));
        }

        /// A premove by two clicks; true when the second one queued it
        bool Queue(ChessGameModel& model, eastl::string_view from, eastl::string_view to, PieceColor premover = PieceColor::White) {
            PremoveClick(model, from, premover);
            return PremoveClick(model, to, premover);
        }

        /// A game from @p fen (the side to move in it is the program's)
        ChessGameModel ModelFrom(eastl::string_view fen) {
            ChessGameModel model;
            model.SetPosition(PositionFromFen(fen));
            return model;
        }

        /// The person plays White, the program has answered nothing yet: Black is to move
        ChessGameModel BlackToMoveAfterE4() {
            ChessGameModel model;
            EXPECT_TRUE(Play(model, "e2e4"));
            return model;
        }

        bool HasTarget(const ChessGameModel& model, eastl::string_view square) {
            const auto& targets = model.GetLegalMoves();
            return eastl::find(targets.begin(), targets.end(), Sq(square)) != targets.end();
        }

    }  // namespace

    TEST(ChessGameModelTest, PremoveClickSelectsAnOwnPieceWithItsPremoveTargets) {
        ChessGameModel model = BlackToMoveAfterE4();
        EXPECT_FALSE(PremoveClick(model, "d2"));
        ASSERT_TRUE(model.HasSelection());
        EXPECT_EQ(model.GetSelected(), Sq("d2"));
        EXPECT_TRUE(HasTarget(model, "d3"));
        EXPECT_TRUE(HasTarget(model, "d4"));
        EXPECT_FALSE(HasTarget(model, "d5"));
        EXPECT_TRUE(model.GetPremoves().empty());
        EXPECT_FALSE(model.GetPremoveColor().has_value());
    }

    TEST(ChessGameModelTest, PremoveClickOnATargetQueuesTheMoveAndEndsTheSelection) {
        ChessGameModel model = BlackToMoveAfterE4();
        EXPECT_TRUE(Queue(model, "d2", "d4"));
        ASSERT_EQ(model.GetPremoves().size(), 1U);
        EXPECT_EQ(model.GetPremoves()[0], (Premove{Sq("d2"), Sq("d4")}));
        EXPECT_EQ(model.GetPremoveColor(), PieceColor::White);
        EXPECT_FALSE(model.HasSelection());
        EXPECT_TRUE(model.GetLegalMoves().empty());
        EXPECT_EQ(model.GetTurn(), PieceColor::Black) << "queueing moves nothing";
    }

    TEST(ChessGameModelTest, PremoveClickOnAnotherOwnPieceReselects) {
        ChessGameModel model = BlackToMoveAfterE4();
        PremoveClick(model, "d2");
        EXPECT_FALSE(PremoveClick(model, "g1"));
        ASSERT_TRUE(model.HasSelection());
        EXPECT_EQ(model.GetSelected(), Sq("g1"));
        EXPECT_TRUE(HasTarget(model, "f3"));
        EXPECT_TRUE(model.GetPremoves().empty());
    }

    TEST(ChessGameModelTest, PremoveClickElsewhereCancelsTheWholeQueue) {
        ChessGameModel model = BlackToMoveAfterE4();
        ASSERT_TRUE(Queue(model, "d2", "d4"));
        ASSERT_TRUE(Queue(model, "g1", "f3"));
        ASSERT_EQ(model.GetPremoves().size(), 2U);

        EXPECT_FALSE(PremoveClick(model, "a5")) << "an empty square with no selection";
        EXPECT_TRUE(model.GetPremoves().empty());
        EXPECT_FALSE(model.GetPremoveColor().has_value());

        ASSERT_TRUE(Queue(model, "d2", "d4"));
        PremoveClick(model, "b1");
        EXPECT_TRUE(model.HasSelection());
        EXPECT_FALSE(PremoveClick(model, "h5")) << "a square that is no target of the selection";
        EXPECT_FALSE(model.HasSelection());
        EXPECT_TRUE(model.GetPremoves().empty());
    }

    TEST(ChessGameModelTest, PremoveClickOnAnEnemyPieceCancelsToo) {
        ChessGameModel model = BlackToMoveAfterE4();
        ASSERT_TRUE(Queue(model, "d2", "d4"));
        EXPECT_FALSE(PremoveClick(model, "e7"));
        EXPECT_TRUE(model.GetPremoves().empty());
        EXPECT_FALSE(model.HasSelection());
    }

    TEST(ChessGameModelTest, PremoveClickIsRefusedForTheSideToMoveAndOutsideTheBoardAndAfterTheEnd) {
        ChessGameModel model;
        EXPECT_FALSE(PremoveClick(model, "e2")) << "White is the side to move";
        EXPECT_FALSE(model.HasSelection());

        ChessGameModel after = BlackToMoveAfterE4();
        EXPECT_FALSE(after.OnPremoveClicked(PieceColor::White, Square{8, 0}));
        EXPECT_FALSE(after.OnPremoveClicked(PieceColor::White, Square{-1, -1}));
        EXPECT_FALSE(after.HasSelection());
        EXPECT_FALSE(PremoveClick(after, "e7", PieceColor::Black)) << "Black is the side to move";
        EXPECT_FALSE(after.HasSelection());

        ChessGameModel mated = ModelFrom("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        ASSERT_TRUE(mated.IsGameOver());
        EXPECT_FALSE(PremoveClick(mated, "g6"));
        EXPECT_FALSE(mated.HasSelection());
    }

    TEST(ChessGameModelTest, TheSecondPremoveMovesThePieceTheFirstOneMoved) {
        ChessGameModel model = BlackToMoveAfterE4();
        ASSERT_TRUE(Queue(model, "g1", "f3"));
        EXPECT_FALSE(PremoveClick(model, "f3"));
        EXPECT_EQ(model.GetSelected(), Sq("f3"));
        EXPECT_TRUE(HasTarget(model, "e5"));
        EXPECT_TRUE(PremoveClick(model, "e5"));
        ASSERT_EQ(model.GetPremoves().size(), 2U);
        EXPECT_EQ(model.GetPremoves()[1], (Premove{Sq("f3"), Sq("e5")}));
        EXPECT_EQ(model.GetPieceAt(Sq("g1")).type, PieceType::Knight) << "the real board is as it was";
        EXPECT_TRUE(model.GetPieceAt(Sq("f3")).IsEmpty());
    }

    TEST(ChessGameModelTest, NoPremoveClickMovesTheVersion) {
        ChessGameModel model = BlackToMoveAfterE4();
        const uint64_t version = model.GetVersion();
        PremoveClick(model, "d2");
        PremoveClick(model, "g1");
        Queue(model, "d2", "d4");
        PremoveClick(model, "a5");
        model.ClearPremoves();
        EXPECT_EQ(model.GetVersion(), version);
    }

    TEST(ChessGameModelTest, PlayPremovePlaysTheFrontOneAndKeepsTheRest) {
        ChessGameModel model = BlackToMoveAfterE4();
        ASSERT_TRUE(Queue(model, "g1", "f3"));
        ASSERT_TRUE(Queue(model, "f3", "e5"));

        EXPECT_FALSE(model.PlayPremove()) << "White is not on the move yet";
        EXPECT_EQ(model.GetPremoves().size(), 2U);

        ASSERT_TRUE(Play(model, "e7e6"));
        const uint64_t version = model.GetVersion();
        EXPECT_TRUE(model.PlayPremove());
        EXPECT_EQ(model.GetVersion(), version + 1) << "the move it plays is a move";
        EXPECT_EQ(model.GetMoves().back(), Mv("g1f3"));
        EXPECT_EQ(model.GetTurn(), PieceColor::Black);
        ASSERT_EQ(model.GetPremoves().size(), 1U);
        EXPECT_EQ(model.GetPremoves()[0], (Premove{Sq("f3"), Sq("e5")}));
        EXPECT_EQ(model.GetPremoveColor(), PieceColor::White);

        EXPECT_FALSE(model.PlayPremove()) << "Black is on the move again";
        ASSERT_TRUE(Play(model, "d7d6"));
        EXPECT_TRUE(model.PlayPremove());
        EXPECT_EQ(model.GetMoves().back(), Mv("f3e5"));
        EXPECT_TRUE(model.GetPremoves().empty());
        EXPECT_FALSE(model.GetPremoveColor().has_value());
        EXPECT_FALSE(model.PlayPremove()) << "an empty queue plays nothing";
    }

    TEST(ChessGameModelTest, APremoveThatStaysLegalPlaysAsTheNextMove) {
        ChessGameModel model = ModelFrom("6k1/8/8/8/8/8/P3N3/4K3 b - - 0 1");
        ASSERT_TRUE(Queue(model, "e2", "g3"));
        ASSERT_TRUE(Play(model, "g8h8"));
        EXPECT_TRUE(model.PlayPremove());
        EXPECT_EQ(model.GetPieceAt(Sq("g3")).type, PieceType::Knight);
    }

    TEST(ChessGameModelTest, APremoveWhosePieceWasCapturedDropsTheWholeQueue) {
        ChessGameModel model = ModelFrom("r3k3/8/8/8/8/8/8/R3K3 b - - 0 1");
        ASSERT_TRUE(Queue(model, "a1", "a7"));
        ASSERT_TRUE(Queue(model, "e1", "e2"));
        ASSERT_TRUE(Play(model, "a8a1"));

        const uint64_t version = model.GetVersion();
        EXPECT_FALSE(model.PlayPremove());
        EXPECT_TRUE(model.GetPremoves().empty());
        EXPECT_FALSE(model.GetPremoveColor().has_value());
        EXPECT_EQ(model.GetVersion(), version) << "a dropped queue is no change of the game";
        EXPECT_EQ(model.GetMoves().size(), 1U);
    }

    TEST(ChessGameModelTest, APremoveThatWouldLeaveTheKingInCheckDropsTheWholeQueue) {
        // The black rook comes to the e file and pins the knight against the king
        ChessGameModel model = ModelFrom("r5k1/8/8/8/8/8/4N3/4K3 b - - 0 1");
        ASSERT_TRUE(Queue(model, "e2", "c3"));
        ASSERT_TRUE(Queue(model, "e1", "d1"));
        ASSERT_TRUE(Play(model, "a8e8"));

        const uint64_t version = model.GetVersion();
        EXPECT_FALSE(model.PlayPremove());
        EXPECT_TRUE(model.GetPremoves().empty());
        EXPECT_EQ(model.GetVersion(), version);
    }

    TEST(ChessGameModelTest, APremovedPromotionPlaysAsAQueen) {
        ChessGameModel model = ModelFrom("4k3/P7/8/8/8/8/8/4K3 b - - 0 1");
        ASSERT_TRUE(Queue(model, "a7", "a8"));
        ASSERT_TRUE(Play(model, "e8f7"));
        EXPECT_TRUE(model.PlayPremove());
        EXPECT_EQ(model.GetPieceAt(Sq("a8")).type, PieceType::Queen);
        EXPECT_EQ(model.GetPieceAt(Sq("a8")).color, PieceColor::White);
        EXPECT_TRUE(model.GetMoves().back().IsPromotion());
        EXPECT_EQ(model.GetMoves().back().PromotionType(), PieceType::Queen);
    }

    TEST(ChessGameModelTest, APremovedPawnDiagonalPlaysAsEnPassantWhenItIsLegal) {
        // White pawn on e5; Black answers d7d5, so exd6 is en passant when its turn comes
        ChessGameModel model = ModelFrom("4k3/3p4/8/4P3/8/8/8/4K3 b - - 0 1");
        ASSERT_TRUE(Queue(model, "e5", "d6"));
        ASSERT_TRUE(Play(model, "d7d5"));
        EXPECT_TRUE(model.PlayPremove());
        EXPECT_EQ(model.GetMoves().back().Kind(), MoveKind::EnPassant);
        EXPECT_TRUE(model.GetPieceAt(Sq("d5")).IsEmpty());
    }

    TEST(ChessGameModelTest, AMoveThatEndsTheGameDropsTheRestOfTheQueue) {
        // A back-rank mate by the premove, a second premove behind it
        ChessGameModel mate = ModelFrom("6k1/1p3ppp/8/8/8/8/8/R3K3 b - - 0 1");
        ASSERT_TRUE(Queue(mate, "a1", "a8"));
        ASSERT_TRUE(Queue(mate, "e1", "e2"));
        ASSERT_TRUE(Play(mate, "b7b6"));
        EXPECT_TRUE(mate.PlayPremove());
        EXPECT_EQ(mate.GetStatus(), GameStatus::Checkmate);
        EXPECT_TRUE(mate.GetPremoves().empty());
        EXPECT_FALSE(mate.GetPremoveColor().has_value());

        // The knight takes the last rook: a draw by insufficient material
        ChessGameModel draw = ModelFrom("4k3/8/8/8/8/1r6/8/N3K3 b - - 0 1");
        ASSERT_TRUE(Queue(draw, "a1", "b3"));
        ASSERT_TRUE(Queue(draw, "b3", "c5"));
        ASSERT_TRUE(Play(draw, "e8d8"));
        EXPECT_TRUE(draw.PlayPremove());
        EXPECT_EQ(draw.GetStatus(), GameStatus::DrawInsufficientMaterial);
        EXPECT_TRUE(draw.GetPremoves().empty());
    }

    TEST(ChessGameModelTest, TheQueueIsDroppedByResetSetPositionAndTakeBack) {
        ChessGameModel reset = BlackToMoveAfterE4();
        ASSERT_TRUE(Queue(reset, "d2", "d4"));
        reset.Reset();
        EXPECT_TRUE(reset.GetPremoves().empty());
        EXPECT_FALSE(reset.GetPremoveColor().has_value());

        ChessGameModel set = BlackToMoveAfterE4();
        ASSERT_TRUE(Queue(set, "d2", "d4"));
        set.SetPosition(PositionFromFen("6k1/8/8/8/8/8/4N3/4K3 b - - 0 1"));
        EXPECT_TRUE(set.GetPremoves().empty());
        EXPECT_FALSE(set.GetPremoveColor().has_value());

        ChessGameModel back = BlackToMoveAfterE4();
        ASSERT_TRUE(Queue(back, "d2", "d4"));
        ASSERT_EQ(back.TakeBack(), 1U);
        EXPECT_TRUE(back.GetPremoves().empty());
        EXPECT_FALSE(back.GetPremoveColor().has_value());
    }

    TEST(ChessGameModelTest, AMoveOfThePremoversColorThatDidNotComeFromTheQueueDropsIt) {
        ChessGameModel model = BlackToMoveAfterE4();
        ASSERT_TRUE(Queue(model, "g1", "f3"));
        ASSERT_TRUE(Play(model, "e7e5"));
        ASSERT_EQ(model.GetPremoves().size(), 1U) << "the other side's move keeps it";

        ASSERT_TRUE(Play(model, "d2d4")) << "a client played for the person";
        EXPECT_TRUE(model.GetPremoves().empty());
        EXPECT_FALSE(model.GetPremoveColor().has_value());
    }

    TEST(ChessGameModelTest, ClearSelectionKeepsTheQueue) {
        ChessGameModel model = BlackToMoveAfterE4();
        ASSERT_TRUE(Queue(model, "d2", "d4"));
        PremoveClick(model, "g1");
        model.ClearSelection();
        EXPECT_FALSE(model.HasSelection());
        EXPECT_EQ(model.GetPremoves().size(), 1U);
        EXPECT_EQ(model.GetPremoveColor(), PieceColor::White);
    }

    TEST(ChessGameModelTest, ARefusedMoveKeepsTheQueue) {
        ChessGameModel model = BlackToMoveAfterE4();
        ASSERT_TRUE(Queue(model, "d2", "d4"));
        EXPECT_FALSE(Play(model, "e7e3"));
        EXPECT_EQ(model.GetPremoves().size(), 1U);
    }

}  // namespace BECore::Chess::Tests
