#include <Chess/Components/AnalysisRows.h>
#include <Chess/Game/EditorText.h>
#include <tests/ChessUiHarness.h>
#include <tests/ShippedOpenings.h>

/// The board, the pieces and the readers while a position is edited (ChessGame::BeginEditing()): real clicks put and take off pieces on the drawn squares, and nothing of the game is
/// marked, moved or counted on the draft. The menu section and the bar that start and drive the editor are other files; here the editor is begun in code. The scene, the helpers and the
/// fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        constexpr int kWaitingFrames = 100;  // frames in which a program that wrongly took the turn would have answered at once (InstantEngine)
        constexpr const char* kQueenOfWhite = "WhiteQueen";
        constexpr int kPawnsOfOneColor = 16;

        SDL_MouseButtonEvent RightButton(float x, float y) {
            SDL_MouseButtonEvent event = Button(x, y, true);
            event.button = SDL_BUTTON_RIGHT;
            return event;
        }

        /// The piece node that the model shows on @p square, null when none is there
        const ChessPieceComponent* NodeShownAt(Scene& scene, const ChessGameModel& model, Square square) {
            for (const auto& node : scene.GetRootNode().GetChildren()) {
                const auto piece = node->GetComponent<ChessPieceComponent>();
                if (piece && model.GetShownSquareOf(piece.Get()) == square) {
                    return piece.Get();
                }
            }
            return nullptr;
        }

        /// How many piece nodes the model shows on the board
        size_t ShownPieceCount(Scene& scene, const ChessGameModel& model) {
            size_t count = 0;
            for (const auto& node : scene.GetRootNode().GetChildren()) {
                const auto piece = node->GetComponent<ChessPieceComponent>();
                if (piece && model.GetShownSquareOf(piece.Get()).IsInside()) {
                    ++count;
                }
            }
            return count;
        }

    }  // namespace

    /// The fixture of the cases below: the editor is begun and the tool is picked in code
    class ChessUiEditorBoardTest : public ChessUiTest {
    protected:
        ChessGame& Game() const {
            ChessGame* game = ChessGame::GetActive();
            EXPECT_NE(game, nullptr);
            return *game;
        }

        PositionEditor& Editor() const {
            return Game().GetEditor();
        }

        void BeginOn(EditStart start) {
            Game().BeginEditing(start);
            _manager->UpdateAll();
        }

        void PickTool(PieceType type, PieceColor color = PieceColor::White) {
            Editor().SetTool(EditorTool{type, color});
        }

        /// A press of the right button on the middle of a square, where it is drawn
        void RightClick(const char* square) {
            Draw();
            const auto at = SquareCenter(square);
            CoreManager::GetUIInput().DispatchMouseButtonDown(RightButton(at.first, at.second));
        }

        /// Where the transform of the piece node standing on @p square (as the draft shows it) is: the cell of that square in the orientation drawn
        void ExpectNodeOnItsCell(const char* square) {
            const ChessPieceComponent* piece = NodeShownAt(GetScene(), Model(), Sq(square));
            ASSERT_NE(piece, nullptr) << square;
            const auto transform = piece->GetNode().GetComponent<TransformComponent>();
            ASSERT_TRUE(transform) << square;
            const auto [column, row] = CellOfSquare(Sq(square), BlackIsAtTheBottom());
            const auto [x, y] = CellOrigin(column, row);
            EXPECT_NEAR(transform->_x, x, kPixelTolerance) << square;
            EXPECT_NEAR(transform->_y, y, kPixelTolerance) << square;
        }
    };

    TEST_F(ChessUiEditorBoardTest, ALeftClickPutsThePickedPieceOnTheClickedSquareAndTheNodeStandsThere) {
        BeginOn(EditStart::Empty);
        PickTool(PieceType::Queen);
        const uint64_t version = Model().GetVersion();

        Click("e4");
        _manager->UpdateAll();

        EXPECT_EQ(Editor().GetPieceAt(Sq("e4")), (Piece{PieceType::Queen, PieceColor::White}));
        EXPECT_EQ(ShownPieceCount(GetScene(), Model()), 1u);
        ExpectNodeOnItsCell("e4");
        const ChessPieceComponent* node = NodeShownAt(GetScene(), Model(), Sq("e4"));
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->GetNode().GetComponent<SpriteRendererComponent>()->_spriteId, PoolString::Intern(kQueenOfWhite)) << "the node of a pawn becomes the queen";
        EXPECT_EQ(Model().GetVersion(), version) << "the game is untouched";
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_FALSE(Model().HasSelection());

        PickTool(PieceType::Rook, PieceColor::Black);
        Click("a8");
        _manager->UpdateAll();
        EXPECT_EQ(Editor().GetPieceAt(Sq("a8")), (Piece{PieceType::Rook, PieceColor::Black}));
        EXPECT_EQ(ShownPieceCount(GetScene(), Model()), 2u);
        ExpectNodeOnItsCell("a8");
        ExpectNodeOnItsCell("e4");
    }

    TEST_F(ChessUiEditorBoardTest, ARightClickTakesThePieceOffAndSoDoesTheSamePieceAgainOrTheEraser) {
        BeginOn(EditStart::Start);
        ASSERT_EQ(ShownPieceCount(GetScene(), Model()), 32u);

        RightClick("e2");
        _manager->UpdateAll();
        EXPECT_TRUE(Editor().GetPieceAt(Sq("e2")).IsEmpty());
        EXPECT_EQ(ShownPieceCount(GetScene(), Model()), 31u);
        EXPECT_EQ(NodeShownAt(GetScene(), Model(), Sq("e2")), nullptr);

        PickTool(PieceType::Knight);
        Click("b1");
        _manager->UpdateAll();
        EXPECT_TRUE(Editor().GetPieceAt(Sq("b1")).IsEmpty()) << "the knight that stood there is taken off by the same tool";

        Editor().SetTool(EditorTool{PieceType::None, PieceColor::White});
        Click("g8");
        _manager->UpdateAll();
        EXPECT_TRUE(Editor().GetPieceAt(Sq("g8")).IsEmpty());
        EXPECT_EQ(ShownPieceCount(GetScene(), Model()), 29u);
        ExpectNodeOnItsCell("a1");
        ExpectNodeOnItsCell("h8");
        EXPECT_TRUE(Game().IsEditing());
    }

    TEST_F(ChessUiEditorBoardTest, WithBlackAtTheBottomAClickPutsThePieceOnTheSquareThePersonSees) {
        ControlPlays("white");
        ASSERT_TRUE(BlackIsAtTheBottom());
        BeginOn(EditStart::Empty);
        PickTool(PieceType::Knight, PieceColor::Black);

        ClickCell(0, 0);  // the top left cell of the turned board is h1
        _manager->UpdateAll();

        EXPECT_EQ(Editor().GetPieceAt(Sq("h1")), (Piece{PieceType::Knight, PieceColor::Black}));
        ExpectNodeOnItsCell("h1");
        ClickCell(7, 7);  // the bottom right one is a8
        EXPECT_EQ(Editor().GetPieceAt(Sq("a8")), (Piece{PieceType::Knight, PieceColor::Black}));
    }

    TEST_F(ChessUiEditorBoardTest, NothingOfTheGameIsMarkedOrSelectedOnTheDraft) {
        ASSERT_TRUE(PlayAll(Model(), {"f2f3", "e7e5", "g2g4", "d8h4"}));
        ASSERT_EQ(Model().GetStatus(), GameStatus::Checkmate);
        Draw();
        ASSERT_FALSE(LastMoveFills().empty());
        ASSERT_FALSE(RectCalls(kMateColor).empty());
        ASSERT_FALSE(RectCalls(kBoardDimColor).empty()) << "the result dims the finished game";

        BeginOn(EditStart::Current);
        Click("e1");
        Draw();

        EXPECT_FALSE(Model().HasSelection());
        EXPECT_TRUE(LastMoveFills().empty());
        EXPECT_TRUE(RectCalls(kMateColor).empty());
        EXPECT_TRUE(RectCalls(kCheckColor).empty());
        EXPECT_TRUE(RectCalls(kSelectedColor).empty());
        EXPECT_TRUE(RectCalls(kLegalMoveColor).empty());
        EXPECT_TRUE(RectCalls(kBoardDimColor).empty()) << "the finished game's dim and bar are not drawn over the draft";
        EXPECT_EQ(RectCalls(kLightSquareColor).size(), 32u) << "the squares are";
        EXPECT_EQ(RectCalls(kDarkSquareColor).size(), 32u);

        Game().CancelEditing();
        Draw();
        EXPECT_FALSE(RectCalls(kBoardDimColor).empty()) << "and the game's result comes back at once";
    }

    TEST_F(ChessUiEditorBoardTest, AClickOnAPieceNeverSelectsOrArmsADragWhileEditing) {
        BeginOn(EditStart::Start);
        PickTool(PieceType::Pawn);
        Draw();
        const auto from = SquareCenter("e2");
        const auto to = SquareCenter("e4");

        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(from.first, from.second, true));
        MouseMove(from.first + kDragStepPixels, from.second, true);
        MouseMove(to.first, to.second, true);
        Release(to.first, to.second);
        _manager->UpdateAll();

        EXPECT_FALSE(Model().HasSelection());
        EXPECT_FALSE(Lifted().has_value());
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_TRUE(Editor().GetPieceAt(Sq("e2")).IsEmpty()) << "the click took the pawn that was there off";
        EXPECT_TRUE(Editor().GetPieceAt(Sq("e4")).IsEmpty()) << "and the release moved nothing";
    }

    TEST_F(ChessUiEditorBoardTest, TheProgramWaitsWhileEditingAndAnswersAfterPlayFromHere) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(Play(Model(), "e2e4"));
        BeginOn(EditStart::Current);

        IdleFrames(kWaitingFrames);
        EXPECT_EQ(Model().GetMoves().size(), 1u) << "the program had the move and did not play it";
        EXPECT_TRUE(Game().IsEditing());

        ASSERT_TRUE(Game().PlayFromHere());
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 1; })) << "the program answers in the new game";
        EXPECT_EQ(Model().GetTurn(), PieceColor::White);
    }

    TEST_F(ChessUiEditorBoardTest, TheHudSaysEditingOrTheReasonAndNamesNeitherTheTurnNorTheOpening) {
        Game().SetOpenings(ShippedOpeningBook());
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        EXPECT_NE(Var("turn"), "Editing");
        EXPECT_FALSE(Var("opening").empty());

        BeginOn(EditStart::Current);
        EXPECT_EQ(Var("turn"), "Editing");
        EXPECT_TRUE(Var("opening").empty());
        EXPECT_TRUE(Var("status").empty());

        Editor().Clear();
        Game().EditChanged();
        EXPECT_FALSE(Game().PlayFromHere());
        EXPECT_FALSE(Game().GetEditorMessage().empty());
        EXPECT_EQ(Var("turn"), String(Game().GetEditorMessage())) << "the reason of a refused Play from here, with no step of any version";

        PickTool(PieceType::King);
        Click("e1");  // an edit takes the message of the refused Play from here away
        EXPECT_EQ(Var("turn"), "Editing");

        for (const char* square : {"a2", "b2", "c2", "d2", "e2", "f2", "g2", "h2", "a3", "b3", "c3", "d3", "e3", "f3", "g3"}) {
            ASSERT_TRUE(Editor().Put(Sq(square), Piece{PieceType::Pawn, PieceColor::White}).has_value()) << square;
        }
        ASSERT_EQ(Editor().CountPieces(PieceColor::White), kPawnsOfOneColor);
        Game().EditChanged();
        PickTool(PieceType::Pawn);
        Click("h3");
        EXPECT_EQ(Var("turn"), String(EditorText::Refusal(EditRefusal::TooManyPieces))) << "the editor's own refusal shows too";

        Game().CancelEditing();
        EXPECT_EQ(Var("turn"), "White to move");
        EXPECT_FALSE(Var("opening").empty()) << "and the game's line is back";
    }

    TEST_F(ChessUiEditorBoardTest, TheMoveListAndTheCapturedMaterialAreHiddenWhileEditingAndBackAfter) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "d7d5", "e4d5"}));
        Settle();
        const UIElement& box = MovePanelElement("MovePanelBox");
        ASSERT_TRUE(box.active.visible) << "the panel is shown in the Wide window of the fixture";
        ASSERT_TRUE(RowShown("Bottom", "Pawn")) << "White has taken a pawn";

        BeginOn(EditStart::Current);
        Settle();
        EXPECT_FALSE(box.active.visible);
        EXPECT_FALSE(RowShown("Bottom", "Pawn"));
        for (const char* group : kMaterialGroups) {
            for (const char* set : kMaterialSets) {
                EXPECT_FALSE(MaterialElement(Format("Material{}{}List", set, group).c_str()).active.visible) << set << group;
                EXPECT_FALSE(MaterialElement(Format("Material{}{}Balance", set, group).c_str()).active.visible) << set << group;
            }
        }

        Game().CancelEditing();
        Settle();
        EXPECT_TRUE(box.active.visible);
        EXPECT_TRUE(RowShown("Bottom", "Pawn"));
    }

    TEST_F(ChessUiEditorBoardTest, ABoardOutsideTheEditorPlaysAsBefore) {
        BeginOn(EditStart::Current);
        Game().CancelEditing();
        _manager->UpdateAll();

        ClickMove("e2", "e4");
        EXPECT_EQ(Model().GetMoves().size(), 1u);
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(false);
    }

    TEST_F(ChessUiEditorBoardTest, APressOnTheMoveListOrTheHintWhileEditingLeavesTheDraftAndTheVersionAlone) {
        ASSERT_TRUE(Play(Model(), "e2e4"));
        ASSERT_TRUE(Play(Model(), "e7e5"));
        BeginOn(EditStart::Current);
        RightClick("a2");  // a full side takes no further piece, so the draft changes by taking one off
        _manager->UpdateAll();
        Draw();  // the rows of the move list are the words of this frame
        const uint64_t version = Model().GetVersion();
        const uint64_t revision = Editor().GetRevision();
        ASSERT_TRUE(Editor().GetPieceAt(Sq("a2")).IsEmpty());

        // The cells of the Moves section (reachable from the menu while editing) emit the same action as the panel's
        PressWith("ChessShowMove", "white0");
        PressWith("ChessShowMove", "back");
        PressWith("ChessShowMove", "latest");
        Press("ChessHint");
        _manager->UpdateAll();

        EXPECT_TRUE(Game().IsEditing()) << "the contract: a draft ends with a step of the version or by Cancel and Play";
        EXPECT_TRUE(Model().IsDrafting());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_EQ(Editor().GetRevision(), revision);
        EXPECT_TRUE(Editor().GetPieceAt(Sq("a2")).IsEmpty());
        EXPECT_FALSE(Model().IsViewing());
        EXPECT_EQ(Model().GetMoves().size(), 2u);
        EXPECT_FALSE(HintAllowedNow(&Model())) << "the button is grey while a position is edited";

        Game().CancelEditing();
        _manager->UpdateAll();
        EXPECT_TRUE(HintAllowedNow(&Model()));
    }

}  // namespace BECore::Chess::Tests
