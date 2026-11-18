#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>
#include <tests/ChessTestUtil.h>

/// Showing a draft (ChessGameModel::ShowDraft()): the third set of handles the position editor draws, and that the game stays where it is.
namespace BECore::Chess::Tests {

    namespace {

        bool Play(ChessGameModel& model, StringView uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        constexpr Square kOffBoard{-1, -1};

        /// A handle for each piece of the opening position, registered at its start square, as the scene does
        class Handles {
        public:
            explicit Handles(ChessGameModel& model) : _model(model) {
                for (int sq = 0; sq < kSquareCount; ++sq) {
                    if (!Position::Start().PieceAt(sq).IsEmpty()) {
                        model.RegisterPiece(&_slots[static_cast<size_t>(sq)], Square::FromIndex(sq));
                        _starts.push_back(sq);
                    }
                }
            }

            [[nodiscard]] const void* At(StringView square) const {
                return &_slots[static_cast<size_t>(SqIdx(square))];
            }

            /// The squares the handles are shown on, in the order of their start squares; off the board is -1
            [[nodiscard]] Vector<int> ShownSquares() const {
                Vector<int> shown;
                for (const int start : _starts) {
                    const Square square = _model.GetShownSquareOf(&_slots[static_cast<size_t>(start)]);
                    shown.push_back(square.IsInside() ? square.Index() : -1);
                }
                return shown;
            }

            [[nodiscard]] int OnBoardCount() const {
                int count = 0;
                for (const int square : ShownSquares()) {
                    count += square >= 0 ? 1 : 0;
                }
                return count;
            }

        private:
            ChessGameModel& _model;
            Vector<int> _starts;
            Array<char, kSquareCount> _slots{};
        };

        ChessGameModel::DraftSquares SquaresOf(const Position& position) {
            ChessGameModel::DraftSquares squares{};
            for (int sq = 0; sq < kSquareCount; ++sq) {
                squares[static_cast<size_t>(sq)] = position.PieceAt(sq);
            }
            return squares;
        }

        const char* const kThreePieces = "4k3/8/8/8/8/8/P7/4K3 w - - 0 1";

    }  // namespace

    TEST(ChessGameModelDraftTest, ADraftPutsExactlyItsPiecesOnTheBoardInTheOrderOfTheDealOfSetPosition) {
        ChessGameModel model;
        const Handles handles(model);
        const Position draft = PositionFromFen(kThreePieces);
        model.ShowDraft(SquaresOf(draft));

        ASSERT_TRUE(model.IsDrafting());
        EXPECT_EQ(handles.OnBoardCount(), 3);

        ChessGameModel reference;
        const Handles referenceHandles(reference);
        reference.SetPosition(draft);
        EXPECT_EQ(handles.ShownSquares(), referenceHandles.ShownSquares()) << "the draft and SetPosition share the one deal";

        EXPECT_EQ(model.GetShownSquareOf(handles.At("a1")), Sq("e1")) << "the first white handle takes the first white piece";
        EXPECT_EQ(model.GetShownPieceOf(handles.At("a1")).type, PieceType::King);
        EXPECT_EQ(model.GetShownSquareOf(handles.At("b1")), Sq("a2"));
        EXPECT_EQ(model.GetShownPieceOf(handles.At("b1")).type, PieceType::Pawn);
        EXPECT_EQ(model.GetShownSquareOf(handles.At("c1")), kOffBoard);
    }

    TEST(ChessGameModelDraftTest, TheGameItselfStaysWhereItIs) {
        ChessGameModel model;
        const Handles handles(model);
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(Play(model, "d7d5"));
        const String gameFen = model.GetPosition().ToFen();
        const uint64_t version = model.GetVersion();
        const GameStatus status = model.GetStatus();

        model.ShowDraft(SquaresOf(PositionFromFen(kThreePieces)));

        EXPECT_EQ(model.GetVersion(), version) << "a draft is no change of the game";
        EXPECT_EQ(model.GetPosition().ToFen(), gameFen);
        EXPECT_EQ(model.GetMoves().size(), 2u);
        EXPECT_EQ(model.GetStatus(), status);
        EXPECT_EQ(model.GetStartFen(), Position::kStartFen);
        EXPECT_EQ(model.GetSquareOf(handles.At("e2")), Sq("e4")) << "the handles of the game do not move";
        EXPECT_EQ(model.GetSquareOf(handles.At("a1")), Sq("a1"));

        EXPECT_FALSE(model.IsViewing()) << "a draft is not a view";
        EXPECT_EQ(model.GetShownPosition().ToFen(), gameFen) << "the shown position stays the game's: the draft may be no legal position";
        EXPECT_EQ(model.GetShownMoveCount(), 2u);
    }

