#include <BECore/Control/Json.h>
#include <Chess/ChessGame.h>
#include <Chess/Game/EditorText.h>
#include <Common/EngineEnvironment.h>
#include <tests/ChessTestUtil.h>

/// ChessGame and its position editor, without a scene: the actions of the editor on a game, and what ends a draft.
namespace BECore::Chess::Tests {

    using namespace BECore::Tests;

    namespace {

        bool Play(ChessGame& game, StringView uci) {
            const auto move = MoveGenerator::ParseUci(game.GetModel().GetPosition(), uci);
            return move.has_value() && game.GetModel().TryMove(*move).has_value();
        }

        /// A handle for each piece of the opening position, as the scene registers them
        class Handles {
        public:
            explicit Handles(ChessGameModel& model) {
                for (int sq = 0; sq < kSquareCount; ++sq) {
                    if (!Position::Start().PieceAt(sq).IsEmpty()) {
                        model.RegisterPiece(&_slots[static_cast<size_t>(sq)], Square::FromIndex(sq));
                    }
                }
            }

            [[nodiscard]] const void* At(StringView square) const {
                return &_slots[static_cast<size_t>(SqIdx(square))];
            }

        private:
            Array<char, kSquareCount> _slots{};
        };

    }  // namespace

    class ChessGameEditingTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        ChessGame game;
    };

    TEST_F(ChessGameEditingTest, AGameIsNotEditedUntilItIsBegun) {
        EXPECT_FALSE(game.IsEditing());
        EXPECT_FALSE(game.PlayFromHere());
        EXPECT_TRUE(game.GetEditorMessage().empty());
        EXPECT_EQ(ChessGame::GetActiveEditor(), nullptr) << "no game is active";
    }

    TEST_F(ChessGameEditingTest, EachStartLoadsItsPosition) {
        ASSERT_TRUE(Play(game, "e2e4"));
        game.BeginEditing(EditStart::Current);
        EXPECT_TRUE(game.IsEditing());
        EXPECT_EQ(game.GetEditor().ToFen(), "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1") << "the pieces, the side and the rights; no en passant square, no clocks";

        game.BeginEditing(EditStart::Empty);
        EXPECT_EQ(game.GetEditor().ToFen(), "8/8/8/8/8/8/8/8 w - - 0 1");

        game.BeginEditing(EditStart::Start);
        EXPECT_EQ(game.GetEditor().ToFen(), Position::Start().ToFen());
    }

    TEST_F(ChessGameEditingTest, AFinishedGameAndAViewedOneCanBeEdited) {
        ASSERT_TRUE(Play(game, "f2f3"));
        ASSERT_TRUE(Play(game, "e7e5"));
        ASSERT_TRUE(Play(game, "g2g4"));
        ASSERT_TRUE(Play(game, "d8h4"));
        ASSERT_TRUE(game.GetModel().IsGameOver());
        game.BeginEditing(EditStart::Current);
        EXPECT_TRUE(game.IsEditing());
        EXPECT_TRUE(game.GetModel().IsGameOver()) << "the game itself stays what it was";

        game.CancelEditing();
        ASSERT_TRUE(game.GetModel().TakeBack(2) == 2u);
        ASSERT_TRUE(game.GetModel().ShowMoves(1));
        game.BeginEditing(EditStart::Current);
        EXPECT_TRUE(game.IsEditing());
        EXPECT_FALSE(game.GetModel().IsViewing());
        EXPECT_EQ(game.GetEditor().ToFen(), "rnbqkbnr/pppp1ppp/8/4p3/8/5P2/PPPPP1PP/RNBQKBNR w KQkq - 0 1") << "the game's position, not the one that was viewed";
    }

    TEST_F(ChessGameEditingTest, AChangeOfTheDraftIsShownOnlyWhenEditChangedIsCalled) {
        const Handles handles(game.GetModel());
        game.BeginEditing(EditStart::Start);
        ASSERT_EQ(game.GetModel().GetShownSquareOf(handles.At("e2")), Sq("e2"));

        ASSERT_TRUE(game.GetEditor().Apply(Sq("e2")));  // the pawn is taken off
        EXPECT_EQ(game.GetModel().GetShownSquareOf(handles.At("e2")), Sq("e2")) << "nothing is dealt per edit";
        game.EditChanged();
        EXPECT_NE(game.GetModel().GetShownSquareOf(handles.At("e2")), Sq("e2"));

        game.CancelEditing();
        game.EditChanged();
        EXPECT_FALSE(game.IsEditing()) << "EditChanged does not start an editor";
    }

    TEST_F(ChessGameEditingTest, PlayFromHereStartsAGameFromTheDraft) {
        const Handles handles(game.GetModel());
        ASSERT_TRUE(Play(game, "e2e4"));
        ASSERT_TRUE(Play(game, "e7e5"));
        const uint64_t version = game.GetModel().GetVersion();
        game.BeginEditing(EditStart::Current);
        ASSERT_TRUE(game.GetEditor().Apply(Sq("h2")));  // the same piece again takes it off; a 17th white piece would be refused
        game.GetEditor().SetSideToMove(PieceColor::Black);
        game.EditChanged();
        const String draftFen = game.GetEditor().ToFen();

        ASSERT_TRUE(game.PlayFromHere());

        EXPECT_FALSE(game.IsEditing());
        EXPECT_EQ(game.GetModel().GetStartFen(), draftFen);
        EXPECT_EQ(game.GetModel().GetPosition().ToFen(), draftFen);
        EXPECT_TRUE(game.GetModel().GetMoves().empty());
        EXPECT_GT(game.GetModel().GetVersion(), version) << "an ordinary new game: the opponent and the save see it";
        EXPECT_EQ(game.GetModel().GetSquareOf(handles.At("a1")), Sq("a1")) << "the handles are dealt to the new position";
        EXPECT_EQ(game.GetModel().GetShownSquareOf(handles.At("a1")), Sq("a1"));
    }

    TEST_F(ChessGameEditingTest, ARefusedDraftStaysWithItsReasonAndTheGameIsUntouched) {
        ASSERT_TRUE(Play(game, "e2e4"));
        const uint64_t version = game.GetModel().GetVersion();
        game.BeginEditing(EditStart::Empty);

        EXPECT_FALSE(game.PlayFromHere());

        EXPECT_TRUE(game.IsEditing());
        EXPECT_EQ(game.GetEditorMessage(), EditorText::Refusal(FenError::BadKingCount));
        EXPECT_EQ(game.GetModel().GetVersion(), version);
        EXPECT_EQ(game.GetModel().GetMoves().size(), 1u);

        game.GetEditor().Apply(Sq("e1"));
        game.EditChanged();
        EXPECT_TRUE(game.GetEditorMessage().empty()) << "any change of the draft drops the reason";
    }

    TEST_F(ChessGameEditingTest, CancellingLeavesTheGameExactlyAsItWas) {
        const Handles handles(game.GetModel());
        ASSERT_TRUE(Play(game, "e2e4"));
        ASSERT_TRUE(Play(game, "d7d5"));
        const String fen = game.GetModel().GetPosition().ToFen();
        const uint64_t version = game.GetModel().GetVersion();
        const GameStatus status = game.GetModel().GetStatus();

        game.BeginEditing(EditStart::Current);
        ASSERT_TRUE(game.GetEditor().Apply(Sq("a2")));
        game.EditChanged();
        game.CancelEditing();

        EXPECT_FALSE(game.IsEditing());
        EXPECT_EQ(game.GetModel().GetPosition().ToFen(), fen);
        EXPECT_EQ(game.GetModel().GetMoves().size(), 2u);
        EXPECT_EQ(game.GetModel().GetStatus(), status);
        EXPECT_EQ(game.GetModel().GetVersion(), version);
        EXPECT_EQ(game.GetModel().GetShownSquareOf(handles.At("e2")), Sq("e4"));
    }

    TEST_F(ChessGameEditingTest, AResetAndAFenOfTheControlApiEndTheDraft) {
        game.BeginEditing(EditStart::Empty);
        game.Reset();
        EXPECT_FALSE(game.IsEditing());

        game.BeginEditing(EditStart::Empty);
        Json request = Json::object();
        request["fen"] = MakeJsonString("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
        ASSERT_TRUE(game.AsControllable()->SetState(request).has_value());
        EXPECT_FALSE(game.IsEditing());
        EXPECT_EQ(game.GetModel().GetPosition().ToFen(), "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
    }

}  // namespace BECore::Chess::Tests
