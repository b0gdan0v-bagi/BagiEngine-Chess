#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>
#include <tests/ChessTestUtil.h>

/// Viewing an earlier position of the game (ChessGameModel::ShowMoves()): what the board is shown, and that the game itself stays where it is.
namespace BECore::Chess::Tests {

    namespace {

        bool Play(ChessGameModel& model, StringView uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        bool PlayAll(ChessGameModel& model, std::initializer_list<const char*> moves) {
            for (const char* uci : moves) {
                if (!Play(model, uci)) {
                    return false;
                }
            }
            return true;
        }

        /// A handle for each piece of the opening position, registered at its start square, as the scene does
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

            [[nodiscard]] const void* At(StringView square) const {
                return &_slots[static_cast<size_t>(SqIdx(square))];
            }

        private:
            Array<char, kSquareCount> _slots{};
        };

        constexpr Square kOffBoard{-1, -1};

        // 1. e4 d5 2. exd5 Qxd5: a capture of each side
        const std::initializer_list<const char*> kTwoCaptures = {"e2e4", "d7d5", "e4d5", "d8d5"};

    }  // namespace

    TEST(ChessGameModelViewTest, AModelStartsShowingItsOwnGame) {
        ChessGameModel model;
        EXPECT_FALSE(model.IsViewing());
        EXPECT_EQ(model.GetShownMoveCount(), 0u);
        ASSERT_TRUE(PlayAll(model, {"e2e4"}));
        EXPECT_EQ(model.GetShownMoveCount(), 1u);
        EXPECT_EQ(model.GetShownPosition().ToFen(), model.GetPosition().ToFen());
    }

    TEST(ChessGameModelViewTest, ViewingShowsTheEarlierPositionAndLeavesTheGameAndItsVersionAlone) {
        ChessGameModel model;
        ASSERT_TRUE(PlayAll(model, kTwoCaptures));
        const String gameFen = model.GetPosition().ToFen();
        const uint64_t version = model.GetVersion();

        ChessGameModel twoMoves;
        ASSERT_TRUE(PlayAll(twoMoves, {"e2e4", "d7d5"}));

        EXPECT_TRUE(model.ShowMoves(2));
        EXPECT_TRUE(model.IsViewing());
        EXPECT_EQ(model.GetShownMoveCount(), 2u);
        EXPECT_EQ(model.GetShownPosition().ToFen(), twoMoves.GetPosition().ToFen());
        EXPECT_EQ(model.GetPosition().ToFen(), gameFen) << "the game stays";
        EXPECT_EQ(model.GetMoves().size(), 4u);
        EXPECT_EQ(model.GetVersion(), version) << "a view is no change of the game, like a selection";

        EXPECT_TRUE(model.ShowMoves(0));
        EXPECT_EQ(model.GetShownPosition().ToFen(), Position::Start().ToFen()) << "the position before the first move can be viewed";
        EXPECT_FALSE(model.ShowMoves(0)) << "the same view again changes nothing";
        EXPECT_EQ(model.GetVersion(), version);
    }

    TEST(ChessGameModelViewTest, TheLastMoveOrMoreIsTheGameItself) {
        ChessGameModel model;
        EXPECT_FALSE(model.ShowMoves(0)) << "a game with no move has nothing earlier";
        EXPECT_FALSE(model.IsViewing());
        ASSERT_TRUE(PlayAll(model, {"e2e4", "e7e5"}));
        EXPECT_FALSE(model.ShowMoves(2));
        EXPECT_FALSE(model.ShowMoves(7));
        EXPECT_FALSE(model.IsViewing());

        ASSERT_TRUE(model.ShowMoves(1));
        EXPECT_TRUE(model.ShowMoves(9)) << "past the end returns to the game";
        EXPECT_FALSE(model.IsViewing());
        ASSERT_TRUE(model.ShowMoves(1));
        EXPECT_TRUE(model.EndViewing());
        EXPECT_FALSE(model.IsViewing());
        EXPECT_FALSE(model.EndViewing()) << "nothing to end";
    }

    TEST(ChessGameModelViewTest, TheShownHandlesStandWhereTheEarlierPositionHasThemAndTheGameHandlesStay) {
        ChessGameModel model;
        const Handles handles(model);
        ASSERT_TRUE(PlayAll(model, kTwoCaptures));
        ASSERT_EQ(model.GetSquareOf(handles.At("e2")), kOffBoard) << "the queen took the pawn on d5";
        ASSERT_EQ(model.GetSquareOf(handles.At("d8")), Sq("d5"));

        ASSERT_TRUE(model.ShowMoves(3));
        EXPECT_EQ(model.GetShownSquareOf(handles.At("e2")), Sq("d5")) << "the pawn that took on d5 is back";
        EXPECT_EQ(model.GetShownSquareOf(handles.At("d8")), Sq("d8"));
        EXPECT_EQ(model.GetShownSquareOf(handles.At("d7")), kOffBoard) << "and the black pawn it took is gone";
        EXPECT_EQ(model.GetSquareOf(handles.At("e2")), kOffBoard) << "the handles of the game do not move";
        EXPECT_EQ(model.GetSquareOf(handles.At("d8")), Sq("d5"));

        ASSERT_TRUE(model.EndViewing());
        EXPECT_EQ(model.GetShownSquareOf(handles.At("e2")), kOffBoard);
        EXPECT_EQ(model.GetShownSquareOf(handles.At("d8")), Sq("d5"));
    }

