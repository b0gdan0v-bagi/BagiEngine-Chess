#include <Chess/Game/StatusText.h>
#include <tests/ChessUiHarness.h>

/// The HUD, the words, the pieces, the views, the Reset button and the sweep of every canvas of the scene through the views. The scene, the helpers and the fixture ChessUiTest are
/// tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    // ---- the screen -------------------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, TheHudPassesTheSizeMatrixWithTheLongestWords) {
        // The Menu button is always shown and says "Menu": there is no second state of the HUD to walk
        ExpectTheHudSurvivesTheMatrix();
    }

    // The board's update lets the program move and the HUD publishes in its draw: if the HUD published first, the frame of the reply would show the words of the position before it
    TEST_F(ChessUiTest, TheFrameInWhichTheProgramAnswersShowsTheWordsOfItsAnswer) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("black");
        ClickMove("e2", "e4");
        ASSERT_TRUE(WaitUntil([&] {
            _manager->UpdateAll();
            Draw();
            return Model().GetMoves().size() == 2;
        })) << "the program answers";
        const StringView turn = CoreManager::GetUIVars().Get(PoolString::Intern("turn"));
        EXPECT_EQ(String(turn.data(), turn.size()), StatusText::Turn(Model(), false)) << "read without another frame";
    }

    TEST_F(ChessUiTest, NoWordOfTheHudIsCutInTheNarrowestWindow) {
        Draw();
        auto canvas = Hud();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);

        // 480 px wide, with the side insets of a phone held sideways: the narrowest the matrix has. The default view is 840 units wide, so the scale is 0.57 and the safe area
        // about 689 units, which is what every row of the HUD has to fit in; Portrait is 720 wide, at the scale 0.667 the safe area is about 590 units
        UICanvasLayout& layout = canvas->GetLayout();
        const HudWords words = AllHudWords();
        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            LayOutInTheNarrowestWindow(layout, shape);
            const float scale = shape.NarrowestScale();
            EXPECT_NEAR(layout.GetResolved().scale, scale, 0.01f);
            EXPECT_NEAR(layout.root->rect.w, shape.NarrowestSafeWidth(), 0.5f) << "the safe area, in view units";

            for (size_t round = 0; round < words.Rounds(); ++round) {
                words.Publish(round);
                const String problems = HudProblems(layout);
                EXPECT_TRUE(problems.empty()) << "round " << round << ":\n" << problems.c_str();
            }
            for (const char* name : {"Turn", "Menu", "Reset", "Status"}) {
                const UIElement* element = layout.root->FindByName(PoolString::Intern(name));
                ASSERT_NE(element, nullptr) << name;
                EXPECT_FALSE(element->rect.IsEmpty()) << name << " is shown";
            }
        }
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, TheMenuButtonIsAFixedWordThatOpensThePanelAndTheOldOpponentButtonIsGone) {
        Draw();
        const UIElement* element = HudElement("Menu");
        ASSERT_NE(element, nullptr);
        const UIElement* menu = element;
        ASSERT_TRUE(IsButton(*menu) && TextOf(*menu) != nullptr) << "the Menu button is a button: a text and a clicker";
        EXPECT_EQ(TextOf(*menu)->text, "Menu") << "a literal: not bound to a variable";
        EXPECT_EQ(ShownText(*menu), "Menu");
        EXPECT_EQ(ClickerOf(*menu)->action, PoolString::Intern("ChessOpenMenu"));
        EXPECT_NEAR(menu->rect.w, 110.0f, 0.5f) << "as wide as Reset";
        EXPECT_NEAR(menu->rect.h, 56.0f, 0.5f);
        EXPECT_EQ(HudElement("Opponent"), nullptr) << "the button was called Opponent when it showed the state; the name is not used any more";
        EXPECT_EQ(Panel()->GetRoot().FindByName(PoolString::Intern("Menu")), nullptr) << "the name Menu is the HUD's alone";
    }

    TEST_F(ChessUiTest, TheHudStaysOutsideTheBoardWithoutInsets) {
        auto canvas = Hud();
        ASSERT_TRUE(canvas);
        _manager->UpdateAll();

        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}, Pair<float, float>{1024.0f, 768.0f}, Pair<float, float>{800.0f, 800.0f},
                                 Pair<float, float>{2560.0f, 1080.0f}}) {
            DrawAt(size.first, size.second);
            const float boardTop = PortraitIsActive() ? kPortraitBoardTop : kBoardTop;
            const float boardBottom = PortraitIsActive() ? kPortraitBoardBottom : kBoardBottom;
            for (const char* name : {"Turn", "Menu", "Reset"}) {
                const UIElement* element = canvas->GetRoot().FindByName(PoolString::Intern(name));
                ASSERT_NE(element, nullptr);
                ASSERT_FALSE(element->rect.IsEmpty()) << name << " is shown at " << size.first << "x" << size.second;
                EXPECT_LE(element->rect.y + element->rect.h, boardTop + 0.01f) << name << " at " << size.first << "x" << size.second;
            }
            const UIElement* status = canvas->GetRoot().FindByName(PoolString::Intern("Status"));
            ASSERT_NE(status, nullptr);
            ASSERT_FALSE(status->rect.IsEmpty()) << "Status is shown at " << size.first << "x" << size.second;
            EXPECT_GE(status->rect.y, boardBottom - 0.01f) << "Status at " << size.first << "x" << size.second;
        }

        // The panel is not a part of the margins (it covers the board while it is open) but its plate stays inside the view, in every one of the shapes
        ASSERT_TRUE(OpenThePanel());
        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}, Pair<float, float>{1024.0f, 768.0f}, Pair<float, float>{800.0f, 800.0f},
                                 Pair<float, float>{2560.0f, 1080.0f}, Pair<float, float>{480.0f, 853.0f}, Pair<float, float>{768.0f, 1024.0f}}) {
            DrawAt(size.first, size.second);
            const UIElement& plate = PanelControl<UIStack>("Plate");
            ASSERT_FALSE(plate.rect.IsEmpty()) << "the plate is shown at " << size.first << "x" << size.second;
            EXPECT_GE(plate.rect.x, -kPixelTolerance) << size.first << "x" << size.second;
            EXPECT_GE(plate.rect.y, -kPixelTolerance) << size.first << "x" << size.second;
            EXPECT_LE(plate.rect.x + plate.rect.w, ViewWidth() + kPixelTolerance) << size.first << "x" << size.second;
            EXPECT_LE(plate.rect.y + plate.rect.h, ViewHeight() + kPixelTolerance) << size.first << "x" << size.second;
            EXPECT_NEAR(plate.rect.x + plate.rect.w * 0.5f, ViewWidth() * 0.5f, 0.5f) << "the plate is centered in the view, at " << size.first << "x" << size.second;
        }
    }

    // ---- the words--------------------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, TheHudSaysWhoseMoveItIs) {
        Draw();
        EXPECT_EQ(ShownText(Label("Turn")), "White to move");
        EXPECT_EQ(ShownText(Label("Status")), "");

        ASSERT_TRUE(Play(Model(), "e2e4"));
        Draw();
        EXPECT_EQ(ShownText(Label("Turn")), "Black to move");
    }

    TEST_F(ChessUiTest, TheHudNamesACheckAndTheEnd) {
        Model().SetPosition(PositionFromFen("4k3/4r3/8/8/8/8/8/4K3 w - - 0 1"));
        Draw();
        EXPECT_EQ(ShownText(Label("Status")), "Check");

        Model().SetPosition(PositionFromFen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"));
        ASSERT_TRUE(Play(Model(), "a1a8"));
        Draw();
        EXPECT_EQ(ShownText(Label("Turn")), "White wins");
        EXPECT_EQ(ShownText(Label("Status")), "Checkmate");
    }

    TEST_F(ChessUiTest, TheWordsAreDrawnAsText) {
        Draw();

        size_t glyphsInTurn = 0;
        const UIElement& turn = Label("Turn");
        const Rect box = ActiveFit().ToOutput(turn.rect.x, turn.rect.y, turn.rect.w, turn.rect.h);
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Texture && call.rect.x >= box.x - 1.0f && call.rect.x + call.rect.w <= box.x + box.w + 1.0f && call.rect.y >= box.y - 1.0f &&
                call.rect.y + call.rect.h <= box.y + box.h + 1.0f) {
                ++glyphsInTurn;
            }
        }
        EXPECT_GE(glyphsInTurn, 10u) << "'White to move' has twelve letters with ink";
    }

    // ---- the pieces -------------------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, EveryPieceIsFittedIntoItsSquare) {
        size_t pieces = 0;
        for (const auto& node : GetScene().GetRootNode().GetChildren()) {
            if (!node->GetComponent<ChessPieceComponent>()) {
                continue;
            }
            ++pieces;
            const auto sprite = node->GetComponent<SpriteRendererComponent>();
            ASSERT_TRUE(sprite) << node->GetName().CStr();
            EXPECT_EQ(sprite->_fit, SpriteFit::Contain) << node->GetName().CStr() << ": the atlas cell is not square, Stretch would draw the piece squat";
        }
        EXPECT_EQ(pieces, kPieceCount);
    }

    TEST_F(ChessUiTest, APieceKeepsTheAspectOfItsCellInsideItsSquare) {
        const auto texture = GiveAtlasCell("WhitePawn_e2");
        _manager->UpdateAll();  // ChessPieceComponent writes the transform of the piece in an update: the node starts at 0, 0
        Draw();

        const DrawCall* piece = TextureCall(*texture);
        ASSERT_NE(piece, nullptr);
        const Rect square = SquareRect("e2");
        EXPECT_NEAR(piece->rect.w / piece->rect.h, kCellWidth / kCellHeight, 1e-4f);
        EXPECT_NEAR(piece->rect.h, square.h, kPixelTolerance) << "as tall as the square, the cell is the taller side";
        EXPECT_LT(piece->rect.w, square.w);
        EXPECT_TRUE(IsCenteredIn(piece->rect, square));
        ASSERT_TRUE(piece->source.has_value());
        EXPECT_TRUE(SameRect(*piece->source, Rect{kCellX, 0.0f, kCellWidth, kCellHeight})) << "the fit changes the destination, not the part of the atlas";
    }

    TEST_F(ChessUiTest, TheSelectionAndTheLegalMovesSitAroundThePiece) {
        const auto texture = GiveAtlasCell("WhitePawn_e2");
        _manager->UpdateAll();
        Model().OnSquareClicked(Sq("e2"));
        ASSERT_TRUE(Model().HasSelection());

        // 1280 x 720 and 2560 x 1080 draw the default view (840 x 800) with bars at the sides, 720 x 1280 the Portrait one (720 x 960) with bars above and below
        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}, Pair<float, float>{2560.0f, 1080.0f}}) {
            SCOPED_TRACE(Format("output {}x{}", size.first, size.second).c_str());
            DrawAt(size.first, size.second);

            const DrawCall* piece = TextureCall(*texture);
            ASSERT_NE(piece, nullptr);
            const auto selected = RectCalls(kSelectedColor);
            ASSERT_EQ(selected.size(), 1u);
            EXPECT_TRUE(SameRect(selected[0]->rect, SquareRect("e2"))) << "the selection is the whole square of the piece";
            EXPECT_TRUE(IsCenteredIn(piece->rect, selected[0]->rect));
            EXPECT_NEAR(piece->rect.w / piece->rect.h, kCellWidth / kCellHeight, 1e-4f) << "not stretched to the highlight";

            const auto moves = RectCalls(kLegalMoveColor);
            ASSERT_EQ(moves.size(), 2u) << "e3 and e4";
            for (const char* target : {"e3", "e4"}) {
                const Rect square = SquareRect(target);
                const bool drawn = std::any_of(moves.begin(), moves.end(), [&](const DrawCall* call) { return SameRect(call->rect, square); });
                EXPECT_TRUE(drawn) << target;
            }
        }
    }

    TEST_F(ChessUiTest, TheCheckSitsAroundTheKing) {
        const auto texture = GiveAtlasCell("BlackKing_e8");
        _manager->UpdateAll();  // the pieces register with the model in the first update; moves played before it would be dealt to them by square, not by identity
        for (const char* uci : {"e2e4", "f7f6", "d1h5"}) {
            ASSERT_TRUE(Play(Model(), uci)) << uci;
        }
        ASSERT_TRUE(Model().IsInCheck());
        _manager->UpdateAll();

        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}}) {
            SCOPED_TRACE(Format("output {}x{}", size.first, size.second).c_str());
            DrawAt(size.first, size.second);

            const DrawCall* king = TextureCall(*texture);
            ASSERT_NE(king, nullptr);
            const auto check = RectCalls(kCheckColor);
            ASSERT_EQ(check.size(), 1u);
            EXPECT_TRUE(SameRect(check[0]->rect, SquareRect("e8")));
            EXPECT_TRUE(IsCenteredIn(king->rect, check[0]->rect));
            EXPECT_NEAR(king->rect.w / king->rect.h, kCellWidth / kCellHeight, 1e-4f) << "not stretched to the highlight";
        }
    }

    // ---- the views of the scene --------------------------------------------------------------------------------------------
    // The default view is 840 x 800, Portrait 720 x 960, Wide 1120 x 800 and Tall 720 x 1204; the board is 640 x 640 in all, so the view that the scene picks for an output is one in which the
    // board is biggest on the screen. The board follows the active view: a square is 80 units in all, its origin is that of the view

    namespace {

        constexpr float kBoardPixelTolerance = 0.01f;

        /// The board's side on the screen when a view of @p viewWidth x @p viewHeight is fitted into an output
        float BoardPixels(float viewWidth, float viewHeight, float outputWidth, float outputHeight) {
            return kBoardUnits * std::min(outputWidth / viewWidth, outputHeight / viewHeight);
        }

    }  // namespace

    TEST_F(ChessUiTest, TheSceneHasTheDefaultViewTheTallViewThePortraitViewAndTheWideView) {
        EXPECT_FLOAT_EQ(GetScene().GetViewWidth(), kViewWidth) << "the default view, until a frame picks another";
        EXPECT_FLOAT_EQ(GetScene().GetViewHeight(), kViewHeight);
        const auto variants = GetScene().GetViewVariants();
        ASSERT_EQ(variants.size(), 3u);
        EXPECT_EQ(variants[0].name, PoolString::Intern(kTallViewName)) << "before Portrait, whose condition holds for every Tall output too: the first that holds wins";
        EXPECT_FLOAT_EQ(variants[0].width, kPortraitViewWidth);
        EXPECT_FLOAT_EQ(variants[0].height, kTallViewHeight);
        EXPECT_EQ(variants[1].name, PoolString::Intern(kPortraitViewName));
        EXPECT_FLOAT_EQ(variants[1].width, kPortraitViewWidth);
        EXPECT_FLOAT_EQ(variants[1].height, kPortraitViewHeight);
        EXPECT_EQ(variants[2].name, PoolString::Intern(kWideViewName));
        EXPECT_FLOAT_EQ(variants[2].width, kWideViewWidth);
        EXPECT_FLOAT_EQ(variants[2].height, kViewHeight);
        EXPECT_LE(kTallMaxAspect, kPortraitViewWidth / kTallViewHeight) << "the width limits the Tall view wherever it is picked, so it is never smaller than Portrait";
    }

    TEST_F(ChessUiTest, TheSceneShowsTheViewWithTheBiggerBoardOnTheScreen) {
        const auto variants = GetScene().GetViewVariants();
        for (const auto& output : kViewOutputs) {
            SCOPED_TRACE(Format("output {}x{}", output.first, output.second).c_str());
            const ActiveSceneView picked = SceneViewSelector::Pick(kViewWidth, kViewHeight, variants, output.first, output.second);
            const float pickedPixels = BoardPixels(picked.width, picked.height, output.first, output.second);
            EXPECT_GE(pickedPixels, BoardPixels(kViewWidth, kViewHeight, output.first, output.second) - kBoardPixelTolerance);
            EXPECT_GE(pickedPixels, BoardPixels(kPortraitViewWidth, kPortraitViewHeight, output.first, output.second) - kBoardPixelTolerance);
            EXPECT_GE(pickedPixels, BoardPixels(kWideViewWidth, kViewHeight, output.first, output.second) - kBoardPixelTolerance);
            EXPECT_GE(pickedPixels, BoardPixels(kPortraitViewWidth, kTallViewHeight, output.first, output.second) - kBoardPixelTolerance);

            if (output.first > 0.0f) {
                DrawAt(output.first, output.second);
                EXPECT_EQ(GetScene().GetActiveView(), picked) << "the scene draws the view that the rule picks";
            }
        }
        // The cases that name the rule: a phone held upright (9:16, 9:19.5) is Tall up to 0.59, a taller window than wide Portrait, a window from 0.875 of its height up to 1.4 (a square
        // too) the default view, 1.4 and wider the Wide view
        const auto nameOf = [&](float width, float height) { return SceneViewSelector::Pick(kViewWidth, kViewHeight, variants, width, height).name; };
        EXPECT_EQ(nameOf(480.0f, 853.0f), PoolString::Intern(kTallViewName)) << "9:16";
        EXPECT_EQ(nameOf(720.0f, 1280.0f), PoolString::Intern(kTallViewName));
        EXPECT_EQ(nameOf(480.0f, 1040.0f), PoolString::Intern(kTallViewName)) << "9:19.5";
        EXPECT_EQ(nameOf(kTallMaxAspect * 1000.0f, 1000.0f), PoolString::Intern(kTallViewName)) << "the edge itself is Tall";
        EXPECT_EQ(nameOf(600.0f, 1000.0f), PoolString::Intern(kPortraitViewName)) << "just over 0.59";
        EXPECT_EQ(nameOf(480.0f, 640.0f), PoolString::Intern(kPortraitViewName)) << "3:4";
        EXPECT_EQ(nameOf(768.0f, 1024.0f), PoolString::Intern(kPortraitViewName));
        EXPECT_EQ(nameOf(874.0f, 1000.0f), PoolString::Intern(kPortraitViewName));
        EXPECT_EQ(nameOf(876.0f, 1000.0f), PoolString());
        EXPECT_EQ(nameOf(800.0f, 800.0f), PoolString());
        EXPECT_EQ(nameOf(1024.0f, 768.0f), PoolString());
        EXPECT_EQ(nameOf(1390.0f, 1000.0f), PoolString()) << "just under 1.4";
        EXPECT_EQ(nameOf(1400.0f, 1000.0f), PoolString::Intern(kWideViewName)) << "the edge itself is Wide";
        EXPECT_EQ(nameOf(kWideMinAspect * 1000.0f, 1000.0f), PoolString::Intern(kWideViewName));
        EXPECT_EQ(nameOf(1920.0f, 1080.0f), PoolString::Intern(kWideViewName));
        EXPECT_EQ(nameOf(2560.0f, 1080.0f), PoolString::Intern(kWideViewName));
    }

    TEST_F(ChessUiTest, NoWindowOfTheMatrixDrawsTheSceneSmallerThanTheNarrowestOneOfTheDefaultView) {
        // The reason the view is widened only from the aspect 1.4 up: a 56 unit control of the HUD is a 32 px touch target at this scale, so a view that shrank the scene further would break them
        const float smallest = kNarrowestWidth / kViewWidth;
        const auto variants = GetScene().GetViewVariants();
        size_t checked = 0;
        for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
            const float width = matrixCase.context.outputWidth;
            const float height = matrixCase.context.outputHeight;
            const ActiveSceneView picked = SceneViewSelector::Pick(kViewWidth, kViewHeight, variants, width, height);
            const float scale = std::min(width / picked.width, height / picked.height);
            EXPECT_GE(scale, smallest - 1e-4f) << matrixCase.label.c_str() << " in the view " << picked.width << " x " << picked.height;
            EXPECT_GE(scale * kSegmentHeight, CoreManager::GetUIEnvironment().GetSettings().minTouchSize - kPixelTolerance) << matrixCase.label.c_str();
            ++checked;
        }
        EXPECT_GT(checked, 0u);
    }

    TEST_F(ChessUiTest, TheHudOfTheWideViewSitsInTheCornersOfItsSafeAreaAndLeavesTheRightFreeForThePanel) {
        DrawAt(1920.0f, 1080.0f);
        ASSERT_EQ(GetScene().GetActiveView().name, PoolString::Intern(kWideViewName));
        constexpr float kEdge = 12.0f;  // the HUD's distance from the corners of the safe area (ChessHud.xml)
        EXPECT_NEAR(HudElement("Turn")->rect.x, kEdge, kPixelTolerance);
        EXPECT_NEAR(HudElement("Reset")->rect.x + HudElement("Reset")->rect.w, kWideViewWidth - kEdge, kPixelTolerance);
        EXPECT_NEAR(HudElement("Status")->rect.x + HudElement("Status")->rect.w, kWideViewWidth - kEdge, kPixelTolerance);
        EXPECT_NEAR(HudElement("Status")->rect.y + HudElement("Status")->rect.h, kViewHeight - kEdge, kPixelTolerance);
        for (const char* name : {"Menu", "Reset"}) {
            EXPECT_LE(HudElement(name)->rect.y + HudElement(name)->rect.h, kBoardTop) << name << " is above the row of the panel";
        }
    }

    TEST_F(ChessUiTest, TheWideViewKeepsTheBoardAndTheScaleOfTheDefaultViewAndAddsTheRoomAtTheRight) {
        const ChessBoardComponent* board = ChessBoardComponent::GetActive();
        ASSERT_NE(board, nullptr);
        for (const auto& output : {Pair<float, float>{1920.0f, 1080.0f}, {1280.0f, 720.0f}, {1400.0f, 1000.0f}, {2560.0f, 1080.0f}, {kWideNarrowestWidth, kNarrowestHeight}}) {
            SCOPED_TRACE(Format("output {}x{}", output.first, output.second).c_str());
            DrawAt(output.first, output.second);
            ASSERT_EQ(GetScene().GetActiveView().name, PoolString::Intern(kWideViewName));
            EXPECT_FLOAT_EQ(ViewWidth(), kWideViewWidth);
            EXPECT_FLOAT_EQ(ViewHeight(), kViewHeight);
            EXPECT_FLOAT_EQ(board->GetOriginX(), kBoardLeft);
            EXPECT_FLOAT_EQ(board->GetOriginY(), kBoardTop);
            EXPECT_FLOAT_EQ(board->GetSquareSize(), kCellSize);
            const ViewTransform defaultFit = ViewTransform::Fit(kViewWidth, kViewHeight, output.first, output.second);
            EXPECT_NEAR(ActiveFit().scale, defaultFit.scale, 1e-4f) << "the height limits both views: nothing gets smaller";
        }
    }

    TEST_F(ChessUiTest, TheNarrowestWindowOfEachViewHasItsOwnScale) {
        DrawAt(kNarrowestWidth, kNarrowestHeight);
        EXPECT_FALSE(PortraitIsActive()) << "a square window keeps the default view";
        EXPECT_NEAR(ActiveFit().scale, kNarrowestWidth / kViewWidth, 1e-4f);

        DrawAt(kNarrowestWidth, kNarrowestPortraitHeight);
        EXPECT_EQ(GetScene().GetActiveView().name, PoolString::Intern(kPortraitViewName));
        EXPECT_NEAR(ActiveFit().scale, kNarrowestWidth / kPortraitViewWidth, 1e-4f) << "3:4 is the shape of the view";

        DrawAt(kNarrowestWidth, kNarrowestTallHeight);
        EXPECT_EQ(GetScene().GetActiveView().name, PoolString::Intern(kTallViewName));
        EXPECT_NEAR(ActiveFit().scale, kNarrowestWidth / kPortraitViewWidth, 1e-4f)
            << "the width is the limit, as in Portrait: 9:16 is taller than the view, so the scene is not smaller for being higher";

        DrawAt(kWideNarrowestWidth, kNarrowestHeight);
        EXPECT_EQ(GetScene().GetActiveView().name, PoolString::Intern(kWideViewName));
        EXPECT_NEAR(ActiveFit().scale, kNarrowestHeight / kViewHeight, 1e-4f) << "the height is the limit, as in the default view: the scene is not smaller for being wider";
    }

    TEST_F(ChessUiTest, TheBoardAndThePiecesFollowTheViewThatIsDrawn) {
        const ChessBoardComponent* board = ChessBoardComponent::GetActive();
        ASSERT_NE(board, nullptr);
        _manager->UpdateAll();

        struct Shape {
            Pair<float, float> output;
            bool portrait;
        };
        // Landscape, portrait, landscape again: the geometry goes there and back
        for (const Shape& shape : {Shape{{1280.0f, 720.0f}, false}, Shape{{480.0f, 853.0f}, true}, Shape{{768.0f, 1024.0f}, true}, Shape{{1024.0f, 768.0f}, false}}) {
            SCOPED_TRACE(Format("output {}x{}", shape.output.first, shape.output.second).c_str());
            DrawAt(shape.output.first, shape.output.second);
            ASSERT_EQ(PortraitIsActive(), shape.portrait);

            const float left = shape.portrait ? kPortraitBoardLeft : kBoardLeft;
            const float top = shape.portrait ? kPortraitBoardTop : kBoardTop;
            EXPECT_FLOAT_EQ(board->GetOriginX(), left);
            EXPECT_FLOAT_EQ(board->GetOriginY(), top);
            EXPECT_FLOAT_EQ(board->GetSquareSize(), kCellSize);

            // A square is where the fixture says, for both orientations of the board
            for (const PieceColor bottom : {PieceColor::White, PieceColor::Black}) {
                for (const char* name : {"a1", "h1", "a8", "h8", "e4"}) {
                    const Square square = Sq(name);
                    const auto [column, row] = CellOfSquare(square, bottom == PieceColor::Black);
                    float x = 0.0f;
                    float y = 0.0f;
                    board->SquareToPixel(square.file, square.rank, bottom, x, y);
                    EXPECT_FLOAT_EQ(x, left + static_cast<float>(column) * kCellSize) << name;
                    EXPECT_FLOAT_EQ(y, top + static_cast<float>(row) * kCellSize) << name;
                }
            }

            // The pieces stand on their squares and the 64 squares drawn make the board of the view, to the pixel
            ExpectPiecesAtTheCellsOf(false);
            float minX = 1e9f;
            float minY = 1e9f;
            float maxX = -1e9f;
            float maxY = -1e9f;
            size_t cells = 0;
            for (const Color& color : {kLightSquareColor, kDarkSquareColor}) {
                for (const DrawCall* call : RectCalls(color)) {
                    minX = std::min(minX, call->rect.x);
                    minY = std::min(minY, call->rect.y);
                    maxX = std::max(maxX, call->rect.x + call->rect.w);
                    maxY = std::max(maxY, call->rect.y + call->rect.h);
                    ++cells;
                }
            }
            EXPECT_EQ(cells, 64u);
            EXPECT_TRUE(SameRect(Rect{minX, minY, maxX - minX, maxY - minY}, OutputRect(Rect{left, top, kBoardUnits, kBoardUnits})));
        }
    }

    // ---- the Reset button -------------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, TheResetActionStartsANewGame) {
        ASSERT_TRUE(Play(Model(), "e2e4"));
        const uint64_t before = Model().GetVersion();

        InputEvents::ActionEvent::Emit(PoolString::Intern("ChessReset"));

        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
        EXPECT_GT(Model().GetVersion(), before);
    }

    TEST_F(ChessUiTest, ClickingTheResetButtonOnTheScreenStartsANewGame) {
        ASSERT_TRUE(Play(Model(), "e2e4"));
        Draw();

        const DrawCall* fill = nullptr;
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Rect && call.color.r == kResetColor.r && call.color.g == kResetColor.g && call.color.b == kResetColor.b) {
                fill = &call;
            }
        }
        ASSERT_NE(fill, nullptr);
        const float x = fill->rect.x + fill->rect.w * 0.5f;
        const float y = fill->rect.y + fill->rect.h * 0.5f;

        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true))) << "the button takes the click";
        EXPECT_NE(Model().GetPosition().ToFen(), String(Position::kStartFen)) << "a press alone does nothing";
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false)));

        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
    }

    TEST_F(ChessUiTest, TheBoardStillTakesItsClicksUnderTheCanvas) {
        Draw();
        const auto from = SquareCenter("e2");
        const auto to = SquareCenter("e4");

        // The canvas does not consume a click that lands on no control; UIInput then gives it to the game, so the board gets it (the model selects, then moves)
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(from.first, from.second, true)));
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(to.first, to.second, true)));

        EXPECT_EQ(Model().GetPosition().ToFen(), String("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"));
    }

    // ---- every canvas of the scene is walked through the views ----------------------------------------------------------------------------

    namespace {

        /// A canvas of the scene (its node) and the test of this file that lays it out on the size matrix with the views of the scene (SceneViews()), so that the Portrait view is checked
        struct CanvasMatrixTest {
            const char* node;
            const char* test;
        };

        const CanvasMatrixTest kCanvasMatrixTests[] = {{"BoardCoordinates", "TheBoardCoordinatesPassTheSizeMatrixWithEveryLetterAndDigit"},
                                                       {"Material", "TheMaterialCanvasPassesTheSizeMatrixWithEveryCountAndEverySign"},
                                                       {"MovePanel", "TheMovePanelPassesTheSizeMatrixWithTheLongestMoves"},
                                                       {"Hud", "TheHudPassesTheSizeMatrixWithTheLongestWords"},
                                                       {"MenuPanel", "TheMenuPanelPassesTheSizeMatrix"},
                                                       {"OpponentPanel", "ThePanelPassesTheSizeMatrix"},
                                                       {"PromotionPanel", "ThePromotionPanelPassesTheSizeMatrix"}};

        /// The canvases among @p canvases (node names) that no row of kCanvasMatrixTests covers
        Vector<String> CanvasesWithoutMatrixTest(const Vector<String>& canvases) {
            Vector<String> uncovered;
            for (const String& canvas : canvases) {
                const bool covered = eastl::any_of(eastl::begin(kCanvasMatrixTests), eastl::end(kCanvasMatrixTests), [&](const CanvasMatrixTest& row) { return canvas == row.node; });
                if (!covered) {
                    uncovered.push_back(canvas);
                }
            }
            return uncovered;
        }

        bool IsRegisteredTestOfTheFixture(const char* name) {
            const testing::UnitTest& unitTest = *testing::UnitTest::GetInstance();
            for (int suite = 0; suite < unitTest.total_test_suite_count(); ++suite) {
                const testing::TestSuite& testSuite = *unitTest.GetTestSuite(suite);
                for (int test = 0; test < testSuite.total_test_count(); ++test) {
                    if (String(testSuite.name()) == "ChessUiTest" && String(testSuite.GetTestInfo(test)->name()) == name) {
                        return true;
                    }
                }
            }
            return false;
        }

        void CollectCanvasNodes(const SceneNode& node, Vector<IntrusivePtr<SceneNode>>& out) {
            for (const auto& child : node.GetChildren()) {
                if (child->GetComponent<UICanvasComponent>()) {
                    out.push_back(child);
                }
                CollectCanvasNodes(*child, out);
            }
        }

    }  // namespace

    TEST_F(ChessUiTest, EveryCanvasOfTheSceneIsCoveredByAMatrixTestThatWalksTheViewsAndPassesIt) {
        Vector<IntrusivePtr<SceneNode>> nodes;
        CollectCanvasNodes(GetScene().GetRootNode(), nodes);
        Vector<String> names;
        for (const auto& node : nodes) {
            names.push_back(String(node->GetName().ToStringView()));
        }
        ASSERT_EQ(names.size(), std::size(kCanvasMatrixTests)) << "a canvas was added or removed: its row of kCanvasMatrixTests too";

        // A canvas without a matrix test would never see the Portrait view: it needs a row, and the test of the row must exist
        const Vector<String> uncovered = CanvasesWithoutMatrixTest(names);
        EXPECT_TRUE(uncovered.empty()) << "no matrix test with the views for the canvas " << (uncovered.empty() ? "" : uncovered.front().c_str());
        for (const CanvasMatrixTest& row : kCanvasMatrixTests) {
            EXPECT_TRUE(IsRegisteredTestOfTheFixture(row.test)) << row.node << ": there is no test " << row.test;
        }

        // And the sweep itself: every canvas through the matrix with the views, whatever the tests of its own check
        for (const auto& node : nodes) {
            SCOPED_TRACE(node->GetName().CStr());
            const auto canvas = node->GetComponent<UICanvasComponent>();
            canvas->UpdateLayout(1280.0f, 720.0f);
            const auto failures = ValidateWithWords(canvas->GetLayout());
            EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        }

        // The check catches a canvas that is not on the list, and a row of a test that does not exist
        Vector<String> withNew = names;
        withNew.push_back("TabletPanel");
        const Vector<String> caught = CanvasesWithoutMatrixTest(withNew);
        ASSERT_EQ(caught.size(), 1u);
        EXPECT_EQ(caught.front(), "TabletPanel");
        EXPECT_FALSE(IsRegisteredTestOfTheFixture("ThePanelThatWasNeverWritten"));
    }

}  // namespace BECore::Chess::Tests
