#include <Chess/Components/ChessMovesSectionComponent.h>
#include <tests/ChessUiHarness.h>

/// The menu section "Moves": the node MovesSection (config/nodes/ChessMovesSection.xml) and what ChessMovesSectionComponent writes into it. The scene, the helpers and the fixture ChessUiTest are
/// tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kSectionNode = "MovesSection";
        constexpr const char* kSectionPlate = "MovesSectionPlate";
        constexpr const char* kSides[] = {"White", "Black"};

        /// Shows every row and its marks (the component shows only what the game has), so the layout holds the list at its full size
        void ShowEverySectionRow(UICanvasComponent& canvas) {
            for (size_t row = 0; row < kMovePanelRows; ++row) {
                EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern(Format("MovesSectionRow{}", row).c_str()), true)) << row;
                for (const char* side : kSides) {
                    EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern(Format("MovesSection{}Mark{}", side, row).c_str()), true)) << side << row;
                }
            }
        }

    }  // namespace

    TEST_F(ChessUiTest, TheMovesSectionPassesTheSizeMatrixWithTheLongestMoves) {
        Draw();
        auto canvas = CanvasOf(kSectionNode);
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        ShowEverySectionRow(*canvas);

        UICanvasLayout& layout = canvas->GetLayout();
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        String report;
        size_t reported = 0;
        for (size_t round = 0; round < kMovePanelWordRounds; ++round) {
            PublishMovePanelRound(round);
            const auto failures = ValidateOnMatrix(layout, options, ViewContext(), SceneViews());
            if (!failures.empty() && reported++ < kReportedCases) {
                report += Format("round {}: {}", round, DescribeMatrixFailures(failures).c_str());
            }
            // The validator does not see a Clip label's cut, nor a plate too low for what it holds
            OnEveryCaseOfTheMatrix(
                layout,
                [&](const String& label, const UICanvasLayout& laidOut) {
                    const String problems = PanelProblems(laidOut, kSectionPlate);
                    if (!problems.empty() && reported++ < kReportedCases) {
                        report += Format("round {}, {}:\n{}", round, label.c_str(), problems.c_str());
                    }
                },
                true);
        }
        EXPECT_TRUE(report.empty()) << report.c_str();
    }

    TEST_F(ChessUiTest, TheMovesSectionIsAHiddenModalCanvasOfTheMenuWithItsComponentFirstAndNamesOfItsOwn) {
        auto canvas = CanvasOf(kSectionNode);
        ASSERT_TRUE(canvas);
        EXPECT_FALSE(canvas->_visible);
        EXPECT_TRUE(canvas->_modal);
        EXPECT_EQ(canvas->_space, CanvasSpace::SceneView);
        EXPECT_TRUE(canvas->_useSafeArea);

        const auto& nodes = GetScene().GetRootNode().GetChildren();
        const SceneNode* node = nullptr;
        for (const auto& each : nodes) {
            if (each->GetName() == PoolString::Intern(kSectionNode)) {
                node = each.Get();
            }
        }
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->GetLayer(), 20) << "the layer of the menu and its sections, over the HUD";
        // The component comes before the canvas: the words are there in the frame the section is shown
        const auto& components = node->GetComponents();
        const auto words = node->GetComponent<ChessMovesSectionComponent>();
        ASSERT_TRUE(words);
        ASSERT_EQ(components.size(), 2u);
        EXPECT_EQ(components[0].Get(), static_cast<IComponent*>(words.Get()));
        EXPECT_EQ(components[1].Get(), static_cast<IComponent*>(canvas.Get()));

        Vector<PoolString> names;
        CollectNames(canvas->GetRoot(), names);
        ASSERT_FALSE(names.empty());
        for (const PoolString& name : names) {
            EXPECT_TRUE(name.ToStringView().starts_with(kSectionNode)) << name.CStr() << ": elements are found by name in every canvas";
        }
        for (const char* other : {"Hud", "BoardCoordinates", "Material", "MovePanel", "MenuPanel", "OpponentPanel", "PromotionPanel"}) {
            const auto otherCanvas = CanvasOf(other);
            ASSERT_TRUE(otherCanvas) << other;
            for (const PoolString& name : names) {
                EXPECT_EQ(otherCanvas->GetRoot().FindByName(name), nullptr) << name.CStr() << " is also in " << other;
            }
        }
        EXPECT_EQ(ClickerOf(ElementOf(kSectionNode, "MovesSectionBack"))->action, "ChessOpenMenu"_intern);
        EXPECT_EQ(canvas->GetRoot().FindByName("MovesSectionClose"_intern), nullptr) << "Back is the one button: the dim closes all";
        EXPECT_EQ(ClickerOf(ElementOf(kSectionNode, "MovesSectionDim"))->action, "ChessCloseMenu"_intern);
        EXPECT_EQ(canvas->GetRoot().children.front()->name, "MovesSectionDim"_intern) << "the first child, so a click on the plate never reaches it";
    }

    TEST_F(ChessUiTest, TheMenuOpensTheMovesSectionAndBackAndTheDimLeaveIt) {
        auto section = CanvasOf(kSectionNode);
        ASSERT_TRUE(section);
        for (const char* way : {"MovesSectionBack", "MovesSectionDim"}) {
            SCOPED_TRACE(way);
            ASSERT_TRUE(ClickButton(kMenuColor));
            ASSERT_TRUE(MenuList()->_visible);
            ASSERT_TRUE(ClickMenuList("MenuSectionMoves").Both());
            EXPECT_TRUE(section->_visible);
            EXPECT_FALSE(MenuList()->_visible);
            EXPECT_FALSE(Panel()->_visible);

            // The dim is clicked in its corner, far from the plate
            const bool dim = StringView(way) == "MovesSectionDim";
            const auto corner = DimCorner(kSectionNode, way);
            const bool clicked = dim ? ClickAtPixel(corner.first, corner.second).Both() : ClickControl(ElementOf(kSectionNode, way)).Both();
            ASSERT_TRUE(clicked);
            EXPECT_FALSE(section->_visible);
            EXPECT_EQ(MenuList()->_visible, StringView(way) == "MovesSectionBack") << "Back returns to the list, the dim closes all";
            if (MenuList()->_visible) {
                ASSERT_TRUE(ClickMenuList("MenuClose").Both());
            }
        }
    }

    TEST_F(ChessUiTest, TheMovesSectionShowsTheRowsOfThePanelAndFollowsTheGameWhileItIsOpen) {
        ASSERT_TRUE(Play(Model(), "e2e4"));
        ASSERT_TRUE(Play(Model(), "e7e5"));
        ASSERT_TRUE(Play(Model(), "g1f3"));
        ASSERT_TRUE(ClickButton(kMenuColor));
        ASSERT_TRUE(ClickMenuList("MenuSectionMoves").Both());
        EXPECT_EQ(ShownMoveRows(kSectionNode), "1. e4 e5 | 2. [Nf3]");
        EXPECT_EQ(ShownMoveRows(kSectionNode), ShownMovePanelRows()) << "the panel of the Wide window shows the same";

        // A window with no panel on the screen: the section is where the list is
        ResizeWithoutInsets(1024.0f, 768.0f);
        ASSERT_FALSE(MovePanelElement("MovePanelBox").active.visible);
        ASSERT_TRUE(Play(Model(), "b8c6"));
        EXPECT_EQ(ShownMoveRows(kSectionNode), "1. e4 e5 | 2. Nf3 [Nc6]") << "in the frame after the move";
        ASSERT_EQ(Model().TakeBack(2), 2u);
        EXPECT_EQ(ShownMoveRows(kSectionNode), "1. e4 [e5]");
        Press("ChessReset");
        EXPECT_EQ(ShownMoveRows(kSectionNode), "");
        for (size_t row = 0; row < kMovePanelRows; ++row) {
            EXPECT_FALSE(ElementOf(kSectionNode, Format("MovesSectionRow{}", row).c_str()).active.visible) << row;
        }
    }

    TEST_F(ChessUiTest, TheMovesSectionReadsTheSignOfABrilliantMoveLikeThePanelWhileTheRatingsAreOn) {
        CountingAnalysisFactory factory({New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        ASSERT_TRUE(ClickButton(kMenuColor));
        ASSERT_TRUE(ClickMenuList("MenuSectionMoves").Both());
        ASSERT_EQ(ShownMoveRows(kSectionNode), "1. e4 [e5]");

        Analyst().SetRatings(true);
        ASSERT_TRUE(WaitForRatings());
        EXPECT_EQ(ShownMoveRows(kSectionNode), "1. e4!! [e5]");
        EXPECT_EQ(ShownMoveRows(kSectionNode), ShownMovePanelRows());

        Analyst().SetRatings(false);
        EXPECT_EQ(ShownMoveRows(kSectionNode), "1. e4 [e5]");
    }

}  // namespace BECore::Chess::Tests
