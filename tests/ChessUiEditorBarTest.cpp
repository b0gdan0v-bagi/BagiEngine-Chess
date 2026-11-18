#include <BECore/UI/UIImageComponent.h>
#include <tests/ChessUiHarness.h>

/// The tool bar of the position editor: the node EditorBar (config/nodes/ChessEditorBar.xml), shown exactly while a position is edited. The scene, the helpers and the fixture ChessUiTest are
/// tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kBarNode = "EditorBar";
        constexpr const char* kBarRow = "EditorBarRow";
        constexpr float kBoardEdge = 640.0f;
        constexpr float kMinBarWidth = 56.0f;
        constexpr float kHomeBarOverlap = 20.0f;  // the most the row covers of the board's lowest rank, and only where the home bar of an inset window lifts the safe edge over the margin

        constexpr const char* kPieceButtons[] = {"EditorBarKing", "EditorBarQueen", "EditorBarRook", "EditorBarBishop", "EditorBarKnight", "EditorBarPawn"};
        constexpr const char* kPieceIds[] = {"king", "queen", "rook", "bishop", "knight", "pawn"};
        constexpr const char* kBarButtons[] = {"EditorBarKing", "EditorBarQueen", "EditorBarRook",  "EditorBarBishop", "EditorBarKnight",
                                               "EditorBarPawn", "EditorBarErase", "EditorBarColor", "EditorBarPlay",   "EditorBarOptions"};

        /// The words of the color button: the two colors of the picked piece
        constexpr const char* kColorWords[] = {"White", "Black"};

        String Var(const char* name) {
            const StringView value = CoreManager::GetUIVars().Get(PoolString::Intern(name));
            return String(value.data(), value.size());
        }

        /// What is wrong with the bar as it is laid out: a button that is cut, too small, or on the board, one problem to a line
        String BarProblems(const UICanvasLayout& layout) {
            String problems;
            const float scale = layout.GetResolved().scale;
            const bool portrait = LaidOutForPortrait(layout);
            const Rect board{portrait ? kPortraitBoardLeft : kBoardLeft, portrait ? kPortraitBoardTop : kBoardTop, kBoardEdge, kBoardEdge};
            for (const char* name : kBarButtons) {
                const UIElement* button = layout.root->FindByName(PoolString::Intern(name));
                if (!button || button->rect.IsEmpty()) {
                    problems += Format("{} has no rect\n", name);
                    continue;
                }
                const Rect& rect = button->rect;
                const UIContentFit fit = button->CheckContent(rect.w - 2.0f * kButtonAir, rect.h, scale);
                if (fit.hasContent && !fit.fits) {
                    problems += Format("{} cuts '{}': {:.1f} units wide, at scale {:.3f}\n", name, ShownText(*button).c_str(), rect.w, scale);
                }
                if (rect.h < kSegmentHeight - kPixelTolerance || rect.w < kMinBarWidth - kPixelTolerance) {
                    problems += Format("{} is {:.1f} x {:.1f} units, under {:.0f}\n", name, rect.w, rect.h, kSegmentHeight);
                }
                const bool overlaps = rect.x < board.x + board.w && rect.x + rect.w > board.x && rect.y < board.y + board.h && rect.y + rect.h > board.y;
                const UICanvasResolved& resolved = layout.GetResolved();
                const bool lifted = resolved.safeBounds.y + resolved.safeBounds.h < resolved.bounds.y + resolved.bounds.h - kPixelTolerance;
                const float depth = board.y + board.h - rect.y;
                if (overlaps && !(lifted && depth <= kHomeBarOverlap + kPixelTolerance)) {
                    problems += Format("{} ({:.1f}, {:.1f}, {:.1f}, {:.1f}) is on the board\n", name, rect.x, rect.y, rect.w, rect.h);
                }
            }
            return problems;
        }

    }  // namespace

    TEST_F(ChessUiTest, TheEditorBarPassesTheSizeMatrixWithEveryWord) {
        Draw();
        auto canvas = CanvasOf(kBarNode);
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        UICanvasLayout& layout = canvas->GetLayout();
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;

        String report;
        size_t reported = 0;
        for (const char* word : kColorWords) {
            CoreManager::GetUIVars().Set("editorColor"_intern, word);
            const auto failures = ValidateOnMatrix(layout, options, ViewContext(), SceneViews());
            if (!failures.empty() && reported++ < kReportedCases) {
                report += Format("{}: {}", word, DescribeMatrixFailures(failures).c_str());
            }
            OnEveryCaseOfTheMatrix(
                layout,
                [&](const String& label, const UICanvasLayout& laidOut) {
                    const String problems = BarProblems(laidOut);
                    if (!problems.empty() && reported++ < kReportedCases) {
                        report += Format("{}, {}:\n{}", word, label.c_str(), problems.c_str());
                    }
                },
                true);
        }
        EXPECT_TRUE(report.empty()) << report.c_str();
    }

    TEST_F(ChessUiTest, TheEditorBarIsAHiddenNonModalCanvasThatBlocksNothingButItsButtons) {
        auto canvas = CanvasOf(kBarNode);
        ASSERT_TRUE(canvas);
        EXPECT_FALSE(canvas->_visible);
        EXPECT_FALSE(canvas->_modal);
        EXPECT_EQ(canvas->_space, CanvasSpace::SceneView);
        EXPECT_TRUE(canvas->_useSafeArea);

        Vector<PoolString> names;
        CollectNames(canvas->GetRoot(), names);
        ASSERT_FALSE(names.empty());
        for (const PoolString& name : names) {
            EXPECT_TRUE(name.ToStringView().starts_with(kBarNode)) << name.CStr() << ": elements are found by name in every canvas";
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                const auto other = node->GetComponent<UICanvasComponent>();
                if (other && other != canvas) {
                    EXPECT_EQ(other->GetRoot().FindByName(name), nullptr) << name.CStr() << " is also in " << node->GetName().CStr();
                }
            }
        }
        const UIElement& row = *canvas->GetRoot().FindByName(PoolString::Intern(kBarRow));
        EXPECT_FALSE(row.blocksInput) << "the row between the buttons is the board's";
        for (const char* name : kBarButtons) {
            EXPECT_NE(ClickerOf(*canvas->GetRoot().FindByName(PoolString::Intern(name))), nullptr) << name;
        }
    }

    /// The fixture of the cases below: the editor is begun in code (the menu is the section's file) and the bar is what is clicked
    class ChessUiEditorBarTest : public ChessUiTest {
    protected:
        ChessGame& Game() const {
            ChessGame* game = ChessGame::GetActive();
            EXPECT_NE(game, nullptr);
            return *game;
        }

        PositionEditor& Editor() const {
            return Game().GetEditor();
        }

        IntrusivePtr<UICanvasComponent> Bar() const {
            return CanvasOf(kBarNode);
        }

        void BeginOn(EditStart start) {
            Game().BeginEditing(start);
            _manager->UpdateAll();
            Draw();
        }

        Taken ClickBar(const char* name) {
            return ClickControl(ElementOf(kBarNode, name));
        }

        PoolString IconOf(const char* name) const {
            return ElementOf(kBarNode, name).GetComponent<UIImageComponent>()->sprite;
        }
    };

    TEST_F(ChessUiEditorBarTest, TheBarIsHiddenOutsideEditingAndShownExactlyWhileAPositionIsEdited) {
        _manager->UpdateAll();
        Draw();
        EXPECT_FALSE(Bar()->_visible);

        BeginOn(EditStart::Empty);
        EXPECT_TRUE(Bar()->_visible);

        Game().CancelEditing();
        _manager->UpdateAll();
        EXPECT_FALSE(Bar()->_visible) << "a stale bar is closed on the next update";

        BeginOn(EditStart::Start);
        ASSERT_TRUE(Bar()->_visible);
        Game().Reset();
        _manager->UpdateAll();
        EXPECT_FALSE(Bar()->_visible);
    }

    TEST_F(ChessUiEditorBarTest, AClickOnAPieceButtonThenOnASquarePutsThatPiece) {
        BeginOn(EditStart::Empty);

        ASSERT_TRUE(ClickBar("EditorBarQueen").Both());
        EXPECT_EQ(Editor().GetTool(), (EditorTool{PieceType::Queen, PieceColor::White}));
        Click("d4");
        _manager->UpdateAll();
        EXPECT_EQ(Editor().GetPieceAt(Sq("d4")), (Piece{PieceType::Queen, PieceColor::White}));

        ASSERT_TRUE(ClickBar("EditorBarKnight").Both());
        Click("f6");
        EXPECT_EQ(Editor().GetPieceAt(Sq("f6")), (Piece{PieceType::Knight, PieceColor::White}));
    }

    TEST_F(ChessUiEditorBarTest, EveryPieceButtonPicksItsPiece) {
        BeginOn(EditStart::Empty);
        ASSERT_EQ(std::size(kPieceButtons), std::size(kPieceIds));
        for (size_t index = 0; index < std::size(kPieceButtons); ++index) {
            SCOPED_TRACE(kPieceButtons[index]);
            ASSERT_TRUE(ClickBar(kPieceButtons[index]).Both());
            const Optional<PieceType> type = FindEditorTool(kPieceIds[index]);
            ASSERT_TRUE(type.has_value());
            EXPECT_EQ(Editor().GetTool().type, *type);
        }
    }

    TEST_F(ChessUiEditorBarTest, TheColorButtonTurnsTheNextPiecesAndTheIconsAndSaysTheColor) {
        BeginOn(EditStart::Empty);
        EXPECT_EQ(Var("editorColor"), "White");
        EXPECT_EQ(IconOf("EditorBarQueen"), "WhiteQueen"_intern);
        EXPECT_EQ(ShownText(ElementOf(kBarNode, "EditorBarColor")), "White");

        ASSERT_TRUE(ClickBar("EditorBarColor").Both());
        Draw();

        EXPECT_EQ(Editor().GetTool().color, PieceColor::Black);
        EXPECT_EQ(Var("editorColor"), "Black");
        EXPECT_EQ(ShownText(ElementOf(kBarNode, "EditorBarColor")), "Black");
        for (size_t index = 0; index < std::size(kPieceButtons); ++index) {
            const PoolString expected = PoolString::Intern(Format("Black{}", EnumUtils<PieceType>::ToString(*FindEditorTool(kPieceIds[index]))));
            EXPECT_EQ(IconOf(kPieceButtons[index]), expected) << kPieceButtons[index];
        }
        ASSERT_TRUE(ClickBar("EditorBarQueen").Both());
        Click("e5");
        EXPECT_EQ(Editor().GetPieceAt(Sq("e5")), (Piece{PieceType::Queen, PieceColor::Black}));
    }

    TEST_F(ChessUiEditorBarTest, ThePickedToolIsTheSelectedButton) {
        BeginOn(EditStart::Empty);
        const auto selected = [&] {
            Vector<String> names;
            for (const char* name : kBarButtons) {
                if (ElementOf(kBarNode, name).selected) {
                    names.push_back(name);
                }
            }
            return names;
        };

        EXPECT_EQ(selected(), Vector<String>{"EditorBarPawn"}) << "a pawn is picked at the start";
        ASSERT_TRUE(ClickBar("EditorBarRook").Both());
        EXPECT_EQ(selected(), Vector<String>{"EditorBarRook"});
        ASSERT_TRUE(ClickBar("EditorBarErase").Both());
        EXPECT_EQ(selected(), Vector<String>{"EditorBarErase"});
        ASSERT_TRUE(ClickBar("EditorBarColor").Both());
        EXPECT_EQ(selected(), Vector<String>{"EditorBarErase"}) << "the color is not a tool";
    }

    TEST_F(ChessUiEditorBarTest, TheEraserTakesThePieceOffTheSquare) {
        BeginOn(EditStart::Start);
        ASSERT_FALSE(Editor().GetPieceAt(Sq("d1")).IsEmpty());

        ASSERT_TRUE(ClickBar("EditorBarErase").Both());
        Click("d1");
        _manager->UpdateAll();

        EXPECT_TRUE(Editor().GetPieceAt(Sq("d1")).IsEmpty());
        EXPECT_TRUE(Editor().GetTool().IsEraser());
    }

    TEST_F(ChessUiEditorBarTest, PlayOfAValidDraftStartsTheGameAndHidesTheBar) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4"}));
        BeginOn(EditStart::Empty);
        ASSERT_TRUE(ClickBar("EditorBarKing").Both());
        Click("e1");
        ASSERT_TRUE(ClickBar("EditorBarColor").Both());
        Click("e8");
        const String fen = Editor().ToFen();

        ASSERT_TRUE(ClickBar("EditorBarPlay").Both());
        _manager->UpdateAll();
        Draw();

        EXPECT_FALSE(Game().IsEditing());
        EXPECT_FALSE(Bar()->_visible);
        EXPECT_EQ(Model().GetStartFen(), fen);
        EXPECT_TRUE(Model().GetMoves().empty());
    }

    TEST_F(ChessUiEditorBarTest, PlayOfADraftTheRulesRefuseKeepsTheBarAndSaysWhyInTheTurnLine) {
        BeginOn(EditStart::Empty);

        ASSERT_TRUE(ClickBar("EditorBarPlay").Both());
        Draw();

        EXPECT_TRUE(Game().IsEditing());
        EXPECT_TRUE(Bar()->_visible);
        EXPECT_EQ(Var("turn"), "Needs one king each");
    }

    TEST_F(ChessUiEditorBarTest, TheMoreButtonOpensThePositionSectionAndTheEditingGoesOn) {
        BeginOn(EditStart::Start);
        Click("e2");  // a pawn off the board

        ASSERT_TRUE(ClickBar("EditorBarOptions").Both());

        const auto section = CanvasOf("PositionSection");
        ASSERT_TRUE(section);
        EXPECT_TRUE(section->_visible);
        EXPECT_FALSE(MenuList()->_visible);
        EXPECT_TRUE(Game().IsEditing());
        EXPECT_TRUE(Editor().GetPieceAt(Sq("e2")).IsEmpty());
    }

}  // namespace BECore::Chess::Tests
