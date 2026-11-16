#include <tests/ChessUiHarness.h>

/// The promotion panel: the picker on the board, its layout, its table and broken copies of it. The scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    // ---- the promotion panel -------------------------------------------------------------------------------------------------------
    // A click on a promotion target leaves the promotion pending in the model; the board shows the modal PromotionPanel for as long as it is. The buttons are clicked where
    // they are drawn (CoreManager::GetUIInput()), the board by a press on its square

    namespace {

        constexpr const char* kBlackPromotionFen = "k7/8/8/8/8/8/p6K/8 b - - 0 1";
        constexpr const char* kMatingPromotionFen = "7k/P7/6K1/8/8/8/8/8 w - - 0 1";  // a8 gives mate with a queen or a rook

        /// The name of the button of a promotion piece in the scene: PromoteQueen, PromoteRook, ...
        String PromoteButton(PieceType piece) {
            return Format("Promote{}", PromotionLabel(piece));
        }

        constexpr const char* kPromotionPlate = "PromotionPlate";
        constexpr float kPromotionPlateHeight = 224.0f;  // written out from the comment of the scene, not read from the XML

    }  // namespace

    TEST_F(ChessUiTest, ThePromotionPanelIsHiddenAtTheStartAndDrawsNothing) {
        auto panel = Promotion();
        ASSERT_TRUE(panel);
        EXPECT_FALSE(panel->_visible);
        EXPECT_TRUE(panel->_modal);
        Draw();
        EXPECT_TRUE(RectCalls(kPromotionPlateColor).empty());
        _manager->UpdateAll();
        EXPECT_FALSE(PromotionShown()) << "an update with nothing pending shows nothing";
    }

    TEST_F(ChessUiTest, AClickOnAPromotionTargetOpensThePanelAndPlaysNothing) {
        StartFrom(kPromotionFen);
        const uint64_t version = Model().GetVersion();
        const String fen = Model().GetPosition().ToFen();

        Click("a7");
        EXPECT_FALSE(PromotionShown()) << "selecting the pawn is not a promotion";
        Click("a8");

        EXPECT_TRUE(PromotionShown());
        EXPECT_TRUE(Model().HasPendingPromotion());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_EQ(Model().GetPosition().ToFen(), fen) << "nothing has moved";
        Draw();
        EXPECT_FALSE(RectCalls(kPromotionPlateColor).empty()) << "shown: the plate is drawn";
        EXPECT_TRUE(RectCalls(kPlateColor).empty()) << "and it is not the opponent panel";
        EXPECT_FALSE(Panel()->_visible);
    }

    TEST_F(ChessUiTest, EachPieceButtonPlaysItsPromotionClosesThePanelAndTheHandleReportsThePiece) {
        for (const PieceType piece : PromotionPieces()) {
            SCOPED_TRACE(String(PromotionLabel(piece)).c_str());
            StartFrom(kPromotionFen);
            const uint64_t version = Model().GetVersion();
            const ChessGameModel::PieceHandle pawn = PieceNodeAt("a7");
            ASSERT_NE(pawn, nullptr);
            ClickMove("a7", "a8");
            ASSERT_TRUE(PromotionShown());

            const Taken taken = ClickPromotion(PromoteButton(piece).c_str());
            EXPECT_TRUE(taken.Both());

            ASSERT_EQ(Model().GetMoves().size(), 1u);
            EXPECT_EQ(Model().GetMoves().back().ToUci().substr(0, 4), String("a7a8"));
            EXPECT_TRUE(Model().GetMoves().back().IsPromotion());
            EXPECT_EQ(Model().GetPieceAt(Sq("a8")).type, piece);
            EXPECT_EQ(Model().GetPieceAt(Sq("a8")).color, PieceColor::White);
            EXPECT_GT(Model().GetVersion(), version);
            EXPECT_FALSE(PromotionShown());
            EXPECT_FALSE(Model().HasPendingPromotion());
            EXPECT_EQ(Model().GetSquareOf(pawn), Sq("a8"));
            EXPECT_EQ(Model().GetPieceOf(pawn).type, piece) << "the handle of the pawn reports the piece";
            EXPECT_EQ(Model().GetTurn(), PieceColor::Black);
        }
    }

    TEST_F(ChessUiTest, TheLetterOfEachPromotionIsTheOneOfItsButton) {
        const Pair<PieceType, const char*> letters[] = {{PieceType::Queen, "a7a8q"}, {PieceType::Rook, "a7a8r"}, {PieceType::Bishop, "a7a8b"}, {PieceType::Knight, "a7a8n"}};
        for (const auto& [piece, uci] : letters) {
            SCOPED_TRACE(uci);
            StartFrom(kPromotionFen);
            ClickMove("a7", "a8");
            ASSERT_TRUE(ClickPromotion(PromoteButton(piece).c_str()).Both());
            ASSERT_EQ(Model().GetMoves().size(), 1u);
            EXPECT_EQ(Model().GetMoves().back().ToUci(), String(uci));
        }
    }

    TEST_F(ChessUiTest, CancelClosesThePanelKeepsThePawnSelectedAndTheSameTargetOpensItAgain) {
        StartFrom(kPromotionFen);
        const uint64_t version = Model().GetVersion();
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());
        ActionLog log;

        EXPECT_TRUE(ClickPromotion("PromotionCancel").Both());

        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessCancelPromotion"));
        EXPECT_FALSE(PromotionShown());
        EXPECT_FALSE(Model().HasPendingPromotion());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("a7"));
        EXPECT_EQ(Model().GetPieceAt(Sq("a7")).type, PieceType::Pawn);
        Draw();
        EXPECT_TRUE(RectCalls(kPromotionPlateColor).empty());

        Click("a8");
        EXPECT_TRUE(PromotionShown()) << "the pawn is still selected, so the same target asks again";
        ASSERT_TRUE(ClickPromotion("PromoteKnight").Both());
        EXPECT_EQ(Model().GetPieceAt(Sq("a8")).type, PieceType::Knight);
    }

    TEST_F(ChessUiTest, WhileThePromotionPanelIsShownThePlateTakesAPressAndTheDimCancelsAndNothingUnderTheDimSeesOne) {
        StartFrom(kPromotionFen);
        const uint64_t version = Model().GetVersion();
        const String fen = Model().GetPosition().ToFen();
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());
        Draw();
        ActionLog log;

        const auto title = ScreenCenter(PromotionElement("PromotionTitle"));
        EXPECT_TRUE(ClickAtPixel(title.first, title.second).Both());
        EXPECT_EQ(log.Total(), 0u) << "the title is no button, and the press did not reach the dim";
        EXPECT_TRUE(PromotionShown());

        // The dim over the board (a1, h8, a8) and over the Reset and Menu buttons of the HUD: each press cancels the promotion and nothing else gets it
        const UIElement& plate = PromotionElement(kPromotionPlate);
        const Pair<float, float> points[] = {SquareCenter("a1"), SquareCenter("h8"), SquareCenter("a8"), ScreenCenter(*HudElement("Reset")), ScreenCenter(*HudElement("Menu"))};
        for (const auto& point : points) {
            SCOPED_TRACE(Format("{},{}", point.first, point.second).c_str());
            ASSERT_FALSE(Covers(plate, point)) << "a point of the dim, not of the plate";
            const size_t before = log.Total();
            EXPECT_TRUE(ClickAtPixel(point.first, point.second).Both());
            EXPECT_EQ(log.Total(), before + 1) << "one action: the dim's";
            EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessCancelPromotion"));
            EXPECT_FALSE(PromotionShown());
            EXPECT_FALSE(Model().HasPendingPromotion());
            EXPECT_EQ(Model().GetVersion(), version) << "Reset under the dim was not pressed";
            EXPECT_EQ(Model().GetPosition().ToFen(), fen);
            EXPECT_EQ(Model().GetSelected(), Sq("a7")) << "the pawn stays selected";
            EXPECT_FALSE(Panel()->_visible) << "the opponent panel stays closed (the Menu button was not pressed)";
            EXPECT_FALSE(MenuList()->_visible);

            Click("a8");
            ASSERT_TRUE(PromotionShown()) << "the same target asks again";
            Draw();
        }
    }

    TEST_F(ChessUiTest, AClickOnThePromotionDimCancelsLikeCancelAndNothingElseOfThePanelDoes) {
        StartFrom(kPromotionFen);
        const uint64_t version = Model().GetVersion();
        Click("a7");
        Click("a8");
        ASSERT_TRUE(PromotionShown());
        ActionLog log;

        // The click that opened the panel pressed on the board, and its release now meets a modal canvas that saw no press: no click on the dim
        ReleaseOn("a8");
        EXPECT_TRUE(PromotionShown());
        EXPECT_EQ(log.Total(), 0u);

        // On the plate, beside the buttons (its padding), and on its title: nothing changes
        Draw();
        const UIElement& plate = PromotionElement(kPromotionPlate);
        const auto padding = PixelOf(plate.rect.x + kDimInset / 2, plate.rect.y + kDimInset / 2);
        const auto title = ScreenCenter(PromotionElement("PromotionTitle"));
        ASSERT_TRUE(Covers(plate, padding));
        for (const auto& point : {padding, title}) {
            EXPECT_TRUE(ClickAtPixel(point.first, point.second).Both());
        }
        EXPECT_EQ(log.Total(), 0u);
        EXPECT_TRUE(PromotionShown());
        EXPECT_TRUE(Model().HasPendingPromotion());

        // Outside the plate: the same as Cancel
        const auto dim = DimCorner("PromotionPanel", "PromotionDim");
        ASSERT_FALSE(Covers(plate, dim));
        EXPECT_TRUE(ClickAtPixel(dim.first, dim.second).Both());
        EXPECT_EQ(log.Total(), 1u);
        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessCancelPromotion"));
        EXPECT_FALSE(PromotionShown());
        EXPECT_FALSE(Model().HasPendingPromotion());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("a7"));
        EXPECT_EQ(Model().GetPieceAt(Sq("a7")).type, PieceType::Pawn);
        Draw();
        EXPECT_TRUE(RectCalls(kPromotionPlateColor).empty());

        Click("a8");
        EXPECT_TRUE(PromotionShown()) << "the pawn is still selected, so the same target asks again";
        ASSERT_TRUE(ClickPromotion("PromoteQueen").Both());
        EXPECT_EQ(Model().GetPieceAt(Sq("a8")).type, PieceType::Queen);
    }

    TEST_F(ChessUiTest, EveryModalCanvasHasADimThatIsItsOwnCloseAndDrawsTheSameUnderThePointer) {
        struct ModalRow {
            const char* node;
            const char* dim;
            const char* action;
        };
        // The dim of a modal canvas is its first element, and a click on it does what the panel's Close or Cancel does
        const ModalRow rows[] = {{"MenuPanel", "MenuDim", "ChessCloseMenu"},
                                 {"OpponentPanel", "Dim", "ChessCloseMenu"},
                                 {"MovesSection", "MovesSectionDim", "ChessCloseMenu"},
                                 {"GameSection", "GameSectionDim", "ChessCloseMenu"},
                                 {"PromotionPanel", "PromotionDim", "ChessCancelPromotion"}};
        size_t modals = 0;
        for (const auto& node : GetScene().GetRootNode().GetChildren()) {
            const auto canvas = node->GetComponent<UICanvasComponent>();
            if (!canvas || !canvas->_modal) {
                continue;
            }
            ++modals;
            SCOPED_TRACE(node->GetName().CStr());
            const ModalRow* row = nullptr;
            for (const ModalRow& candidate : rows) {
                if (node->GetName() == PoolString::Intern(candidate.node)) {
                    row = &candidate;
                }
            }
            ASSERT_NE(row, nullptr) << "a modal canvas needs a dim that closes it, and a row here";
            ASSERT_FALSE(canvas->GetRoot().children.empty());
            const UIElement& dim = *canvas->GetRoot().children.front();
            EXPECT_EQ(dim.name, PoolString::Intern(row->dim)) << "the dim is the first element, so everything else is hit before it";
            const UIInputClicker* clicker = ClickerOf(dim);
            ASSERT_NE(clicker, nullptr);
            EXPECT_EQ(clicker->action, PoolString::Intern(row->action));
            EXPECT_FALSE(clicker->stateLook) << "the dim stays the plain rect under the pointer";
            EXPECT_TRUE(dim.blocksInput);
        }
        EXPECT_EQ(modals, std::size(rows));

        // Shown, with the pointer over its dim, a dim is still one rect of the dim color
        const auto show = [&](const char* node) {
            if (StringView(node) == "MenuPanel") {
                return ClickButton(kMenuColor);
            }
            if (StringView(node) == "OpponentPanel") {
                return OpenThePanel();
            }
            if (StringView(node) == "MovesSection") {
                return ClickButton(kMenuColor) && ClickMenuList("MenuSectionMoves").Both();
            }
            if (StringView(node) == "GameSection") {
                return ClickButton(kMenuColor) && ClickMenuList("MenuSectionGame").Both();
            }
            StartFrom(kPromotionFen);
            ClickMove("a7", "a8");
            return PromotionShown();
        };
        for (const ModalRow& row : rows) {
            SCOPED_TRACE(row.node);
            ASSERT_TRUE(show(row.node));
            Draw();
            ASSERT_EQ(RectCalls(kDimColor).size(), 1u);
            const auto corner = DimCorner(row.node, row.dim);
            MouseMove(corner.first, corner.second, false);
            Draw();
            EXPECT_EQ(RectCalls(kDimColor).size(), 1u) << "hovered, the dim is still the plain rect";
            Press("ChessCloseMenu");
            Press("ChessCancelPromotion");
            _manager->UpdateAll();
            Draw();
            ASSERT_TRUE(RectCalls(kDimColor).empty()) << "closed again";
        }
    }

    TEST_F(ChessUiTest, AResetASetPositionOrAMoveOfSomeoneElseClosesThePanelAtTheNextFrame) {
        const auto open = [&] {
            StartFrom(kPromotionFen);
            ClickMove("a7", "a8");
            ASSERT_TRUE(PromotionShown());
        };

        open();
        Model().Reset();
        _manager->UpdateAll();
        EXPECT_FALSE(PromotionShown()) << "Reset";
        EXPECT_FALSE(Model().HasPendingPromotion());

        open();
        Model().SetPosition(PositionFromFen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"));
        _manager->UpdateAll();
        EXPECT_FALSE(PromotionShown()) << "SetPosition";

        open();
        ASSERT_TRUE(Play(Model(), "a1b1"));  // the control API plays a move of its own
        _manager->UpdateAll();
        EXPECT_FALSE(PromotionShown()) << "a move of someone else";
        EXPECT_FALSE(Model().HasPendingPromotion());

        // The Reset button cannot be pressed under the panel, but the action can be emitted (the control API's reset does the same to the model)
        open();
        Press("ChessReset");
        _manager->UpdateAll();
        EXPECT_FALSE(PromotionShown()) << "the action ChessReset";
    }

    TEST_F(ChessUiTest, APromotionThatMatesEndsTheGameAndClosesThePanel) {
        StartFrom(kMatingPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());

        ASSERT_TRUE(ClickPromotion("PromoteQueen").Both());

        EXPECT_FALSE(PromotionShown());
        EXPECT_EQ(Model().GetStatus(), GameStatus::Checkmate);
        EXPECT_EQ(Model().GetWinner(), Optional<PieceColor>(PieceColor::White));
        EXPECT_EQ(Var("turn"), "White wins");
        EXPECT_EQ(Var("status"), "Checkmate");

        // The knight does not mate (king and knight against a king is a draw): the same position, the other choice, ends the game another way
        StartFrom(kMatingPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(ClickPromotion("PromoteKnight").Both());
        EXPECT_EQ(Model().GetStatus(), GameStatus::DrawInsufficientMaterial);
        EXPECT_FALSE(PromotionShown());
    }

    TEST_F(ChessUiTest, AnIdThatIsInNoTableChangesNothingAndLeavesThePanelOpen) {
        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());
        const uint64_t version = Model().GetVersion();

        for (const char* value : {"", "Queen", "QUEEN", "queen ", "pawn", "king", "que", "nonsense"}) {
            PressWith("ChessPromote", value);
        }
        Press("ChessPromote");  // no value at all
        _manager->UpdateAll();

        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_TRUE(Model().HasPendingPromotion());
        EXPECT_TRUE(PromotionShown());
        EXPECT_EQ(Model().GetPieceAt(Sq("a7")).type, PieceType::Pawn);
    }

    TEST_F(ChessUiTest, APromotionPressWithNothingPendingChangesNothing) {
        StartFrom(kPromotionFen);
        const uint64_t version = Model().GetVersion();
        Press("ChessCancelPromotion");
        PressWith("ChessPromote", "queen");
        _manager->UpdateAll();
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_FALSE(PromotionShown());
        EXPECT_EQ(Model().GetPieceAt(Sq("a7")).type, PieceType::Pawn);
    }

    TEST_F(ChessUiTest, AwayFromTheLastRankAClickNeverOpensThePanel) {
        ClickMove("e2", "e4");
        EXPECT_FALSE(PromotionShown());
        EXPECT_EQ(Model().GetMoves().size(), 1u);
    }

    TEST_F(ChessUiTest, WhileTheProgramPlaysTheOtherColorThePersonsPromotionWorksAndTheProgramAnswersIt) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());

        ASSERT_TRUE(ClickPromotion("PromoteRook").Both());

        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().front().ToUci(), String("a7a8r"));
        EXPECT_FALSE(PromotionShown());
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the program answers the promotion";
        EXPECT_EQ(Model().GetTurn(), PieceColor::White);
        EXPECT_FALSE(PromotionShown());
    }

    TEST_F(ChessUiTest, TheProgramTakingTheTurnClosesThePanel) {
        // The person selected the pawn and asked for the promotion; the program (switched on in the meantime) takes the turn and drops the half-made move
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());

        ASSERT_TRUE(Opponent().SetSide(PieceColor::White).has_value());
        _manager->UpdateAll();

        EXPECT_FALSE(Model().HasPendingPromotion());
        EXPECT_FALSE(PromotionShown());
    }

    TEST_F(ChessUiTest, ABlackPromotionWorks) {
        StartFrom(kBlackPromotionFen);
        ClickMove("a2", "a1");
        ASSERT_TRUE(PromotionShown());

        ASSERT_TRUE(ClickPromotion("PromoteKnight").Both());

        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), String("a2a1n"));
        EXPECT_EQ(Model().GetPieceAt(Sq("a1")).type, PieceType::Knight);
        EXPECT_EQ(Model().GetPieceAt(Sq("a1")).color, PieceColor::Black);
        EXPECT_FALSE(PromotionShown());
        EXPECT_EQ(Model().GetTurn(), PieceColor::White);
    }

    TEST_F(ChessUiTest, ThePromotedPieceIsDrawnWithItsSprite) {
        StartFrom(kPromotionFen);
        const auto* node = PieceNodeAt("a7");
        ASSERT_NE(node, nullptr);
        ClickMove("a7", "a8");
        ASSERT_TRUE(ClickPromotion("PromoteBishop").Both());
        _manager->UpdateAll();  // ChessPieceComponent switches the sprite in an update

        const auto sprite = node->GetNode().GetComponent<SpriteRendererComponent>();
        ASSERT_TRUE(sprite);
        EXPECT_EQ(sprite->_spriteId, PoolString::Intern("WhiteBishop"));
    }

    // ---- the promotion panel: the layout ---------------------------------------------------------------------------------------------
    // The checks of the opponent panel (the size matrix, PanelProblems) run on the node PromotionPanel. The panel has no HTML + CSS twin: ChessHudWebTest holds the HUD, the opponent panel
    // and the menu list

    TEST_F(ChessUiTest, ThePromotionPanelPassesTheSizeMatrix) {
        auto panel = Promotion();
        ASSERT_TRUE(panel) << "the shipped scene has the node PromotionPanel";
        panel->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        const auto failures = ValidateWithWords(panel->GetLayout());
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(ChessUiTest, NoWordOfThePromotionPanelIsCutOnAnyCaseOfTheMatrix) {
        auto panel = Promotion();
        ASSERT_TRUE(panel);
        panel->UpdateLayout(1280.0f, 720.0f);
        const String problems = PanelProblemsOnMatrix(panel->GetLayout(), kReportedCases, kPromotionPlate);
        EXPECT_TRUE(problems.empty()) << problems.c_str();
    }

    TEST_F(ChessUiTest, NoWordOfThePromotionPanelIsCutInTheNarrowestWindowAndEveryButtonIsATouchTarget) {
        auto panel = Promotion();
        ASSERT_TRUE(panel);
        panel->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = panel->GetLayout();
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        Vector<String> buttons{"PromotionCancel"};
        for (const PieceType piece : PromotionPieces()) {
            buttons.push_back(PromoteButton(piece));
        }
        ASSERT_EQ(buttons.size(), 5u) << "four pieces and Cancel";

        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            LayOutInTheNarrowestWindow(layout, shape);

            const float scale = shape.NarrowestScale();
            EXPECT_NEAR(layout.GetResolved().scale, scale, 0.01f);
            EXPECT_NEAR(layout.root->rect.w, shape.NarrowestSafeWidth(), 0.5f) << "the safe area, in view units: about 689 and 590";
            const String problems = PanelProblems(layout, kPromotionPlate);
            EXPECT_TRUE(problems.empty()) << problems.c_str();

            // 94% of the safe area up to maxWidth: 640 in the default view (94% of even its narrowest safe area is more), about 555 in the narrowest Portrait window
            EXPECT_NEAR(PromotionElement(kPromotionPlate).rect.w, std::min(640.0f, 0.94f * layout.root->rect.w), kPixelTolerance);

            for (const String& name : buttons) {
                const UIElement& button = PromotionButton(name.c_str());
                EXPECT_GE(button.rect.h, kSegmentHeight - kPixelTolerance) << name.c_str();
                EXPECT_GE(std::min(button.rect.w, button.rect.h) * layout.GetResolved().scale, minTouch - kPixelTolerance) << name.c_str() << " is a touch target on the smallest window";
                const UIContentFit fit = button.CheckContent(button.rect.w - 2.0f * kButtonAir, button.rect.h, layout.GetResolved().scale);
                EXPECT_TRUE(fit.hasContent && fit.fits) << name.c_str() << " '" << ShownText(button).c_str() << "' in " << button.rect.w << " units";
            }
        }
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, ThePromotionPlateIsInsideTheViewCenteredAndLaidOutAsTheCommentOfTheSceneSays) {
        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());

        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}, Pair<float, float>{1024.0f, 768.0f}, Pair<float, float>{800.0f, 800.0f},
                                 Pair<float, float>{2560.0f, 1080.0f}, Pair<float, float>{480.0f, 853.0f}, Pair<float, float>{768.0f, 1024.0f}}) {
            SCOPED_TRACE(Format("{}x{}", size.first, size.second).c_str());
            DrawAt(size.first, size.second);
            const UIElement& plate = PromotionElement(kPromotionPlate);
            ASSERT_FALSE(plate.rect.IsEmpty()) << "the plate is shown";
            EXPECT_GE(plate.rect.x, -kPixelTolerance);
            EXPECT_GE(plate.rect.y, -kPixelTolerance);
            EXPECT_LE(plate.rect.x + plate.rect.w, ViewWidth() + kPixelTolerance);
            EXPECT_LE(plate.rect.y + plate.rect.h, ViewHeight() + kPixelTolerance);
            EXPECT_NEAR(plate.rect.x + plate.rect.w * 0.5f, ViewWidth() * 0.5f, 0.5f) << "the plate is centered in the view";
            EXPECT_LE(plate.rect.w, 640.0f + kPixelTolerance);
            EXPECT_NEAR(plate.rect.h, kPromotionPlateHeight, kPixelTolerance);

            const UIElement& row = PromotionControl<UIStack>("PromotionPieces");
            float left = row.rect.x;
            float width = -1.0f;
            for (const PieceType piece : PromotionPieces()) {
                const UIElement& button = PromotionElement(PromoteButton(piece).c_str());
                EXPECT_NEAR(button.rect.x, left, kPixelTolerance) << PromoteButton(piece).c_str();
                EXPECT_NEAR(button.rect.y, row.rect.y, kPixelTolerance) << PromoteButton(piece).c_str();
                EXPECT_NEAR(button.rect.h, kSegmentHeight, kPixelTolerance) << PromoteButton(piece).c_str();
                width = width < 0.0f ? button.rect.w : width;
                EXPECT_NEAR(button.rect.w, width, kPixelTolerance) << PromoteButton(piece).c_str() << ": equal shares by flex";
                left = button.rect.x + button.rect.w + 8.0f;
            }
            EXPECT_NEAR(left - 8.0f, row.rect.x + row.rect.w, kPixelTolerance) << "the four buttons fill the row";
            const UIElement& cancel = PromotionElement("PromotionCancel");
            const UIElement& title = PromotionElement("PromotionTitle");
            EXPECT_GE(cancel.rect.y, row.rect.y + row.rect.h + 12.0f - kPixelTolerance) << "Cancel is under the row";
            EXPECT_LE(title.rect.y + title.rect.h, row.rect.y - 12.0f + kPixelTolerance) << "the title is over the row";
        }
    }

    // ---- the promotion panel: the words and the ids come from the table -----------------------------------------------------------------

    TEST_F(ChessUiTest, TheTextValueAndActionOfEveryPieceButtonAreThoseOfThePromotionTable) {
        const UIStack& row = PromotionControl<UIStack>("PromotionPieces");
        const auto pieces = PromotionPieces();
        ASSERT_EQ(row.children.size(), pieces.size()) << "exactly the pieces of the table, no more and no fewer";
        ASSERT_EQ(pieces.size(), 4u);
        EXPECT_EQ(pieces[0], PieceType::Queen) << "queen first";

        for (size_t i = 0; i < pieces.size(); ++i) {
            const PieceType piece = pieces[i];
            SCOPED_TRACE(PromoteButton(piece).c_str());
            const UIElement& button = *row.children[i];
            const UIInputClicker* clicker = ClickerOf(button);
            ASSERT_NE(clicker, nullptr);
            EXPECT_EQ(button.name, PoolString::Intern(PromoteButton(piece).c_str())) << "the buttons are in the order of the table";
            const StringView label = PromotionLabel(piece);
            const StringView id = PromotionId(piece);
            EXPECT_EQ(ShownText(button), String(label.data(), label.size()));
            EXPECT_EQ(String(clicker->value.ToStringView().data(), clicker->value.ToStringView().size()), String(id.data(), id.size()));
            EXPECT_EQ(clicker->action, PoolString::Intern(kPromoteAction));
            EXPECT_EQ(FindPromotion(clicker->value.ToStringView()), Optional<PieceType>(piece)) << "the value is an id the board accepts";
        }

        const UIElement& cancel = PromotionButton("PromotionCancel");
        EXPECT_EQ(ShownText(cancel), "Cancel");
        EXPECT_EQ(ClickerOf(cancel)->action, PoolString::Intern(kCancelPromotionAction));
    }

    TEST_F(ChessUiTest, EveryActionOfThePromotionPanelIsOneTheBoardAnswers) {
        auto panel = Promotion();
        ASSERT_TRUE(panel);
        Vector<const UIElement*> controls;
        CollectControls(panel->GetRoot(), controls);
        size_t buttons = 0;
        for (const UIElement* control : controls) {
            if (const UIInputClicker* button = ClickerOf(*control)) {
                ++buttons;
                EXPECT_TRUE(button->action == PoolString::Intern(kPromoteAction) || button->action == PoolString::Intern(kCancelPromotionAction))
                    << control->name.CStr() << " emits " << button->action.CStr() << ", which ChessPromotionPanelComponent does not answer";
            }
        }
        EXPECT_EQ(buttons, 5u);

        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(Model().HasPendingPromotion());
        Press(kCancelPromotionAction);
        EXPECT_FALSE(Model().HasPendingPromotion()) << kCancelPromotionAction;
        ClickMove("a7", "a8");
        ASSERT_TRUE(Model().HasPendingPromotion());
        PressWith(kPromoteAction, "queen");
        EXPECT_FALSE(Model().HasPendingPromotion()) << kPromoteAction;
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "a7a8q");
    }

    TEST_F(ChessUiTest, NoElementNameIsInTwoCanvasesOfTheScene) {
        // Elements are found by name in every canvas of the scene (SetVisibleByName and the lookups of the tests), so no two canvases may share a name
        const struct {
            const char* node;
            IntrusivePtr<UICanvasComponent> canvas;
        } canvases[] = {{"Hud", Hud()}, {"BoardCoordinates", Coordinate()}, {"Material", Material()}, {"MenuPanel", MenuList()}, {"OpponentPanel", Panel()}, {"PromotionPanel", Promotion()}};
        constexpr size_t kCanvasCount = std::size(canvases);
        Vector<PoolString> names[kCanvasCount];
        for (size_t i = 0; i < kCanvasCount; ++i) {
            ASSERT_TRUE(canvases[i].canvas) << canvases[i].node;
            CollectNames(canvases[i].canvas->GetRoot(), names[i]);
            EXPECT_FALSE(names[i].empty()) << canvases[i].node;
        }
        for (size_t first = 0; first < kCanvasCount; ++first) {
            for (size_t second = first + 1; second < kCanvasCount; ++second) {
                for (const PoolString& name : names[first]) {
                    EXPECT_EQ(eastl::find(names[second].begin(), names[second].end(), name), names[second].end())
                        << name.CStr() << " is in both " << canvases[first].node << " and " << canvases[second].node;
                }
            }
        }
    }

    // ---- the promotion panel: broken copies, so that the checks can fail ---------------------------------------------------------------------

    TEST_F(ChessUiTest, APromotionButtonUnderTheTouchSizeIsCaught) {
        // The row stretches its buttons to its own height (crossAlign), so the row is what makes the four piece buttons low; Cancel has a height of its own
        String cancel = ShippedElementsXml("PromotionPanel");
        ASSERT_FALSE(cancel.empty());
        ASSERT_NO_FATAL_FAILURE(Replace(cancel, R"(name="PromotionCancel" height="56")", R"(name="PromotionCancel" height="40")"));
        CanvasCopy lowCancel(cancel);
        EXPECT_TRUE(HasIssue(ValidateWithWords(lowCancel.layout), UILayoutIssueKind::TouchTargetTooSmall));
        EXPECT_NE(PanelProblemsOnMatrix(lowCancel.layout, kReportedCases, kPromotionPlate).find("PromotionCancel is 40.0 units high"), String::npos);

        String row = ShippedElementsXml("PromotionPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(row, R"(name="PromotionPieces" height="56")", R"(name="PromotionPieces" height="40")"));
        CanvasCopy lowRow(row);
        EXPECT_TRUE(HasIssue(ValidateWithWords(lowRow.layout), UILayoutIssueKind::TouchTargetTooSmall));
        EXPECT_NE(PanelProblemsOnMatrix(lowRow.layout, kReportedCases, kPromotionPlate).find("PromoteBishop is 40.0 units high"), String::npos);

        CanvasCopy shipped(ShippedElementsXml("PromotionPanel"));
        EXPECT_TRUE(PanelProblemsOnMatrix(shipped.layout, kReportedCases, kPromotionPlate).empty()) << "the same checks pass on the shipped panel";
        EXPECT_FALSE(HasIssue(ValidateWithWords(shipped.layout), UILayoutIssueKind::TouchTargetTooSmall));
    }

    TEST_F(ChessUiTest, APromotionWordCutByAFontThatIsTooBigIsCaught) {
        String xml = ShippedElementsXml("PromotionPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(name="PromoteKnight" flex="1" height="56" fontSize="26")", R"(name="PromoteKnight" flex="1" height="56" fontSize="60")"));
        CanvasCopy copy(xml);
        const String problems = PanelProblemsOnMatrix(copy.layout, kReportedCases, kPromotionPlate);
        EXPECT_NE(problems.find("PromoteKnight cuts 'Knight'"), String::npos) << problems.c_str();
    }

    TEST_F(ChessUiTest, APromotionPlateTooNarrowForTheFourButtonsIsCaught) {
        // A plate of 300 units leaves a button too narrow for "Knight" with its air; the shipped 640 does not
        String xml = ShippedElementsXml("PromotionPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(maxWidth="640")", R"(maxWidth="300")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        const String problems = PanelProblems(copy.layout, kPromotionPlate);
        EXPECT_NE(problems.find("cuts '"), String::npos) << problems.c_str();
    }

    TEST_F(ChessUiTest, APromotionPlateTallerThanTheSafeAreaIsCaught) {
        String xml = ShippedElementsXml("PromotionPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(height="224")", R"(height="1200")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        EXPECT_NE(PanelProblems(copy.layout, kPromotionPlate).find("leaves the safe area"), String::npos);
    }

}  // namespace BECore::Chess::Tests