    TEST(ChessGameModelDraftTest, EndingTheDraftPutsTheShownPiecesBackWhereTheGameHasThem) {
        ChessGameModel model;
        const Handles handles(model);
        ASSERT_TRUE(Play(model, "e2e4"));
        model.ShowDraft(SquaresOf(PositionFromFen(kThreePieces)));
        ASSERT_EQ(model.GetShownSquareOf(handles.At("e2")), kOffBoard);

        EXPECT_TRUE(model.EndDraft());
        EXPECT_FALSE(model.IsDrafting());
        EXPECT_EQ(model.GetShownSquareOf(handles.At("e2")), Sq("e4"));
        EXPECT_EQ(handles.OnBoardCount(), 32);
        EXPECT_FALSE(model.EndDraft()) << "nothing to end";
    }

    TEST(ChessGameModelDraftTest, EveryChangeOfTheGameEndsTheDraft) {
        const auto drafted = [](ChessGameModel& model) { model.ShowDraft(SquaresOf(PositionFromFen(kThreePieces))); };

        ChessGameModel moved;
        drafted(moved);
        ASSERT_TRUE(Play(moved, "e2e4"));
        EXPECT_FALSE(moved.IsDrafting()) << "a move";

        ChessGameModel reset;
        drafted(reset);
        reset.Reset();
        EXPECT_FALSE(reset.IsDrafting()) << "Reset";

        ChessGameModel positioned;
        drafted(positioned);
        positioned.SetPosition(PositionFromFen(kThreePieces));
        EXPECT_FALSE(positioned.IsDrafting()) << "SetPosition";

        ChessGameModel takenBack;
        ASSERT_TRUE(Play(takenBack, "e2e4"));
        drafted(takenBack);
        ASSERT_EQ(takenBack.TakeBack(), 1u);
        EXPECT_FALSE(takenBack.IsDrafting()) << "TakeBack";

        ChessGameModel ended;
        drafted(ended);
        ASSERT_TRUE(ended.Resign(PieceColor::White).has_value());
        EXPECT_FALSE(ended.IsDrafting()) << "the end of the game";
    }

    TEST(ChessGameModelDraftTest, ATakeBackThatTakesNothingKeepsTheDraft) {
        ChessGameModel model;
        model.ShowDraft(SquaresOf(PositionFromFen(kThreePieces)));
        EXPECT_EQ(model.TakeBack(), 0u);
        EXPECT_TRUE(model.IsDrafting());
    }

    TEST(ChessGameModelDraftTest, ViewingAndTheClicksOfTheGameEndTheDraftAndDoNothingElse) {
        ChessGameModel viewing;
        ASSERT_TRUE(Play(viewing, "e2e4"));
        ASSERT_TRUE(Play(viewing, "e7e5"));
        viewing.ShowDraft(SquaresOf(PositionFromFen(kThreePieces)));
        EXPECT_TRUE(viewing.ShowMoves(1)) << "a view replaces the draft";
        EXPECT_FALSE(viewing.IsDrafting());
        EXPECT_TRUE(viewing.IsViewing());

        ChessGameModel back;
        ASSERT_TRUE(Play(back, "e2e4"));
        back.ShowDraft(SquaresOf(PositionFromFen(kThreePieces)));
        EXPECT_TRUE(back.EndViewing()) << "the game's own position again ends the draft";
        EXPECT_FALSE(back.IsDrafting());

        ChessGameModel clicked;
        clicked.ShowDraft(SquaresOf(PositionFromFen(kThreePieces)));
        EXPECT_FALSE(clicked.OnSquareClicked(Sq("e2")));
        EXPECT_FALSE(clicked.IsDrafting());
        EXPECT_FALSE(clicked.HasSelection()) << "the click that ended the draft selects nothing";

        ChessGameModel premoving;
        ASSERT_TRUE(Play(premoving, "e2e4"));
        premoving.ShowDraft(SquaresOf(PositionFromFen(kThreePieces)));
        EXPECT_FALSE(premoving.OnPremoveClicked(PieceColor::White, Sq("d2")));
        EXPECT_FALSE(premoving.IsDrafting());
        EXPECT_FALSE(premoving.HasSelection());
    }

