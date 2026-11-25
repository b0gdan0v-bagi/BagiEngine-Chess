#include <Chess/ChessGame.h>
#include <Common/EngineEnvironment.h>
#include <tests/ChessTestJson.h>
#include <tests/ChessTestUtil.h>

/// The "editor" member of the control state and the editor's share of the state version, over a real ChessGame (the adapter is wired by it).
namespace BECore::Chess::Tests {

    namespace {

        class ChessGameControlEditorTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                ::BECore::Tests::EngineEnvironment::EnsureCoreServices();
            }

            DomControl Control() {
                return DomControl(*game.AsControllable());
            }

            Json State() {
                auto state = Control().GetState();
                EXPECT_TRUE(state.has_value());
                return state ? *state : Json();
            }

            /// The "editor" member as the state has it; the test fails when the member is missing
            Json EditorOf(const Json& state) {
                const Json* found = FindJsonMember(state, "editor");
                EXPECT_NE(found, nullptr) << "the member is there, null or not";
                return found ? *found : Json();
            }

            static String Text(const Json& json, StringView key) {
                const auto value = GetJsonString(json, key);
                return value ? String(value->c_str(), value->size()) : String("<missing>");
            }

            ChessGame game;
        };

    }  // namespace

    TEST_F(ChessGameControlEditorTest, TheEditorIsNullInAPlainGameAndTheObjectWhileEditing) {
        EXPECT_TRUE(EditorOf(State()).is_null());

        game.BeginEditing(EditStart::Empty);

        const Json state = State();
        const Json editor = EditorOf(state);
        ASSERT_TRUE(editor.is_object());
        EXPECT_EQ(Text(editor, "fen"), "8/8/8/8/8/8/8/8 w - - 0 1");
        EXPECT_EQ(Text(editor, "error"), "Needs one king each") << "the reason Play would give, for a position the rules refuse";
        const Json* tool = FindJsonMember(editor, "tool");
        ASSERT_NE(tool, nullptr);
        EXPECT_EQ(Text(*tool, "piece"), "pawn");
        EXPECT_EQ(Text(*tool, "color"), "white");
        EXPECT_EQ(Text(state, "fen"), String(Position::kStartFen.data(), Position::kStartFen.size())) << "the game's own position is not the draft";
    }

    TEST_F(ChessGameControlEditorTest, AValidDraftHasNoErrorAndTheEraserHasNoColor) {
        game.BeginEditing(EditStart::Start);
        game.GetEditor().SetTool(EditorTool{PieceType::None, PieceColor::Black});

        const Json editor = EditorOf(State());

        const Json* error = FindJsonMember(editor, "error");
        ASSERT_NE(error, nullptr);
        EXPECT_TRUE(error->is_null());
        EXPECT_EQ(Text(editor, "fen"), "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        const Json* tool = FindJsonMember(editor, "tool");
        ASSERT_NE(tool, nullptr);
        EXPECT_EQ(Text(*tool, "piece"), "erase");
        const Json* color = FindJsonMember(*tool, "color");
        ASSERT_NE(color, nullptr);
        EXPECT_TRUE(color->is_null());
    }

    TEST_F(ChessGameControlEditorTest, AnEditMovesTheVersionAndTheGameStaysWhatItWas) {
        game.BeginEditing(EditStart::Current);
        const uint64_t before = Control().GetStateVersion();
        const uint64_t gameVersion = game.GetModel().GetVersion();

        EXPECT_TRUE(game.GetEditor().Apply(Sq("e2")));
        game.EditChanged();

        EXPECT_GT(Control().GetStateVersion(), before);
        EXPECT_EQ(game.GetModel().GetVersion(), gameVersion);
        const Json state = State();
        EXPECT_EQ(Text(EditorOf(state), "fen"), "rnbqkbnr/pppppppp/8/8/8/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1");
        EXPECT_EQ(Text(state, "fen"), String(Position::kStartFen.data(), Position::kStartFen.size()));
    }

    TEST_F(ChessGameControlEditorTest, TheVersionNeverGoesDownWhenADraftEnds) {
        uint64_t last = Control().GetStateVersion();
        const auto expectNotBelow = [&](const char* what) {
            const uint64_t now = Control().GetStateVersion();
            EXPECT_GE(now, last) << what;
            last = now;
        };

        game.BeginEditing(EditStart::Current);
        expectNotBelow("begin");
        EXPECT_TRUE(game.GetEditor().Apply(Sq("e2")));
        game.EditChanged();
        expectNotBelow("edit");

        const uint64_t open = Control().GetStateVersion();
        game.CancelEditing();
        EXPECT_GT(Control().GetStateVersion(), open) << "the editor going from the state is a change a push has to report";
        EXPECT_TRUE(EditorOf(State()).is_null());
        expectNotBelow("cancel");

        game.BeginEditing(EditStart::Start);
        expectNotBelow("begin again");
        game.EditChanged();
        EXPECT_TRUE(game.PlayFromHere());
        expectNotBelow("play from here");
        EXPECT_TRUE(EditorOf(State()).is_null());
    }

    TEST_F(ChessGameControlEditorTest, ASetStateOfAFenEndsTheDraft) {
        game.BeginEditing(EditStart::Empty);
        ASSERT_TRUE(EditorOf(State()).is_object());
        Json request = Json::object();
        request["fen"] = MakeJsonString(StringView("8/8/8/8/8/8/8/K6k w - - 0 1"));

        const auto result = Control().SetState(request);

        ASSERT_TRUE(result.has_value()) << result.error().message.c_str();
        EXPECT_FALSE(game.IsEditing());
        const Json state = State();
        EXPECT_TRUE(EditorOf(state).is_null());
        EXPECT_EQ(Text(state, "fen"), "8/8/8/8/8/8/8/K6k w - - 0 1");
    }

    TEST_F(ChessGameControlEditorTest, AResetEndsTheDraft) {
        game.BeginEditing(EditStart::Start);

        EXPECT_TRUE(Control().Reset(Json::object()).has_value());

        EXPECT_TRUE(EditorOf(State()).is_null());
    }

    TEST_F(ChessGameControlEditorTest, EditingDoesNotChangeTheActionsOrTheMoves) {
        Json move = Json::object();
        move["type"] = "move";
        move["uci"] = "e2e4";
        ASSERT_TRUE(Control().PerformAction(move).has_value());
        const auto actionsBefore = Control().GetActions();
        ASSERT_TRUE(actionsBefore.has_value());

        game.BeginEditing(EditStart::Empty);

        const auto actionsDuring = Control().GetActions();
        ASSERT_TRUE(actionsDuring.has_value());
        EXPECT_EQ(actionsDuring->dump(), actionsBefore->dump()) << "list_actions is the game's, not the draft's";
        const Json state = State();
        const Json* moves = FindJsonMember(state, "moves");
        ASSERT_NE(moves, nullptr);
        EXPECT_EQ(moves->size(), 1u);
    }

}  // namespace BECore::Chess::Tests