    TEST(ChessGameModelViewTest, APawnThatPromotesLaterIsAPawnInTheView) {
        ChessGameModel model;
        const Handles handles(model);
        ASSERT_TRUE(PlayAll(model, {"h2h4", "g7g5", "h4g5", "h7h6", "g5h6", "f8g7", "h6h7", "e7e6", "h7g8q"}));
        ASSERT_EQ(model.GetPieceOf(handles.At("h2")).type, PieceType::Queen);

        ASSERT_TRUE(model.ShowMoves(8));
        EXPECT_EQ(model.GetShownPieceOf(handles.At("h2")).type, PieceType::Pawn);
        EXPECT_EQ(model.GetShownSquareOf(handles.At("h2")), Sq("h7"));
        EXPECT_EQ(model.GetShownSquareOf(handles.At("g8")), Sq("g8")) << "the knight it takes is still there";
        EXPECT_EQ(model.GetPieceOf(handles.At("h2")).type, PieceType::Queen);
    }

    TEST(ChessGameModelViewTest, StartingToViewDropsTheSelectionAndAPendingPromotionButKeepsThePremoves) {
        ChessGameModel model;
        model.SetPosition(PositionFromFen("k7/4P3/8/8/8/8/8/K7 w - - 0 1"));
        ASSERT_TRUE(PlayAll(model, {"a1a2", "a8b8"}));
        model.OnSquareClicked(Sq("e7"));
        model.OnSquareClicked(Sq("e8"));
        ASSERT_TRUE(model.HasPendingPromotion());
        ASSERT_TRUE(model.ShowMoves(1));
        EXPECT_FALSE(model.HasPendingPromotion());
        EXPECT_FALSE(model.HasSelection());

        ChessGameModel premoving;
        ASSERT_TRUE(PlayAll(premoving, {"e2e4"}));
        premoving.OnPremoveClicked(PieceColor::White, Sq("d2"));
        ASSERT_TRUE(premoving.OnPremoveClicked(PieceColor::White, Sq("d4")));
        ASSERT_TRUE(premoving.ShowMoves(0));
        EXPECT_EQ(premoving.GetPremoves().size(), 1u) << "the queue is no part of the game's position and waits for the program's answer";
    }

    TEST(ChessGameModelViewTest, EveryChangeOfTheGameReturnsToIt) {
        ChessGameModel model;
        ASSERT_TRUE(PlayAll(model, {"e2e4", "e7e5", "g1f3"}));

        ASSERT_TRUE(model.ShowMoves(1));
        ASSERT_TRUE(Play(model, "b8c6")) << "a move is played on the game, not on the view";
        EXPECT_FALSE(model.IsViewing());
        EXPECT_EQ(model.GetShownMoveCount(), 4u);

        ASSERT_TRUE(model.ShowMoves(1));
        EXPECT_EQ(model.TakeBack(1), 1u);
        EXPECT_FALSE(model.IsViewing());
        EXPECT_EQ(model.GetShownMoveCount(), 3u);

        ASSERT_TRUE(model.ShowMoves(1));
        model.SetPosition(PositionFromFen("k7/p7/8/8/8/8/P7/K7 w - - 0 1"));
        EXPECT_FALSE(model.IsViewing());

        ASSERT_TRUE(PlayAll(model, {"a2a3"}));
        ASSERT_TRUE(model.ShowMoves(0));
        model.Reset();
        EXPECT_FALSE(model.IsViewing());
        EXPECT_EQ(model.GetShownPosition().ToFen(), Position::Start().ToFen());
    }

    TEST(ChessGameModelViewTest, AClickWhileViewingOnlyReturnsToTheGame) {
        ChessGameModel model;
        ASSERT_TRUE(PlayAll(model, {"e2e4", "e7e5"}));
        const uint64_t version = model.GetVersion();

        ASSERT_TRUE(model.ShowMoves(0));
        EXPECT_FALSE(model.OnSquareClicked(Sq("g1")));
        EXPECT_FALSE(model.IsViewing());
        EXPECT_FALSE(model.HasSelection()) << "the click does not select on the game's board either";
        EXPECT_EQ(model.GetVersion(), version);

        model.OnSquareClicked(Sq("g1"));
        ASSERT_TRUE(model.HasSelection());
        ASSERT_TRUE(model.ShowMoves(1));
        EXPECT_FALSE(model.OnSquareClicked(Sq("f3"))) << "a target of the dropped selection plays nothing";
        EXPECT_EQ(model.GetMoves().size(), 2u);

        ASSERT_TRUE(model.ShowMoves(1));
        EXPECT_FALSE(model.OnPremoveClicked(PieceColor::Black, Sq("d7")));
        EXPECT_FALSE(model.IsViewing());
        EXPECT_FALSE(model.HasSelection());
    }

    TEST(ChessGameModelViewTest, APieceThatRegistersWhileViewingIsInTheView) {
        ChessGameModel model;
        ASSERT_TRUE(PlayAll(model, {"e2e4", "e7e5"}));
        ASSERT_TRUE(model.ShowMoves(1));
        char pawn = 0;
        model.RegisterPiece(&pawn, Sq("e7"));
        EXPECT_EQ(model.GetShownSquareOf(&pawn), Sq("e7"));
        EXPECT_EQ(model.GetSquareOf(&pawn), Sq("e5"));
        model.UnregisterPiece(&pawn);
        EXPECT_EQ(model.GetShownSquareOf(&pawn), kOffBoard);
    }

}  // namespace BECore::Chess::Tests
