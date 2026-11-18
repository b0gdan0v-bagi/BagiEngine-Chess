#include <tests/ChessUiHarness.h>

/// The board: its orientation, the coordinates, the premoves, the hint, the last move, the drag and the board in the Portrait view. The scene, the helpers and the fixture ChessUiTest are
/// tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    // ---- the orientation of the board ---------------------------------------------------------------------------------------------
    // The board is drawn from Black's side exactly when the program plays White. The expected cells are written out in the fixture (CellOfSquare), not taken from BoardOrientation.h,
    // and the clicks go through the real mouse path

    namespace {

        const Color kResultBarColor{25, 25, 25, 245};  // the bar of a win of Black (ChessResultComponent.cpp)

        constexpr const char* kBlackToMoveAfterE4 = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
        constexpr const char* kFourRooks = "r3k2r/8/8/8/8/8/4K3/r6r b - - 0 1";  // a rook in each corner of the board
        constexpr const char* kBlackInCheck = "4k3/8/8/8/8/8/8/4RK2 b - - 0 1";
        constexpr const char* kBlackPromotesOnE1 = "7k/8/8/8/8/8/4p3/K7 b - - 0 1";

    }  // namespace

    TEST_F(ChessUiTest, WithTheProgramAsWhiteTheBoardAndThePiecesAreTurnedBy180Degrees) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f})) << "two people: a1 at the bottom left";

        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());

        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{660.0f, 80.0f})) << "a1 at the top right";
        EXPECT_EQ(PieceOrigin("WhiteRook_h1"), (Pair<float, float>{100.0f, 80.0f})) << "h1 at the top left";
        EXPECT_EQ(PieceOrigin("BlackRook_a8"), (Pair<float, float>{660.0f, 640.0f})) << "a8 at the bottom right";
        ExpectPiecesAtTheCellsOf(true);

        // The squares of the board: the cell of a1 is the one of h8 in the other orientation, with the color of a1 (dark) in both
        Draw();
        const Rect a1 = SquareRect("a1");
        const Rect h8 = SquareRect("h8");
        EXPECT_GT(a1.x, h8.x);
        EXPECT_LT(a1.y, h8.y);
        const auto fillOf = [this](const Rect& cell) -> Optional<Color> {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && SameRect(call.rect, cell)) {
                    return call.color;
                }
            }
            return std::nullopt;
        };
        ASSERT_TRUE(fillOf(a1).has_value());
        EXPECT_TRUE(SameColor(*fillOf(a1), kDarkSquareColor)) << "a1 is a dark square wherever it is drawn";
        ASSERT_TRUE(fillOf(h8).has_value());
        EXPECT_TRUE(SameColor(*fillOf(h8), kDarkSquareColor)) << "and so is h8";
        ASSERT_TRUE(fillOf(SquareRect("h1")).has_value());
        EXPECT_TRUE(SameColor(*fillOf(SquareRect("h1")), kLightSquareColor));
    }

    TEST_F(ChessUiTest, ASwitchOfTheThemeRecoloursTheBoardAtTheNextDraw) {
        UIThemes& themes = CoreManager::GetUIThemes();
        const PoolString slate = "slate"_intern;
        const PoolString classic = "classic"_intern;
        ASSERT_TRUE(themes.SetTheme(slate));
        const Color slateLight = *themes.Find("boardLight");
        const Color slateDark = *themes.Find("boardDark");
        const Color slateSelected = *themes.Find("boardSelected");
        ASSERT_TRUE(themes.SetTheme(classic));

        Click("e2");
        ASSERT_TRUE(Model().HasSelection());
        Draw();
        EXPECT_EQ(RectCalls(kLightSquareColor).size(), 32u);
        EXPECT_EQ(RectCalls(kDarkSquareColor).size(), 32u);
        EXPECT_EQ(RectCalls(kSelectedColor).size(), 1u);
        EXPECT_TRUE(RectCalls(slateLight).empty());

        ASSERT_TRUE(themes.SetTheme(slate));
        Draw();
        EXPECT_EQ(RectCalls(slateLight).size(), 32u) << "the 64 squares in the two colors of slate";
        EXPECT_EQ(RectCalls(slateDark).size(), 32u);
        EXPECT_TRUE(RectCalls(kLightSquareColor).empty()) << "and in none of classic";
        EXPECT_TRUE(RectCalls(kDarkSquareColor).empty());
        EXPECT_EQ(RectCalls(slateSelected).size(), 1u) << "the selection is drawn in the selection color of slate";
        EXPECT_TRUE(RectCalls(kSelectedColor).empty());

        ASSERT_TRUE(themes.SetTheme(classic));
        Draw();
        EXPECT_EQ(RectCalls(kLightSquareColor).size(), 32u) << "the way back brings the old colors";
        EXPECT_EQ(RectCalls(kDarkSquareColor).size(), 32u);
        EXPECT_EQ(RectCalls(kSelectedColor).size(), 1u);
        EXPECT_TRUE(RectCalls(slateLight).empty());
    }

    TEST_F(ChessUiTest, AClickOnTheDrawnSquareSelectsThatSquareInTheTurnedBoard) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        StartFrom(kBlackToMoveAfterE4);
        ASSERT_TRUE(BlackIsAtTheBottom());
        ASSERT_EQ(Model().GetTurn(), PieceColor::Black);

        // e7 is the fifth file from the right and the second row from the bottom
        const Rect e7 = SquareRect("e7");
        EXPECT_TRUE(SameRect(e7, CellRect(3, 6)));
        Click("e7");

        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("e7"));
        ASSERT_EQ(Model().GetLegalMoves().size(), 2u);
        Draw();
        const auto selected = RectCalls(kSelectedColor);
        ASSERT_EQ(selected.size(), 1u);
        EXPECT_TRUE(SameRect(selected[0]->rect, CellRect(3, 6)));
        const auto moves = RectCalls(kLegalMoveColor);
        ASSERT_EQ(moves.size(), 2u);
        for (const char* target : {"e6", "e5"}) {
            const Rect square = SquareRect(target);
            EXPECT_TRUE(std::any_of(moves.begin(), moves.end(), [&](const DrawCall* call) { return SameRect(call->rect, square); })) << target;
        }
        EXPECT_TRUE(SameRect(SquareRect("e6"), CellRect(3, 5)));
        EXPECT_TRUE(SameRect(SquareRect("e5"), CellRect(3, 4)));

        // The cell where e2 stands on a board with White at the bottom (fifth column, second row from the bottom) is d7 here, not e2
        Model().ClearSelection();
        ClickCell(4, 6);
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("d7"));

        // e2 itself is drawn near the top, and a white pawn is not the person's to pick
        Model().ClearSelection();
        Click("e2");
        EXPECT_FALSE(Model().HasSelection());
    }

    TEST_F(ChessUiTest, APersonsMoveByClicksInTheTurnedBoardIsPlayed) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        StartFrom(kBlackToMoveAfterE4);

        ClickMove("e7", "e5");

        ASSERT_FALSE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "e7e5");
        EXPECT_EQ(Model().GetPieceAt(Sq("e5")).type, PieceType::Pawn);
        EXPECT_EQ(Model().GetPieceAt(Sq("e5")).color, PieceColor::Black);
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(true);
    }

    TEST_F(ChessUiTest, TheFourCornersOfTheTurnedBoardAreTheSquaresTheyShow) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        StartFrom(kFourRooks);
        ASSERT_EQ(Model().GetTurn(), PieceColor::Black);

        // The corners by their place on the screen: the clicks go through the mouse path, a rook of Black stands on each
        const struct {
            int column;
            int row;
            const char* square;
        } corners[] = {{7, 0, "a1"}, {0, 0, "h1"}, {7, 7, "a8"}, {0, 7, "h8"}};
        for (const auto& corner : corners) {
            SCOPED_TRACE(corner.square);
            Model().ClearSelection();
            ClickCell(corner.column, corner.row);
            ASSERT_TRUE(Model().HasSelection());
            EXPECT_EQ(Model().GetSelected(), Sq(corner.square));
            EXPECT_TRUE(SameRect(SquareRect(corner.square), CellRect(corner.column, corner.row)));
        }
    }

    TEST_F(ChessUiTest, TheCheckSitsOnTheKingsTurnedCell) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        StartFrom(kBlackInCheck);
        ASSERT_TRUE(Model().IsInCheck());

        Draw();
        const auto check = RectCalls(kCheckColor);
        ASSERT_EQ(check.size(), 1u);
        EXPECT_TRUE(SameRect(check[0]->rect, CellRect(3, 7))) << "e8: the fifth file from the right, the bottom row";
        EXPECT_TRUE(SameRect(check[0]->rect, SquareRect("e8")));
    }

    TEST_F(ChessUiTest, OnlyAMateLooksMatedAndAGameResignedOrDrawnInCheckKeepsTheCheck) {
        ASSERT_TRUE(PlayAll(Model(), {"f2f3", "e7e5", "g2g4", "d8h4"}));
        ASSERT_EQ(Model().GetStatus(), GameStatus::Checkmate);
        Draw();
        ASSERT_EQ(RectCalls(kMateColor).size(), 1u);
        EXPECT_TRUE(SameRect(RectCalls(kMateColor)[0]->rect, SquareRect("e1")));
        EXPECT_TRUE(RectCalls(kCheckColor).empty());

        for (const bool resign : {true, false}) {
            SCOPED_TRACE(resign ? "resigned" : "drawn");
            StartFrom(kBlackInCheck);
            ASSERT_TRUE(Model().IsInCheck());
            ASSERT_TRUE(resign ? Model().Resign(PieceColor::Black).has_value() : Model().AgreeDraw().has_value());
            ASSERT_TRUE(Model().IsGameOver());
            Draw();
            EXPECT_TRUE(RectCalls(kMateColor).empty()) << "the king was not mated";
            ASSERT_EQ(RectCalls(kCheckColor).size(), 1u);
            EXPECT_TRUE(SameRect(RectCalls(kCheckColor)[0]->rect, SquareRect("e8")));
        }
    }

    TEST_F(ChessUiTest, APromotionInTheTurnedBoardAsksInAPlateThatDoesNotMove) {
        const auto plateOf = [this]() -> Optional<Rect> {
            Draw();
            const auto plate = RectCalls(kPromotionPlateColor);
            return plate.empty() ? std::nullopt : Optional<Rect>(plate.front()->rect);
        };

        // Two people: the plate as it is drawn with White at the bottom
        StartFrom(kBlackPromotesOnE1);
        ClickMove("e2", "e1");
        ASSERT_TRUE(PromotionShown());
        const auto plain = plateOf();
        ASSERT_TRUE(plain.has_value());
        Press("ChessCancelPromotion");
        _manager->UpdateAll();
        ASSERT_FALSE(PromotionShown());

        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        StartFrom(kBlackPromotesOnE1);
        ASSERT_TRUE(BlackIsAtTheBottom());
        EXPECT_TRUE(SameRect(SquareRect("e1"), CellRect(3, 0))) << "e1 is on the top row, the pawn goes towards it";

        Click("e2");
        Click("e1");

        EXPECT_TRUE(Model().HasPendingPromotion());
        EXPECT_TRUE(PromotionShown());
        EXPECT_TRUE(Model().GetMoves().empty()) << "nothing has moved";
        const auto turned = plateOf();
        ASSERT_TRUE(turned.has_value());
        EXPECT_TRUE(SameRect(*turned, *plain)) << "the plate is fixed on the screen";

        ClickPromotion("PromoteQueen");
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "e2e1q");
    }

    TEST_F(ChessUiTest, APromotionInTheTurnedBoardOfThePortraitViewAsksInAPlateThatDoesNotMove) {
        const auto plateOf = [this]() -> Optional<Rect> {
            Draw();
            const auto plate = RectCalls(kPromotionPlateColor);
            return plate.empty() ? std::nullopt : Optional<Rect>(plate.front()->rect);
        };
        ResizeWithoutInsets(kNarrowestWidth, kNarrowestPortraitHeight);
        ASSERT_TRUE(PortraitIsActive());

        StartFrom(kBlackPromotesOnE1);
        ClickMove("e2", "e1");
        ASSERT_TRUE(PromotionShown());
        const auto plain = plateOf();
        ASSERT_TRUE(plain.has_value());
        Press("ChessCancelPromotion");
        _manager->UpdateAll();
        ASSERT_FALSE(PromotionShown());

        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        StartFrom(kBlackPromotesOnE1);
        ASSERT_TRUE(BlackIsAtTheBottom());
        ASSERT_TRUE(PortraitIsActive());
        EXPECT_TRUE(SameRect(SquareRect("e1"), CellRect(3, 0))) << "e1 is on the top row of the Portrait board";

        Click("e2");
        Click("e1");
        EXPECT_TRUE(PromotionShown());
        EXPECT_TRUE(Model().GetMoves().empty()) << "nothing has moved";
        const auto turned = plateOf();
        ASSERT_TRUE(turned.has_value());
        EXPECT_TRUE(SameRect(*turned, *plain)) << "the plate is fixed on the screen";

        ClickPromotion("PromoteQueen");
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "e2e1q");
    }

    TEST_F(ChessUiTest, TheDimAndTheResultBarCoverTheTurnedBoardTheSame) {
        const auto dimOf = [this]() -> Optional<Rect> {
            Draw();
            const auto dim = RectCalls(kBoardDimColor);
            return dim.empty() ? std::nullopt : Optional<Rect>(dim.front()->rect);
        };

        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        StartFrom(kBlackMatesOnA1);
        ASSERT_TRUE(BlackIsAtTheBottom());

        ClickMove("a8", "a1");

        ASSERT_TRUE(Model().IsGameOver());
        EXPECT_EQ(Model().GetWinner(), Optional<PieceColor>(PieceColor::Black));
        const auto dim = dimOf();
        ASSERT_TRUE(dim.has_value());
        EXPECT_TRUE(SameRect(*dim, ActiveFit().ToOutput(100.0f, kBoardTop, 640.0f, 640.0f))) << "the whole board, as with White at the bottom";
        EXPECT_FALSE(RectCalls(kResultBarColor).empty()) << "the bar of the winner";
    }

    TEST_F(ChessUiTest, TheBoardTurnsBackWhenNobodyOrBlackIsTheProgram) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(false);

        ControlPlays("white");
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(true);
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{660.0f, 80.0f}));

        ControlPlays(nullptr);
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(false);
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f})) << "nobody plays: White at the bottom again";

        ControlPlays("black");
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(false);
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f})) << "the program plays Black: the person has White at the bottom";
        EXPECT_FALSE(BlackIsAtTheBottom());
    }

    TEST_F(ChessUiTest, TheColorSegmentsAndTheModeTurnTheBoardAtTheNextFrame) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        PressWith("ChessSetMode", "computer");  // the person has White, the program plays Black
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f})) << "a1 at the bottom left";

        PressWith("ChessSetColor", "black");
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{660.0f, 80.0f})) << "the person has Black: the board is turned";
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 1; })) << "and the program, now White, opens";
        _manager->UpdateAll();  // the pieces follow the move of this frame in the next one
        ExpectPiecesAtTheCellsOf(true);

        PressWith("ChessSetColor", "white");
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f})) << "the person has White again";
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2 && !Opponent().IsThinking(); })) << "the program, now Black, answers the opening";
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(false);  // every piece, the moved ones too
    }

    TEST_F(ChessUiTest, ABlackPickedWhileTwoPlayDoesNotTurnTheBoardUntilComputerIsPicked) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        PressWith("ChessSetColor", "black");
        _manager->UpdateAll();
        EXPECT_EQ(Var("opponentColor"), "black");
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f})) << "nobody plays yet: nothing turns";

        PressWith("ChessSetMode", "computer");
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{660.0f, 80.0f})) << "now the program plays White";

        PressWith("ChessSetMode", "two");
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f})) << "two people: White at the bottom";
        EXPECT_EQ(Var("opponentColor"), "black") << "the color stays remembered";
    }

    TEST_F(ChessUiTest, ARefusedPickLeavesTheOrientationAsItWas) {
        SKIP_WITHOUT_WORKER_THREADS();
        Vector<String> asked;
        Opponent().SetEngineFactory([&asked](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            const StringView id = OpponentEngineId(engine);
            asked.emplace_back(id.data(), id.size());
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
            }
            return New<GatedEngine>(true);
        });
        _manager->UpdateAll();
        StartFromTheStockfishName();

        // Computer with a Stockfish that is not there changes nothing, a color picked before it included
        PressWith("ChessSetColor", "black");
        PressWith("ChessSetMode", "computer");
        _manager->UpdateAll();
        ASSERT_EQ(asked.size(), 1u);
        EXPECT_EQ(Var("opponentMode"), "two") << "the pick was refused";
        EXPECT_FALSE(BlackIsAtTheBottom());
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f}));

        // The way on is the own engine; the board turns with Computer. A refused Stockfish pick afterwards leaves it turned
        PressWith("ChessSetEngine", "own");
        PressWith("ChessSetMode", "computer");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{660.0f, 80.0f}));
        PressWith("ChessSetEngine", "stockfish");
        _manager->UpdateAll();
        EXPECT_EQ(Var("opponentEngine"), "own") << "refused";
        EXPECT_TRUE(BlackIsAtTheBottom());
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{660.0f, 80.0f}));
    }

    TEST_F(ChessUiTest, ResetAndSetStateKeepTheTurnedBoardAndDealThePiecesToItsCells) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());

        Press("ChessReset");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom()) << "a reset keeps the opponent, so the orientation";
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{660.0f, 80.0f}));
        ExpectPiecesAtTheCellsOf(true);

        Model().Reset();
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(true);

        Json request = Json::object();
        request["fen"] = kBlackToMoveAfterE4;
        const auto result = ChessGame::GetActive()->AsControllable()->SetState(request);
        ASSERT_TRUE(result.has_value());
        _manager->UpdateAll();
        EXPECT_TRUE(BlackIsAtTheBottom());
        ExpectPiecesAtTheCellsOf(true);
    }

    TEST_F(ChessUiTest, TheCoordinateVariablesStartAsWhitesViewOfTheBoard) {
        EXPECT_EQ(Coordinates(true), "abcdefgh");
        EXPECT_EQ(Coordinates(false), "87654321");
    }

    TEST_F(ChessUiTest, WithTheProgramAsWhiteTheCoordinateVariablesReadFromBlacksSide) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        EXPECT_EQ(Coordinates(true), "hgfedcba");
        EXPECT_EQ(Coordinates(false), "12345678");
    }

    TEST_F(ChessUiTest, TheCoordinateVariablesTurnBackWithTwoPeopleAndWithTheProgramAsBlack) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_EQ(Coordinates(true), "hgfedcba");

        ControlPlays(nullptr);
        _manager->UpdateAll();
        EXPECT_EQ(Coordinates(true), "abcdefgh") << "two people: White at the bottom";
        EXPECT_EQ(Coordinates(false), "87654321");

        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_EQ(Coordinates(true), "hgfedcba");
        ControlPlays("black");
        _manager->UpdateAll();
        EXPECT_EQ(Coordinates(true), "abcdefgh") << "the program plays Black: the person has White at the bottom";
        EXPECT_EQ(Coordinates(false), "87654321");
    }

    TEST_F(ChessUiTest, TheCoordinateVariablesFollowTheColorSegmentsAndTheModeOfThePanel) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        PressWith("ChessSetColor", "black");
        _manager->UpdateAll();
        EXPECT_EQ(Coordinates(true), "abcdefgh") << "a color picked while two people play turns nothing";
        PressWith("ChessSetMode", "computer");
        _manager->UpdateAll();
        EXPECT_EQ(Coordinates(true), "hgfedcba");
        EXPECT_EQ(Coordinates(false), "12345678");
        PressWith("ChessSetMode", "two");
        _manager->UpdateAll();
        EXPECT_EQ(Coordinates(true), "abcdefgh");
    }

    TEST_F(ChessUiTest, AResetAndASetStateKeepTheCoordinatesOfTheBoardThatIsTurned) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        _manager->UpdateAll();

        Press("ChessReset");
        _manager->UpdateAll();
        EXPECT_EQ(Coordinates(true), "hgfedcba") << "a reset keeps the opponent, so the orientation";
        EXPECT_EQ(Coordinates(false), "12345678");

        Json request = Json::object();
        request["fen"] = kBlackToMoveAfterE4;
        ASSERT_TRUE(ChessGame::GetActive()->AsControllable()->SetState(request).has_value());
        _manager->UpdateAll();
        EXPECT_EQ(Coordinates(true), "hgfedcba");
        EXPECT_EQ(Coordinates(false), "12345678");

        ControlPlays(nullptr);
        _manager->UpdateAll();
        EXPECT_EQ(Coordinates(true), "abcdefgh");
        EXPECT_EQ(Coordinates(false), "87654321");
    }

    // ---- the board coordinates: the labels in the scene ---------------------------------------------------------------------------
    // The node BoardCoordinates of the shipped scene is a canvas of 16 UILabels at the edges of the board (ChessBoardCoordinates.xml has the reasons for its numbers). The layout is checked the way
    // the HUD's is: the validator and CheckContent() on the size matrix

    TEST_F(ChessUiTest, TheBoardCoordinatesPassTheSizeMatrixWithEveryLetterAndDigit) {
        Draw();  // the board publishes the variables
        auto canvas = Coordinate();
        ASSERT_TRUE(canvas) << "the shipped scene has the node BoardCoordinates";
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update

        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        for (size_t round = 0; round < kBoardEdgeCells; ++round) {
            PublishCoordinateRound(round);
            const auto failures = ValidateOnMatrix(canvas->GetLayout(), options, ViewContext(), SceneViews());
            EXPECT_TRUE(failures.empty()) << "round " << round << ": " << DescribeMatrixFailures(failures).c_str();
        }
        const String problems = BoardLabelProblemsOnMatrix(canvas->GetLayout());
        EXPECT_TRUE(problems.empty()) << problems.c_str();
    }

    TEST_F(ChessUiTest, NoCoordinateLabelIsCutInTheNarrowestWindow) {
        Draw();
        auto canvas = Coordinate();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = canvas->GetLayout();
        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            LayOutInTheNarrowestWindow(layout, shape);
            EXPECT_NEAR(layout.GetResolved().scale, shape.NarrowestScale(), 0.01f) << "about 10 px for a font of 18 units in the default view, 12 in Portrait";

            for (size_t round = 0; round < kBoardEdgeCells; ++round) {
                PublishCoordinateRound(round);
                const String problems = BoardLabelProblems(layout);
                EXPECT_TRUE(problems.empty()) << "round " << round << ":\n" << problems.c_str();
            }
        }
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, TheCoordinateCanvasHasNoSafeAreaAndNoControl) {
        const auto canvas = Coordinate();
        ASSERT_TRUE(canvas);
        EXPECT_FALSE(canvas->_useSafeArea) << "the board is at fixed scene units: an inset must not move a label";
        EXPECT_FALSE(canvas->_modal);
        EXPECT_TRUE(canvas->_visible) << "shown by default";
        size_t labels = 0;
        for (const BoardLabel& spec : ExpectedBoardLabels()) {
            const UIElement* element = canvas->GetRoot().FindByName(PoolString::Intern(spec.name));
            ASSERT_NE(element, nullptr) << spec.name.c_str();
            ++labels;
            EXPECT_FALSE(element->blocksInput) << spec.name.c_str();
            EXPECT_FALSE(element->IsInteractive()) << spec.name.c_str();
            EXPECT_TRUE(IsLabel(*element)) << spec.name.c_str();
        }
        EXPECT_EQ(labels, 2 * kBoardEdgeCells);
        EXPECT_EQ(canvas->GetRoot().children.size(), 2 * kBoardEdgeCells) << "nothing else is in the canvas";
    }

    TEST_F(ChessUiTest, EveryCoordinateLabelIsInTheCornerOfItsEdgeCellOnTheShippedScene) {
        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            DrawAt(shape.output.first, shape.output.second);
            ASSERT_EQ(PortraitIsActive(), shape.portrait);
            const float boardBottom = shape.boardTop + static_cast<float>(kBoardEdgeCells) * kCellSize;
            for (const BoardLabel& spec : ExpectedBoardLabels()) {
                SCOPED_TRACE(spec.name.c_str());
                const UIElement* found = CoordinateLabel(spec.name.c_str());
                ASSERT_NE(found, nullptr);
                const UIElement& label = *found;
                const float cellX = shape.boardLeft + static_cast<float>(spec.column) * kCellSize;
                const float cellY = shape.boardTop + static_cast<float>(spec.row) * kCellSize;
                const auto [expectedX, expectedY] = CellOrigin(spec.column, spec.row);
                EXPECT_FLOAT_EQ(cellX, expectedX);
                EXPECT_FLOAT_EQ(cellY, expectedY);
                EXPECT_GE(label.rect.x, spec.file ? cellX + kPieceRight : cellX) << "the rank numbers are left of the piece, the file letters right of it";
                EXPECT_LE(label.rect.x + label.rect.w, spec.file ? cellX + kCellSize : cellX + kPieceLeft);
                EXPECT_GE(label.rect.y, spec.file ? cellY + kCellSize - kLabelBand : cellY) << "the rank numbers are at the top of the cell, the file letters at the bottom";
                EXPECT_LE(label.rect.y + label.rect.h, spec.file ? cellY + kCellSize : cellY + kLabelBand);
                EXPECT_GE(label.rect.x, shape.boardLeft) << "inside the board";
                EXPECT_LE(label.rect.x + label.rect.w, shape.boardLeft + static_cast<float>(kBoardEdgeCells) * kCellSize);
                EXPECT_GE(label.rect.y, shape.boardTop);
                EXPECT_LE(label.rect.y + label.rect.h, boardBottom);
            }
            EXPECT_EQ(BoardLabelProblems(Coordinate()->GetLayout()), "") << "the same checks the matrix runs";
        }
    }

    TEST_F(ChessUiTest, NoCoordinateGlyphReachesAPieceInEitherOrientation) {
        SKIP_WITHOUT_WORKER_THREADS();
        // The width the labels keep clear is the width of a piece that Contain fits into a cell: the real draw of a rook says so
        const auto whiteA1 = GiveAtlasCell("WhiteRook_a1");
        const auto whiteH1 = GiveAtlasCell("WhiteRook_h1");
        const auto blackA8 = GiveAtlasCell("BlackRook_a8");
        const auto blackH8 = GiveAtlasCell("BlackRook_h8");
        const ITexture* const rooks[] = {whiteA1.Get(), whiteH1.Get(), blackA8.Get(), blackH8.Get()};
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());

        for (const ViewShape& shape : kViewShapes) {
            for (const bool turned : {false, true}) {
                SCOPED_TRACE(Format("{}, {}", shape.name, turned ? "the program plays White: the board is turned" : "two people").c_str());
                ControlPlays(turned ? "white" : nullptr);
                DrawAt(shape.output.first, shape.output.second);
                _manager->UpdateAll();
                Draw();

                size_t pieces = 0;
                size_t glyphs = 0;
                for (const DrawCall& call : _renderer->calls) {
                    if (call.kind != DrawKind::Texture || std::find(std::begin(rooks), std::end(rooks), call.texture) == std::end(rooks)) {
                        continue;
                    }
                    ++pieces;
                    // a rook is as wide as its atlas cell says, and the corner cells are the ones whose labels are closest
                    EXPECT_NEAR(call.rect.w / call.rect.h, kCellWidth / kCellHeight, 1e-4f);
                    for (const DrawCall& other : _renderer->calls) {
                        if (IsLabelGlyph(other)) {
                            EXPECT_FALSE(Touch(call.rect, other.rect, 0.0f))
                                << "a glyph at (" << other.rect.x << ", " << other.rect.y << ") is on a piece at (" << call.rect.x << ", " << call.rect.y << ")";
                        }
                    }
                }
                for (const DrawCall& call : _renderer->calls) {
                    glyphs += IsLabelGlyph(call) ? 1 : 0;
                }
                EXPECT_EQ(pieces, 4u) << "a rook in each corner";
                EXPECT_EQ(glyphs, 2 * kBoardEdgeCells) << "one glyph for each of the 16 labels";

                // the constants of the test are the piece of the game: a rook is drawn between kPieceLeft and kPieceRight of its corner cell, whole height
                const DrawCall* rook = TextureCall(*whiteA1);
                ASSERT_NE(rook, nullptr);
                const auto [column, row] = CellOfSquare(Sq("a1"), turned);
                const auto [x, y] = CellOrigin(column, row);
                EXPECT_TRUE(SameRect(rook->rect, OutputRect(Rect{x + kPieceLeft, y, kPieceWidth, kCellSize})));
            }
        }
    }

    TEST_F(ChessUiTest, TheCoordinateColorsReadOnTheSquaresThatAreDrawnInBothOrientations) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());

        for (const ViewShape& shape : kViewShapes) {
            for (const bool turned : {false, true}) {
                SCOPED_TRACE(Format("{}, {}", shape.name, turned ? "the program plays White: the board is turned" : "two people").c_str());
                ControlPlays(turned ? "white" : nullptr);
                DrawAt(shape.output.first, shape.output.second);
                _manager->UpdateAll();
                Draw();

                for (const BoardLabel& spec : ExpectedBoardLabels()) {
                    SCOPED_TRACE(spec.name.c_str());
                    const UIElement* found = CoordinateLabel(spec.name.c_str());
                    ASSERT_NE(found, nullptr);
                    const UIElement& label = *found;
                    const Rect cell = CellRect(spec.column, spec.row);
                    const bool light = (spec.column + spec.row) % 2 == 0;

                    // the square the board really fills there
                    const DrawCall* square = nullptr;
                    for (const DrawCall& call : _renderer->calls) {
                        if (call.kind == DrawKind::Rect && SameRect(call.rect, cell)) {
                            square = &call;
                            break;
                        }
                    }
                    ASSERT_NE(square, nullptr);
                    EXPECT_TRUE(SameColor(square->color, light ? kLightSquareColor : kDarkSquareColor)) << (light ? "a light cell" : "a dark cell");
                    EXPECT_EQ(IsLightCell(spec.column, spec.row), light) << "BoardCoordinates.h says what the board paints";
                    EXPECT_TRUE(SameColor(TextOf(label)->textColor, light ? kDarkSquareColor : kLightSquareColor)) << "dark brown on a light cell, cream on a dark one";

                    // and the glyph that is drawn in the label's box has that color
                    const Rect box = OutputRect(label.rect);
                    size_t inBox = 0;
                    for (const DrawCall& call : _renderer->calls) {
                        if (call.kind == DrawKind::Texture && LiesIn(call.rect, box, kGlyphTolerance)) {
                            ++inBox;
                            EXPECT_TRUE(SameColor(call.color, TextOf(label)->textColor));
                            EXPECT_TRUE(LiesIn(call.rect, cell, kGlyphTolerance)) << "inside the cell it labels";
                        }
                    }
                    EXPECT_EQ(inBox, 1u) << "one glyph in the box";
                }
            }
        }
    }

    TEST_F(ChessUiTest, TheCoordinatesAreDrawnAfterTheSquaresAndBeforeThePiecesTheResultAndTheHud) {
        // The order of the scene: Board and BoardCoordinates share the layer -1 and the second is written after the first; the pieces are on 0, the result on 1, the HUD on 10, the panels on 20
        const auto& nodes = GetScene().GetRootNode().GetChildren();
        const auto indexOf = [&](const char* name) -> int {
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (nodes[i]->GetName() == PoolString::Intern(name)) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        };
        const auto layerOf = [&](const char* name) { return nodes[static_cast<size_t>(indexOf(name))]->GetLayer(); };
        ASSERT_GE(indexOf("Board"), 0);
        ASSERT_GE(indexOf("BoardCoordinates"), 0);
        EXPECT_EQ(indexOf("BoardCoordinates"), indexOf("Board") + 1) << "written right after the Board";
        EXPECT_EQ(layerOf("Board"), -1);
        EXPECT_EQ(layerOf("BoardCoordinates"), -1);
        EXPECT_GT(layerOf("Result"), 0);
        EXPECT_GT(layerOf("Hud"), layerOf("Result"));
        EXPECT_GT(layerOf("MenuPanel"), layerOf("Hud"));
        EXPECT_EQ(layerOf("MenuPanel"), layerOf("OpponentPanel"));
        EXPECT_GT(layerOf("OpponentPanel"), layerOf("Hud"));

        // and the frame says the same: squares, then glyphs, then a piece, then the dim of a finished game and the buttons of the HUD
        const auto king = GiveAtlasCell("WhiteKing_e1");  // a piece that is still on the board in this position (at g1)
        StartFrom(kBlackMatesOnA1);
        ASSERT_TRUE(Play(Model(), "a8a1"));
        ASSERT_TRUE(Model().IsGameOver());
        _manager->UpdateAll();
        Draw();

        const auto& calls = _renderer->calls;
        long lastSquare = -1;
        long firstGlyph = -1;
        long lastGlyph = -1;
        long firstPiece = -1;
        long dim = -1;
        long resetButton = -1;
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
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kResetColor)) {
                resetButton = at;
            }
        }
        ASSERT_GE(lastSquare, 0);
        ASSERT_GE(firstGlyph, 0);
        ASSERT_GE(firstPiece, 0) << "the king is drawn";
        ASSERT_GE(dim, 0) << "the game is over: the board is dimmed";
        ASSERT_GE(resetButton, 0);
        EXPECT_LT(lastSquare, firstGlyph) << "the labels are over the squares";
        EXPECT_LT(lastGlyph, firstPiece) << "and under the pieces";
        EXPECT_LT(firstPiece, dim);
        EXPECT_LT(lastGlyph, dim) << "and under the dim of a finished game";
        EXPECT_LT(lastGlyph, resetButton) << "and under the HUD";
    }

    TEST_F(ChessUiTest, TheCoordinateLabelsTakeNoClickAndTheBoardGetsItInTheirCells) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        StartFrom(kFourRooks);  // a rook of Black in each corner, Black to move
        ASSERT_EQ(Model().GetTurn(), PieceColor::Black);
        const auto canvas = Coordinate();
        ASSERT_TRUE(canvas);
        const struct {
            bool turned;
            const char* label;
            const char* square;
        } presses[] = {{false, "RankLabel0", "a8"}, {false, "FileLabel0", "a1"}, {false, "FileLabel7", "h1"}, {false, "RankLabel7", "a1"},
                       {true, "RankLabel0", "h1"},  {true, "FileLabel0", "h8"},  {true, "FileLabel7", "a8"},  {true, "RankLabel7", "h8"}};
        for (const ViewShape& shape : kViewShapes) {
            DrawAt(shape.output.first, shape.output.second);
            for (const BoardLabel& spec : ExpectedBoardLabels()) {
                SCOPED_TRACE(Format("{} {}", shape.name, spec.name).c_str());
                const UIElement* label = CoordinateLabel(spec.name.c_str());
                ASSERT_NE(label, nullptr);
                const Rect& rect = label->rect;  // canvas units: the view units of a SceneView canvas
                EXPECT_EQ(canvas->GetLayout().HitTest(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f), nullptr) << "not found by a hit test";
            }

            for (const auto& press : presses) {
                SCOPED_TRACE(Format("{}: {} with the board {}", shape.name, press.label, press.turned ? "turned" : "upright").c_str());
                ControlPlays(press.turned ? "white" : nullptr);
                _manager->UpdateAll();
                Model().ClearSelection();
                Draw();
                const UIElement* label = CoordinateLabel(press.label);
                ASSERT_NE(label, nullptr);
                const auto [x, y] = ScreenCenter(*label);
                EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true))) << "the canvas does not take the press";
                EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false)));
                ASSERT_TRUE(Model().HasSelection()) << "the board got it";
                EXPECT_EQ(Model().GetSelected(), Sq(press.square)) << "the square drawn in that cell";
            }
        }
    }

    TEST_F(ChessUiTest, TheCoordinateLabelsTurnTheirTextAndKeepTheirPlaceOnTheScreen) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        const auto read = [&](bool files) {
            Draw();
            String text;
            for (int index = 0; index <= kLastBoardIndex; ++index) {
                const UIElement* label = CoordinateLabel((files ? "FileLabel{}"_format(index) : "RankLabel{}"_format(index)).c_str());
                if (label != nullptr) {
                    text += ShownText(*label);
                }
            }
            return text;
        };
        const auto rects = [&] {
            Vector<Rect> found;
            for (const BoardLabel& spec : ExpectedBoardLabels()) {
                const UIElement* label = CoordinateLabel(spec.name.c_str());
                found.push_back(label != nullptr ? label->rect : Rect{});
            }
            return found;
        };

        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            ControlPlays(nullptr);
            DrawAt(shape.output.first, shape.output.second);
            _manager->UpdateAll();
            EXPECT_EQ(read(true), "abcdefgh") << "White at the bottom: a..h from the left";
            EXPECT_EQ(read(false), "87654321") << "8 at the top";
            const Vector<Rect> upright = rects();

            ControlPlays("white");
            _manager->UpdateAll();
            EXPECT_EQ(read(true), "hgfedcba") << "the board is turned by 180 degrees: h..a from the left";
            EXPECT_EQ(read(false), "12345678") << "1 at the top";
            const Vector<Rect> turned = rects();
            ASSERT_EQ(turned.size(), upright.size());
            for (size_t i = 0; i < turned.size(); ++i) {
                EXPECT_TRUE(SameRect(turned[i], upright[i])) << "label " << i << " is fixed on the screen";
            }

            ControlPlays("black");
            _manager->UpdateAll();
            EXPECT_EQ(read(true), "abcdefgh");
            EXPECT_EQ(read(false), "87654321");
        }
    }

    TEST_F(ChessUiTest, TheCoordinateColorsAreTheSameWhenTheBoardIsTurned) {
        SKIP_WITHOUT_WORKER_THREADS();
        // The turn by 180 degrees keeps the color of every cell, which is why the colors of the labels are fixed in the XML
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        Draw();
        Vector<Color> before;
        for (const BoardLabel& spec : ExpectedBoardLabels()) {
            const UIElement* label = CoordinateLabel(spec.name.c_str());
            ASSERT_NE(label, nullptr);
            before.push_back(TextOf(*label)->textColor);
        }
        ControlPlays("white");
        _manager->UpdateAll();
        Draw();
        size_t index = 0;
        for (const BoardLabel& spec : ExpectedBoardLabels()) {
            const UIElement* label = CoordinateLabel(spec.name.c_str());
            ASSERT_NE(label, nullptr);
            EXPECT_TRUE(SameColor(TextOf(*label)->textColor, before[index++])) << spec.name.c_str();
        }
    }

    // ---- the board coordinates: broken copies, so that the checks can fail ---------------------------------------------------------------------

    TEST_F(ChessUiTest, ABrokenCopyOfTheCoordinateLabelsIsCaughtByTheChecks) {
        Draw();  // the variables the labels show
        const String shipped = ShippedElementsXml("BoardCoordinates");
        ASSERT_FALSE(shipped.empty());
        const auto problemsOf = [&](const String& xml) {
            CanvasCopy copy(xml, false);
            return BoardLabelProblemsOnMatrix(copy.layout, 100000);
        };

        EXPECT_EQ(problemsOf(shipped), "") << "the same checks pass on the shipped labels";

        String bigFont = shipped;
        Replace(bigFont, "fontSize=\"18\"", "fontSize=\"60\"");
        EXPECT_NE(problemsOf(bigFont).find("RankLabel0 cuts"), String::npos) << "a font of 60 does not fit the box";

        String narrow = shipped;
        Replace(narrow, "name=\"RankLabel0\" x=\"102\" y=\"82\" width=\"13\"", "name=\"RankLabel0\" x=\"102\" y=\"82\" width=\"3\"");
        EXPECT_NE(problemsOf(narrow).find("RankLabel0 cuts"), String::npos) << "a box of 3 units cuts the glyph";

        String wide = shipped;
        Replace(wide, "name=\"RankLabel0\" x=\"102\" y=\"82\" width=\"13\"", "name=\"RankLabel0\" x=\"102\" y=\"82\" width=\"30\"");
        EXPECT_NE(problemsOf(wide).find("RankLabel0 (102.0, 82.0, 30.0, 26.0) is not in the corner"), String::npos) << "a box that reaches the piece";

        String inside = shipped;
        Replace(inside, "name=\"RankLabel3\" x=\"102\"", "name=\"RankLabel3\" x=\"182\"");
        EXPECT_NE(problemsOf(inside).find("RankLabel3 (182.0"), String::npos) << "a label under a cell that is not at the edge";

        String rankAtTheBottom = shipped;
        Replace(rankAtTheBottom, "name=\"RankLabel0\" x=\"102\" y=\"82\"", "name=\"RankLabel0\" x=\"102\" y=\"132\"");
        EXPECT_NE(problemsOf(rankAtTheBottom).find("RankLabel0 (102.0, 132.0"), String::npos) << "a rank number in the bottom left of its cell is not in the corner";

        String fileAtTheTop = shipped;
        Replace(fileAtTheTop, "name=\"FileLabel7\" x=\"725\" y=\"692\"", "name=\"FileLabel7\" x=\"725\" y=\"642\"");
        EXPECT_NE(problemsOf(fileAtTheTop).find("FileLabel7 (725.0, 642.0"), String::npos) << "a file letter in the top right of its cell is not in the corner";

        String wrongColor = shipped;
        Replace(wrongColor, "textColor=\"@boardDark\"", "textColor=\"@boardLight\"");
        EXPECT_NE(problemsOf(wrongColor).find("RankLabel0 has not the color"), String::npos) << "cream on a light cell";

        String missing = shipped;
        Replace(missing, "name=\"FileLabel7\"", "name=\"FileLabelX\"");
        EXPECT_NE(problemsOf(missing).find("FileLabel7 is missing"), String::npos);

        // The Portrait view has its own numbers: a label left where the default view has it is not in a corner of its cell there, and a missing variant leaves all sixteen
        String strayInPortrait = shipped;
        Replace(strayInPortrait, "<set element=\"RankLabel3\" x=\"42\"", "<set element=\"RankLabel3\" x=\"102\"");
        EXPECT_NE(problemsOf(strayInPortrait).find("RankLabel3 (102.0, 386.0"), String::npos) << "a rank number of the Portrait view that stayed in the default place";

        String noVariant = shipped;
        Replace(noVariant, "orientation:portrait", "orientation:landscape");
        EXPECT_NE(problemsOf(noVariant).find("RankLabel0 (102.0, 82.0"), String::npos) << "without the variant the labels stay where the default view has them";
    }

    TEST_F(ChessUiTest, TheHudStillStaysOutsideTheBoardWithTheCoordinatesOnIt) {
        // The labels are over the board and the HUD is in its margins; neither is moved by the other
        ExpectTheHudSurvivesTheMatrix();
        Draw();
        const auto hud = Hud();
        ASSERT_TRUE(hud);
        for (const char* name : {"Turn", "Menu", "Reset"}) {
            const UIElement* element = hud->GetRoot().FindByName(PoolString::Intern(name));
            ASSERT_NE(element, nullptr) << name;
            EXPECT_LE(element->rect.y + element->rect.h, kBoardTop + 0.01f) << name;
        }
    }

    TEST_F(ChessUiTest, InEveryOutputTheHudIsAboveAndBelowTheBoardOfItsViewAndStatusIsUnderTheMaterialRows) {
        StartFrom("4k3/8/8/8/8/8/8/4K3 w - - 0 1");  // every row of the material is shown
        size_t portraitOutputs = 0;
        for (const auto& output : kViewOutputs) {
            if (output.first < 2.0f || output.second < 2.0f) {
                continue;  // an output that is no window: the layout has nothing to say
            }
            SCOPED_TRACE(Format("{}x{}", output.first, output.second).c_str());
            DrawAt(output.first, output.second);
            DrawAt(output.first, output.second);
            const bool portrait = PortraitIsActive();
            portraitOutputs += portrait ? 1 : 0;
            const float boardTop = portrait ? kPortraitBoardTop : kBoardTop;
            const float boardBottom = portrait ? kPortraitBoardBottom : kBoardBottom;
            for (const char* name : {"Turn", "Menu", "Reset"}) {
                const UIElement& element = *HudElement(name);
                ASSERT_FALSE(element.rect.IsEmpty()) << name;
                EXPECT_LE(element.rect.y + element.rect.h, boardTop + kPixelTolerance) << name << " is above the board";
            }
            const UIElement& status = *HudElement("Status");
            ASSERT_FALSE(status.rect.IsEmpty());
            EXPECT_GE(status.rect.y, boardBottom - kPixelTolerance) << "Status is under the board";
            if (portrait) {
                EXPECT_GE(status.rect.y, kMaterialPortraitListBottom + kMinGap) << "and under the lower row of the material";
                EXPECT_FLOAT_EQ(status.rect.y, ViewHeight() - (kPortraitViewHeight - kPortraitStatusTop)) << "as far above the bottom of its view as in Portrait, also in Tall";
            }
            EXPECT_EQ(MaterialClearance(Material()->GetLayout(), Hud()->GetLayout(), kMinGap), "") << "the rows of the material and the HUD keep apart";
        }
        EXPECT_GT(portraitOutputs, 3u);
    }

    TEST_F(ChessUiTest, InThePortraitViewTheHudKeepsAboveTheBoardAndUnderTheMaterialInEveryWindowOfTheMatrix) {
        Draw();
        auto hud = Hud();
        auto material = Material();
        ASSERT_TRUE(hud);
        ASSERT_TRUE(material);
        hud->UpdateLayout(1280.0f, 720.0f);
        material->UpdateLayout(1280.0f, 720.0f);
        ShowEveryMaterialRow(*material);

        size_t portraitCases = 0;
        size_t pushedDown = 0;
        float lowestTopRow = 0.0f;
        for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
            const UILayoutContext context = ContextOf(matrixCase, true);
            hud->GetLayout().Update(context);
            if (!LaidOutForPortrait(hud->GetLayout())) {
                continue;
            }
            material->GetLayout().Update(context);
            ++portraitCases;
            SCOPED_TRACE(matrixCase.label.c_str());
            for (const char* name : {"Turn", "Menu", "Reset"}) {
                const UIElement* element = hud->GetRoot().FindByName(PoolString::Intern(name));
                ASSERT_NE(element, nullptr);
                ASSERT_FALSE(element->rect.IsEmpty()) << name;
                // A notch pushes the row down by 7 % of the width of the view at most: it stays above the board all the same, with the material rows in the band between (which it may cover:
                // the known limit of the material, held by TheShownMaterialIsClearOfTheHudAndTheCoordinateLabelsOnTheSizeMatrix)
                EXPECT_LE(element->rect.y + element->rect.h, kPortraitBoardTop) << name << " is above the board";
                lowestTopRow = std::max(lowestTopRow, element->rect.y + element->rect.h);
                pushedDown += element->rect.y > 12.0f + kPixelTolerance ? 1 : 0;
            }
            const UIElement* status = hud->GetRoot().FindByName("Status"_intern);
            ASSERT_NE(status, nullptr);
            ASSERT_FALSE(status->rect.IsEmpty());
            EXPECT_GE(status->rect.y, kMaterialPortraitListBottom + kMinGap) << "Status is under the lower row of the material, whatever the inset at the bottom";
        }
        hud->GetLayout().Invalidate();
        material->GetLayout().Invalidate();
        EXPECT_EQ(portraitCases, 42u);
        EXPECT_GT(pushedDown, 0u) << "the notch cases push the row down";
        EXPECT_LE(lowestTopRow, kPortraitHudBottom + kPortraitNotchUnits + kPixelTolerance) << "by the notch and no more";
    }

    TEST_F(ChessUiTest, TheStateOfTheControlApiIsTheSameWhateverSideIsAtTheBottom) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());

        const auto state = ChessGame::GetActive()->AsControllable()->GetState();
        ASSERT_TRUE(state.has_value());
        const Json* board = FindJsonMember(*state, "board");
        ASSERT_NE(board, nullptr);
        ASSERT_EQ(board->size(), 8u);
        const char* const expected[] = {"rnbqkbnr", "pppppppp", "........", "........", "........", "........", "PPPPPPPP", "RNBQKBNR"};
        for (size_t row = 0; row < board->size(); ++row) {
            EXPECT_EQ((*board)[row], MakeJsonString(expected[row])) << "row " << row << ": rank 8 first, white upper case";
        }
    }

    // ---- premoves: the clicks of the person while the program thinks ----------------------------------------------------------------
    // The model's flow is ChessGameModelTest's and the controller's play is OpponentControllerTest's; here the clicks come through the scene

    TEST_F(ChessUiTest, WhileTheProgramThinksAClickOnAnOwnPieceSelectsItAndAClickOnItsTargetQueuesAPremove) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; })) << "the program has the search and waits at its gate";
        const uint64_t version = Model().GetVersion();

        Click("d2");
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("d2"));
        EXPECT_EQ(Model().GetLegalMoves().size(), 4u) << "d3, d4 and the two diagonals, which a premove has with or without a piece there";
        Draw();
        EXPECT_EQ(RectCalls(kSelectedColor).size(), 1u) << "the selection is drawn as ever";
        EXPECT_EQ(RectCalls(kLegalMoveColor).size(), 4u);

        Click("d4");
        ASSERT_EQ(Model().GetPremoves().size(), 1u);
        EXPECT_EQ(Model().GetPremoves()[0], (Premove{Sq("d2"), Sq("d4")}));
        EXPECT_FALSE(Model().HasSelection());
        Draw();
        const auto marks = RectCalls(kPremoveColor);
        ASSERT_EQ(marks.size(), 2u);
        EXPECT_TRUE(SameRect(marks[0]->rect, SquareRect("d2")) || SameRect(marks[1]->rect, SquareRect("d2")));
        EXPECT_TRUE(SameRect(marks[0]->rect, SquareRect("d4")) || SameRect(marks[1]->rect, SquareRect("d4")));
        EXPECT_EQ(Model().GetVersion(), version) << "a premove is no change of the game, so the search is not stale";
        EXPECT_TRUE(Opponent().IsThinking());
        EXPECT_FALSE(engine->WasCancelled());
        EXPECT_EQ(Model().GetMoves().size(), 1u);

        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() >= 3; })) << "the answer and then the premove";
        EXPECT_EQ(Model().GetMoves()[2].ToUci(), "d2d4");
        EXPECT_TRUE(Model().GetPremoves().empty());
        Draw();
        EXPECT_TRUE(RectCalls(kPremoveColor).empty()) << "a played premove is not drawn any more";
    }

    TEST_F(ChessUiTest, ASecondPremoveMovesThePieceTheFirstOneMovedAndAnotherOwnPieceReselects) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));

        Click("d2");
        Click("g1");
        EXPECT_EQ(Model().GetSelected(), Sq("g1")) << "another own piece takes the selection";
        Click("f3");
        ASSERT_EQ(Model().GetPremoves().size(), 1u);
        Click("f3");  // the knight is on f3 on the premove board: this click selects it
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("f3"));
        Click("g5");

        ASSERT_EQ(Model().GetPremoves().size(), 2u);
        EXPECT_EQ(Model().GetPremoves()[0], (Premove{Sq("g1"), Sq("f3")}));
        EXPECT_EQ(Model().GetPremoves()[1], (Premove{Sq("f3"), Sq("g5")}));
    }

    TEST_F(ChessUiTest, WhileTheProgramThinksAClickOnAnEmptyOrAnEnemySquareCancelsTheWholeQueue) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        const uint64_t version = Model().GetVersion();

        ClickMove("d2", "d4");
        ClickMove("b1", "c3");
        ASSERT_EQ(Model().GetPremoves().size(), 2u);
        Click("e7");  // a piece of the program's
        EXPECT_TRUE(Model().GetPremoves().empty());
        EXPECT_FALSE(Model().GetPremoveColor().has_value());

        ClickMove("d2", "d4");
        ASSERT_EQ(Model().GetPremoves().size(), 1u);
        Click("a5");  // nothing stands there, and nothing is selected
        EXPECT_TRUE(Model().GetPremoves().empty());

        ClickMove("d2", "d4");
        Click("g1");
        Click("a5");  // a selection and a square that is none of its targets
        EXPECT_TRUE(Model().GetPremoves().empty());
        EXPECT_FALSE(Model().HasSelection());
        Draw();
        EXPECT_TRUE(RectCalls(kPremoveColor).empty());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_TRUE(Opponent().IsThinking());
        EXPECT_FALSE(engine->WasCancelled());
    }

    TEST_F(ChessUiTest, APremoveToThePromotionRankAsksNoPieceAndPlaysAsAQueen) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom("4k3/1P6/8/8/8/8/8/4K3 b - - 0 1");  // Black, the program, is to move; White's pawn on b7 is one step from the last rank
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));

        ClickMove("b7", "b8");

        EXPECT_EQ(Model().GetPremoves().size(), 1u);
        EXPECT_FALSE(Model().HasPendingPromotion());
        EXPECT_FALSE(PromotionShown());
        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() >= 2; }));
        EXPECT_EQ(Model().GetPieceAt(Sq("b8")).type, PieceType::Queen);
        EXPECT_EQ(Model().GetPieceAt(Sq("b8")).color, PieceColor::White);
    }

    TEST_F(ChessUiTest, WithTwoPeopleAClickOnABlackPieceOnWhitesTurnIsNoPremove) {
        _manager->UpdateAll();

        Click("e7");
        Click("e5");

        EXPECT_FALSE(Model().HasSelection());
        EXPECT_TRUE(Model().GetPremoves().empty());
        EXPECT_FALSE(Model().GetPremoveColor().has_value());
        Click("e2");
        EXPECT_TRUE(Model().HasSelection()) << "an ordinary selection";
        EXPECT_TRUE(Model().GetPremoves().empty());
    }

    TEST_F(ChessUiTest, WithTheProgramAsWhiteAPremoveOfBlackIsClickedOnTheTurnedBoard) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; })) << "the program opens and waits at its gate";
        ASSERT_EQ(Model().GetTurn(), PieceColor::White);

        Click("e7");
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("e7"));
        ClickCell(3, 4);  // the cell where e5 is drawn on this board
        ASSERT_EQ(Model().GetPremoves().size(), 1u);
        EXPECT_EQ(Model().GetPremoves()[0], (Premove{Sq("e7"), Sq("e5")}));
        EXPECT_EQ(Model().GetPremoveColor(), Optional<PieceColor>(PieceColor::Black));
        Draw();
        const auto marks = RectCalls(kPremoveColor);
        ASSERT_EQ(marks.size(), 2u);
        EXPECT_TRUE(SameRect(marks[0]->rect, CellRect(3, 6)) || SameRect(marks[1]->rect, CellRect(3, 6))) << "e7 is the second row from the bottom, the fifth column from the right";

        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() >= 2; }));
        EXPECT_EQ(Model().GetMoves()[1].ToUci(), "e7e5");
    }

    TEST_F(ChessUiTest, TheResetButtonDropsTheQueue) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        ClickMove("d2", "d4");
        ASSERT_EQ(Model().GetPremoves().size(), 1u);

        Press("ChessReset");
        _manager->UpdateAll();

        EXPECT_TRUE(Model().GetPremoves().empty());
        EXPECT_TRUE(Model().GetMoves().empty());
        Draw();
        EXPECT_TRUE(RectCalls(kPremoveColor).empty());
    }

    TEST_F(ChessUiTest, TheProgramsMateEndsTheQueueAndAClickAfterItStartsANewGame) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto mate = MoveGenerator::ParseUci(PositionFromFen(kBlackMatesOnA1), "a8a1");
        ASSERT_TRUE(mate.has_value());
        const auto engine = New<GatedEngine>(true, *mate);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kBlackMatesOnA1);
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        ClickMove("g1", "h1");
        ASSERT_EQ(Model().GetPremoves().size(), 1u);

        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().IsGameOver(); }));

        EXPECT_EQ(Model().GetMoves().size(), 1u) << "the premove did not play after the mate";
        EXPECT_TRUE(Model().GetPremoves().empty());
        Click("e4");
        EXPECT_FALSE(Model().IsGameOver()) << "a click after the end starts the next game";
        EXPECT_TRUE(Model().GetPremoves().empty());
    }

    TEST_F(ChessUiTest, AfterTheProgramsMateAClickStartsANewGameAndTheProgramMovesAgain) {
        CountingFactory factory({New<MatingEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        Model().SetPosition(PositionFromFen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"));
        ASSERT_TRUE(Opponent().SetSide(PieceColor::White).has_value());

        ASSERT_TRUE(FramesUntil([&] { return Model().IsGameOver(); }));
        EXPECT_EQ(Model().GetStatus(), GameStatus::Checkmate);
        EXPECT_EQ(Model().GetWinner(), Optional<PieceColor>(PieceColor::White));
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves()[0].ToUci(), "a1a8") << "the program's move";
        EXPECT_FALSE(Opponent().GetError().has_value());
        Draw();
        EXPECT_EQ(ShownText(Label("Turn")), "White wins") << "the HUD says it, whoever played the mate";

        const uint64_t version = Model().GetVersion();
        Click("e4");  // the game is over, so this click is the "next game" one even though White is the program

        EXPECT_FALSE(Model().IsGameOver());
        EXPECT_GT(Model().GetVersion(), version);
        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
        EXPECT_FALSE(Model().HasSelection());
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 1; })) << "the new game opens with the move of the program, which plays White";
        EXPECT_EQ(Model().GetTurn(), PieceColor::Black);
        EXPECT_FALSE(Model().IsGameOver());
    }

    TEST_F(ChessUiTest, AResetWhileTheProgramThinksDropsItsAnswerAndANewSearchFollowsOnItsTurn) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(false);  // ignores its token: its answer comes late and must still be thrown away
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        ASSERT_TRUE(Opponent().IsThinking());

        InputEvents::ActionEvent::Emit("ChessReset"_intern);  // the Reset button
        _manager->UpdateAll();

        EXPECT_FALSE(Opponent().IsThinking()) << "the version moved: the search is not wanted any more";
        engine->Open();
        ASSERT_TRUE(WaitUntil([&] { return engine->Answered() == 1; })) << "the stale search returns its move";
        IdleFrames();
        EXPECT_TRUE(Model().GetMoves().empty()) << "and the move is never played";
        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
        EXPECT_FALSE(Opponent().GetError().has_value());
        EXPECT_EQ(engine->Calls(), 1) << "it was white's turn after the reset: nothing to search";

        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "it is the program's turn again: a new search, and its answer is played";
        EXPECT_EQ(engine->Calls(), 2);
        EXPECT_EQ(factory.Calls(), 1) << "the same opponent: a reset does not remake it";
    }

    TEST_F(ChessUiTest, ARefusedPickLocksNobodyOut) {
        CountingFactory factory(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        const auto picked = Opponent().SetSide(PieceColor::Black);

        ASSERT_FALSE(picked.has_value());
        EXPECT_FALSE(Opponent().PlaysFor(PieceColor::Black));
        ClickMove("e2", "e4");
        ClickMove("e7", "e5");
        IdleFrames();
        EXPECT_EQ(Model().GetMoves().size(), 2u) << "both colors are people's";
        EXPECT_EQ(factory.Calls(), 1);
        EXPECT_FALSE(Opponent().GetError().has_value()) << "the move of the game is the person having seen it";
    }

    TEST_F(ChessUiTest, AnEngineThatFailedLocksTheProgramsSideButNotTheGameAndNothingIsRetried) {
        const auto engine = New<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));

        IdleFrames();
        EXPECT_TRUE(Opponent().PlaysFor(PieceColor::Black)) << "the error does not take the seat away";
        EXPECT_EQ(engine->Calls(), 1) << "no search per frame";
        Click("e7");
        EXPECT_FALSE(Model().HasSelection()) << "black is the program's: the click does nothing, the game waits for the person to act";
        Click("e5");

        EXPECT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_TRUE(Opponent().GetError().has_value());
        EXPECT_EQ(engine->Calls(), 1);
    }

    TEST_F(ChessUiTest, TheResetButtonLetsThePersonOutOfAnEngineError) {
        const auto engine = New<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));

        InputEvents::ActionEvent::Emit("ChessReset"_intern);
        _manager->UpdateAll();

        EXPECT_FALSE(Opponent().GetError().has_value());
        EXPECT_TRUE(Model().GetMoves().empty());
        ClickMove("e2", "e4");
        EXPECT_EQ(Model().GetMoves().size(), 1u) << "the person plays again";
        ASSERT_TRUE(FramesUntil([&] { return Opponent().GetError().has_value(); })) << "the program is still Black and fails again: the person acted, so it was asked again";
        EXPECT_EQ(engine->Calls(), 2);
    }

    TEST_F(ChessUiTest, ChangingTheSideLetsThePersonOutOfAnEngineErrorAndNobodyIsTheWayOutForGood) {
        const auto engine = New<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));
        const uint64_t revision = Opponent().GetRevision();

        ASSERT_TRUE(Opponent().CycleSide().has_value());  // the program plays White now, the person Black

        EXPECT_FALSE(Opponent().GetError().has_value());
        EXPECT_GT(Opponent().GetRevision(), revision);
        EXPECT_TRUE(Opponent().PlaysFor(PieceColor::White));
        ClickMove("e7", "e5");
        EXPECT_EQ(Model().GetMoves().size(), 2u) << "black is the person's after the change";
        ASSERT_TRUE(FramesUntil([&] { return Opponent().GetError().has_value(); })) << "white is the program's, its engine fails again";
        Click("d2");
        EXPECT_FALSE(Model().HasSelection());

        ASSERT_TRUE(Opponent().CycleSide().has_value());

        EXPECT_FALSE(Opponent().GetError().has_value());
        EXPECT_FALSE(Opponent().GetPlays().has_value());
        ClickMove("d2", "d4");
        ClickMove("d7", "d5");
        IdleFrames();
        EXPECT_EQ(Model().GetMoves().size(), 4u) << "two people on one screen again";
        EXPECT_EQ(engine->Calls(), 2);
    }

    TEST_F(ChessUiTest, ChangingTheLevelLetsThePersonOutOfAnEngineErrorAndAsksTheNewEngineOnce) {
        const auto engine = New<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));

        ASSERT_TRUE(Opponent().CycleLevel().has_value());

        EXPECT_FALSE(Opponent().GetError().has_value());
        ASSERT_EQ(factory.Calls(), 2);
        EXPECT_EQ(factory.Levels()[1], GetOpponentLevels()[(kDefaultOpponentLevel + 1) % GetOpponentLevels().size()].id);
        ASSERT_TRUE(FramesUntil([&] { return Opponent().GetError().has_value(); })) << "black is still the program's and to move: the new engine is asked, and fails";
        IdleFrames();
        EXPECT_EQ(engine->Calls(), 2) << "once per act of the person, not per frame";
    }

    TEST_F(ChessUiTest, StoppingTheGameDestroysTheOpponentAndCancelsItsSearch) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));

        ChessGame::GetActive()->Stop();  // what GameManager does when it switches away from a game or shuts down; the game itself is still there

        EXPECT_FALSE(Opponent().GetPlays().has_value());
        EXPECT_FALSE(Opponent().IsThinking());
        EXPECT_TRUE(WaitUntil([&] { return engine->WasCancelled(); })) << "the search was told to stop";
        IdleFrames();
        EXPECT_EQ(engine->Calls(), 1) << "nobody plays any more, so nothing is asked";
        EXPECT_EQ(Model().GetMoves().size(), 1u);
    }

    TEST_F(ChessUiTest, ShuttingTheGameDownWithASearchInFlightJoinsNothing) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(false);  // never looks at its token: only the gate can end its search
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        ASSERT_TRUE(Opponent().IsThinking());

        GameManager::GetInstance().Shutdown();  // Stop() destroys the opponent, as Application::Cleanup() does before the task system goes down

        EXPECT_EQ(ChessGame::GetActive(), nullptr);
        EXPECT_EQ(engine->Answered(), 0) << "the search is still at its gate: nothing waited for it";
        EXPECT_EQ(engine->Calls(), 1);
        _manager->UpdateAll();  // a frame after the game is gone asks nothing
        EXPECT_EQ(engine->Calls(), 1);

        engine->Open();  // frees the worker: the engine is alive as long as the search needs it, and the test does not leave a thread behind
        EXPECT_TRUE(WaitUntil([&] { return engine->Answered() == 1; }));
    }

    // ---- the hint on the board ----------------------------------------------------------------------------------------------------
    // The analyst holds the hint for one version of the position; the board fills its two squares (under the selection and the pieces) while it does. An engine double answers the
    // scripted move e2e4 through the analyst's factory, and the frames are real ones

    TEST_F(ChessUiTest, WithNoHintAskedForNoExtraSquareIsFilled) {
        CountingAnalysisFactory factory({New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        Settle();
        EXPECT_TRUE(HintFills().first.empty());
        EXPECT_TRUE(HintFills().second.empty());
        Analyst().SetEvaluation(true);  // an evaluation alone is no hint
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetEvaluation(Model()).has_value(); }));
        Draw();
        EXPECT_TRUE(HintFills().first.empty());
        EXPECT_TRUE(HintFills().second.empty());
        EXPECT_EQ(RectCalls(kLightSquareColor).size() + RectCalls(kDarkSquareColor).size(), 64u);
    }

    TEST_F(ChessUiTest, AReadyHintFillsTheTwoSquaresOfItsMoveInTheirOwnColorsWithWhiteAtTheBottom) {
        CountingAnalysisFactory factory({New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(ShowTheHint());
        ASSERT_FALSE(BlackIsAtTheBottom());

        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}}) {
            SCOPED_TRACE(Format("output {}x{}", size.first, size.second).c_str());
            DrawAt(size.first, size.second);
            const auto [from, to] = HintFills();
            ASSERT_EQ(from.size(), 1u);
            ASSERT_EQ(to.size(), 1u);
            EXPECT_TRUE(SameRect(from[0]->rect, SquareRect("e2"))) << "the square the piece leaves";
            EXPECT_TRUE(SameRect(to[0]->rect, SquareRect("e4"))) << "the square it goes to";
        }
    }

    TEST_F(ChessUiTest, AReadyHintFillsTheTurnedCellsWhenTheProgramPlaysWhite) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory opponentFactory({New<GatedEngine>(true)});  // the program opens and holds its answer: the board stays as it is
        Opponent().SetEngineFactory(opponentFactory.AsFactory());
        CountingAnalysisFactory factory({New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        ASSERT_TRUE(ShowTheHint());

        Draw();
        const auto [from, to] = HintFills();
        ASSERT_EQ(from.size(), 1u);
        ASSERT_EQ(to.size(), 1u);
        EXPECT_TRUE(SameRect(from[0]->rect, SquareRect("e2")));
        EXPECT_TRUE(SameRect(to[0]->rect, SquareRect("e4")));
        // written out: e2 is the cell column 3, row 1 of the turned grid, e4 column 3, row 3
        EXPECT_TRUE(SameRect(from[0]->rect, CellRect(3, 1)));
        EXPECT_TRUE(SameRect(to[0]->rect, CellRect(3, 3)));
    }

    TEST_F(ChessUiTest, TheHintIsDrawnOverTheSquaresAndUnderThePiecesAndASelectionStaysOnTopOfIt) {
        const auto pawn = GiveAtlasCell("WhitePawn_e2");
        CountingAnalysisFactory factory({New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(ShowTheHint());
        Model().OnSquareClicked(Sq("e2"));  // a selection is not a change of the position: the hint stays
        ASSERT_TRUE(Model().HasSelection());
        Draw();

        long lastSquare = -1;
        long hintFrom = -1;
        long hintTo = -1;
        long selection = -1;
        long legal = -1;
        long piece = -1;
        const auto& calls = _renderer->calls;
        for (size_t i = 0; i < calls.size(); ++i) {
            const DrawCall& call = calls[i];
            const long at = static_cast<long>(i);
            if (call.kind == DrawKind::Rect && (SameColor(call.color, kLightSquareColor) || SameColor(call.color, kDarkSquareColor))) {
                lastSquare = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kHintFromColor)) {
                hintFrom = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kHintToColor)) {
                hintTo = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kSelectedColor)) {
                selection = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kLegalMoveColor)) {
                legal = at;
            } else if (call.kind == DrawKind::Texture && call.texture == pawn.Get()) {
                piece = at;
            }
        }
        ASSERT_GE(hintFrom, 0);
        ASSERT_GE(hintTo, 0);
        ASSERT_GE(selection, 0);
        ASSERT_GE(legal, 0);
        ASSERT_GE(piece, 0);
        EXPECT_LT(lastSquare, hintFrom) << "over the squares";
        EXPECT_LT(hintFrom, hintTo);
        EXPECT_LT(hintTo, selection) << "under the selection";
        EXPECT_LT(hintTo, legal) << "and under the legal moves";
        EXPECT_LT(hintTo, piece) << "and under the pieces";
        EXPECT_LT(selection, piece);
    }

    TEST_F(ChessUiTest, AMoveByClicksAResetASetPositionAndTheControlApiTakeTheHintOffAtTheNextFrame) {
        CountingAnalysisFactory factory({New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        const auto expectHint = [&](bool shown) {
            Draw();
            EXPECT_EQ(HintFills().first.size(), shown ? 1u : 0u);
            EXPECT_EQ(HintFills().second.size(), shown ? 1u : 0u);
        };

        ASSERT_TRUE(ShowTheHint());
        expectHint(true);
        ClickMove("e2", "e3");  // a person's move
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        expectHint(false);

        ASSERT_TRUE(ShowTheHint());
        expectHint(true);
        Click("a7");  // the first click of a move only selects: the hint stays
        expectHint(true);
        Model().Reset();
        expectHint(false);

        ASSERT_TRUE(ShowTheHint());
        expectHint(true);
        Model().SetPosition(PositionFromFen(kBlackToMoveAfterE4));
        expectHint(false);

        ASSERT_TRUE(ShowTheHint());
        expectHint(true);
        Json request = Json::object();  // set_state and reset of the control API go through the same version
        request["fen"] = "k7/8/8/8/8/8/4K3/7R w - - 0 1";
        ASSERT_TRUE(ChessGame::GetActive()->AsControllable()->SetState(request).has_value());
        expectHint(false);

        ASSERT_TRUE(ShowTheHint());
        expectHint(true);
        ASSERT_TRUE(ChessGame::GetActive()->AsControllable()->Reset(Json::object()).has_value());
        expectHint(false);
    }

    TEST_F(ChessUiTest, AFinishedGameShowsNoHint) {
        CountingAnalysisFactory factory({New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Mate, 1}, Mv("a8a1"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        StartFrom(kBlackMatesOnA1);
        ASSERT_TRUE(ShowTheHint());
        Draw();
        ASSERT_EQ(HintFills().first.size(), 1u) << "a8";
        EXPECT_TRUE(SameRect(HintFills().first[0]->rect, SquareRect("a8")));
        EXPECT_TRUE(SameRect(HintFills().second[0]->rect, SquareRect("a1")));

        ASSERT_TRUE(Play(Model(), "a8a1"));
        ASSERT_TRUE(Model().IsGameOver());
        _manager->UpdateAll();
        Draw();
        EXPECT_TRUE(HintFills().first.empty());
        EXPECT_TRUE(HintFills().second.empty());
        const auto refused = Analyst().RequestHint(Model());
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error(), HintRefusal::GameOver);
        IdleFrames(10);
        Draw();
        EXPECT_TRUE(HintFills().first.empty());
        EXPECT_TRUE(HintFills().second.empty());
    }

    TEST_F(ChessUiTest, WhileTheEngineStillThinksNothingIsDrawnAndTheAnswerBringsTheHint) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"));
        engine->Gate(true);
        CountingAnalysisFactory factory({engine});
        Analyst().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Analyst().RequestHint(Model()).has_value());
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() > 0; })) << "the engine is asked and holds its answer";
        IdleFrames(20);
        Draw();
        EXPECT_TRUE(Analyst().IsHintRequested(Model()));
        EXPECT_TRUE(Analyst().IsThinking());
        EXPECT_TRUE(HintFills().first.empty());
        EXPECT_TRUE(HintFills().second.empty());

        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetHint(Model()).has_value(); }));
        Draw();
        EXPECT_EQ(HintFills().first.size(), 1u);
        EXPECT_EQ(HintFills().second.size(), 1u);
    }

    TEST_F(ChessUiTest, TheHintPlaysNothingAndLeavesThePiecesAndTheStateOfTheGameAsTheyAre) {
        CountingAnalysisFactory factory({New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        const auto boardOf = [&] {
            const auto state = ChessGame::GetActive()->AsControllable()->GetState();
            EXPECT_TRUE(state.has_value());
            const Json* board = state ? FindJsonMember(*state, "board") : nullptr;
            const Json* fen = state ? FindJsonMember(*state, "fen") : nullptr;
            const Json* moves = state ? FindJsonMember(*state, "moves") : nullptr;
            EXPECT_TRUE(board && fen && moves);
            return board && fen && moves ? DumpJson(*board) + DumpJson(*fen) + DumpJson(*moves) : String();
        };
        const String before = boardOf();
        const uint64_t modelVersion = Model().GetVersion();
        ASSERT_TRUE(ShowTheHint());
        IdleFrames(20);
        Draw();
        ASSERT_EQ(HintFills().first.size(), 1u);
        EXPECT_EQ(boardOf(), before);
        EXPECT_EQ(Model().GetVersion(), modelVersion) << "the model is not touched";
        EXPECT_TRUE(Model().GetMoves().empty());
        ExpectPiecesAtTheCellsOf(false);
        EXPECT_EQ(PieceOrigin("WhitePawn_e2"), (Pair<float, float>{420.0f, 560.0f}));
    }

    TEST_F(ChessUiTest, WithNoGameActiveTheBoardDrawsNoHint) {
        CountingAnalysisFactory factory({New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(ShowTheHint());
        Draw();
        ASSERT_EQ(HintFills().first.size(), 1u);

        GameManager::GetInstance().Shutdown();
        ASSERT_EQ(ChessGame::GetActive(), nullptr);
        Draw();
        EXPECT_TRUE(HintFills().first.empty());
        EXPECT_TRUE(HintFills().second.empty());
        EXPECT_EQ(RectCalls(kLightSquareColor).size() + RectCalls(kDarkSquareColor).size(), 64u) << "the squares are still drawn";
    }

    // ---- the last move on the board -----------------------------------------------------------------------------------------------
    // The board fills the from and to squares of GetMoves().back() in one color of its own, under the check, the hint and the selection; it keeps nothing, so a new game has no mark

    TEST_F(ChessUiTest, ANewGameMarksNoLastMove) {
        Draw();
        EXPECT_TRUE(LastMoveFills().empty());
        EXPECT_EQ(RectCalls(kLightSquareColor).size() + RectCalls(kDarkSquareColor).size(), 64u);
    }

    TEST_F(ChessUiTest, AMoveByClicksMarksItsTwoSquaresAndTheNextMoveReplacesThem) {
        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}}) {
            SCOPED_TRACE(Format("output {}x{}", size.first, size.second).c_str());
            Model().Reset();
            ClickMove("e2", "e4");
            ASSERT_EQ(Model().GetMoves().size(), 1u);
            DrawAt(size.first, size.second);
            auto fills = LastMoveFills();
            ASSERT_EQ(fills.size(), 2u);
            EXPECT_TRUE(SameRect(fills[0]->rect, SquareRect("e2"))) << "the square the piece left";
            EXPECT_TRUE(SameRect(fills[1]->rect, SquareRect("e4"))) << "the square it went to";

            ClickMove("e7", "e5");  // two people: the other side moves, and the mark follows
            ASSERT_EQ(Model().GetMoves().size(), 2u);
            DrawAt(size.first, size.second);
            fills = LastMoveFills();
            ASSERT_EQ(fills.size(), 2u);
            EXPECT_TRUE(SameRect(fills[0]->rect, SquareRect("e7")));
            EXPECT_TRUE(SameRect(fills[1]->rect, SquareRect("e5")));
        }
    }

    TEST_F(ChessUiTest, AMoveByTheControlApiAndAMoveByDragMarkTheirSquares) {
        Json action = Json::object();
        action["type"] = "move";
        action["uci"] = "g1f3";
        ASSERT_TRUE(ChessGame::GetActive()->AsControllable()->PerformAction(action).has_value());
        Draw();
        auto fills = LastMoveFills();
        ASSERT_EQ(fills.size(), 2u);
        EXPECT_TRUE(SameRect(fills[0]->rect, SquareRect("g1")));
        EXPECT_TRUE(SameRect(fills[1]->rect, SquareRect("f3")));

        Drag("e7", "e5");
        ASSERT_EQ(Model().GetMoves().size(), 2u);
        Draw();
        fills = LastMoveFills();
        ASSERT_EQ(fills.size(), 2u);
        EXPECT_TRUE(SameRect(fills[0]->rect, SquareRect("e7")));
        EXPECT_TRUE(SameRect(fills[1]->rect, SquareRect("e5")));
    }

    TEST_F(ChessUiTest, TheProgramsAnswerMarksItsSquares) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));
        Draw();
        const auto [from, to] = LastMoveRects();
        const auto fills = LastMoveFills();
        ASSERT_EQ(fills.size(), 2u);
        EXPECT_TRUE(SameRect(fills[0]->rect, from));
        EXPECT_TRUE(SameRect(fills[1]->rect, to));
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves().back().To()).color, PieceColor::Black) << "it is the black move that is marked, not the person's e2e4";
        EXPECT_FALSE(SameRect(from, SquareRect("e2")));
    }

    TEST_F(ChessUiTest, AResetASetPositionAndSetStateWithAFenTakeTheMarkOff) {
        const auto expectMarked = [&](bool marked) {
            Draw();
            EXPECT_EQ(LastMoveFills().size(), marked ? 2u : 0u);
        };
        ClickMove("e2", "e4");
        expectMarked(true);
        Model().Reset();
        expectMarked(false);

        ClickMove("d2", "d4");
        expectMarked(true);
        Model().SetPosition(PositionFromFen(kBlackToMoveAfterE4));
        expectMarked(false);

        ClickMove("e7", "e5");
        expectMarked(true);
        Json request = Json::object();
        request["fen"] = "k7/8/8/8/8/8/4K3/7R w - - 0 1";
        ASSERT_TRUE(ChessGame::GetActive()->AsControllable()->SetState(request).has_value());
        expectMarked(false);

        ASSERT_TRUE(Play(Model(), "h1h2"));
        expectMarked(true);
        ASSERT_TRUE(ChessGame::GetActive()->AsControllable()->Reset(Json::object()).has_value());
        expectMarked(false);
    }

    TEST_F(ChessUiTest, CastlingMarksTheKingsMoveEnPassantAndPromotionThePawnsSquares) {
        const auto expectMarks = [&](const char* from, const char* to) {
            SCOPED_TRACE(Format("{}{}", from, to).c_str());
            Draw();
            const auto fills = LastMoveFills();
            ASSERT_EQ(fills.size(), 2u);
            EXPECT_TRUE(SameRect(fills[0]->rect, SquareRect(from)));
            EXPECT_TRUE(SameRect(fills[1]->rect, SquareRect(to)));
        };

        StartFrom("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        ASSERT_TRUE(Play(Model(), "e1g1"));  // the rook goes to f1 and is not marked
        expectMarks("e1", "g1");
        ASSERT_TRUE(Play(Model(), "e8c8"));
        expectMarks("e8", "c8");

        StartFrom("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
        ASSERT_TRUE(Play(Model(), "e5d6"));  // the pawn taken on d5 is not marked
        expectMarks("e5", "d6");

        StartFrom("8/P6k/8/8/8/8/8/K7 w - - 0 1");
        ASSERT_TRUE(Play(Model(), "a7a8q"));
        expectMarks("a7", "a8");
        StartFrom("8/P6k/8/8/8/8/8/K7 w - - 0 1");
        Click("a7");
        Click("a8");
        ASSERT_TRUE(Model().HasPendingPromotion());
        Draw();
        EXPECT_TRUE(LastMoveFills().empty()) << "a promotion still waiting for its piece is no move yet";
        PressWith(kPromoteAction, "knight");
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        expectMarks("a7", "a8");
    }

    TEST_F(ChessUiTest, TheTurnedBoardMarksTheTurnedCells) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory opponentFactory({New<GatedEngine>(true)});  // the program opens and holds its answer: the board stays as it is
        Opponent().SetEngineFactory(opponentFactory.AsFactory());
        _manager->UpdateAll();
        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        ASSERT_TRUE(Play(Model(), "e2e4"));
        Draw();
        const auto fills = LastMoveFills();
        ASSERT_EQ(fills.size(), 2u);
        EXPECT_TRUE(SameRect(fills[0]->rect, SquareRect("e2")));
        EXPECT_TRUE(SameRect(fills[1]->rect, SquareRect("e4")));
        // written out: e2 is the cell column 3, row 1 of the turned grid, e4 column 3, row 3
        EXPECT_TRUE(SameRect(fills[0]->rect, CellRect(3, 1)));
        EXPECT_TRUE(SameRect(fills[1]->rect, CellRect(3, 3)));
    }

    TEST_F(ChessUiTest, TheLastMoveIsDrawnOverTheSquaresAndUnderTheCheckTheHintTheSelectionAndTheLegalMoves) {
        CountingAnalysisFactory factory({New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e8d8"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        StartFrom("4k3/8/8/8/8/8/8/3R1K2 w - - 0 1");
        ASSERT_TRUE(Play(Model(), "d1e1"));  // check: the rook's squares are marked and the king's square is red
        ASSERT_TRUE(Model().IsInCheck());
        ASSERT_TRUE(ShowTheHint());
        Click("e8");  // a selection of the king: its own square and the squares it can go to
        ASSERT_TRUE(Model().HasSelection());
        Draw();

        long lastSquare = -1;
        long firstMark = -1;
        long lastMark = -1;
        long check = -1;
        long hintFrom = -1;
        long selection = -1;
        long legal = -1;
        const auto& calls = _renderer->calls;
        for (size_t i = 0; i < calls.size(); ++i) {
            const DrawCall& call = calls[i];
            if (call.kind != DrawKind::Rect) {
                continue;
            }
            const long at = static_cast<long>(i);
            if (SameColor(call.color, kLightSquareColor) || SameColor(call.color, kDarkSquareColor)) {
                lastSquare = at;
            } else if (SameColor(call.color, kLastMoveColor)) {
                firstMark = firstMark < 0 ? at : firstMark;
                lastMark = at;
            } else if (SameColor(call.color, kCheckColor)) {
                check = at;
            } else if (SameColor(call.color, kHintFromColor)) {
                hintFrom = at;
            } else if (SameColor(call.color, kSelectedColor)) {
                selection = at;
            } else if (SameColor(call.color, kLegalMoveColor)) {
                legal = at;
            }
        }
        ASSERT_GE(firstMark, 0);
        ASSERT_GE(check, 0);
        ASSERT_GE(hintFrom, 0);
        ASSERT_GE(selection, 0);
        ASSERT_GE(legal, 0);
        EXPECT_EQ(LastMoveFills().size(), 2u);
        EXPECT_LT(lastSquare, firstMark) << "over the squares";
        EXPECT_LT(lastMark, check) << "under the check";
        EXPECT_LT(lastMark, hintFrom) << "under the hint";
        EXPECT_LT(lastMark, selection) << "under the selection";
        EXPECT_LT(lastMark, legal) << "and under the legal moves";
    }

    TEST_F(ChessUiTest, TheMarkOfTheLastMoveStaysAfterTheGameEnds) {
        StartFrom(kBlackMatesOnA1);
        ASSERT_TRUE(Play(Model(), "a8a1"));
        ASSERT_TRUE(Model().IsGameOver());
        Draw();
        const auto fills = LastMoveFills();
        ASSERT_EQ(fills.size(), 2u);
        EXPECT_TRUE(SameRect(fills[0]->rect, SquareRect("a8")));
        EXPECT_TRUE(SameRect(fills[1]->rect, SquareRect("a1")));
    }

    TEST_F(ChessUiTest, WithNoGameActiveTheBoardMarksNoLastMove) {
        ClickMove("e2", "e4");
        Draw();
        ASSERT_EQ(LastMoveFills().size(), 2u);
        GameManager::GetInstance().Shutdown();
        ASSERT_EQ(ChessGame::GetActive(), nullptr);
        Draw();
        EXPECT_TRUE(LastMoveFills().empty());
        EXPECT_EQ(RectCalls(kLightSquareColor).size() + RectCalls(kDarkSquareColor).size(), 64u);
    }

    // A watch: both colors are programs (OpponentController::ChooseSeats). Gated engines that never answer keep the turn where it is, so the guard of the board is what is looked at.
    namespace {
        constexpr ProgramSides kWatchBoth{true, true};
        constexpr const char* kWatchBlackToMove = "rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3";      // White has taken a pawn, Black to move
        constexpr const char* kWatchWhiteIsMated = "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3";  // the fool's mate: the game is over
    }  // namespace

    TEST_F(ChessUiTest, WatchTheBoardTakesNoClickOnWhitesTurnAndKeepsTheSearchAlone) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = New<GatedEngine>(true);
        const auto black = New<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return white->Calls() == 1; })) << "White's program has the search and waits at its gate";
        ASSERT_EQ(Model().GetTurn(), PieceColor::White);
        const uint64_t version = Model().GetVersion();

        for (const char* square : {"e2", "e4", "d2", "d4", "a4", "e7", "g1"}) {
            Click(square);
            EXPECT_FALSE(Model().HasSelection()) << square;
        }
        IdleFrames();

        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_FALSE(Model().HasPendingPromotion());
        EXPECT_EQ(Model().GetVersion(), version) << "the clicks did not move the game, so the search is not stale";
        EXPECT_TRUE(Opponent().IsThinking());
        EXPECT_FALSE(white->WasCancelled());
        EXPECT_EQ(white->Calls(), 1);
        EXPECT_EQ(black->Calls(), 0);
    }

    TEST_F(ChessUiTest, WatchTheBoardTakesNoClickOnBlacksTurnEither) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = New<GatedEngine>(true);
        const auto black = New<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kWatchBlackToMove);
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return black->Calls() == 1; })) << "Black's program has the search and waits at its gate";
        ASSERT_EQ(Model().GetTurn(), PieceColor::Black);
        const uint64_t version = Model().GetVersion();

        for (const char* square : {"e7", "e5", "d7", "d6", "a2", "d5", "g8"}) {
            Click(square);
            EXPECT_FALSE(Model().HasSelection()) << square;
        }
        IdleFrames();

        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_TRUE(Opponent().IsThinking());
        EXPECT_FALSE(black->WasCancelled());
        EXPECT_EQ(black->Calls(), 1);
    }

    TEST_F(ChessUiTest, WatchNoClickQueuesAPremove) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = New<GatedEngine>(true);
        const auto black = New<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kWatchBlackToMove);
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return black->Calls() == 1; }));

        ClickMove("d2", "d3");
        ClickMove("d2", "d4");
        IdleFrames(5);

        EXPECT_TRUE(Model().GetPremoves().empty());
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_TRUE(Model().GetMoves().empty());
    }

    TEST_F(ChessUiTest, WatchNoClickStartsAPromotion) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = New<GatedEngine>(true);
        const auto black = New<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kPromotionFen);  // the pawn on a7 would promote on a8 for a person
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return white->Calls() == 1; }));
        const uint64_t version = Model().GetVersion();

        ClickMove("a7", "a8");
        IdleFrames(5);

        EXPECT_FALSE(Model().HasSelection());
        EXPECT_FALSE(Model().HasPendingPromotion());
        EXPECT_FALSE(PromotionShown());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_TRUE(Model().GetMoves().empty());
    }

    TEST_F(ChessUiTest, WatchStartingDropsTheSelectionOfThePersonWhoseTurnItIs) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = New<GatedEngine>(true);
        const auto black = New<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        Click("e2");
        ASSERT_TRUE(Model().HasSelection()) << "two people: the click selects";

        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());

        EXPECT_FALSE(Model().HasSelection()) << "the program took the turn: the half-made move of the person goes";
        Draw();
        EXPECT_TRUE(RectCalls(kSelectedColor).empty()) << "and no selection is drawn";
        ASSERT_TRUE(FramesUntil([&] { return white->Calls() == 1; }));
    }

    TEST_F(ChessUiTest, WatchWhiteStaysAtTheBottomAndTheMaterialGroupsAreNotSwapped) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true), New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kWhiteTookAPawn);
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "BottomPawn");

        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        _manager->UpdateAll();
        Settle();

        EXPECT_TRUE(Opponent().IsWatching());
        EXPECT_FALSE(Opponent().GetPlays().has_value());
        EXPECT_FALSE(BlackIsAtTheBottom());
        ExpectPiecesAtTheCellsOf(false);
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f}));
        EXPECT_EQ(Coordinates(true), "abcdefgh");
        EXPECT_EQ(Coordinates(false), "87654321");
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn") << "White's captures stay in the lower group";
        EXPECT_EQ(MaterialSprite("Bottom", "Pawn"), "BlackPawn");
        EXPECT_EQ(Var("materialBottomBalance"), "+1");
        EXPECT_EQ(Var("materialTopBalance"), "");
    }

    TEST_F(ChessUiTest, WatchAClickAfterTheEndStartsANewGameAndTheProgramsOpenIt) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = New<GatedEngine>(true);
        const auto black = New<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kWatchWhiteIsMated);
        ASSERT_TRUE(Model().IsGameOver());
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        IdleFrames(10);
        EXPECT_EQ(white->Calls(), 0) << "nothing is asked for a finished game";
        EXPECT_EQ(black->Calls(), 0);
        const uint64_t version = Model().GetVersion();

        Click("e4");  // the game is over, so this is the "next game" click, also while two programs play

        EXPECT_FALSE(Model().IsGameOver());
        EXPECT_GT(Model().GetVersion(), version);
        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
        EXPECT_FALSE(Model().HasSelection());
        ASSERT_TRUE(FramesUntil([&] { return white->Calls() == 1; })) << "the new game opens with White's program";
        EXPECT_TRUE(Opponent().IsWatching()) << "the click starts the next game, not a person's game";

        Click("e2");
        EXPECT_FALSE(Model().HasSelection()) << "and the board is locked again";
    }

    TEST_F(ChessUiTest, WatchTheResetButtonStartsAgainAndTheBoardStaysLocked) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = New<GatedEngine>(true);
        const auto black = New<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kWatchBlackToMove);
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return black->Calls() == 1; }));

        Press("ChessReset");
        _manager->UpdateAll();

        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
        EXPECT_TRUE(Opponent().IsWatching()) << "Reset keeps the opponent";
        ASSERT_TRUE(FramesUntil([&] { return white->Calls() == 1; })) << "and the program opens the new game";
        Click("e2");
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_EQ(factory.Calls(), 2) << "no new engine for a reset";
    }

    TEST_F(ChessUiTest, WatchTwoPlayersInThePanelGivesTheBoardBackToClicks) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = New<GatedEngine>(true);
        const auto black = New<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return white->Calls() == 1; }));
        Click("e2");
        ASSERT_FALSE(Model().HasSelection());

        PressWith(kSetModeAction, "two");
        _manager->UpdateAll();

        EXPECT_FALSE(Opponent().IsWatching());
        EXPECT_FALSE(Opponent().PlaysFor(PieceColor::White));
        EXPECT_FALSE(Opponent().PlaysFor(PieceColor::Black));
        EXPECT_FALSE(Opponent().IsThinking());
        EXPECT_TRUE(WaitUntil([&] { return white->WasCancelled(); })) << "the search of the program that left is cancelled";
        ClickMove("e2", "e4");
        EXPECT_EQ(Model().GetMoves().size(), 1u) << "both colors move by clicks again";
        ClickMove("e7", "e5");
        EXPECT_EQ(Model().GetMoves().size(), 2u);
    }

    TEST_F(ChessUiTest, WatchNobodyThroughTheControlApiGivesTheBoardBackToClicks) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true), New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ControlPlays("both");
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().IsWatching());
        Click("e2");
        ASSERT_FALSE(Model().HasSelection());

        ControlPlays(nullptr);
        _manager->UpdateAll();

        EXPECT_FALSE(Opponent().IsWatching());
        Click("e2");
        EXPECT_TRUE(Model().HasSelection());
    }

    TEST_F(ChessUiTest, WatchATakeoverLeavesTheClicksToThePersonsColorOnly) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = New<GatedEngine>(true);
        const auto black = New<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return white->Calls() == 1; }));

        ASSERT_TRUE(Opponent().ChooseSeats(ProgramSides{true, false}, {}, {}).has_value());  // the person takes Black over
        _manager->UpdateAll();
        EXPECT_FALSE(Opponent().IsWatching());
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::White));
        EXPECT_TRUE(BlackIsAtTheBottom()) << "one program: the board is turned for the person";
        EXPECT_FALSE(white->WasCancelled()) << "White's seat is unchanged: its search stays";

        Click("e2");
        EXPECT_FALSE(Model().HasSelection()) << "White's turn is the program's";
        white->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 1; }));
        EXPECT_EQ(Model().GetTurn(), PieceColor::Black);

        Click("a2");
        EXPECT_FALSE(Model().HasSelection()) << "a white piece is not the person's";
        Click("e7");
        EXPECT_TRUE(Model().HasSelection()) << "Black's turn is the person's";
        EXPECT_EQ(black->Calls(), 0) << "Black's program left, and nothing asked it";
    }

    TEST_F(ChessUiTest, WatchAnErrorOfOneSeatDoesNotUnlockTheBoard) {
        const auto white = New<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "no engine in the test"});
        const auto black = New<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return Opponent().GetError().has_value(); })) << "White's program failed";
        const uint64_t version = Model().GetVersion();

        Click("e2");
        Click("e4");
        IdleFrames(20);

        EXPECT_TRUE(Opponent().GetError().has_value()) << "the error stands until the game moves";
        EXPECT_TRUE(Opponent().PlaysFor(PieceColor::White));
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetVersion(), version);
    }

    // ---- the drag --------------------------------------------------------------------------------------------------------------
    // A press acts as a click and arms a drag; the release over a legal target plays the move through the path of a second click, a release anywhere else calls nothing on the
    // model (the piece goes back and stays selected). Pointer events are dispatched through UIInput, like the window does, so the canvases get them first

    TEST_F(ChessUiTest, ADragPlaysWhatTwoClicksPlay) {
        const uint64_t start = Model().GetVersion();
        ClickMove("e2", "e4");
        const String byClicks = Model().GetPosition().ToFen();
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        const String clickedMove = Model().GetMoves().back().ToUci();
        EXPECT_EQ(Model().GetVersion(), start + 1);

        Model().Reset();
        const uint64_t again = Model().GetVersion();
        Drag("e2", "e4");

        EXPECT_EQ(Model().GetPosition().ToFen(), byClicks);
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), clickedMove);
        EXPECT_EQ(Model().GetVersion(), again + 1);
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_EQ(Model().GetTurn(), PieceColor::Black);
        EXPECT_FALSE(Lifted().has_value());
    }

    TEST_F(ChessUiTest, ADragToTheStartSquareAndAReleaseWithoutMotionPlayNothingAndKeepTheSelection) {
        const uint64_t version = Model().GetVersion();

        Drag("e2", "e2");
        EXPECT_TRUE(Model().GetMoves().empty());
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("e2"));
        EXPECT_EQ(Model().GetLegalMoves().size(), 2u);

        Model().ClearSelection();
        Click("e2");
        ReleaseOn("e2");
        EXPECT_TRUE(Model().GetMoves().empty());
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("e2"));
        EXPECT_EQ(Model().GetLegalMoves().size(), 2u);
        EXPECT_EQ(Model().GetVersion(), version);
    }

    TEST_F(ChessUiTest, ADragOffTheBoardPlaysNothingKeepsTheSelectionAndAClickFinishesTheMove) {
        Draw();
        const uint64_t version = Model().GetVersion();
        const Pair<float, float> places[] = {
            PixelOf(50.0f, 400.0f),   // the left margin, where the material chips are
            PixelOf(420.0f, 770.0f),  // under the board
            PixelOf(420.0f, 40.0f),   // above it
            PixelOf(800.0f, 400.0f),  // the right margin
            {5.0f, 5.0f},             // outside the view altogether
        };
        for (const auto& place : places) {
            SCOPED_TRACE(place.first);
            DragBetween(SquareCenter("e2"), place);
            EXPECT_TRUE(Model().GetMoves().empty());
            EXPECT_EQ(Model().GetVersion(), version);
            ASSERT_TRUE(Model().HasSelection());
            EXPECT_EQ(Model().GetSelected(), Sq("e2"));
            EXPECT_EQ(Model().GetLegalMoves().size(), 2u);
            EXPECT_FALSE(Lifted().has_value());
        }

        Click("e4");
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "e2e4");
    }

    TEST_F(ChessUiTest, ADragToASquareThatIsNotATargetOrOntoAnotherOwnPieceKeepsTheSelectionOnTheStartSquare) {
        const uint64_t version = Model().GetVersion();
        for (const char* to : {"e5", "d2", "b1", "e7", "h8"}) {
            SCOPED_TRACE(to);
            Drag("e2", to);
            EXPECT_TRUE(Model().GetMoves().empty());
            EXPECT_EQ(Model().GetVersion(), version);
            ASSERT_TRUE(Model().HasSelection());
            EXPECT_EQ(Model().GetSelected(), Sq("e2")) << "the release did not select the piece it landed on";
            EXPECT_EQ(Model().GetLegalMoves().size(), 2u);
        }
    }

    TEST_F(ChessUiTest, ADragCapturesLikeTwoClicks) {
        Drag("e2", "e4");
        Drag("d7", "d5");
        Drag("e4", "d5");

        ASSERT_EQ(Model().GetMoves().size(), 3u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "e4d5");
        EXPECT_EQ(Model().GetPieceAt(Sq("d5")).type, PieceType::Pawn);
        EXPECT_EQ(Model().GetPieceAt(Sq("d5")).color, PieceColor::White);
        EXPECT_TRUE(Model().GetPieceAt(Sq("e4")).IsEmpty());
    }

    TEST_F(ChessUiTest, APressOnAnotherPieceOfTheSideToMoveArmsTheDragOfThatPieceAndOtherPressesArmNothing) {
        Click("d2");
        ASSERT_EQ(Model().GetSelected(), Sq("d2"));
        Draw();
        const auto e2 = SquareCenter("e2");
        const auto e4 = SquareCenter("e4");
        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(e2.first, e2.second, true));
        ASSERT_EQ(Model().GetSelected(), Sq("e2"));
        MouseMove(e2.first + kDragStepPixels, e2.second, true);
        const auto lifted = Lifted();
        ASSERT_TRUE(lifted.has_value());
        EXPECT_EQ(lifted->from, Sq("e2"));
        Release(e4.first, e4.second);
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "e2e4");

        Model().Reset();
        // A piece of the side that is not to move, an empty square and (with a piece selected) an enemy piece arm nothing
        Drag("e7", "e5");
        Drag("e4", "e5");
        Click("e2");
        Drag("e7", "e4");
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_FALSE(Model().HasSelection()) << "the press on the enemy piece dropped the selection and the release did not make a move";
    }

    TEST_F(ChessUiTest, TheProgramsTurnTakesNoDragAndDoesNotCancelItsSearch) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        Drag("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; })) << "Black's program has the search and waits at its gate";
        const uint64_t version = Model().GetVersion();

        Drag("e7", "e5");
        Drag("d7", "d5");
        Drag("g8", "f6");
        IdleFrames(5);

        EXPECT_FALSE(Model().HasSelection());
        EXPECT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_TRUE(Opponent().IsThinking());
        EXPECT_FALSE(engine->WasCancelled());
        EXPECT_EQ(engine->Calls(), 1);
        EXPECT_FALSE(Lifted().has_value());
    }

    TEST_F(ChessUiTest, WatchTheBoardTakesNoDragOnEitherTurn) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = New<GatedEngine>(true);
        const auto black = New<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return white->Calls() == 1; }));
        const uint64_t version = Model().GetVersion();

        Drag("e2", "e4");
        Drag("d2", "d4");
        IdleFrames(5);
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_FALSE(white->WasCancelled());

        StartFrom(kWatchBlackToMove);
        ASSERT_TRUE(FramesUntil([&] { return black->Calls() == 1; }));
        const uint64_t blackVersion = Model().GetVersion();
        Drag("e7", "e5");
        Drag("d7", "d6");
        IdleFrames(5);
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetVersion(), blackVersion);
        EXPECT_FALSE(black->WasCancelled());
    }

    TEST_F(ChessUiTest, ThePressAfterTheGameEndedStartsANewGameAndItsReleasePlaysNothing) {
        StartFrom(kWatchWhiteIsMated);
        ASSERT_TRUE(Model().IsGameOver());

        Drag("e2", "e4");

        EXPECT_FALSE(Model().IsGameOver());
        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_FALSE(Lifted().has_value());
    }

    TEST_F(ChessUiTest, WhileAPanelIsShownADragOnTheBoardArmsNothing) {
        ASSERT_TRUE(OpenThePanel());
        Drag("e2", "e4");
        EXPECT_FALSE(Model().HasSelection()) << "the opponent panel took the press";
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_FALSE(Lifted().has_value());
        Press("ChessCloseMenu");
        ASSERT_FALSE(Panel()->_visible);

        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());
        const uint64_t version = Model().GetVersion();
        Drag("a7", "a8");
        // A press and a release on the dim are a click on it: the drag cancels the promotion, and the board armed nothing
        EXPECT_FALSE(PromotionShown());
        EXPECT_FALSE(Model().HasPendingPromotion());
        EXPECT_EQ(Model().GetSelected(), Sq("a7"));
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_FALSE(Lifted().has_value());
    }

    TEST_F(ChessUiTest, ADragToTheLastRankOpensThePromotionPanelAndMovesNothingUntilAPieceIsPicked) {
        StartFrom(kPromotionFen);
        const uint64_t version = Model().GetVersion();
        const String fen = Model().GetPosition().ToFen();

        Drag("a7", "a8");

        EXPECT_TRUE(Model().HasPendingPromotion());
        EXPECT_TRUE(PromotionShown());
        EXPECT_EQ(Model().GetPosition().ToFen(), fen) << "nothing has moved";
        EXPECT_EQ(Model().GetVersion(), version);
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("a7"));

        ASSERT_TRUE(ClickPromotion("PromotionCancel").Both());
        EXPECT_FALSE(PromotionShown());
        EXPECT_TRUE(Model().HasSelection()) << "Cancel keeps the pawn selected";
        EXPECT_EQ(Model().GetSelected(), Sq("a7"));

        Drag("a7", "a8");
        EXPECT_TRUE(PromotionShown()) << "a second drag asks again";
        ASSERT_TRUE(ClickPromotion("PromoteQueen").Both());
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "a7a8q");
    }

    TEST_F(ChessUiTest, ADragInTheTurnedBoardGoesByTheDrawnCells) {
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        StartFrom(kBlackToMoveAfterE4);
        ASSERT_TRUE(BlackIsAtTheBottom());
        const uint64_t version = Model().GetVersion();

        // Where e5 would be with White at the bottom (fifth column, fourth row from the top) is d4 on this board, not a target of the pawn on e7
        DragCells(3, 6, 4, 3);
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetVersion(), version);
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("e7"));

        DragCells(3, 6, 3, 4);  // e7 to e5 as it is drawn
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "e7e5");
    }

    TEST_F(ChessUiTest, ADragThatWentStaleBetweenPressAndReleaseIsDropped) {
        Draw();
        const auto e2 = SquareCenter("e2");
        const auto e4 = SquareCenter("e4");
        const auto press = [&] {
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(e2.first, e2.second, true));
            MouseMove(e2.first + kDragStepPixels, e2.second, true);
            EXPECT_TRUE(Lifted().has_value());
        };

        // After each change the piece is selected again by hand, so only the check of the drag (not the selection) can keep the release from playing e2e4
        press();
        Model().SetPosition(PositionFromFen(Position::kStartFen));
        Model().OnSquareClicked(Sq("e2"));
        EXPECT_FALSE(Lifted().has_value()) << "SetPosition";
        Release(e4.first, e4.second);
        EXPECT_TRUE(Model().GetMoves().empty());

        press();
        Model().Reset();
        Model().OnSquareClicked(Sq("e2"));
        EXPECT_FALSE(Lifted().has_value()) << "Reset";
        Release(e4.first, e4.second);
        EXPECT_TRUE(Model().GetMoves().empty());

        Model().Reset();
        press();
        ASSERT_TRUE(Play(Model(), "g1f3"));  // the control API plays a move of its own
        EXPECT_FALSE(Lifted().has_value()) << "a move of someone else";
        Release(e4.first, e4.second);
        EXPECT_EQ(Model().GetMoves().size(), 1u);
    }

    TEST_F(ChessUiTest, ALostReleaseIsCleanedUpByTheNextMotionWithoutTheButton) {
        Draw();
        const auto e2 = SquareCenter("e2");
        const auto reset = ScreenCenter(*HudElement("Reset"));
        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(e2.first, e2.second, true));
        MouseMove(e2.first + kDragStepPixels, e2.second, true);
        MouseMove(reset.first, reset.second, true);
        EXPECT_TRUE(Lifted().has_value()) << "the button is still held";
        // The release over the Reset button is taken by the HUD: the board gets nothing. The next motion has no button held
        Release(reset.first, reset.second);
        EXPECT_TRUE(Lifted().has_value()) << "the release never reached the board";
        const auto board = SquareCenter("e3");
        MouseMove(board.first, board.second, false);
        EXPECT_FALSE(Lifted().has_value());

        ReleaseOn("e4");
        EXPECT_TRUE(Model().GetMoves().empty());
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("e2"));
    }

    TEST_F(ChessUiTest, TheLiftedPieceIsTheSquareItCameFromAndTheTopLeftIsThePointerMinusHalfASquare) {
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        for (const bool turned : {false, true}) {
            SCOPED_TRACE(turned ? "turned" : "White at the bottom");
            ControlPlays(turned ? "white" : nullptr);
            StartFrom(turned ? kBlackToMoveAfterE4 : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
            ASSERT_EQ(BlackIsAtTheBottom(), turned);
            const char* piece = turned ? "e7" : "e2";
            Draw();
            const auto at = SquareCenter(piece);

            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.first, at.second, true));
            EXPECT_FALSE(Lifted().has_value()) << "right after the press";
            MouseMove(at.first + kUnderThresholdPixels, at.second, true);
            EXPECT_FALSE(Lifted().has_value()) << "under the threshold";

            const float pointerX = at.first + kPointerOffsetX;
            const float pointerY = at.second + kPointerOffsetY;
            MouseMove(pointerX, pointerY, true);
            const auto lifted = Lifted();
            ASSERT_TRUE(lifted.has_value());
            EXPECT_EQ(lifted->from, Sq(piece));
            EXPECT_NEAR(lifted->x, GetScene().ScreenToSceneX(pointerX) - kHalfSquare, kPixelTolerance);
            EXPECT_NEAR(lifted->y, GetScene().ScreenToSceneY(pointerY) - kHalfSquare, kPixelTolerance);

            MouseMove(at.first + 1.0f, at.second, true);
            EXPECT_TRUE(Lifted().has_value()) << "once lifted the piece stays lifted";
            Release(pointerX, pointerY);
            EXPECT_FALSE(Lifted().has_value()) << "after the release";
        }
    }

    TEST_F(ChessUiTest, TwoClicksStillPlayAndAClickWithItsReleaseLeavesThePieceSelected) {
        ClickMove("e2", "e4");
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_FALSE(Model().HasSelection());

        // A click as a mouse makes it: the press, then the release at the same place
        Click("e7");
        ReleaseOn("e7");
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("e7"));
        Click("e5");
        ReleaseOn("e5");
        ASSERT_EQ(Model().GetMoves().size(), 2u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "e7e5");
        EXPECT_FALSE(Model().HasSelection());
    }

    TEST_F(ChessUiTest, ADraggedPieceIsCenteredUnderThePointerOnTheDragLayerAndGoesBackOrLandsOnTheTargetOnLetGo) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        for (const bool turned : {false, true}) {
            SCOPED_TRACE(turned ? "turned" : "White at the bottom");
            ControlPlays(turned ? "white" : nullptr);
            StartFrom(turned ? kBlackToMoveAfterE4 : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
            ASSERT_EQ(BlackIsAtTheBottom(), turned);
            const char* piece = turned ? "e7" : "e2";
            const char* target = turned ? "e5" : "e4";
            const char* node = turned ? "BlackPawn_e7" : "WhitePawn_e2";
            const auto [homeColumn, homeRow] = CellOfSquare(Sq(piece), turned);
            const auto [targetColumn, targetRow] = CellOfSquare(Sq(target), turned);
            const auto home = CellOrigin(homeColumn, homeRow);
            const auto landing = CellOrigin(targetColumn, targetRow);
            ExpectPiecesAtTheCellsOf(turned);

            // In the hand: the transform is the pointer minus half a square, the node is on the drag layer and no other piece is
            Draw();
            const auto at = SquareCenter(piece);
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.first, at.second, true));
            _manager->UpdateAll();
            EXPECT_EQ(PieceOrigin(node), home) << "just pressed: not lifted yet";
            EXPECT_EQ(NodeLayer(node), kPieceLayer);
            const float pointerX = at.first + kPointerOffsetX;
            const float pointerY = at.second + kPointerOffsetY;
            MouseMove(pointerX, pointerY, true);
            _manager->UpdateAll();
            EXPECT_NEAR(PieceOrigin(node).first, GetScene().ScreenToSceneX(pointerX) - kHalfSquare, kPixelTolerance);
            EXPECT_NEAR(PieceOrigin(node).second, GetScene().ScreenToSceneY(pointerY) - kHalfSquare, kPixelTolerance);
            EXPECT_EQ(NodeLayer(node), kDragLayer);
            size_t others = 0;
            for (const auto& child : GetScene().GetRootNode().GetChildren()) {
                if (child->GetComponent<ChessPieceComponent>() && child->GetName() != PoolString::Intern(node)) {
                    EXPECT_EQ(child->GetLayer(), kPieceLayer) << child->GetName().CStr();
                    ++others;
                }
            }
            EXPECT_EQ(others, kPieceCount - 1);
            for (const auto& child : GetScene().GetRootNode().GetChildren()) {
                if (child->GetName() == PoolString::Intern(node)) {
                    EXPECT_FLOAT_EQ(child->GetComponent<TransformComponent>()->_width, 2.0f * kHalfSquare) << "the size of a square";
                }
            }

            // Let go off the board, in the margin: the piece is back on its cell and its layer, still selected
            Release(PixelOf(50.0f, 400.0f).first, PixelOf(50.0f, 400.0f).second);
            _manager->UpdateAll();
            EXPECT_EQ(PieceOrigin(node), home) << "off the board";
            EXPECT_EQ(NodeLayer(node), kPieceLayer);
            EXPECT_TRUE(Model().HasSelection());

            // Dragged onto its own square and let go there: back as well
            Drag(piece, piece);
            _manager->UpdateAll();
            EXPECT_EQ(PieceOrigin(node), home) << "the start square";
            EXPECT_EQ(NodeLayer(node), kPieceLayer);

            // Let go on the target: on the target cell at once, on the piece layer
            Click(piece);
            MouseMove(at.first + kDragStepPixels, at.second, true);
            _manager->UpdateAll();
            EXPECT_EQ(NodeLayer(node), kDragLayer);
            const auto over = SquareCenter(target);
            MouseMove(over.first, over.second, true);
            Release(over.first, over.second);
            _manager->UpdateAll();
            ASSERT_EQ(Model().GetMoves().size(), 1u);
            EXPECT_EQ(PieceOrigin(node), landing) << "dropped on the target";
            EXPECT_EQ(NodeLayer(node), kPieceLayer);
            ExpectPiecesAtTheCellsOf(turned);
        }
    }

    TEST_F(ChessUiTest, ADraggedPieceIsDrawnOverThePiecesAndTheChipsAndUnderTheHud) {
        const auto pawn = GiveAtlasCell("WhitePawn_b2");
        const auto king = GiveAtlasCell("WhiteKing_e1");
        _manager->UpdateAll();                                      // the pieces register, so that the handles follow the moves
        for (const char* uci : {"e2e4", "d7d5", "e4d5", "d8d5"}) {  // a pawn each is gone: there are chips to draw over, and White is to move
            ASSERT_TRUE(Play(Model(), uci)) << uci;
        }
        _manager->UpdateAll();
        Settle();
        ASSERT_NE(ShownMaterialRows(), "");

        const auto indexes = [&] {
            struct Found {
                long pawn = -1;
                long king = -1;
                long firstChip = -1;
                long lastChip = -1;
                long resetButton = -1;
            } found;
            for (size_t i = 0; i < _renderer->calls.size(); ++i) {
                const DrawCall& call = _renderer->calls[i];
                const long at = static_cast<long>(i);
                if (call.kind == DrawKind::Texture && call.texture == pawn.Get()) {
                    found.pawn = at;
                } else if (call.kind == DrawKind::Texture && call.texture == king.Get()) {
                    found.king = at;
                } else if (call.kind == DrawKind::Rect && SameColor(call.color, kMaterialChipColor)) {
                    found.firstChip = found.firstChip < 0 ? at : found.firstChip;
                    found.lastChip = at;
                } else if (call.kind == DrawKind::Rect && SameColor(call.color, kResetColor)) {
                    found.resetButton = at;
                }
            }
            return found;
        };

        Draw();
        auto resting = indexes();
        ASSERT_GE(resting.pawn, 0);
        ASSERT_GE(resting.king, 0);
        ASSERT_GE(resting.firstChip, 0);
        EXPECT_LT(resting.pawn, resting.firstChip) << "a piece on the board is under the chips";

        Click("b2");
        const auto at = SquareCenter("b2");
        MouseMove(at.first + kDragStepPixels, at.second, true);
        ASSERT_TRUE(Lifted().has_value());
        _manager->UpdateAll();
        Draw();
        const auto lifted = indexes();
        ASSERT_GE(lifted.pawn, 0);
        ASSERT_GE(lifted.king, 0);
        ASSERT_GE(lifted.lastChip, 0);
        ASSERT_GE(lifted.resetButton, 0);
        EXPECT_GT(lifted.pawn, lifted.king) << "over the other pieces";
        EXPECT_GT(lifted.pawn, lifted.lastChip) << "over the captured material";
        EXPECT_LT(lifted.pawn, lifted.resetButton) << "under the HUD";

        // The input order of the canvases follows the draw order, which the piece on its higher layer must not shuffle among them: the topmost is asked first
        const auto rankOf = [&](const char* name) {
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(name)) {
                    return GetScene().GetDrawRank(*node);
                }
            }
            ADD_FAILURE() << name;
            return -1;
        };
        EXPECT_LT(rankOf("BoardCoordinates"), rankOf("Material"));
        EXPECT_LT(rankOf("Material"), rankOf("Hud"));
        EXPECT_LT(rankOf("Hud"), rankOf("MenuPanel"));
        EXPECT_LT(rankOf("MenuPanel"), rankOf("OpponentPanel"));
        EXPECT_LT(rankOf("OpponentPanel"), rankOf("PromotionPanel"));

        Release(at.first, at.second);
        _manager->UpdateAll();
        Draw();
        resting = indexes();
        EXPECT_LT(resting.pawn, resting.firstChip) << "let go: under the chips again";
    }

    TEST_F(ChessUiTest, AGameThatMovesUnderTheHandPutsThePieceBackAtOnce) {
        _manager->UpdateAll();
        Draw();
        const auto at = SquareCenter("e2");
        const auto home = PieceOrigin("WhitePawn_e2");
        const auto lift = [&] {
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.first, at.second, true));
            MouseMove(at.first + kDragStepPixels, at.second, true);
            _manager->UpdateAll();
            ASSERT_EQ(NodeLayer("WhitePawn_e2"), kDragLayer);
            ASSERT_NE(PieceOrigin("WhitePawn_e2"), home);
        };

        lift();
        Model().Reset();
        _manager->UpdateAll();
        EXPECT_FALSE(Lifted().has_value());
        EXPECT_EQ(PieceOrigin("WhitePawn_e2"), home) << "a reset";
        EXPECT_EQ(NodeLayer("WhitePawn_e2"), kPieceLayer);

        lift();
        ASSERT_TRUE(Play(Model(), "g1f3"));  // a move of the control API
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhitePawn_e2"), home) << "a move of someone else";
        EXPECT_EQ(NodeLayer("WhitePawn_e2"), kPieceLayer);

        Model().Reset();
        lift();
        Model().SetPosition(PositionFromFen(Position::kStartFen));
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhitePawn_e2"), home) << "a new position";
        EXPECT_EQ(NodeLayer("WhitePawn_e2"), kPieceLayer);
    }

    // ---- the board in the Portrait view ------------------------------------------------------------------------------------------
    // What the entry asks of the board holds in the Portrait view as in the default one: a click and a drag go by the cells that are drawn, in both orientations, and the dim and the bar
    // cover the board. The expected cells are written out here from the numbers of the view (the fit of 720 x 960, or 720 x 1204 for the Tall view, the board at (40, 144) in both), not
    // taken from the board component

    namespace {

        // 480 x 853 is the Tall view with thin bars above and below it, 1000 x 1200 Portrait with bars at the sides, 768 x 1024 (3:4) Portrait with none
        const Pair<float, float> kPortraitOutputs[] = {{480.0f, 853.0f}, {768.0f, 1024.0f}, {1000.0f, 1200.0f}};
        constexpr int kPawnRow = 6;    // the row of the pawns of the person at the bottom, from the top
        constexpr int kTargetRow = 4;  // where a pawn goes with its double step

        /// The height of the view an upright output shows: Tall up to its aspect, else Portrait (ChessUiScreenTest holds the rule)
        float UprightViewHeight(const Pair<float, float>& output) {
            return output.first / output.second <= kTallMaxAspect ? kTallViewHeight : kPortraitViewHeight;
        }

        /// The cell (column from the left, row from the top) of the board in the Portrait or the Tall view, in the pixels of an output
        Rect PortraitCell(const Pair<float, float>& output, int column, int row) {
            const ViewTransform fit = ViewTransform::Fit(kPortraitViewWidth, UprightViewHeight(output), output.first, output.second);
            return fit.ToOutput(kPortraitBoardLeft + static_cast<float>(column) * kCellSize, kPortraitBoardTop + static_cast<float>(row) * kCellSize, kCellSize, kCellSize);
        }

        Pair<float, float> MiddleOf(const Rect& rect) {
            return {rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
        }

        /// The column of the e file: the fifth from the left with White at the bottom, the fourth with Black
        int EColumn(bool turned) {
            return turned ? 3 : 4;
        }

    }  // namespace

    TEST_F(ChessUiTest, AClickOnADrawnSquareOfThePortraitBoardSelectsThatSquareInBothOrientations) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        for (const auto& output : kPortraitOutputs) {
            for (const bool turned : {false, true}) {
                SCOPED_TRACE(Format("output {}x{}, {}", output.first, output.second, turned ? "turned" : "White at the bottom").c_str());
                ASSERT_TRUE(ShowPortrait(output));
                ControlPlays(turned ? "white" : nullptr);
                StartFrom(turned ? kBlackToMoveAfterE4 : Position::kStartFen.data());
                ASSERT_EQ(BlackIsAtTheBottom(), turned);
                Model().ClearSelection();

                const int column = EColumn(turned);
                const auto at = MiddleOf(PortraitCell(output, column, kPawnRow));
                CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.first, at.second, true));
                ASSERT_TRUE(Model().HasSelection());
                EXPECT_EQ(Model().GetSelected(), Sq(turned ? "e7" : "e2"));

                Draw();
                const auto selected = RectCalls(kSelectedColor);
                ASSERT_EQ(selected.size(), 1u);
                EXPECT_TRUE(SameRect(selected[0]->rect, PortraitCell(output, column, kPawnRow)));
                const auto moves = RectCalls(kLegalMoveColor);
                ASSERT_EQ(moves.size(), 2u);
                for (const int row : {kPawnRow - 1, kTargetRow}) {
                    const Rect cell = PortraitCell(output, column, row);
                    EXPECT_TRUE(std::any_of(moves.begin(), moves.end(), [&](const DrawCall* call) { return SameRect(call->rect, cell); })) << "row " << row;
                }
                Release(at.first, at.second);
            }
        }
    }

    TEST_F(ChessUiTest, TheFourCornersOfThePortraitBoardAreTheSquaresTheyShowInBothOrientations) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        for (const auto& output : kPortraitOutputs) {
            for (const bool turned : {false, true}) {
                SCOPED_TRACE(Format("output {}x{}, {}", output.first, output.second, turned ? "turned" : "White at the bottom").c_str());
                ASSERT_TRUE(ShowPortrait(output));
                ControlPlays(turned ? "white" : nullptr);
                StartFrom(kFourRooks);
                ASSERT_EQ(BlackIsAtTheBottom(), turned);

                // By their place on the screen; a rook of Black stands on each corner square
                const struct {
                    int column;
                    int row;
                    const char* square;
                } corners[] = {{0, 7, turned ? "h8" : "a1"}, {7, 7, turned ? "a8" : "h1"}, {0, 0, turned ? "h1" : "a8"}, {7, 0, turned ? "a1" : "h8"}};
                for (const auto& corner : corners) {
                    SCOPED_TRACE(corner.square);
                    Model().ClearSelection();
                    const auto at = MiddleOf(PortraitCell(output, corner.column, corner.row));
                    CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.first, at.second, true));
                    ASSERT_TRUE(Model().HasSelection());
                    EXPECT_EQ(Model().GetSelected(), Sq(corner.square));
                    EXPECT_TRUE(SameRect(SquareRect(corner.square), PortraitCell(output, corner.column, corner.row)));
                    Release(at.first, at.second);
                }
            }
        }
    }

    TEST_F(ChessUiTest, AClickInABarOutsideThePortraitViewSelectsNothingAndAClickInsideItDoes) {
        // 480 x 720: the view is 480 x 640 pixels, bars of 40 above and below; 1000 x 1200: the view is 900 wide, bars of 50 at the sides
        StartFrom(kFourRooks);
        const struct {
            Pair<float, float> output;
            Pair<float, float> inBar;
        } shapes[] = {{{480.0f, 720.0f}, {240.0f, 20.0f}}, {{480.0f, 720.0f}, {240.0f, 700.0f}}, {{1000.0f, 1200.0f}, {25.0f, 600.0f}}, {{1000.0f, 1200.0f}, {975.0f, 600.0f}}};
        for (const auto& shape : shapes) {
            SCOPED_TRACE(Format("output {}x{}, click ({}, {})", shape.output.first, shape.output.second, shape.inBar.first, shape.inBar.second).c_str());
            ASSERT_TRUE(ShowPortrait(shape.output));
            Model().ClearSelection();
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(shape.inBar.first, shape.inBar.second, true));
            Release(shape.inBar.first, shape.inBar.second);
            EXPECT_FALSE(Model().HasSelection()) << "a bar is not the board";

            const auto a1 = MiddleOf(PortraitCell(shape.output, 0, 7));
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(a1.first, a1.second, true));
            Release(a1.first, a1.second);
            EXPECT_TRUE(Model().HasSelection()) << "the same board takes a click on a1";
        }
    }

    TEST_F(ChessUiTest, TheDimAndTheResultBarCoverThePortraitBoardInBothOrientations) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        for (const auto& output : {kPortraitOutputs[0], kPortraitOutputs[2]}) {
            for (const bool turned : {false, true}) {
                SCOPED_TRACE(Format("output {}x{}, {}", output.first, output.second, turned ? "turned" : "White at the bottom").c_str());
                ASSERT_TRUE(ShowPortrait(output));
                ControlPlays(turned ? "white" : nullptr);
                StartFrom(kBlackMatesOnA1);
                ASSERT_EQ(BlackIsAtTheBottom(), turned);

                ClickMove("a8", "a1");
                ASSERT_TRUE(Model().IsGameOver());
                Draw();

                const ViewTransform fit = ViewTransform::Fit(kPortraitViewWidth, UprightViewHeight(output), output.first, output.second);
                const auto dim = RectCalls(kBoardDimColor);
                ASSERT_EQ(dim.size(), 1u);
                EXPECT_TRUE(SameRect(dim[0]->rect, fit.ToOutput(kPortraitBoardLeft, kPortraitBoardTop, kBoardUnits, kBoardUnits))) << "the whole board";
                const auto bar = RectCalls(kResultBarColor);
                ASSERT_EQ(bar.size(), 1u);
                const float middle = kPortraitBoardTop + (kBoardUnits - kCellSize) * 0.5f;
                EXPECT_TRUE(SameRect(bar[0]->rect, fit.ToOutput(kPortraitBoardLeft, middle, kBoardUnits, kCellSize))) << "one square high, across the middle of the board";
            }
        }
    }

    TEST_F(ChessUiTest, TheMarksOfThePortraitBoardSitAroundTheirPiecesInBothOrientations) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        const auto output = kPortraitOutputs[0];
        for (const bool turned : {false, true}) {
            SCOPED_TRACE(turned ? "turned" : "White at the bottom");
            ASSERT_TRUE(ShowPortrait(output));
            ControlPlays(turned ? "white" : nullptr);
            StartFrom(turned ? kBlackToMoveAfterE4 : Position::kStartFen.data());
            const auto texture = GiveAtlasCell(turned ? "BlackPawn_e7" : "WhitePawn_e2");
            _manager->UpdateAll();

            const int column = EColumn(turned);
            const auto from = MiddleOf(PortraitCell(output, column, kPawnRow));
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(from.first, from.second, true));
            Release(from.first, from.second);
            ASSERT_TRUE(Model().HasSelection());
            Draw();
            const DrawCall* piece = TextureCall(*texture);
            ASSERT_NE(piece, nullptr);
            const auto selected = RectCalls(kSelectedColor);
            ASSERT_EQ(selected.size(), 1u);
            EXPECT_TRUE(SameRect(selected[0]->rect, PortraitCell(output, column, kPawnRow)));
            EXPECT_TRUE(IsCenteredIn(piece->rect, selected[0]->rect)) << "the piece is inside its selected square";

            // The move marks the two squares it joins, and the pawn stands on the second
            const auto to = MiddleOf(PortraitCell(output, column, kTargetRow));
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(to.first, to.second, true));
            Release(to.first, to.second);
            ASSERT_EQ(Model().GetMoves().size(), 1u);
            _manager->UpdateAll();
            Draw();
            const auto marks = LastMoveFills();
            ASSERT_EQ(marks.size(), 2u);
            EXPECT_TRUE(SameRect(marks[0]->rect, PortraitCell(output, column, kPawnRow)));
            EXPECT_TRUE(SameRect(marks[1]->rect, PortraitCell(output, column, kTargetRow)));
            const DrawCall* moved = TextureCall(*texture);
            ASSERT_NE(moved, nullptr);
            EXPECT_TRUE(IsCenteredIn(moved->rect, marks[1]->rect)) << "the pawn stands on the square the move went to";
        }
    }

    TEST_F(ChessUiTest, ADragOnThePortraitBoardLiftsThePieceUnderThePointerAndAReleaseOnATargetPlaysTheMove) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        for (const auto& output : kPortraitOutputs) {
            for (const bool turned : {false, true}) {
                SCOPED_TRACE(Format("output {}x{}, {}", output.first, output.second, turned ? "turned" : "White at the bottom").c_str());
                ASSERT_TRUE(ShowPortrait(output));
                ControlPlays(turned ? "white" : nullptr);
                StartFrom(turned ? kBlackToMoveAfterE4 : Position::kStartFen.data());
                const int column = EColumn(turned);
                const char* node = turned ? "BlackPawn_e7" : "WhitePawn_e2";
                const auto from = MiddleOf(PortraitCell(output, column, kPawnRow));
                const auto to = MiddleOf(PortraitCell(output, column, kTargetRow));

                // In the hand: the piece is a square centered on the pointer, in the units of the Portrait view
                CoreManager::GetUIInput().DispatchMouseButtonDown(Button(from.first, from.second, true));
                const float pointerX = from.first + kPointerOffsetX;
                const float pointerY = from.second + kPointerOffsetY;
                MouseMove(pointerX, pointerY, true);
                _manager->UpdateAll();
                ASSERT_TRUE(Lifted().has_value());
                EXPECT_NEAR(PieceOrigin(node).first, GetScene().ScreenToSceneX(pointerX) - kHalfSquare, kPixelTolerance);
                EXPECT_NEAR(PieceOrigin(node).second, GetScene().ScreenToSceneY(pointerY) - kHalfSquare, kPixelTolerance);
                EXPECT_EQ(NodeLayer(node), kDragLayer);

                // Let go on a square that is no target: nothing is played, the piece goes back to its cell and stays selected
                const auto off = MiddleOf(PortraitCell(output, turned ? 7 : 0, 0));
                MouseMove(off.first, off.second, true);
                Release(off.first, off.second);
                _manager->UpdateAll();
                EXPECT_EQ(Model().GetMoves().size(), 0u);
                EXPECT_TRUE(Model().HasSelection());
                ExpectPiecesAtTheCellsOf(turned);

                // And the drag to the target plays it
                CoreManager::GetUIInput().DispatchMouseButtonDown(Button(from.first, from.second, true));
                MouseMove(from.first + kDragStepPixels, from.second, true);
                MouseMove(to.first, to.second, true);
                Release(to.first, to.second);
                ASSERT_EQ(Model().GetMoves().size(), 1u);
                EXPECT_EQ(Model().GetMoves().back().ToUci(), turned ? "e7e5" : "e2e4");
                _manager->UpdateAll();
                EXPECT_EQ(NodeLayer(node), kPieceLayer);
                ExpectPiecesAtTheCellsOf(turned);
            }
        }
    }

    TEST_F(ChessUiTest, AWindowThatTurnsToPortraitInTheMiddleOfADragEndsTheDragWithoutAMove) {
        DrawAt(1280.0f, 720.0f);
        ASSERT_FALSE(PortraitIsActive());
        _manager->UpdateAll();
        const auto from = SquareCenter("e2");
        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(from.first, from.second, true));
        MouseMove(from.first + kDragStepPixels, from.second, true);
        _manager->UpdateAll();
        ASSERT_TRUE(Lifted().has_value());
        ASSERT_EQ(Model().GetSelected(), Sq("e2"));

        // The window is turned upright while the button is held: the scene units of the drag are those of the other view
        ASSERT_TRUE(ShowPortrait(kPortraitOutputs[0]));
        EXPECT_FALSE(Lifted().has_value());
        const auto target = MiddleOf(PortraitCell(kPortraitOutputs[0], 4, kTargetRow));
        MouseMove(target.first, target.second, true);
        Release(target.first, target.second);
        _manager->UpdateAll();

        EXPECT_TRUE(Model().GetMoves().empty()) << "e2e4 was not played";
        EXPECT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("e2"));
        EXPECT_EQ(NodeLayer("WhitePawn_e2"), kPieceLayer);
        ExpectPiecesAtTheCellsOf(false);
        EXPECT_FALSE(Lifted().has_value());
    }

}  // namespace BECore::Chess::Tests
