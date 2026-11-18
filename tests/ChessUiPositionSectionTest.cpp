#include <Chess/Game/EditorText.h>
#include <tests/ChessUiHarness.h>

/// The menu section "Position": the node PositionSection (config/nodes/ChessPositionSection.xml) that begins, sets up, plays and cancels the editing of a position. The scene, the helpers and the
/// fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kSectionNode = "PositionSection";
        constexpr const char* kSectionPlate = "PositionSectionPlate";
        constexpr float kMaxPlateHeight = 560.0f;  // every view's safe area holds the plate, and the row of two columns keeps it this low

        /// Every control of the section that a click acts on, by name
        constexpr const char* kSectionControls[] = {"PositionSectionCurrent",    "PositionSectionEmpty",      "PositionSectionStart",       "PositionSectionSideRow",
                                                    "PositionSectionCastleKRow", "PositionSectionCastleQRow", "PositionSectionCastleKbRow", "PositionSectionCastleQbRow",
                                                    "PositionSectionPlay",       "PositionSectionCancel",     "PositionSectionBack"};

        /// The longest line the editor can say: the panel's own line must hold it
        String LongestMessage() {
            return String(EditorText::kMaxRefusalChars, 'W');
        }

        String Var(const char* name) {
            const StringView value = CoreManager::GetUIVars().Get(PoolString::Intern(name));
            return String(value.data(), value.size());
        }

    }  // namespace

    TEST_F(ChessUiTest, ThePositionSectionPassesTheSizeMatrixWithEveryWord) {
        Draw();
        auto canvas = CanvasOf(kSectionNode);
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        CoreManager::GetUIVars().Set("editorMessage"_intern, StringView(LongestMessage()));

        const auto failures = ValidateWithWords(canvas->GetLayout());
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        const String problems = PanelProblemsOnMatrix(canvas->GetLayout(), kReportedCases, kSectionPlate);
        EXPECT_TRUE(problems.empty()) << problems.c_str();
        EXPECT_LE(ElementOf(kSectionNode, kSectionPlate).rect.h, kMaxPlateHeight + kPixelTolerance);
    }

    class ChessUiPositionSectionTest : public ChessUiTest {
    protected:
        ChessGame& Game() const {
            ChessGame* game = ChessGame::GetActive();
            EXPECT_NE(game, nullptr);
            return *game;
        }

        PositionEditor& Editor() const {
            return Game().GetEditor();
        }

        IntrusivePtr<UICanvasComponent> Section() const {
            return CanvasOf(kSectionNode);
        }

        Taken ClickSection(const char* name) {
            return ClickControl(ElementOf(kSectionNode, name));
        }

        /// The Menu button of the HUD and then the Position button of the list, clicked where they are drawn; whether the section is shown afterwards
        bool OpenTheSection() {
            if (!ClickButton(kMenuColor) || !MenuList() || !MenuList()->_visible || !ClickMenuList("MenuSectionPosition").Both()) {
                return false;
            }
            return Section() && Section()->_visible;
        }

        bool MenuIsClosed() const {
            return !MenuList()->_visible && !Section()->_visible;
        }

        bool IsGreyed(const char* name) const {
            return ClickerOf(ElementOf(kSectionNode, name))->disabled;
        }

        void PutPiece(const char* square, PieceType type, PieceColor color) {
            ASSERT_TRUE(Editor().Put(Sq(square), Piece{type, color}).has_value());
        }
    };

    TEST_F(ChessUiPositionSectionTest, EveryControlIsATouchTargetAndNoWordIsCutInTheNarrowestWindow) {
        auto canvas = Section();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = canvas->GetLayout();
        CoreManager::GetUIVars().Set("editorMessage"_intern, StringView(LongestMessage()));
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;

        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            LayOutInTheNarrowestWindow(layout, shape);
            const String problems = PanelProblems(layout, kSectionPlate);
            EXPECT_TRUE(problems.empty()) << problems.c_str();
            for (const char* name : kSectionControls) {
                const UIElement& control = ElementOf(kSectionNode, name);
                EXPECT_GE(control.rect.h, kSegmentHeight - kPixelTolerance) << name;
                EXPECT_GE(std::min(control.rect.w, control.rect.h) * layout.GetResolved().scale, minTouch - kPixelTolerance) << name << " is a touch target on the smallest window";
            }
        }
        layout.Invalidate();
    }

    TEST_F(ChessUiPositionSectionTest, ThePositionSectionIsAHiddenModalCanvasOfTheMenuWithNamesOfItsOwn) {
        auto canvas = Section();
        ASSERT_TRUE(canvas);
        EXPECT_FALSE(canvas->_visible);
        EXPECT_TRUE(canvas->_modal);
        EXPECT_EQ(canvas->_space, CanvasSpace::SceneView);
        EXPECT_TRUE(canvas->_useSafeArea);

        Vector<PoolString> names;
        CollectNames(canvas->GetRoot(), names);
        ASSERT_FALSE(names.empty());
        for (const PoolString& name : names) {
            EXPECT_TRUE(name.ToStringView().starts_with(kSectionNode)) << name.CStr() << ": elements are found by name in every canvas";
        }
        for (const char* other : {"Hud", "BoardCoordinates", "Material", "MovePanel", "MenuPanel", "OpponentPanel", "MovesSection", "GameSection", "SoundPanel", "PromotionPanel"}) {
            const auto otherCanvas = CanvasOf(other);
            ASSERT_TRUE(otherCanvas) << other;
            for (const PoolString& name : names) {
                EXPECT_EQ(otherCanvas->GetRoot().FindByName(name), nullptr) << name.CStr() << " is also in " << other;
            }
        }
        EXPECT_EQ(ClickerOf(ElementOf(kSectionNode, "PositionSectionBack"))->action, "ChessOpenMenu"_intern);
        EXPECT_EQ(ClickerOf(ElementOf(kSectionNode, "PositionSectionDim"))->action, "ChessCloseMenu"_intern);
        EXPECT_EQ(canvas->GetRoot().children.front()->name, "PositionSectionDim"_intern) << "the first child, so a click on the plate never reaches it";
    }

    TEST_F(ChessUiPositionSectionTest, TheMenuOpensThePositionSectionAndBackAndTheDimLeaveIt) {
        for (const char* way : {"PositionSectionBack", "PositionSectionDim"}) {
            SCOPED_TRACE(way);
            ASSERT_TRUE(OpenTheSection());
            EXPECT_FALSE(MenuList()->_visible);

            const bool dim = StringView(way) == "PositionSectionDim";
            const auto corner = DimCorner(kSectionNode, way);
            const bool clicked = dim ? ClickAtPixel(corner.first, corner.second).Both() : ClickSection(way).Both();
            ASSERT_TRUE(clicked);
            EXPECT_FALSE(Section()->_visible);
            EXPECT_EQ(MenuList()->_visible, !dim) << "Back returns to the list, the dim closes all";
            if (MenuList()->_visible) {
                ASSERT_TRUE(ClickMenuList("MenuClose").Both());
            }
        }
    }

    TEST_F(ChessUiPositionSectionTest, NothingThatNeedsAnEditorIsOfferedWhileNoPositionIsEdited) {
        ASSERT_TRUE(OpenTheSection());
        Draw();
        for (const char* name : {"PositionSectionSideRow", "PositionSectionCastleKRow", "PositionSectionCastleQRow", "PositionSectionCastleKbRow", "PositionSectionCastleQbRow", "PositionSectionPlay",
                                 "PositionSectionCancel"}) {
            EXPECT_TRUE(IsGreyed(name)) << name;
        }
        for (const char* name : {"PositionSectionCurrent", "PositionSectionEmpty", "PositionSectionStart", "PositionSectionBack"}) {
            EXPECT_FALSE(IsGreyed(name)) << name;
        }

        const String fen = Editor().ToFen();
        ASSERT_TRUE(ClickSection("PositionSectionPlay").Both());
        ASSERT_TRUE(ClickSection("PositionSectionCancel").Both());
        EXPECT_FALSE(Game().IsEditing());
        EXPECT_TRUE(Section()->_visible) << "a greyed button does nothing, the menu stays";
        EXPECT_EQ(Editor().ToFen(), fen);
    }

    TEST_F(ChessUiPositionSectionTest, EmptyBoardStartsTheEditorOnAnEmptyBoardAndClosesTheMenu) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        const uint64_t version = Model().GetVersion();
        ASSERT_TRUE(OpenTheSection());

        ASSERT_TRUE(ClickSection("PositionSectionEmpty").Both());

        EXPECT_TRUE(Game().IsEditing());
        EXPECT_TRUE(MenuIsClosed());
        for (int index = 0; index < kSquareCount; ++index) {
            EXPECT_TRUE(Editor().GetSquares()[static_cast<size_t>(index)].IsEmpty()) << index;
        }
        EXPECT_EQ(Model().GetVersion(), version) << "the game is untouched";
        EXPECT_EQ(Model().GetMoves().size(), 2u);
    }

    TEST_F(ChessUiPositionSectionTest, StartPositionStartsTheEditorOnTheOpeningPosition) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4"}));
        ASSERT_TRUE(OpenTheSection());

        ASSERT_TRUE(ClickSection("PositionSectionStart").Both());

        EXPECT_TRUE(Game().IsEditing());
        EXPECT_EQ(Editor().Build()->ToFen(), Position::Start().ToFen());
        EXPECT_TRUE(MenuIsClosed());
    }

    TEST_F(ChessUiPositionSectionTest, EditThisPositionKeepsThePiecesOfTheGame) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5", "g1f3"}));
        ASSERT_TRUE(OpenTheSection());

        ASSERT_TRUE(ClickSection("PositionSectionCurrent").Both());

        EXPECT_TRUE(Game().IsEditing());
        EXPECT_TRUE(MenuIsClosed());
        EXPECT_EQ(Editor().GetPieceAt(Sq("e4")), (Piece{PieceType::Pawn, PieceColor::White}));
        EXPECT_EQ(Editor().GetPieceAt(Sq("f3")), (Piece{PieceType::Knight, PieceColor::White}));
        EXPECT_TRUE(Editor().GetPieceAt(Sq("g1")).IsEmpty());
        EXPECT_EQ(Editor().GetSideToMove(), PieceColor::Black);
    }

    TEST_F(ChessUiPositionSectionTest, TheSideToMoveSwitchChangesTheDraftAndShowsIt) {
        Game().BeginEditing(EditStart::Start);
        ASSERT_TRUE(OpenTheSection());
        Draw();
        EXPECT_EQ(Var("editorBlackToMove"), "white");
        EXPECT_FALSE(IsGreyed("PositionSectionSideRow"));

        ASSERT_TRUE(ClickSection("PositionSectionSideRow").Both());
        EXPECT_EQ(Editor().GetSideToMove(), PieceColor::Black);
        Draw();
        EXPECT_EQ(Var("editorBlackToMove"), "black");
        EXPECT_NE(Editor().ToFen().find(" b "), String::npos);

        ASSERT_TRUE(ClickSection("PositionSectionSideRow").Both());
        EXPECT_EQ(Editor().GetSideToMove(), PieceColor::White);
        EXPECT_TRUE(Section()->_visible) << "a toggle keeps the section open";
    }

    TEST_F(ChessUiPositionSectionTest, EachCastlingSwitchTurnsItsRightAndIsGreyWhileTheKingOrTheRookIsNotOnItsSquare) {
        struct Switch {
            const char* row;
            const char* variable;
            CastlingRight right;
        };
        const Switch switches[] = {{"PositionSectionCastleKRow", "editorCastleK", CastlingRight::WhiteKingSide},
                                   {"PositionSectionCastleQRow", "editorCastleQ", CastlingRight::WhiteQueenSide},
                                   {"PositionSectionCastleKbRow", "editorCastleKb", CastlingRight::BlackKingSide},
                                   {"PositionSectionCastleQbRow", "editorCastleQb", CastlingRight::BlackQueenSide}};
        Game().BeginEditing(EditStart::Start);
        ASSERT_TRUE(OpenTheSection());
        Draw();
        for (const Switch& castle : switches) {
            SCOPED_TRACE(castle.row);
            EXPECT_FALSE(IsGreyed(castle.row));
            EXPECT_TRUE(Editor().GetCastling(castle.right));
            ASSERT_TRUE(ClickSection(castle.row).Both());
            EXPECT_FALSE(Editor().GetCastling(castle.right));
            Draw();
            EXPECT_EQ(Var(castle.variable).substr(0, 1), "-");
            ASSERT_TRUE(ClickSection(castle.row).Both());
            EXPECT_TRUE(Editor().GetCastling(castle.right));
        }
        EXPECT_EQ(Editor().Build()->ToFen(), Position::Start().ToFen());

        Editor().Remove(Sq("h1"));
        Game().EditChanged();
        Draw();
        EXPECT_TRUE(IsGreyed("PositionSectionCastleKRow")) << "its rook left";
        EXPECT_FALSE(IsGreyed("PositionSectionCastleQRow"));
        EXPECT_FALSE(Editor().GetCastling(CastlingRight::WhiteKingSide));
        const String fen = Editor().ToFen();
        ASSERT_TRUE(ClickSection("PositionSectionCastleKRow").Both());
        EXPECT_EQ(Editor().ToFen(), fen) << "a greyed switch does nothing";
    }

    TEST_F(ChessUiPositionSectionTest, ACastlingSwitchTurnsOnWhenTheKingAndTheRookAreSetOnTheirSquares) {
        Game().BeginEditing(EditStart::Empty);
        PutPiece("e1", PieceType::King, PieceColor::White);
        PutPiece("e8", PieceType::King, PieceColor::Black);
        Game().EditChanged();
        ASSERT_TRUE(OpenTheSection());
        Draw();
        EXPECT_TRUE(IsGreyed("PositionSectionCastleKRow")) << "no rook yet";

        PutPiece("h1", PieceType::Rook, PieceColor::White);
        Game().EditChanged();
        Draw();
        ASSERT_FALSE(IsGreyed("PositionSectionCastleKRow"));
        ASSERT_TRUE(ClickSection("PositionSectionCastleKRow").Both());

        EXPECT_TRUE(Editor().GetCastling(CastlingRight::WhiteKingSide));
        EXPECT_NE(Editor().ToFen().find(" K "), String::npos);
    }

    TEST_F(ChessUiPositionSectionTest, PlayOfADraftTheRulesRefuseKeepsTheMenuOpenAndSaysWhyInThePanel) {
        Game().BeginEditing(EditStart::Empty);
        ASSERT_TRUE(OpenTheSection());
        Draw();
        ASSERT_FALSE(IsGreyed("PositionSectionPlay"));

        ASSERT_TRUE(ClickSection("PositionSectionPlay").Both());
        Draw();

        EXPECT_TRUE(Game().IsEditing());
        EXPECT_TRUE(Section()->_visible);
        EXPECT_EQ(Var("editorMessage"), "Needs one king each");
        EXPECT_EQ(Var("turn"), "Needs one king each") << "the HUD says it too";

        PutPiece("e1", PieceType::King, PieceColor::White);
        Game().EditChanged();
        Draw();
        EXPECT_EQ(Var("editorMessage"), "") << "an edit clears the reason";
    }

    TEST_F(ChessUiPositionSectionTest, PlayOfAValidDraftStartsANewGameFromItAndClosesTheMenu) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4"}));
        Game().BeginEditing(EditStart::Empty);
        PutPiece("e1", PieceType::King, PieceColor::White);
        PutPiece("e8", PieceType::King, PieceColor::Black);
        PutPiece("a1", PieceType::Rook, PieceColor::White);
        Game().EditChanged();
        ASSERT_TRUE(OpenTheSection());
        const String fen = Editor().ToFen();

        ASSERT_TRUE(ClickSection("PositionSectionPlay").Both());

        EXPECT_FALSE(Game().IsEditing());
        EXPECT_TRUE(MenuIsClosed());
        EXPECT_EQ(Model().GetStartFen(), fen);
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetStatus(), GameStatus::Ongoing);
    }

    TEST_F(ChessUiPositionSectionTest, CancelEndsTheEditingLeavesTheGameAsItWasAndClosesTheMenu) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        const uint64_t version = Model().GetVersion();
        const String fen = Model().GetPosition().ToFen();
        Game().BeginEditing(EditStart::Empty);
        ASSERT_TRUE(OpenTheSection());
        Draw();
        ASSERT_FALSE(IsGreyed("PositionSectionCancel"));

        ASSERT_TRUE(ClickSection("PositionSectionCancel").Both());

        EXPECT_FALSE(Game().IsEditing());
        EXPECT_TRUE(MenuIsClosed());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_EQ(Model().GetPosition().ToFen(), fen);
        EXPECT_EQ(Model().GetMoves().size(), 2u);
    }

    TEST_F(ChessUiPositionSectionTest, AnActionWithAValueInNoTableChangesNothing) {
        Game().BeginEditing(EditStart::Start);
        const String fen = Editor().ToFen();
        const uint64_t revision = Editor().GetRevision();

        InputEvents::ActionEvent::Emit("ChessEditSide"_intern, "green"_intern);
        InputEvents::ActionEvent::Emit("ChessEditCastle"_intern, "K"_intern);
        InputEvents::ActionEvent::Emit("ChessEditBegin"_intern, "nowhere"_intern);
        _manager->UpdateAll();

        EXPECT_EQ(Editor().ToFen(), fen);
        EXPECT_EQ(Editor().GetRevision(), revision);
        EXPECT_TRUE(Game().IsEditing());
    }

}  // namespace BECore::Chess::Tests