    TEST(ChessGameModelDraftTest, ShowingADraftEndsAViewAndDropsTheSelectionAndAPendingPromotionButKeepsThePremoves) {
        ChessGameModel viewed;
        ASSERT_TRUE(Play(viewed, "e2e4"));
        ASSERT_TRUE(Play(viewed, "e7e5"));
        ASSERT_TRUE(viewed.ShowMoves(1));
        viewed.ShowDraft(SquaresOf(PositionFromFen(kThreePieces)));
        EXPECT_FALSE(viewed.IsViewing());
        EXPECT_TRUE(viewed.IsDrafting());

        ChessGameModel promoting;
        promoting.SetPosition(PositionFromFen("k7/4P3/8/8/8/8/8/K7 w - - 0 1"));
        promoting.OnSquareClicked(Sq("e7"));
        promoting.OnSquareClicked(Sq("e8"));
        ASSERT_TRUE(promoting.HasPendingPromotion());
        promoting.ShowDraft(SquaresOf(PositionFromFen(kThreePieces)));
        EXPECT_FALSE(promoting.HasPendingPromotion());
        EXPECT_FALSE(promoting.HasSelection());

        ChessGameModel premoving;
        ASSERT_TRUE(Play(premoving, "e2e4"));
        premoving.OnPremoveClicked(PieceColor::White, Sq("d2"));
        ASSERT_TRUE(premoving.OnPremoveClicked(PieceColor::White, Sq("d4")));
        premoving.ShowDraft(SquaresOf(PositionFromFen(kThreePieces)));
        EXPECT_EQ(premoving.GetPremoves().size(), 1u);
    }

    TEST(ChessGameModelDraftTest, ADraftWithSixteenPiecesOfEachColorPlacesAllThirtyTwoHandles) {
        ChessGameModel model;
        const Handles handles(model);
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(Play(model, "d7d5"));
        ASSERT_TRUE(Play(model, "e4d5"));

        model.ShowDraft(SquaresOf(Position::Start()));
        EXPECT_EQ(handles.OnBoardCount(), 32);
        EXPECT_EQ(model.GetShownSquareOf(handles.At("e2")), Sq("e2")) << "the pawn the game took on d5 stands at home in the draft";
        EXPECT_EQ(model.GetShownSquareOf(handles.At("d7")), Sq("d7"));
    }

    TEST(ChessGameModelDraftTest, AHandleRegisteredDuringADraftIsPlacedInIt) {
        ChessGameModel model;
        Array<char, 2> slots{};
        model.RegisterPiece(&slots[0], Sq("e1"));
        model.ShowDraft(SquaresOf(PositionFromFen(kThreePieces)));
        ASSERT_EQ(model.GetShownSquareOf(&slots[0]), Sq("e1")) << "the only white handle takes the first white piece";

        model.RegisterPiece(&slots[1], Sq("a1"));
        EXPECT_EQ(model.GetShownSquareOf(&slots[1]), Sq("e1")) << "the handles are dealt again, the one with the lower start square first";
        EXPECT_EQ(model.GetShownSquareOf(&slots[0]), Sq("a2"));
        EXPECT_EQ(model.GetSquareOf(&slots[1]), Sq("a1")) << "the game's own track is the start square";

        model.UnregisterPiece(&slots[0]);
        EXPECT_EQ(model.GetShownSquareOf(&slots[0]), kOffBoard);
        EXPECT_EQ(model.GetShownSquareOf(&slots[1]), Sq("e1"));
    }

}  // namespace BECore::Chess::Tests
