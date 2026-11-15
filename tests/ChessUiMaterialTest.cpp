#include <tests/ChessUiHarness.h>

/// The captured material: the node Material, the Portrait view and the layout of its rows. The scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    // ---- the captured material: the node Material ---------------------------------------------------------------------------------------
    // A canvas in the left margin (ChessMaterial.xml has the reasons for its numbers) that ChessMaterialComponent gives its words and its sprites. Two groups: the lower one shows what the color at
    // the bottom of the board has taken, the upper one what the other color has taken. The rows are shown while the lists lie inside the safe area (Game/MaterialLayout.h). The library of
    // textures is empty in the test, so an icon has its sprite name and draws nothing

    namespace {

        constexpr const char* kBlackTookTheQueen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNB1KBNR w KQkq - 0 1";      // the queen of White is gone: +9 for Black
        constexpr const char* kBlackCanTakeAPawn = "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2";  // Black to move: d5e4 takes a pawn
        constexpr const char* kPawnAboutToPromote = "8/P6k/8/8/8/8/8/K7 w - - 0 1";                                 // White: a pawn; Black: a king. a7a8q makes a queen
        constexpr const char* kEightPawnsAgainstNothing = "4k3/8/8/8/8/8/PPPPPPPP/4K3 w - - 0 1";                   // White: king and eight pawns, Black: a bare king
        constexpr const char* kEmptyMaterialWords = "||||||||||||";                                                 // twelve variables, each followed by "|"

        bool FenSetsTheState(const char* fen) {
            Json request = Json::object();
            request["fen"] = fen;
            return ChessGame::GetActive()->AsControllable()->SetState(request).has_value();
        }

    }  // namespace

    TEST_F(ChessUiTest, TheMaterialCanvasIsOnItsOwnLayerOverThePiecesAndUnderTheHud) {
        const auto& nodes = GetScene().GetRootNode().GetChildren();
        const auto indexOf = [&](const char* name) -> int {
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (nodes[i]->GetName() == PoolString::Intern(name)) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        };
        for (const char* name : {"Board", "BoardCoordinates", "Material", "Result", "Hud", "MenuPanel", "OpponentPanel", "PromotionPanel"}) {
            ASSERT_GE(indexOf(name), 0) << "the scene has the node " << name;
        }
        const auto layerOf = [&](const char* name) { return nodes[static_cast<size_t>(indexOf(name))]->GetLayer(); };
        EXPECT_EQ(layerOf("Material"), 5);
        EXPECT_GT(indexOf("Material"), indexOf("BoardCoordinates")) << "written after the coordinates";
        EXPECT_GT(layerOf("Material"), 0) << "over the pieces (layer 0)";
        EXPECT_GT(layerOf("Material"), layerOf("Result")) << "and over the result bar";
        EXPECT_LT(layerOf("Material"), layerOf("Hud"));
        EXPECT_LT(layerOf("Material"), layerOf("MenuPanel"));
        EXPECT_LT(layerOf("Material"), layerOf("OpponentPanel"));
        EXPECT_LT(layerOf("Material"), layerOf("PromotionPanel"));

        // The component comes before the canvas on the node: it gives the variables their words before the canvas draws in the same frame
        const auto& components = nodes[static_cast<size_t>(indexOf("Material"))]->GetComponents();
        const auto words = nodes[static_cast<size_t>(indexOf("Material"))]->GetComponent<ChessMaterialComponent>();
        const auto canvas = Material();
        ASSERT_TRUE(words);
        ASSERT_TRUE(canvas);
        ASSERT_EQ(components.size(), 2u);
        EXPECT_EQ(components[0].Get(), static_cast<IComponent*>(words.Get())) << "the component is listed first";
        EXPECT_EQ(components[1].Get(), static_cast<IComponent*>(canvas.Get()));

        EXPECT_EQ(canvas->_space, CanvasSpace::SceneView);
        EXPECT_FALSE(canvas->_useSafeArea) << "fixed scene units like the board; the rule of the lists reads the safe area itself";
        EXPECT_FALSE(canvas->_modal);
        EXPECT_TRUE(canvas->_visible);
    }

    TEST_F(ChessUiTest, TheMaterialCanvasHasTwoGroupsOfFiveRowsAndNothingInItTakesAClick) {
        const auto canvas = Material();
        ASSERT_TRUE(canvas);
        const UIElement& root = canvas->GetRoot();
        ASSERT_EQ(root.children.size(), 4u) << "two lists and two balances";
        for (const UIElement* element : AllElements(root)) {
            EXPECT_FALSE(element->blocksInput) << element->name.CStr();
            EXPECT_FALSE(element->IsInteractive()) << element->name.CStr();
            if (element != &root) {
                EXPECT_EQ(String(element->name.CStr()).rfind("Material", 0), 0u) << element->name.CStr() << " starts with Material: elements are found by name in every canvas";
            }
        }

        for (const char* group : {"Top", "Bottom"}) {
            SCOPED_TRACE(group);
            const UIElement& list = MaterialElement(Format("Material{}List", group).c_str());
            EXPECT_NE(list.Cast<UIStack>(), nullptr);
            ASSERT_EQ(list.children.size(), 5u);
            for (size_t index = 0; index < 5; ++index) {
                const UIElement& row = *list.children[index];
                EXPECT_EQ(row.name, PoolString::Intern(Format("Material{}{}Row", group, kMaterialPieces[index]).c_str())) << "the order of the rows: queen, rook, bishop, knight, pawn";
                EXPECT_FALSE(row.style.visible) << row.name.CStr() << " starts hidden";
                ASSERT_EQ(row.children.size(), 2u);
                EXPECT_NE(row.children[0]->GetComponent<UIImageComponent>(), nullptr) << "the icon";
                const UITextComponent* count = IsLabel(*row.children[1]) ? TextOf(*row.children[1]) : nullptr;
                ASSERT_NE(count, nullptr) << "the count";
                EXPECT_EQ(count->text, Format("{{material{}{}}}", group, kMaterialPieces[index]));
            }
            const UIElement& balanceElement = MaterialElement(Format("Material{}Balance", group).c_str());
            const UITextComponent* balance = IsLabel(balanceElement) ? TextOf(balanceElement) : nullptr;
            ASSERT_NE(balance, nullptr);
            EXPECT_EQ(balance->text, Format("{{material{}Balance}}", group));
            EXPECT_EQ(balance->overflow, TextOverflow::AutoFit) << "the sign is shrunk into its box and never cut";
        }
    }

    TEST_F(ChessUiTest, TheListsAndTheBalancesLieInTheMarginOfTheBoardWhereTheLayoutRuleSaysSo) {
        Settle();
        const Rect top = MaterialElement("MaterialTopList").rect;
        const Rect bottom = MaterialElement("MaterialBottomList").rect;
        EXPECT_TRUE(SameRect(top, Rect{8.0f, 84.0f, 64.0f, 216.0f}));
        EXPECT_TRUE(SameRect(bottom, Rect{8.0f, 500.0f, 64.0f, 216.0f}));
        // the box of Game/MaterialLayout.h is the one of the two lists
        EXPECT_FLOAT_EQ(kMaterialListLeft, std::min(top.x, bottom.x));
        EXPECT_FLOAT_EQ(kMaterialListTop, top.y);
        EXPECT_FLOAT_EQ(kMaterialListRight, std::max(top.x + top.w, bottom.x + bottom.w));
        EXPECT_FLOAT_EQ(kMaterialListBottom, bottom.y + bottom.h);
        EXPECT_LE(top.y + top.h, bottom.y) << "the lists do not meet";
        const Rect topBalance = MaterialElement("MaterialTopBalance").rect;
        const Rect bottomBalance = MaterialElement("MaterialBottomBalance").rect;
        EXPECT_TRUE(SameRect(topBalance, Rect{77.0f, 84.0f, 23.0f, 28.0f}));
        EXPECT_TRUE(SameRect(bottomBalance, Rect{77.0f, 500.0f, 23.0f, 28.0f}));
        EXPECT_LE(topBalance.x + topBalance.w, kMaterialMarginRight) << "the balance box ends where the board begins";
        EXPECT_GE(topBalance.x, top.x + top.w) << "and does not touch the list";
    }

    TEST_F(ChessUiTest, TheMaterialStartsWithNoRowShownAndEmptyWords) {
        EXPECT_EQ(MaterialWords(), kEmptyMaterialWords) << "after the first frame";
        Settle();
        EXPECT_EQ(ShownMaterialRows(), "");
        EXPECT_EQ(MaterialWords(), kEmptyMaterialWords);
    }

    TEST_F(ChessUiTest, NoRowIsShownInTheFirstFrameWhateverTheGameSays) {
        // The canvas has not been laid out when the component runs in the first frame, so the safe area is not known and the rows wait: nothing flashes for a frame
        StartFrom(kWhiteTookAPawn);
        Draw();
        EXPECT_EQ(CoreManager::GetUIVars().Get(PoolString::Intern("materialBottomPawn")), "x1") << "the words are there at once";
        EXPECT_EQ(ShownMaterialRows(), "") << "the rows are not";
        Draw();
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn") << "the next frame has the safe area";
    }

    TEST_F(ChessUiTest, ACaptureByClicksShowsThePieceInTheGroupOfTheColorThatTookItAndTheLeadOfThePoints) {
        Settle();
        ClickMove("e2", "e4");
        ClickMove("d7", "d5");
        ASSERT_EQ(ShownMaterialRows(), "") << "nothing is gone yet";
        ClickMove("e4", "d5");
        ASSERT_EQ(Model().GetMoves().size(), 3u);

        EXPECT_EQ(MaterialCount("Bottom", "Pawn"), "x1") << "White is at the bottom, and White has taken a pawn";
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn");
        EXPECT_EQ(MaterialSprite("Bottom", "Pawn"), "BlackPawn") << "the piece that was taken is Black's";
        EXPECT_EQ(MaterialWords(), "||||||||||x1|+1|") << "only the pawn count and the lead of White: the other ten words are empty";
        EXPECT_EQ(Var("materialTopBalance"), "");
        EXPECT_EQ(Var("materialBottomBalance"), "+1");

        ClickMove("d8", "d5");
        ASSERT_EQ(Model().GetMoves().size(), 4u);
        EXPECT_EQ(MaterialCount("Top", "Pawn"), "x1");
        EXPECT_EQ(MaterialCount("Bottom", "Pawn"), "x1");
        EXPECT_EQ(ShownMaterialRows(), "TopPawn BottomPawn");
        EXPECT_EQ(MaterialSprite("Top", "Pawn"), "WhitePawn");
        EXPECT_EQ(Var("materialTopBalance"), "");
        EXPECT_EQ(Var("materialBottomBalance"), "") << "an equal material leads for nobody";
    }

    TEST_F(ChessUiTest, TheRowsOfAGroupFollowTheOrderOfTheTableAndTheBalanceIsInTheGroupThatIsAhead) {
        StartFrom(kBlackTookTheQueen);
        Settle();
        EXPECT_EQ(MaterialCount("Top", "Queen"), "x1") << "Black has taken the queen: the upper group, because White is at the bottom";
        EXPECT_EQ(MaterialSprite("Top", "Queen"), "WhiteQueen");
        EXPECT_EQ(ShownMaterialRows(), "TopQueen");
        EXPECT_EQ(Var("materialTopBalance"), "+9");
        EXPECT_EQ(Var("materialBottomBalance"), "");

        StartFrom(kEightPawnsAgainstNothing);
        Settle();
        EXPECT_EQ(MaterialCount("Bottom", "Queen"), "x1") << "White has taken everything of Black's but the king";
        EXPECT_EQ(MaterialCount("Bottom", "Rook"), "x2");
        EXPECT_EQ(MaterialCount("Bottom", "Bishop"), "x2");
        EXPECT_EQ(MaterialCount("Bottom", "Knight"), "x2");
        EXPECT_EQ(MaterialCount("Bottom", "Pawn"), "x8");
        EXPECT_EQ(MaterialCount("Top", "Queen"), "x1") << "and Black everything of White's but the king and the pawns";
        EXPECT_EQ(MaterialCount("Top", "Pawn"), "");
        EXPECT_EQ(ShownMaterialRows(), "TopQueen TopRook TopBishop TopKnight BottomQueen BottomRook BottomBishop BottomKnight BottomPawn");
        EXPECT_EQ(Var("materialBottomBalance"), "+8");
        EXPECT_EQ(Var("materialTopBalance"), "");
    }

    TEST_F(ChessUiTest, TheShownRowsPackAtTheTopOfTheirListAndAHiddenRowTakesNoRoom) {
        StartFrom(kEightPawnsAgainstNothing);
        Settle();
        const float step = MaterialRow("Bottom", "Rook").rect.y - MaterialRow("Bottom", "Queen").rect.y;
        EXPECT_NEAR(step, 44.0f, kPixelTolerance) << "a row is 40 units high and 4 apart";
        EXPECT_NEAR(MaterialRow("Bottom", "Queen").rect.y, 500.0f, kPixelTolerance);

        StartFrom(kWhiteTookAPawn);
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "BottomPawn");
        EXPECT_NEAR(MaterialRow("Bottom", "Pawn").rect.y, 500.0f, kPixelTolerance) << "the first row of the list: the four above it take no room";
        EXPECT_TRUE(MaterialRow("Bottom", "Queen").rect.IsEmpty());
    }

    TEST_F(ChessUiTest, WithTheProgramAsWhiteTheGroupsSwapWithTheBoard) {
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kWhiteTookAPawn);
        Settle();
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn") << "two people: White at the bottom, and White has taken the pawn";

        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        Settle();
        EXPECT_EQ(MaterialCount("Top", "Pawn"), "x1") << "Black is at the bottom now: White's captures are in the upper group";
        EXPECT_EQ(MaterialCount("Bottom", "Pawn"), "");
        EXPECT_EQ(ShownMaterialRows(), "TopPawn");
        EXPECT_EQ(MaterialSprite("Top", "Pawn"), "BlackPawn") << "the sprite is the piece that was taken: the upper group is White's, so the pieces are Black's";
        EXPECT_EQ(MaterialSprite("Bottom", "Pawn"), "WhitePawn") << "the icons of the lower group were given the other color too";
        EXPECT_EQ(Var("materialTopBalance"), "+1") << "the lead goes with the group";
        EXPECT_EQ(Var("materialBottomBalance"), "");

        ControlPlays(nullptr);
        _manager->UpdateAll();
        Settle();
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn") << "nobody plays: White is at the bottom again";
        EXPECT_EQ(MaterialSprite("Bottom", "Pawn"), "BlackPawn");
        EXPECT_EQ(Var("materialBottomBalance"), "+1");
    }

    TEST_F(ChessUiTest, WithTheProgramAsBlackAndWithTwoPeopleWhiteStaysAtTheBottomOfTheDisplayToo) {
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kBlackTookTheQueen);
        ControlPlays("black");
        _manager->UpdateAll();
        ASSERT_FALSE(BlackIsAtTheBottom());
        Settle();
        EXPECT_EQ(ShownMaterialRows(), "TopQueen") << "the program plays Black: White stays at the bottom, and Black's captures are in the upper group";
        EXPECT_EQ(Var("materialTopBalance"), "+9");
    }

    TEST_F(ChessUiTest, ThePanelTurnsTheDisplayWithTheBoard) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kBlackTookTheQueen);
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "TopQueen");

        PressWith(kSetColorAction, "black");  // the person plays Black, but two people are playing: nothing turns
        _manager->UpdateAll();
        Settle();
        EXPECT_EQ(ShownMaterialRows(), "TopQueen");

        PressWith(kSetModeAction, "computer");  // the person has Black, the program White: Black is at the bottom
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        Settle();
        EXPECT_EQ(ShownMaterialRows(), "BottomQueen") << "Black's capture is in the lower group now";
        EXPECT_EQ(MaterialSprite("Bottom", "Queen"), "WhiteQueen");
        EXPECT_EQ(Var("materialBottomBalance"), "+9");
        EXPECT_EQ(Var("materialTopBalance"), "");
    }

    TEST_F(ChessUiTest, TheDisplayFollowsAResetASetStateAndASetPosition) {
        StartFrom(kBlackTookTheQueen);
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "TopQueen");

        Model().Reset();
        Settle();
        EXPECT_EQ(MaterialWords(), kEmptyMaterialWords) << "a new game: nothing is gone";
        EXPECT_EQ(ShownMaterialRows(), "");

        ASSERT_TRUE(FenSetsTheState(kWhiteTookAPawn)) << "set_state of the control API";
        _manager->UpdateAll();
        Settle();
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn");
        EXPECT_EQ(Var("materialBottomBalance"), "+1");

        StartFrom(kBlackTookTheQueen);  // SetPosition
        Settle();
        EXPECT_EQ(ShownMaterialRows(), "TopQueen");
        EXPECT_EQ(Var("materialBottomBalance"), "");
        EXPECT_EQ(Var("materialTopBalance"), "+9");
    }

    TEST_F(ChessUiTest, APromotionChangesTheBalanceAndTheCountOfThePawns) {
        StartFrom(kPawnAboutToPromote);
        Settle();
        EXPECT_EQ(MaterialCount("Top", "Queen"), "x1") << "White has no queen";
        EXPECT_EQ(MaterialCount("Top", "Pawn"), "x7");
        EXPECT_EQ(Var("materialBottomBalance"), "+1") << "a pawn against a bare king";

        ClickMove("a7", "a8");
        ASSERT_TRUE(Model().HasPendingPromotion());
        PressWith(kPromoteAction, "queen");
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        Settle();
        EXPECT_EQ(Var("materialBottomBalance"), "+9") << "the new queen counts: nine points against nothing";
        EXPECT_EQ(MaterialCount("Top", "Queen"), "") << "White has a queen again: none is gone";
        EXPECT_EQ(MaterialCount("Top", "Pawn"), "x8") << "and its pawn is: the position cannot tell that a pawn became the queen";
    }

    TEST_F(ChessUiTest, AMoveOfTheProgramIsSeenByTheDisplayInTheSameFrameItIsPlayed) {
        StartFrom(kBlackCanTakeAPawn);
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "");
        const auto take = MoveGenerator::ParseUci(Model().GetPosition(), "d5e4");
        ASSERT_TRUE(take.has_value());
        const auto engine = MakeShared<GatedEngine>(true, *take);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 1; })) << "the program took the pawn";
        Settle();
        EXPECT_EQ(MaterialCount("Top", "Pawn"), "x1") << "Black is the program and has taken a pawn: the upper group, White is at the bottom";
        EXPECT_EQ(MaterialSprite("Top", "Pawn"), "WhitePawn");
        EXPECT_EQ(Var("materialTopBalance"), "+1");
        EXPECT_EQ(ShownMaterialRows(), "TopPawn");
    }

    TEST_F(ChessUiTest, WithNoGameActiveTheDisplayIsEmpty) {
        StartFrom(kWhiteTookAPawn);
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "BottomPawn");

        GameManager::GetInstance().Shutdown();
        ASSERT_EQ(ChessGame::GetActive(), nullptr);
        Draw();
        Draw();
        EXPECT_EQ(MaterialWords(), kEmptyMaterialWords);
        EXPECT_EQ(ShownMaterialRows(), "");
    }

    TEST_F(ChessUiTest, TheMaterialCanvasTakesNoClickAndTheBoardStillGetsItsOwn) {
        StartFrom(kWhiteTookAPawn);
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "BottomPawn");
        const auto canvas = Material();
        ASSERT_TRUE(canvas);

        // a hit test of the canvas finds nothing at the middle of any element, whether it is shown or not
        for (const UIElement* element : AllElements(canvas->GetRoot())) {
            const Rect& rect = element->rect;
            EXPECT_EQ(canvas->GetLayout().HitTest(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f), nullptr) << element->name.CStr();
        }
        // and a press where the shown row and the balance are drawn is not taken by the UI
        for (const char* name : {"MaterialBottomPawnRow", "MaterialBottomBalance", "MaterialBottomList", "MaterialTopBalance"}) {
            const auto [x, y] = ScreenCenter(MaterialElement(name));
            EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true))) << name;
            EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false))) << name;
        }

        // the board right next to the balance boxes (their right edge is the left edge of the board) takes a press
        ASSERT_TRUE(Model().GetTurn() == PieceColor::Black);
        const Rect nextToTheBalance = OutputRect(Rect{kMaterialMarginRight + 1.0f, 100.0f, 2.0f, 2.0f});  // inside the cell of a8, a hair from the balance box
        EXPECT_FALSE(Model().HasSelection());
        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(nextToTheBalance.x + nextToTheBalance.w * 0.5f, nextToTheBalance.y + nextToTheBalance.h * 0.5f, true));
        ASSERT_TRUE(Model().HasSelection()) << "the press went through the margin to the board";
        EXPECT_EQ(Model().GetSelected(), Sq("a8"));
    }

    TEST_F(ChessUiTest, TheMaterialIsDrawnOverThePiecesAndTheResultBarAndUnderTheHudAndThePanels) {
        const auto king = GiveAtlasCell("WhiteKing_e1");
        StartFrom(kBlackMatesOnA1);
        ASSERT_TRUE(Play(Model(), "a8a1"));
        ASSERT_TRUE(Model().IsGameOver());
        _manager->UpdateAll();
        Settle();
        ASSERT_NE(ShownMaterialRows(), "") << "most of the pieces are gone";
        ASSERT_TRUE(OpenThePanel());
        Draw();

        const auto& calls = _renderer->calls;
        long lastSquare = -1;
        long firstGlyph = -1;
        long lastGlyph = -1;
        long firstPiece = -1;
        long dim = -1;
        long firstChip = -1;
        long lastChip = -1;
        long resetButton = -1;
        long plate = -1;
        for (size_t i = 0; i < calls.size(); ++i) {
            const DrawCall& call = calls[i];
            const long at = static_cast<long>(i);
            if (call.kind == DrawKind::Rect && (SameColor(call.color, kLightSquareColor) || SameColor(call.color, kDarkSquareColor))) {
                lastSquare = at;
            } else if (IsLabelGlyph(call)) {
                firstGlyph = firstGlyph < 0 ? at : firstGlyph;
                lastGlyph = at;
            } else if (call.kind == DrawKind::Texture && call.texture == king.Get()) {
                firstPiece = firstPiece < 0 ? at : firstPiece;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kBoardDimColor)) {
                dim = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kMaterialChipColor)) {
                firstChip = firstChip < 0 ? at : firstChip;
                lastChip = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kResetColor)) {
                resetButton = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kPlateColor)) {
                plate = at;
            }
        }
        ASSERT_GE(lastSquare, 0);
        ASSERT_GE(lastGlyph, 0);
        ASSERT_GE(firstPiece, 0) << "the king is drawn";
        ASSERT_GE(dim, 0) << "the game is over: the board is dimmed";
        ASSERT_GE(firstChip, 0) << "the rows are drawn";
        ASSERT_GE(resetButton, 0);
        ASSERT_GE(plate, 0) << "the opponent panel is open";
        EXPECT_LT(lastSquare, firstChip) << "over the squares";
        EXPECT_LT(lastGlyph, firstChip) << "and over the coordinates";
        EXPECT_LT(firstPiece, firstChip) << "over the pieces";
        EXPECT_LT(dim, firstChip) << "and over the dim of the finished game";
        EXPECT_LT(lastChip, resetButton) << "under the HUD";
        EXPECT_LT(lastChip, plate) << "and under the panels";
    }

    TEST_F(ChessUiTest, InTheNarrowestWindowWithSideInsetsTheListsAreHiddenAndTheBalanceStays) {
        StartFrom(kWhiteTookAPawn);
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "BottomPawn") << "a window with no insets: the lists are shown";

        ResizeWithSideInsets(kNarrowestWidth, kNarrowestHeight);
        const auto canvas = Material();
        ASSERT_TRUE(canvas);
        const Rect safe = canvas->GetLayout().GetResolved().safeBounds;
        EXPECT_NEAR(safe.x, 75.6f, 0.2f) << "9 % of 480 px at the fit scale 0.571";
        EXPECT_EQ(ShownMaterialRows(), "") << "the box of the lists reaches out of the safe area";
        EXPECT_EQ(Var("materialBottomPawn"), "x1") << "the words are not taken away, only the rows";
        EXPECT_EQ(Var("materialBottomBalance"), "+1") << "the balance is still there";

        const UIElement& balance = MaterialElement("MaterialBottomBalance");
        ASSERT_TRUE(IsLabel(balance));
        EXPECT_EQ(ShownText(balance), "+1");
        EXPECT_GE(balance.rect.x, safe.x) << "inside the safe area";
        EXPECT_LE(balance.rect.x + balance.rect.w, kMaterialMarginRight) << "inside the margin";
        EXPECT_FALSE(balance.rect.IsEmpty());
        EXPECT_TRUE(balance.active.visible);

        // The sign of a big lead is shrunk into its box (AutoFit), not cut (the text component does not expose its last layout, so the check is the component's own fit)
        const float scale = canvas->GetLayout().GetResolved().scale;
        EXPECT_EQ(TextOf(balance)->overflow, TextOverflow::AutoFit);
        for (const char* lead : {"+1", "+39", "+99"}) {
            CoreManager::GetUIVars().Set(PoolString::Intern("materialBottomBalance"), lead);
            Draw();
            const UIContentFit fit = balance.CheckContent(balance.rect.w, balance.rect.h, scale);
            EXPECT_TRUE(fit.hasContent) << lead;
            EXPECT_TRUE(fit.fits) << lead << " is inside its box";
        }

        ResizeWithoutInsets(1280.0f, 720.0f);
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn") << "the window grows again: the rows come back";
        ResizeWithSideInsets(kNarrowestWidth, kNarrowestHeight);
        EXPECT_EQ(ShownMaterialRows(), "");
    }

    TEST_F(ChessUiTest, AHiddenListStaysHiddenWhenTheGameChangesAndAppearsWhenTheWindowHasRoom) {
        StartFrom(kWhiteTookAPawn);
        ResizeWithSideInsets(kNarrowestWidth, kNarrowestHeight);
        ASSERT_EQ(ShownMaterialRows(), "");
        ASSERT_TRUE(Play(Model(), "d8d5")) << "Black takes the pawn back";
        Settle();
        EXPECT_EQ(MaterialCount("Top", "Pawn"), "x1");
        EXPECT_EQ(ShownMaterialRows(), "") << "a change of the counts does not show a row the window has no room for";
        EXPECT_EQ(Var("materialTopBalance"), "");

        ResizeWithoutInsets(1280.0f, 720.0f);
        EXPECT_EQ(ShownMaterialRows(), "TopPawn BottomPawn");
    }

    // ---- the captured material in the Portrait view ----------------------------------------------------------------------------------------
    // The view is 720 x 960 (SceneConfig.xml): a 720 x 960 window shows it one to one, so a rect of the layout is the pixel it is drawn at

    namespace {

        constexpr float kPortraitWindowWidth = kPortraitViewWidth;
        constexpr float kPortraitWindowHeight = kPortraitViewHeight;
        constexpr float kPortraitRowStep = 68.0f;  // a row is 64 units wide and 4 apart
        constexpr float kPortraitUpperListY = 92.0f;
        constexpr float kPortraitLowerListY = 792.0f;

    }  // namespace

    TEST_F(ChessUiTest, InThePortraitViewTheGroupsAreTwoRowsAboveAndBelowTheBoardInTheBoxOfTheLayoutRule) {
        StartFrom("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
        DrawAt(kPortraitWindowWidth, kPortraitWindowHeight);
        DrawAt(kPortraitWindowWidth, kPortraitWindowHeight);
        ASSERT_TRUE(PortraitIsActive());
        ASSERT_EQ(ShownMaterialRows(), "TopQueen TopRook TopBishop TopKnight TopPawn BottomQueen BottomRook BottomBishop BottomKnight BottomPawn");

        const Rect top = MaterialElement("MaterialTopList").rect;
        const Rect bottom = MaterialElement("MaterialBottomList").rect;
        EXPECT_TRUE(SameRect(top, Rect{40.0f, kPortraitUpperListY, 336.0f, 40.0f}));
        EXPECT_TRUE(SameRect(bottom, Rect{40.0f, kPortraitLowerListY, 336.0f, 40.0f}));
        // the box of Game/MaterialLayout.h is the one of the two lists
        EXPECT_FLOAT_EQ(kMaterialPortraitListLeft, std::min(top.x, bottom.x));
        EXPECT_FLOAT_EQ(kMaterialPortraitListTop, top.y);
        EXPECT_FLOAT_EQ(kMaterialPortraitListRight, std::max(top.x + top.w, bottom.x + bottom.w));
        EXPECT_FLOAT_EQ(kMaterialPortraitListBottom, bottom.y + bottom.h);
        EXPECT_LE(top.y + top.h, kPortraitBoardTop) << "the upper list is above the board";
        EXPECT_GE(bottom.y, kPortraitBoardBottom) << "the lower one is under it";

        for (const char* group : kMaterialGroups) {
            SCOPED_TRACE(group);
            const Rect list = group == kMaterialGroups[0] ? top : bottom;
            for (size_t index = 0; index < std::size(kMaterialPieces); ++index) {
                const Rect& row = MaterialRow(group, kMaterialPieces[index]).rect;
                EXPECT_TRUE(SameRect(row, Rect{list.x + static_cast<float>(index) * kPortraitRowStep, list.y, 64.0f, 40.0f})) << kMaterialPieces[index] << ": the rows go along the list, queen first";
            }
            const Rect balance = MaterialElement(Format("Material{}Balance", group).c_str()).rect;
            EXPECT_TRUE(SameRect(balance, Rect{384.0f, list.y, 48.0f, 40.0f})) << "beside the end of the list";
            EXPECT_GE(balance.x, list.x + list.w);
        }
    }

    TEST_F(ChessUiTest, InThePortraitViewTheShownRowsPackAtTheStartOfTheirListAndAHiddenRowTakesNoRoom) {
        StartFrom(kWhiteTookAPawn);
        DrawAt(kPortraitWindowWidth, kPortraitWindowHeight);
        DrawAt(kPortraitWindowWidth, kPortraitWindowHeight);
        ASSERT_TRUE(PortraitIsActive());
        ASSERT_EQ(ShownMaterialRows(), "BottomPawn");
        EXPECT_TRUE(SameRect(MaterialRow("Bottom", "Pawn").rect, Rect{40.0f, kPortraitLowerListY, 64.0f, 40.0f})) << "the first place of the list: the four before it take no room";
        EXPECT_TRUE(MaterialRow("Bottom", "Queen").rect.IsEmpty());

        StartFrom(kEightPawnsAgainstNothing);
        DrawAt(kPortraitWindowWidth, kPortraitWindowHeight);
        DrawAt(kPortraitWindowWidth, kPortraitWindowHeight);
        EXPECT_NEAR(MaterialRow("Bottom", "Rook").rect.x - MaterialRow("Bottom", "Queen").rect.x, kPortraitRowStep, kPixelTolerance) << "a row is 64 units wide and 4 apart";
        EXPECT_NEAR(MaterialRow("Bottom", "Queen").rect.x, 40.0f, kPixelTolerance);
        EXPECT_NEAR(MaterialRow("Bottom", "Pawn").rect.x, 40.0f + 4.0f * kPortraitRowStep, kPixelTolerance);
    }

    TEST_F(ChessUiTest, InThePortraitViewTheGroupsSwapWithTheBoardAndTheBalanceGoesWithItsGroup) {
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kWhiteTookAPawn);
        DrawAt(kPortraitWindowWidth, kPortraitWindowHeight);
        DrawAt(kPortraitWindowWidth, kPortraitWindowHeight);
        ASSERT_TRUE(PortraitIsActive());
        ASSERT_EQ(ShownMaterialRows(), "BottomPawn") << "two people: White at the bottom, and White has taken the pawn";
        EXPECT_NEAR(MaterialRow("Bottom", "Pawn").rect.y, kPortraitLowerListY, kPixelTolerance) << "under the board";
        EXPECT_EQ(Var("materialBottomBalance"), "+1");

        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        DrawAt(kPortraitWindowWidth, kPortraitWindowHeight);
        DrawAt(kPortraitWindowWidth, kPortraitWindowHeight);
        EXPECT_EQ(ShownMaterialRows(), "TopPawn") << "Black is at the bottom now: White's capture is in the upper group, above the board";
        EXPECT_NEAR(MaterialRow("Top", "Pawn").rect.y, kPortraitUpperListY, kPixelTolerance);
        EXPECT_EQ(Var("materialTopBalance"), "+1");
        EXPECT_EQ(ShownText(MaterialElement("MaterialTopBalance")), "+1");
        EXPECT_NEAR(MaterialElement("MaterialTopBalance").rect.y, kPortraitUpperListY, kPixelTolerance);
        EXPECT_EQ(Var("materialBottomBalance"), "");
    }

    TEST_F(ChessUiTest, InThePortraitViewTheListsAreHiddenWhereTheSafeAreaTakesTheirBoxAndTheEdgeCountsAsInside) {
        StartFrom(kWhiteTookAPawn);
        const auto resize = [&](const Rect& safeArea) {
            _renderer->width = kPortraitWindowWidth;
            _renderer->height = kPortraitWindowHeight;
            CoreManager::GetUIEnvironment().SetSafeAreaOverride(safeArea);
            Settle();
            ASSERT_TRUE(PortraitIsActive());
        };
        resize(Rect{0.0f, 0.0f, kPortraitWindowWidth, kPortraitWindowHeight});
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn");
        resize(Rect{kMaterialPortraitListLeft, kMaterialPortraitListTop, kMaterialPortraitListRight - kMaterialPortraitListLeft, kMaterialPortraitListBottom - kMaterialPortraitListTop});
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn") << "a safe area that is the box itself: the edge counts as inside";

        resize(Rect{kMaterialPortraitListLeft + 1.0f, 0.0f, kPortraitWindowWidth - kMaterialPortraitListLeft - 1.0f, kPortraitWindowHeight});
        EXPECT_EQ(ShownMaterialRows(), "") << "an inset on the left that reaches into the box";
        resize(Rect{0.0f, kMaterialPortraitListTop + 1.0f, kPortraitWindowWidth, kPortraitWindowHeight - kMaterialPortraitListTop - 1.0f});
        EXPECT_EQ(ShownMaterialRows(), "") << "a notch at the top that reaches into the upper row's place";
        resize(Rect{0.0f, 0.0f, kPortraitWindowWidth, kMaterialPortraitListBottom - 1.0f});
        EXPECT_EQ(ShownMaterialRows(), "") << "an inset at the bottom that reaches into the lower row";
        resize(Rect{0.0f, 0.0f, kMaterialPortraitListRight - 1.0f, kPortraitWindowHeight});
        EXPECT_EQ(ShownMaterialRows(), "") << "an inset on the right";
        EXPECT_EQ(Var("materialBottomPawn"), "x1") << "the words are not taken away, only the rows";
        EXPECT_EQ(ShownText(MaterialElement("MaterialBottomBalance")), "+1") << "the balance stays";

        resize(Rect{0.0f, 0.0f, kPortraitWindowWidth, kPortraitWindowHeight});
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn") << "the room is back: so are the rows";
    }

    TEST_F(ChessUiTest, InThePortraitViewWithSideInsetsInTheNarrowestWindowTheListsAreHiddenAndTheBalanceStaysInsideTheSafeArea) {
        StartFrom(kWhiteTookAPawn);
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "BottomPawn") << "the default window: the lists are shown";

        ResizeWithSideInsets(kNarrowestWidth, kNarrowestPortraitHeight);
        ASSERT_TRUE(PortraitIsActive());
        const Rect safe = Material()->GetLayout().GetResolved().safeBounds;
        EXPECT_NEAR(safe.x, 64.8f, 0.2f) << "9 % of 480 px at the fit scale 0.667";
        EXPECT_EQ(ShownMaterialRows(), "") << "the box of the lists starts at x 40, left of the safe area";
        EXPECT_EQ(Var("materialBottomPawn"), "x1");
        EXPECT_EQ(Var("materialBottomBalance"), "+1");
        const UIElement& balance = MaterialElement("MaterialBottomBalance");
        ASSERT_TRUE(IsLabel(balance));
        EXPECT_EQ(ShownText(balance), "+1");
        EXPECT_TRUE(LiesIn(balance.rect, safe)) << "the balance is inside the safe area";
        EXPECT_FALSE(balance.rect.IsEmpty());

        ResizeWithoutInsets(kNarrowestWidth, kNarrowestPortraitHeight);
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn") << "no insets: the rows come back";
    }

    TEST_F(ChessUiTest, TheMaterialFollowsTheViewWhenTheWindowTurns) {
        StartFrom(kWhiteTookAPawn);
        Settle();
        ASSERT_FALSE(PortraitIsActive());
        ASSERT_EQ(ShownMaterialRows(), "BottomPawn");
        EXPECT_TRUE(SameRect(MaterialRow("Bottom", "Pawn").rect, Rect{8.0f, 500.0f, 64.0f, 40.0f}));

        ResizeWithoutInsets(kPortraitWindowWidth, kPortraitWindowHeight);
        ASSERT_TRUE(PortraitIsActive());
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn");
        EXPECT_TRUE(SameRect(MaterialRow("Bottom", "Pawn").rect, Rect{40.0f, kPortraitLowerListY, 64.0f, 40.0f})) << "under the board";

        ResizeWithoutInsets(1280.0f, 720.0f);
        ASSERT_FALSE(PortraitIsActive());
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn");
        EXPECT_TRUE(SameRect(MaterialRow("Bottom", "Pawn").rect, Rect{8.0f, 500.0f, 64.0f, 40.0f})) << "back in the margin left of the board";
    }

    TEST_F(ChessUiTest, InThePortraitViewTheMaterialTakesNoClick) {
        StartFrom("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
        DrawAt(kPortraitWindowWidth, kPortraitWindowHeight);
        DrawAt(kPortraitWindowWidth, kPortraitWindowHeight);
        ASSERT_TRUE(PortraitIsActive());
        ASSERT_NE(ShownMaterialRows(), "");
        for (const char* name : {"MaterialTopQueenRow", "MaterialBottomPawnRow", "MaterialTopBalance", "MaterialBottomBalance", "MaterialTopList", "MaterialBottomList"}) {
            const auto [x, y] = ScreenCenter(MaterialElement(name));
            EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true))) << name;
            EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false))) << name;
        }
    }

    TEST_F(ChessUiTest, EveryBalanceFromPlusOneToPlusNinetyNineFitsItsBoxInTheNarrowestWindowOfEachView) {
        Draw();
        auto canvas = Material();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        UICanvasLayout& layout = canvas->GetLayout();

        size_t checked = 0;
        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            LayOutInTheNarrowestWindow(layout, shape);
            EXPECT_NEAR(layout.GetResolved().scale, kNarrowestWidth / shape.viewWidth, 0.01f);
            const float scale = layout.GetResolved().scale;
            for (const char* group : {"Top", "Bottom"}) {
                const UIElement* label = &MaterialElement(Format("Material{}Balance", group).c_str());
                ASSERT_TRUE(IsLabel(*label));
                const PoolString variable = PoolString::Intern(Format("material{}Balance", group).c_str());
                for (int points = 1; points <= 99; ++points) {
                    const String text = BalanceText(points);
                    CoreManager::GetUIVars().Set(variable, StringView(text));
                    const UIContentFit fit = label->CheckContent(label->rect.w, label->rect.h, scale);
                    ASSERT_TRUE(fit.hasContent) << text.c_str();
                    EXPECT_TRUE(fit.fits) << group << ": " << text.c_str() << " is cut in a box of " << label->rect.w << " x " << label->rect.h << " units at scale " << scale;
                    ++checked;
                }
            }
        }
        EXPECT_EQ(checked, 2 * 198u);
        layout.Invalidate();
    }

    // ---- the captured material: the layout ---------------------------------------------------------------------------------------------
    // The layout of the node Material. The rows are shown by hand for the matrix (the component shows only the rows of the pieces that are gone), so the lists are laid out at their full size

    namespace {

        /// The elements of the canvas Material as the shipped XML has them, with the rows shown (the copies of the tests start from this text)
        String ShownMaterialXml() {
            String xml = ShippedElementsXml("Material");
            const String hidden = " visible=\"false\"";
            for (size_t at = xml.find(hidden); at != String::npos; at = xml.find(hidden)) {
                xml.erase(at, hidden.size());
            }
            return xml;
        }

    }  // namespace

    TEST_F(ChessUiTest, TheMaterialCanvasHasTheElementsAndTheOrderOfTheScene) {
        const auto canvas = Material();
        ASSERT_TRUE(canvas);
        Vector<String> expected;
        for (const char* group : kMaterialGroups) {
            expected.push_back(Format("Material{}List", group));
            for (const char* piece : kMaterialPieces) {
                expected.push_back(Format("Material{}{}Row", group, piece));
                expected.push_back(Format("Material{}{}Icon", group, piece));
                expected.push_back(Format("Material{}{}Count", group, piece));
            }
            expected.push_back(Format("Material{}Balance", group));
        }
        const Vector<const UIElement*> elements = AllElements(canvas->GetRoot());
        ASSERT_EQ(elements.size(), 35u) << "the root and 2 lists, 10 rows, 10 icons, 10 counts and 2 balances";
        size_t stacks = 0;
        size_t images = 0;
        size_t labels = 0;
        for (size_t index = 0; index < expected.size(); ++index) {
            const UIElement& element = *elements[index + 1];
            EXPECT_EQ(element.name, PoolString::Intern(expected[index].c_str())) << "element " << index << " in the order of the scene: list, rows (row, icon, count), balance";
            stacks += element.Cast<UIStack>() != nullptr ? 1 : 0;
            images += element.GetComponent<UIImageComponent>() != nullptr ? 1 : 0;
            labels += IsLabel(element) ? 1 : 0;
        }
        EXPECT_EQ(stacks, 12u) << "two lists and ten rows";
        EXPECT_EQ(images, 10u);
        EXPECT_EQ(labels, 12u) << "ten counts and two balances";
    }

    TEST_F(ChessUiTest, TheMaterialCanvasPassesTheSizeMatrixWithEveryCountAndEverySign) {
        Draw();
        auto canvas = Material();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        ShowEveryMaterialRow(*canvas);

        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        for (size_t round = 0; round < kMaterialRounds; ++round) {
            PublishMaterialRound(round);
            const auto failures = ValidateOnMatrix(canvas->GetLayout(), options, ViewContext(), SceneViews());
            ASSERT_TRUE(failures.empty()) << "round " << round << ": " << DescribeMatrixFailures(failures).c_str();
        }
        const String problems = MaterialProblemsOnMatrix(canvas->GetLayout());
        EXPECT_TRUE(problems.empty()) << problems.c_str();
    }

    TEST_F(ChessUiTest, TheLongestCountWordOfTheMatrixIsTheMostARowCanShow) {
        // Two bare kings: everything else is gone. The summary is where the largest count comes from; the matrix lays out every word up to it
        const Position bare = PositionFromFen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
        const MaterialSummary summary = SummarizeMaterial(bare);
        int most = 0;
        for (const PieceColor color : {PieceColor::White, PieceColor::Black}) {
            for (const PieceType type : CapturableTypes()) {
                most = std::max(most, summary.GetMissing(color, type));
            }
        }
        EXPECT_EQ(most, kMostCaptured) << "the pawns: the longest count word is 'x" << most << "'";
    }

    TEST_F(ChessUiTest, TheListsAreHiddenOnlyInTheWindowsWhoseSideInsetsTakeTheBoxOfTheirView) {
        Draw();
        auto canvas = Material();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = canvas->GetLayout();

        size_t hidden[2] = {};  // the default view, then Portrait
        size_t shown[2] = {};
        String hiddenCases;
        for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
            layout.Update(ContextOf(matrixCase, true));
            const bool portrait = LaidOutForPortrait(layout);
            const Rect safe = layout.GetResolved().safeBounds;
            const bool fits = MaterialListFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h, portrait ? MaterialView::Portrait : MaterialView::Default);
            const bool sideInsets = matrixCase.label.find("side insets") != String::npos;
            const float boxLeft = portrait ? kMaterialPortraitListLeft : kMaterialListLeft;
            if (!fits) {
                EXPECT_TRUE(sideInsets) << matrixCase.label.c_str() << ": only side insets take the box (a notch takes the top 7 % of the short side, which is above the lists)";
                EXPECT_GT(safe.x, boxLeft) << matrixCase.label.c_str();
                ++hidden[portrait ? 1 : 0];
                hiddenCases += Format("  {} (safe area from x = {:.1f}, {})\n", matrixCase.label.c_str(), safe.x, portrait ? "Portrait" : "default view");
            } else {
                ++shown[portrait ? 1 : 0];
                EXPECT_LE(safe.x, boxLeft) << matrixCase.label.c_str();
            }
        }
        layout.Invalidate();
        // The side inset is 9 % of the short side in pixels. Where the short side is the width (1:1 and the three portrait shapes) the view fills the width and the inset is 9 % of its
        // width in units, whatever the size: 75.6 of the 840 of the default view, 64.8 of the 720 of Portrait (more than the 40 its box starts at). In a landscape window the view is
        // fitted to the height, the inset is 72 units at most, and the bars at the sides of the view (113 units or more, at 4:3) are wider than that
        for (const size_t view : {size_t{0}, size_t{1}}) {
            EXPECT_GT(hidden[view], 0u) << (view == 0 ? "default view" : "Portrait") << ": the narrowest window of the matrix is one of them";
            EXPECT_GT(shown[view], 0u) << (view == 0 ? "default view" : "Portrait");
        }
        EXPECT_GT(shown[0], hidden[0]);
        std::cout << "[   INFO   ] the lists are hidden in " << hidden[0] << " of " << hidden[0] + shown[0] << " cases of the default view and in " << hidden[1] << " of " << hidden[1] + shown[1]
                  << " of Portrait, over the size matrix:\n"
                  << hiddenCases.c_str();
    }

    TEST_F(ChessUiTest, NoMaterialElementIsCutOrOutOfPlaceInTheNarrowestWindow) {
        Draw();
        auto canvas = Material();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);
        ShowEveryMaterialRow(*canvas);
        UICanvasLayout& layout = canvas->GetLayout();
        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            const bool portrait = shape.portrait;
            LayOutInTheNarrowestWindow(layout, shape);
            const Rect safe = layout.GetResolved().safeBounds;
            EXPECT_NEAR(safe.x, portrait ? 64.8f : 75.6f, 0.2f) << "9 % of 480 px at the fit scale";
            EXPECT_FALSE(MaterialListFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h, portrait ? MaterialView::Portrait : MaterialView::Default))
                << "the lists are hidden here: the component shows no row";

            for (size_t round = 0; round < kMaterialRounds; ++round) {
                PublishMaterialRound(round);
                const String problems = MaterialProblems(layout);
                ASSERT_TRUE(problems.empty()) << "round " << round << ":\n" << problems.c_str();
            }
            // The balance boxes are the part that stays: inside the safe area, and clear of the board and of the coordinate labels (the default view: the margin ends at x 100)
            for (const char* group : kMaterialGroups) {
                const UIElement& balance = MaterialElement(Format("Material{}Balance", group).c_str());
                EXPECT_GE(balance.rect.x, safe.x) << group;
                EXPECT_LE(balance.rect.x + balance.rect.w, portrait ? safe.x + safe.w : kMaterialMarginRight) << group;
            }
        }
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, TheShownMaterialIsClearOfTheHudAndTheCoordinateLabelsOnTheSizeMatrix) {
        Draw();
        auto material = Material();
        auto hud = Hud();
        auto coordinates = Coordinate();
        ASSERT_TRUE(material);
        ASSERT_TRUE(hud);
        ASSERT_TRUE(coordinates);
        material->UpdateLayout(1280.0f, 720.0f);
        hud->UpdateLayout(1280.0f, 720.0f);
        coordinates->UpdateLayout(1280.0f, 720.0f);
        ShowEveryMaterialRow(*material);
        PublishMaterialRound(0);

        size_t notchCasesWithAnOverlap[2] = {};  // the default view, then Portrait
        size_t notchCases[2] = {};
        String portraitOverlaps;
        for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
            const UILayoutContext context = ContextOf(matrixCase, true);
            material->GetLayout().Update(context);
            hud->GetLayout().Update(context);
            coordinates->GetLayout().Update(context);
            const bool portrait = LaidOutForPortrait(material->GetLayout());
            const String labels = MaterialClearance(material->GetLayout(), coordinates->GetLayout(), 1.0f);
            EXPECT_TRUE(labels.empty()) << matrixCase.label.c_str() << ":\n" << labels.c_str();
            const String controls = MaterialClearance(material->GetLayout(), hud->GetLayout(), kMinGap);
            if (matrixCase.label.find("notch") != String::npos) {
                const size_t view = portrait ? 1 : 0;
                ++notchCases[view];
                if (!controls.empty()) {
                    ++notchCasesWithAnOverlap[view];
                    if (portrait) {
                        portraitOverlaps += Format("  {}\n", matrixCase.label.c_str());
                    }
                    // Not only close: the Turn label lies on top of the first row of the upper group, and in the default view of its balance too (src/Chess/Components/CLAUDE.md, "The captured
                    // material", "Known limits"); in Portrait the balance is beside the rows, to the right of the label
                    const String touching = MaterialClearance(material->GetLayout(), hud->GetLayout(), 0.0f);
                    EXPECT_EQ(touching.find("MaterialTopBalance (") != String::npos, !portrait) << matrixCase.label.c_str() << ":\n" << touching.c_str();
                    EXPECT_NE(touching.find("MaterialTopQueenRow ("), String::npos) << matrixCase.label.c_str() << ":\n" << touching.c_str();
                    EXPECT_NE(touching.find(") and Turn ("), String::npos) << matrixCase.label.c_str() << ":\n" << touching.c_str();
                }
            } else {
                EXPECT_TRUE(controls.empty()) << matrixCase.label.c_str() << ":\n" << controls.c_str();
            }
        }
        material->GetLayout().Invalidate();
        hud->GetLayout().Invalidate();
        coordinates->GetLayout().Invalidate();
        // The counts are pinned: a change of the layout or of the matrix moves them and has to be looked at
        EXPECT_EQ(notchCases[0], 32u);
        EXPECT_EQ(notchCasesWithAnOverlap[0], 32u);
        EXPECT_EQ(notchCases[1], 14u);
        EXPECT_EQ(notchCasesWithAnOverlap[1], 5u) << "only where the view fills the window (a taller window has a bar above it, which takes the notch)";
        // The HUD keeps inside the safe area, and a notch pushes its top row down by 7 % of the short side, onto the first row of the upper group
        std::cout << "[   INFO   ] the HUD reaches the top group in " << notchCasesWithAnOverlap[0] << " of " << notchCases[0] << " notch cases of the default view and in "
                  << notchCasesWithAnOverlap[1] << " of " << notchCases[1] << " of Portrait:\n"
                  << portraitOverlaps.c_str() << "\n";
    }

    TEST_F(ChessUiTest, TheShownMaterialStaysInItsPlaceAndClearOfTheHudAndTheCoordinatesInSevenWindowShapes) {
        // The shapes of the default view and of Portrait: in the first the groups are in the margin left of the board, in the second in the bands above and below it
        StartFrom("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{1024.0f, 768.0f}, Pair<float, float>{800.0f, 800.0f}, Pair<float, float>{2560.0f, 1080.0f},
                                 Pair<float, float>{720.0f, 1280.0f}, Pair<float, float>{768.0f, 1024.0f}, Pair<float, float>{480.0f, 853.0f}}) {
            SCOPED_TRACE(Format("{}x{}", size.first, size.second).c_str());
            DrawAt(size.first, size.second);
            DrawAt(size.first, size.second);  // the rows wait for the first layout
            ASSERT_EQ(ShownMaterialRows(), "TopQueen TopRook TopBishop TopKnight TopPawn BottomQueen BottomRook BottomBishop BottomKnight BottomPawn") << "two bare kings: everything is gone";
            const UICanvasLayout& material = Material()->GetLayout();
            EXPECT_EQ(MaterialProblems(material), "");
            EXPECT_EQ(MaterialClearance(material, Hud()->GetLayout(), kMinGap), "") << "no safe area: the HUD is in the margins above and below the board";
            EXPECT_EQ(MaterialClearance(material, Coordinate()->GetLayout(), 1.0f), "");
            for (const char* group : kMaterialGroups) {
                for (const char* piece : kMaterialPieces) {
                    const Rect& rect = MaterialRow(group, piece).rect;
                    if (PortraitIsActive()) {
                        const bool upper = group == kMaterialGroups[0];
                        EXPECT_LE(rect.x + rect.w, kPortraitViewWidth) << group << piece << " is inside the view";
                        EXPECT_EQ(rect.y + rect.h <= kPortraitBoardTop, upper) << group << piece << ": the upper group is above the board, the lower one is not";
                        EXPECT_EQ(rect.y >= kPortraitBoardBottom, !upper) << group << piece << ": the lower group is under the board, the upper one is not";
                    } else {
                        EXPECT_LE(rect.x + rect.w, kMaterialMarginRight) << group << piece << " does not reach the board";
                    }
                    EXPECT_GE(rect.x, 0.0f) << group << piece;
                }
            }
        }
    }

    TEST_F(ChessUiTest, AnIconKeepsTheAspectOfItsAtlasCellCenteredInsideItsRow) {
        // The library is empty in the test, so every icon is given the atlas of the pieces and a cell of it by hand; UIImageComponent fits the cell into its rect
        StartFrom("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
        const Vector<uint8_t> pixels(static_cast<size_t>(kAtlasWidth) * kAtlasHeight * 4, 0);
        const IntrusivePtr<ITexture> atlas = _renderer->CreateTexture(pixels.data(), kAtlasWidth, kAtlasHeight);
        for (const char* group : kMaterialGroups) {
            for (const char* piece : kMaterialPieces) {
                UIImageComponent* icon = MaterialElement(Format("Material{}{}Icon", group, piece).c_str()).GetComponent<UIImageComponent>();
                ASSERT_NE(icon, nullptr) << group << piece;
                icon->GetSprite().SetTexture(atlas, Rect{kCellX, 0.0f, kCellWidth, kCellHeight});
            }
        }

        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}}) {
            SCOPED_TRACE(Format("{}x{}", size.first, size.second).c_str());
            DrawAt(size.first, size.second);
            _renderer->calls.clear();
            DrawAt(size.first, size.second);
            ASSERT_NE(ShownMaterialRows(), "");
            Vector<const DrawCall*> drawn;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.texture == atlas.Get()) {
                    drawn.push_back(&call);
                }
            }
            ASSERT_EQ(drawn.size(), 10u) << "one picture per icon, in the order of the scene";
            size_t index = 0;
            for (const char* group : kMaterialGroups) {
                for (const char* piece : kMaterialPieces) {
                    SCOPED_TRACE(Format("{}{}", group, piece).c_str());
                    const Rect& call = drawn[index++]->rect;
                    const Rect box = OutputRect(MaterialElement(Format("Material{}{}Icon", group, piece).c_str()).rect);
                    EXPECT_NEAR(call.w / call.h, kCellWidth / kCellHeight, 0.01f) << "the aspect of the atlas cell, not of the 26-unit box";
                    EXPECT_GE(call.x, box.x - 0.5f);
                    EXPECT_GE(call.y, box.y - 0.5f);
                    EXPECT_LE(call.x + call.w, box.x + box.w + 0.5f) << "inside its box";
                    EXPECT_LE(call.y + call.h, box.y + box.h + 0.5f);
                    EXPECT_NEAR(call.x + call.w * 0.5f, box.x + box.w * 0.5f, 0.5f) << "centered";
                    EXPECT_NEAR(call.y + call.h * 0.5f, box.y + box.h * 0.5f, 0.5f);
                    EXPECT_NEAR(call.h, box.h, 0.5f) << "the cell is tall: the height of the box is the limit, and the picture fills it";
                }
            }
        }
    }

    TEST_F(ChessUiTest, ABrokenCopyOfTheMaterialCanvasIsCaughtByTheChecks) {
        Draw();
        const String shipped = ShownMaterialXml();
        ASSERT_FALSE(shipped.empty());
        auto hud = Hud();
        ASSERT_TRUE(hud);
        hud->UpdateLayout(1280.0f, 720.0f);
        const auto problemsOf = [&](const String& xml) {
            CanvasCopy copy(xml, false);
            return MaterialProblemsOnMatrix(copy.layout, 3, 100000);
        };

        EXPECT_EQ(problemsOf(shipped), "") << "the same checks pass on the shipped canvas";

        String wide = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(wide, R"(name="MaterialTopList" x="8" y="84" width="64")", R"(name="MaterialTopList" x="8" y="84" width="104")"));
        EXPECT_NE(problemsOf(wide).find("MaterialTopList (8.0, 84.0, 104.0"), String::npos) << "a list that reaches the board leaves the margin";

        String bigFont = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(bigFont, R"(name="MaterialTopQueenCount" flex="1" fontSize="20")", R"(name="MaterialTopQueenCount" flex="1" fontSize="60")"));
        EXPECT_NE(problemsOf(bigFont).find("MaterialTopQueenCount cuts"), String::npos) << "a font of 60 does not fit the count box";

        String smallBox = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(smallBox, R"(name="MaterialTopBalance" x="77" y="84" width="23")", R"(name="MaterialTopBalance" x="77" y="84" width="8")"));
        EXPECT_NE(problemsOf(smallBox).find("MaterialTopBalance cuts"), String::npos) << "a balance box of 8 units cuts the sign even at the smallest size";

        String lowRow = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(lowRow, R"(name="MaterialTopQueenRow" height="40")", R"(name="MaterialTopQueenRow" height="20")"));
        EXPECT_NE(problemsOf(lowRow).find("MaterialTopQueenRow is 20.0 units high"), String::npos);

        String missing = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(missing, R"(name="MaterialTopPawnRow")", R"(name="MaterialTopPawnRowX")"));
        EXPECT_NE(problemsOf(missing).find("MaterialTopPawnRow is missing"), String::npos);

        // A list that moved up under the first row of the HUD: the clearance against the HUD finds it, in a window without insets
        String underTheHud = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(underTheHud, R"(name="MaterialTopList" x="8" y="84")", R"(name="MaterialTopList" x="8" y="20")"));
        CanvasCopy moved(underTheHud, false);
        EXPECT_NE(problemsOf(underTheHud).find("MaterialTopList (8.0, 20.0"), String::npos) << "above the rows of the board";
        const UIMatrixCase* plain = nullptr;
        for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
            if (matrixCase.label == "16:9 1280x720 no insets") {
                plain = &matrixCase;
            }
        }
        ASSERT_NE(plain, nullptr);
        moved.layout.Update(ContextOf(*plain));
        hud->GetLayout().Update(ContextOf(*plain));
        EXPECT_NE(MaterialClearance(moved.layout, hud->GetLayout(), kMinGap).find("MaterialTopQueenRow"), String::npos) << "the first row is on the Turn label";
        CanvasCopy fine(shipped, false);
        fine.layout.Update(ContextOf(*plain));
        EXPECT_EQ(MaterialClearance(fine.layout, hud->GetLayout(), kMinGap), "");
        hud->GetLayout().Invalidate();
    }

}  // namespace BECore::Chess::Tests
