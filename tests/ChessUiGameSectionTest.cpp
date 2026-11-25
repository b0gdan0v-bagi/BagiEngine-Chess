#include <BECore/UI/UIEnvironment.h>
#include <Chess/Components/ChessGameSectionComponent.h>
#include <tests/ChessUiHarness.h>

/// Resigning and agreeing a draw as the person does it: ChessGame::OfferDraw() driven through real frames, and what the HUD says of the judge's answer; the menu section "Game"
/// (config/nodes/ChessGameSection.xml) that holds the buttons. The scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        constexpr int kEvenScore = 0;
        constexpr int kProgramIsClearlyBetter = 300;  // centipawns for the program: far over what it accepts a draw at

        AnalysisScore Cp(int value) {
            return AnalysisScore{AnalysisScoreKind::Centipawns, value};
        }

        constexpr const char* kSectionNode = "GameSection";
        constexpr const char* kSectionPlate = "GameSectionPlate";

        /// One state of the canvas: the section's plate with no question or with one, or the HUD's question alone on the plate GameSectionConfirm
        struct GameState {
            GameQuestion question = GameQuestion::None;
            bool fromHud = false;
        };

        constexpr GameState kGameStates[] = {{GameQuestion::None, false}, {GameQuestion::Resign, false}, {GameQuestion::Draw, false}, {GameQuestion::Resign, true}};

        String Describe(const GameState& state) {
            return Format("{}{}", EnumUtils<GameQuestion>::ToString(state.question), state.fromHud ? " from the HUD" : "");
        }

        /// Shows what the component shows in the state: Resign and Draw, the question line and Yes / No, or the HUD's plate (the layout holds a hidden element at no height; every
        /// control follows its row or plate here, or PanelProblems() would take its empty rect for a missing height)
        void ShowGameState(UICanvasComponent& canvas, const GameState& state) {
            const bool confirmOnly = state.question != GameQuestion::None && state.fromHud;
            const bool asking = state.question != GameQuestion::None && !confirmOnly;
            CoreManager::GetUIVars().Set("gameQuestion"_intern, StringView(state.question == GameQuestion::Draw ? StatusText::DrawQuestion() : StatusText::ResignQuestion()));
            for (const char* name : {"GameSectionPlate", "GameSectionTitle", "GameSectionButtons", "GameSectionBack"}) {
                EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern(name), !confirmOnly)) << name;
            }
            for (const char* name : {"GameSectionQuestion", "GameSectionAnswers", "GameSectionYes", "GameSectionNo"}) {
                EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern(name), asking)) << name;
            }
            for (const char* name : {"GameSectionResign", "GameSectionDraw"}) {
                EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern(name), !asking && !confirmOnly)) << name;
            }
            for (const char* name : {"GameSectionConfirm", "GameSectionConfirmQuestion", "GameSectionConfirmAnswers", "GameSectionConfirmYes", "GameSectionConfirmNo"}) {
                EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern(name), confirmOnly)) << name;
            }
        }

        /// The plate a state shows
        const char* PlateOf(const GameState& state) {
            return state.fromHud ? "GameSectionConfirm" : kSectionPlate;
        }

    }  // namespace

    TEST_F(ChessUiTest, TheGameSectionPassesTheSizeMatrixWithEveryWord) {
        Draw();
        auto canvas = CanvasOf(kSectionNode);
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update

        UICanvasLayout& layout = canvas->GetLayout();
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        String report;
        size_t reported = 0;
        for (const GameState& state : kGameStates) {
            ShowGameState(*canvas, state);
            const auto failures = ValidateOnMatrix(layout, options, ViewContext(), SceneViews());
            if (!failures.empty() && reported++ < kReportedCases) {
                report += Format("{}: {}", Describe(state).c_str(), DescribeMatrixFailures(failures).c_str());
            }
            // The validator does not see a Clip label's cut, nor a plate too low for what it holds
            OnEveryCaseOfTheMatrix(
                layout,
                [&](const String& label, const UICanvasLayout& laidOut) {
                    const String problems = PanelProblems(laidOut, PlateOf(state));
                    if (!problems.empty() && reported++ < kReportedCases) {
                        report += Format("{}, {}:\n{}", Describe(state).c_str(), label.c_str(), problems.c_str());
                    }
                },
                true);
        }
        EXPECT_TRUE(report.empty()) << report.c_str();
    }

    TEST_F(ChessUiTest, TwoPeopleAgreeADrawAtOnce) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        const uint64_t version = Model().GetVersion();

        ASSERT_TRUE(ChessGame::GetActive()->OfferDraw().has_value());
        EXPECT_EQ(Model().GetStatus(), GameStatus::DrawAgreed);
        EXPECT_EQ(Model().GetVersion(), version + 1);
        EXPECT_FALSE(Model().GetWinner().has_value());
        EXPECT_EQ(Var("turn"), "Draw");
        EXPECT_EQ(Var("status"), "Draw agreed");
        EXPECT_EQ(ChessGame::GetActive()->OfferDraw().error(), DrawOfferRefusal::GameOver);
    }

    TEST_F(ChessUiTest, AnOfferTheProgramAcceptsEndsTheGameAsAnAgreedDrawAndTheProgramThinksWhileItJudges) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory opponentFactory({New<InstantEngine>()});
        Opponent().SetEngineFactory(opponentFactory.AsFactory());
        const auto judge = New<AnalysingEngine>(Cp(kEvenScore));
        judge->Gate(true);
        CountingAnalysisFactory analysisFactory({judge});
        Analyst().SetEngineFactory(analysisFactory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());

        ASSERT_TRUE(ChessGame::GetActive()->OfferDraw().has_value());
        EXPECT_EQ(Var("turn"), "Computer is thinking") << "the judge is a search of the program, though the game has not moved";
        ASSERT_TRUE(FramesUntil([&] { return judge->Calls() == 1; }));
        EXPECT_FALSE(Model().IsGameOver()) << "the judge holds its answer";
        EXPECT_EQ(analysisFactory.Engines().size(), 1u);
        EXPECT_EQ(analysisFactory.Engines()[0], "own") << "the own engine judges, whatever the person picked";

        judge->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetStatus() == GameStatus::DrawAgreed; }));
        EXPECT_FALSE(Model().GetWinner().has_value());
        EXPECT_EQ(Var("turn"), "Draw");
        EXPECT_EQ(Var("status"), "Draw agreed");
        EXPECT_EQ(ChessGame::GetActiveDrawJudge()->GetState(Model()), DrawOfferState::Idle);
    }

    TEST_F(ChessUiTest, AnOfferTheProgramRefusesShowsDrawDeclinedUntilTheGameMoves) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory opponentFactory({New<InstantEngine>()});
        Opponent().SetEngineFactory(opponentFactory.AsFactory());
        // The engine is scripted: whatever the position, it says the side to move (White, the person) is that much worse off
        CountingAnalysisFactory analysisFactory({New<AnalysingEngine>(Cp(-kProgramIsClearlyBetter))});
        Analyst().SetEngineFactory(analysisFactory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());

        ASSERT_TRUE(ChessGame::GetActive()->OfferDraw().has_value());
        ASSERT_TRUE(FramesUntil([&] { return ChessGame::GetActiveDrawJudge()->GetState(Model()) == DrawOfferState::Declined; }));
        EXPECT_FALSE(Model().IsGameOver());
        EXPECT_EQ(Var("status"), "Draw declined");
        EXPECT_EQ(Var("turn"), "White to move") << "the program is not thinking any more";
        IdleFrames();
        EXPECT_EQ(Var("status"), "Draw declined") << "it stays until the game moves";

        ASSERT_TRUE(ChessGame::GetActive()->OfferDraw().has_value());
        EXPECT_EQ(analysisFactory.Calls(), 1) << "the same position is not judged twice";

        ASSERT_TRUE(Play(Model(), "e2e4"));
        EXPECT_NE(Var("status"), "Draw declined") << "a move clears it";
        EXPECT_EQ(ChessGame::GetActiveDrawJudge()->GetState(Model()), DrawOfferState::Idle);
    }

    TEST_F(ChessUiTest, ADeclinedOfferIsShownOverCheck) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory opponentFactory({New<InstantEngine>()});
        Opponent().SetEngineFactory(opponentFactory.AsFactory());
        CountingAnalysisFactory analysisFactory({New<AnalysingEngine>(Cp(-kProgramIsClearlyBetter))});
        Analyst().SetEngineFactory(analysisFactory.AsFactory());
        _manager->UpdateAll();
        // White, the person, is in check and to move
        ASSERT_TRUE(FenSetsTheState("4k3/8/8/8/8/8/4r3/4K3 w - - 0 1"));
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ASSERT_EQ(Var("status"), "Check");
        ASSERT_TRUE(ChessGame::GetActive()->OfferDraw().has_value());
        ASSERT_TRUE(FramesUntil([&] { return ChessGame::GetActiveDrawJudge()->GetState(Model()) == DrawOfferState::Declined; }));
        EXPECT_EQ(Var("status"), "Draw declined");
    }

    TEST_F(ChessUiTest, TheOfferIsRefusedOffTheTurnOfThePersonAndForTwoPrograms) {
        ChessGame& game = *ChessGame::GetActive();
        ControlPlays("white");
        ASSERT_EQ(game.OfferDraw().error(), DrawOfferRefusal::NotYourTurn) << "the program is to move";
        ControlPlays("both");
        EXPECT_EQ(game.OfferDraw().error(), DrawOfferRefusal::NoPerson);
        EXPECT_FALSE(Model().IsGameOver());
        EXPECT_EQ(game.GetDrawJudge().GetState(Model()), DrawOfferState::Idle);
    }

    // ---- the section: its canvas ----------------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, TheGameSectionIsAHiddenModalCanvasOfTheMenuWithItsComponentFirstAndNamesOfItsOwn) {
        auto canvas = GameSection();
        ASSERT_TRUE(canvas);
        EXPECT_FALSE(canvas->_visible);
        EXPECT_TRUE(canvas->_modal);
        EXPECT_EQ(canvas->_space, CanvasSpace::SceneView);
        EXPECT_TRUE(canvas->_useSafeArea);

        const SceneNode* node = nullptr;
        for (const auto& each : GetScene().GetRootNode().GetChildren()) {
            if (each->GetName() == PoolString::Intern(kSectionNode)) {
                node = each.Get();
            }
        }
        ASSERT_NE(node, nullptr);
        EXPECT_EQ(node->GetLayer(), 20) << "the layer of the menu and its sections, over the HUD";
        // The component comes before the canvas: its question and greyed looks are written in the frame the section is shown
        const auto& components = node->GetComponents();
        const auto behind = node->GetComponent<ChessGameSectionComponent>();
        ASSERT_TRUE(behind);
        ASSERT_EQ(components.size(), 2u);
        EXPECT_EQ(components[0].Get(), static_cast<IComponent*>(behind.Get()));
        EXPECT_EQ(components[1].Get(), static_cast<IComponent*>(canvas.Get()));

        Vector<PoolString> names;
        CollectNames(canvas->GetRoot(), names);
        ASSERT_FALSE(names.empty());
        for (size_t i = 0; i < names.size(); ++i) {
            EXPECT_TRUE(names[i].ToStringView().starts_with(kSectionNode)) << names[i].CStr() << ": elements are found by name in every canvas";
            EXPECT_EQ(eastl::count(names.begin(), names.end(), names[i]), 1) << names[i].CStr() << " is twice in the section";
        }
        for (const char* other : {"Hud", "BoardCoordinates", "Material", "MovePanel", "MenuPanel", "OpponentPanel", "MovesSection", "PromotionPanel"}) {
            const auto otherCanvas = CanvasOf(other);
            ASSERT_TRUE(otherCanvas) << other;
            for (const PoolString& name : names) {
                EXPECT_EQ(otherCanvas->GetRoot().FindByName(name), nullptr) << name.CStr() << " is also in " << other;
            }
        }
        const struct {
            const char* name;
            const char* action;
        } buttons[] = {{"GameSectionResign", "ChessResign"}, {"GameSectionDraw", "ChessDraw"},     {"GameSectionYes", "ChessGameYes"},
                       {"GameSectionNo", "ChessGameNo"},     {"GameSectionBack", "ChessOpenMenu"}, {"GameSectionDim", "ChessCloseMenu"}};
        for (const auto& [name, action] : buttons) {
            EXPECT_EQ(ClickerOf(ElementOf(kSectionNode, name))->action, PoolString::Intern(action)) << name;
        }
        EXPECT_EQ(canvas->GetRoot().children.front()->name, "GameSectionDim"_intern) << "the first child, so a click on the plate never reaches it";
        EXPECT_EQ(canvas->GetRoot().FindByName("GameSectionClose"_intern), nullptr) << "Back is the one button: the dim closes all";
    }

    TEST_F(ChessUiTest, NoWordOfTheGameSectionIsCutInTheNarrowestWindowOfEachViewAndEveryButtonIsATouchTarget) {
        Draw();
        auto canvas = CanvasOf(kSectionNode);
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = canvas->GetLayout();
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;

        for (const GameState& state : kGameStates) {
            ShowGameState(*canvas, state);
            for (const ViewShape& shape : kViewShapes) {
                SCOPED_TRACE(Format("{}, {}", Describe(state).c_str(), shape.name).c_str());
                LayOutInTheNarrowestWindow(layout, shape);
                const float scale = layout.GetResolved().scale;
                EXPECT_NEAR(scale, shape.NarrowestScale(), 0.01f);
                const String problems = PanelProblems(layout, PlateOf(state));
                EXPECT_TRUE(problems.empty()) << problems.c_str();

                // Three buttons are on the screen in every state of the section (Resign and Draw or Yes and No, and Back), only Yes and No for the HUD's question
                size_t onScreen = 0;
                for (const char* name : {"GameSectionResign", "GameSectionDraw", "GameSectionYes", "GameSectionNo", "GameSectionBack", "GameSectionConfirmYes", "GameSectionConfirmNo"}) {
                    const UIElement& button = ElementOf(kSectionNode, name);
                    if (button.rect.IsEmpty()) {
                        continue;
                    }
                    ++onScreen;
                    EXPECT_NEAR(button.rect.h, kSegmentHeight, kPixelTolerance) << name;
                    EXPECT_GE(std::min(button.rect.w, button.rect.h) * scale, minTouch - kPixelTolerance) << name << " is a touch target on the smallest window";
                    const UIContentFit fit = button.CheckContent(button.rect.w - 2.0f * kButtonAir, button.rect.h, scale);
                    EXPECT_TRUE(fit.hasContent && fit.fits) << name << " '" << ShownText(button).c_str() << "' in " << button.rect.w << " units";
                }
                EXPECT_EQ(onScreen, state.fromHud ? 2u : 3u);
            }
        }
        layout.Invalidate();
    }

    // ---- the section: clicks through the real scene ---------------------------------------------------------------------------------

    TEST_F(ChessUiTest, TheMenuOpensTheGameSectionAndBackAndTheDimLeaveIt) {
        auto section = GameSection();
        ASSERT_TRUE(section);
        for (const char* way : {"GameSectionBack", "GameSectionDim"}) {
            SCOPED_TRACE(way);
            ASSERT_TRUE(OpenTheGameSection());
            EXPECT_FALSE(MenuList()->_visible);
            EXPECT_FALSE(Panel()->_visible);
            EXPECT_FALSE(CanvasOf("MovesSection")->_visible);

            const bool dim = StringView(way) == "GameSectionDim";
            const auto corner = DimCorner(kSectionNode, way);
            ASSERT_TRUE(dim ? ClickAtPixel(corner.first, corner.second).Both() : ClickGameSection(way).Both());
            EXPECT_FALSE(section->_visible);
            EXPECT_EQ(MenuList()->_visible, StringView(way) == "GameSectionBack") << "Back returns to the list, the dim closes all";
            if (MenuList()->_visible) {
                ASSERT_TRUE(ClickMenuList("MenuClose").Both());
            }
        }
    }

    TEST_F(ChessUiTest, ResigningAsksFirstAndYesEndsTheGameForThePersonAndClosesTheMenu) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(OpenTheGameSection());
        Draw();
        EXPECT_TRUE(ElementOf(kSectionNode, "GameSectionResign").active.visible);
        EXPECT_TRUE(ElementOf(kSectionNode, "GameSectionDraw").active.visible);
        EXPECT_FALSE(ElementOf(kSectionNode, "GameSectionQuestion").active.visible) << "the section starts with the two buttons, not the question";

        ASSERT_TRUE(ClickGameSection("GameSectionResign").Both());
        Draw();
        EXPECT_FALSE(Model().IsGameOver()) << "Resign only asks";
        EXPECT_EQ(Var("gameQuestion"), "Resign?");
        EXPECT_EQ(ShownText(ElementOf(kSectionNode, "GameSectionQuestion")), "Resign?");
        EXPECT_TRUE(ElementOf(kSectionNode, "GameSectionQuestion").active.visible);
        EXPECT_TRUE(ElementOf(kSectionNode, "GameSectionAnswers").active.visible);
        EXPECT_FALSE(ElementOf(kSectionNode, "GameSectionResign").active.visible);
        EXPECT_FALSE(ElementOf(kSectionNode, "GameSectionDraw").active.visible);

        ASSERT_TRUE(ClickGameSection("GameSectionYes").Both());
        EXPECT_EQ(Model().GetStatus(), GameStatus::Resigned);
        EXPECT_EQ(Model().GetResigned(), PieceColor::White) << "against the program the person's color";
        EXPECT_EQ(Model().GetWinner(), PieceColor::Black);
        EXPECT_FALSE(GameSection()->_visible);
        EXPECT_FALSE(MenuList()->_visible);
        EXPECT_EQ(Var("turn"), "Black wins");
        EXPECT_EQ(Var("status"), "White resigned");
    }

    TEST_F(ChessUiTest, ThePersonResignsForTheirOwnColorAlsoWhileTheProgramIsToMove) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ControlPlays("white");  // the program is White and to move: the person is Black
        _manager->UpdateAll();
        ASSERT_TRUE(OpenTheGameSection());
        ASSERT_TRUE(ClickGameSection("GameSectionResign").Both());
        ASSERT_TRUE(ClickGameSection("GameSectionYes").Both());
        EXPECT_EQ(Model().GetStatus(), GameStatus::Resigned);
        EXPECT_EQ(Model().GetResigned(), PieceColor::Black);
        EXPECT_EQ(Model().GetWinner(), PieceColor::White);
    }

    TEST_F(ChessUiTest, WithTwoPeopleTheSideToMoveResigns) {
        ASSERT_TRUE(OpenTheGameSection());
        ASSERT_TRUE(ClickGameSection("GameSectionResign").Both());
        ASSERT_TRUE(ClickGameSection("GameSectionYes").Both());
        EXPECT_EQ(Model().GetResigned(), PieceColor::White);

        Model().Reset();
        ASSERT_TRUE(Play(Model(), "e2e4"));
        ASSERT_TRUE(OpenTheGameSection());
        ASSERT_TRUE(ClickGameSection("GameSectionResign").Both());
        ASSERT_TRUE(ClickGameSection("GameSectionYes").Both());
        EXPECT_EQ(Model().GetResigned(), PieceColor::Black);
        EXPECT_EQ(Model().GetWinner(), PieceColor::White);
        EXPECT_FALSE(MenuList()->_visible);
    }

    TEST_F(ChessUiTest, NoKeepsTheGameAndAnOpenQuestionIsGoneWhenTheMenuIsClosedAndOpenedAgain) {
        ASSERT_TRUE(OpenTheGameSection());
        ASSERT_TRUE(ClickGameSection("GameSectionResign").Both());
        ASSERT_TRUE(ClickGameSection("GameSectionNo").Both());
        Draw();
        EXPECT_FALSE(Model().IsGameOver());
        EXPECT_TRUE(GameSection()->_visible) << "No stays in the section";
        EXPECT_TRUE(ElementOf(kSectionNode, "GameSectionResign").active.visible);
        EXPECT_TRUE(ElementOf(kSectionNode, "GameSectionDraw").active.visible);
        EXPECT_FALSE(ElementOf(kSectionNode, "GameSectionQuestion").active.visible);

        // Every other way out drops the question as well: the dim and Back
        for (const char* way : {"GameSectionDim", "GameSectionBack"}) {
            SCOPED_TRACE(way);
            ASSERT_TRUE(ClickGameSection("GameSectionResign").Both());
            Draw();
            ASSERT_TRUE(ElementOf(kSectionNode, "GameSectionQuestion").active.visible);
            if (StringView(way) == "GameSectionDim") {
                const auto corner = DimCorner(kSectionNode, way);
                ASSERT_TRUE(ClickAtPixel(corner.first, corner.second).Both());
            } else {
                ASSERT_TRUE(ClickGameSection(way).Both());
            }
            if (MenuList()->_visible) {
                ASSERT_TRUE(ClickMenuList("MenuSectionGame").Both());
            } else {
                ASSERT_TRUE(OpenTheGameSection());
            }
            Draw();
            EXPECT_TRUE(ElementOf(kSectionNode, "GameSectionResign").active.visible) << "the two buttons, not the question";
            EXPECT_FALSE(ElementOf(kSectionNode, "GameSectionQuestion").active.visible);
            EXPECT_FALSE(Model().IsGameOver());
        }

        Press("ChessGameYes");
        EXPECT_FALSE(Model().IsGameOver()) << "a Yes with no question open ends nothing";
    }

    TEST_F(ChessUiTest, AMoveOfTheProgramWhileTheQuestionIsOpenDismissesIt) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(Play(Model(), "e2e4"));
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        ASSERT_TRUE(OpenTheGameSection());
        ASSERT_TRUE(ClickGameSection("GameSectionResign").Both());
        Draw();
        ASSERT_TRUE(ElementOf(kSectionNode, "GameSectionQuestion").active.visible);

        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the program answered";
        Draw();
        EXPECT_FALSE(ElementOf(kSectionNode, "GameSectionQuestion").active.visible) << "the position it asked about is gone";
        EXPECT_TRUE(ElementOf(kSectionNode, "GameSectionResign").active.visible);
        EXPECT_FALSE(Model().IsGameOver());
        EXPECT_TRUE(GameSection()->_visible) << "the section stays open";
    }

    TEST_F(ChessUiTest, WithTwoPeopleDrawAsksAndYesAgreesAndNoDoesNot) {
        ASSERT_TRUE(Play(Model(), "e2e4"));
        ASSERT_TRUE(OpenTheGameSection());
        ASSERT_TRUE(ClickGameSection("GameSectionDraw").Both());
        Draw();
        EXPECT_FALSE(Model().IsGameOver());
        EXPECT_EQ(Var("gameQuestion"), "Agree a draw?");
        EXPECT_TRUE(ElementOf(kSectionNode, "GameSectionQuestion").active.visible);

        ASSERT_TRUE(ClickGameSection("GameSectionNo").Both());
        Draw();
        EXPECT_FALSE(Model().IsGameOver());
        EXPECT_TRUE(ElementOf(kSectionNode, "GameSectionDraw").active.visible);

        ASSERT_TRUE(ClickGameSection("GameSectionDraw").Both());
        ASSERT_TRUE(ClickGameSection("GameSectionYes").Both());
        EXPECT_EQ(Model().GetStatus(), GameStatus::DrawAgreed);
        EXPECT_FALSE(Model().GetWinner().has_value());
        EXPECT_FALSE(GameSection()->_visible);
        EXPECT_FALSE(MenuList()->_visible);
        EXPECT_EQ(Var("status"), "Draw agreed");
    }

    TEST_F(ChessUiTest, DrawAgainstTheProgramIsOfferedAtOnceAndTheMenuClosesForTheAnswerOfTheHud) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory opponentFactory({New<InstantEngine>()});
        Opponent().SetEngineFactory(opponentFactory.AsFactory());
        CountingAnalysisFactory analysisFactory({New<AnalysingEngine>(Cp(kEvenScore))});
        Analyst().SetEngineFactory(analysisFactory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(OpenTheGameSection());
        ASSERT_TRUE(ClickGameSection("GameSectionDraw").Both());
        EXPECT_FALSE(GameSection()->_visible) << "no question: the person's offer needs no confirmation";
        EXPECT_FALSE(MenuList()->_visible);
        ASSERT_TRUE(FramesUntil([&] { return Model().GetStatus() == GameStatus::DrawAgreed; }));
        EXPECT_EQ(Var("status"), "Draw agreed");
    }

    // ---- the section: the greyed states -------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, ResignAndDrawAreGreyedWhenTheGameCannotTakeThem) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true), New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenTheGameSection());  // a canvas that was never shown has no layout state to read
        const auto greyed = [&](const char* name) {
            Draw();
            return ClickerOf(ElementOf(kSectionNode, name))->disabled;
        };

        EXPECT_FALSE(greyed("GameSectionResign")) << "two people";
        EXPECT_FALSE(greyed("GameSectionDraw"));

        ControlPlays("white");
        EXPECT_FALSE(greyed("GameSectionResign")) << "the person resigns on the program's turn too";
        EXPECT_TRUE(greyed("GameSectionDraw")) << "the offer is made on the person's turn";
        Press("ChessDraw");
        EXPECT_FALSE(Model().IsGameOver());
        EXPECT_EQ(ChessGame::GetActiveDrawJudge()->GetState(Model()), DrawOfferState::Idle) << "a press older than the grey look asks the rule again";

        ControlPlays("black");
        EXPECT_FALSE(greyed("GameSectionResign"));
        EXPECT_FALSE(greyed("GameSectionDraw")) << "White, the person, is to move";

        ControlPlays("both");
        EXPECT_TRUE(greyed("GameSectionResign")) << "two programs have no person to resign";
        EXPECT_TRUE(greyed("GameSectionDraw"));
        Press("ChessResign");
        Draw();
        EXPECT_FALSE(ElementOf(kSectionNode, "GameSectionQuestion").active.visible);

        ControlPlays(nullptr);
        ASSERT_TRUE(Model().Resign(PieceColor::White).has_value());
        EXPECT_TRUE(greyed("GameSectionResign")) << "a finished game";
        EXPECT_TRUE(greyed("GameSectionDraw"));

        Model().Reset();
        EXPECT_FALSE(greyed("GameSectionResign")) << "a new game";
        EXPECT_FALSE(greyed("GameSectionDraw"));
    }

    // ---- the HUD's Resign: the question alone, one rule -----------------------------------------------------------------------

    TEST_F(ChessUiTest, TheResignButtonOfTheHudShowsOnlyTheQuestionWithYesAndNoAndYesResigns) {
        ASSERT_TRUE(Play(Model(), "e2e4"));
        UIElement* resign = HudElement("Resign");
        ASSERT_NE(resign, nullptr);
        ASSERT_TRUE(IsButton(*resign) && TextOf(*resign) != nullptr);
        EXPECT_EQ(TextOf(*resign)->text, "Resign") << "a literal: not bound to a variable";
        EXPECT_EQ(ClickerOf(*resign)->action, "ChessResign"_intern) << "the action of the section's Resign: one confirmation, one rule";

        ASSERT_TRUE(ClickControl(*resign).Both());
        Draw();
        EXPECT_FALSE(Model().IsGameOver()) << "the HUD's Resign only asks";
        EXPECT_TRUE(GameSection()->_visible);
        EXPECT_FALSE(MenuList()->_visible);
        EXPECT_EQ(ShownText(ElementOf(kSectionNode, "GameSectionConfirmQuestion")), "Resign?");
        for (const char* name : {"GameSectionConfirmQuestion", "GameSectionConfirmYes", "GameSectionConfirmNo"}) {
            EXPECT_FALSE(ElementOf(kSectionNode, name).rect.IsEmpty()) << name << " is on the screen";
        }
        for (const char* name : {"GameSectionTitle", "GameSectionResign", "GameSectionDraw", "GameSectionQuestion", "GameSectionYes", "GameSectionNo", "GameSectionBack"}) {
            EXPECT_TRUE(ElementOf(kSectionNode, name).rect.IsEmpty()) << name << ": only the question and Yes / No";
        }

        ASSERT_TRUE(ClickGameSection("GameSectionConfirmYes").Both());
        EXPECT_EQ(Model().GetStatus(), GameStatus::Resigned);
        EXPECT_EQ(Model().GetResigned(), PieceColor::Black) << "two people: the side to move";
        EXPECT_FALSE(GameSection()->_visible);
        EXPECT_FALSE(MenuList()->_visible);
        EXPECT_EQ(Var("status"), "Black resigned");
    }

    TEST_F(ChessUiTest, NoAfterTheResignButtonOfTheHudReturnsToTheGameAndTheMenuStillOpensTheWholeSection) {
        ASSERT_TRUE(ClickControl(*HudElement("Resign")).Both());
        ASSERT_TRUE(ClickGameSection("GameSectionConfirmNo").Both());
        Draw();
        EXPECT_FALSE(Model().IsGameOver());
        EXPECT_FALSE(MenuList()->_visible) << "No returns to the game, not to the menu";
        for (const MenuSection& section : MenuSections()) {
            EXPECT_FALSE(CanvasOf(String(section.node).c_str())->_visible) << String(section.node).c_str() << ": no section either";
        }
        Click("e2");
        EXPECT_TRUE(Model().HasSelection()) << "the board takes the next click";
        EXPECT_EQ(Model().GetSelected(), Sq("e2"));

        ASSERT_TRUE(OpenTheGameSection());
        Draw();
        EXPECT_FALSE(ElementOf(kSectionNode, "GameSectionPlate").rect.IsEmpty()) << "the menu's way shows the whole section";
        EXPECT_TRUE(ElementOf(kSectionNode, "GameSectionConfirm").rect.IsEmpty());
        ASSERT_TRUE(ClickGameSection("GameSectionResign").Both());
        Draw();
        EXPECT_TRUE(ElementOf(kSectionNode, "GameSectionQuestion").active.visible) << "the section's question stays in the section";
        EXPECT_FALSE(ElementOf(kSectionNode, "GameSectionBack").rect.IsEmpty());
        ASSERT_TRUE(ClickGameSection("GameSectionNo").Both());
        EXPECT_TRUE(GameSection()->_visible) << "No of the section stays in the section";
    }

    TEST_F(ChessUiTest, AMoveOfTheProgramWhileTheQuestionOfTheHudIsOpenReturnsToTheGame) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(Play(Model(), "e2e4"));
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        ASSERT_TRUE(ClickControl(*HudElement("Resign")).Both());
        ASSERT_TRUE(GameSection()->_visible);

        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the program answered";
        Draw();
        EXPECT_FALSE(GameSection()->_visible) << "the position it asked about is gone, and the section was never opened";
        EXPECT_FALSE(Model().IsGameOver());
    }

    TEST_F(ChessUiTest, TheResignButtonOfTheHudIsGreyedWhileNobodyCanResign) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<GatedEngine>(true), New<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        const auto greyed = [&] {
            Draw();
            return ClickerOf(*HudElement("Resign"))->disabled;
        };

        EXPECT_FALSE(greyed()) << "two people";
        ControlPlays("white");
        EXPECT_FALSE(greyed()) << "the person resigns on the program's turn too";

        ControlPlays("both");
        EXPECT_TRUE(greyed()) << "two programs have no person to resign";
        Press("ChessResign");
        Draw();
        EXPECT_FALSE(GameSection()->_visible) << "a press older than the grey look asks the rule again";

        ControlPlays(nullptr);
        ASSERT_TRUE(Model().Resign(PieceColor::White).has_value());
        EXPECT_TRUE(greyed()) << "a finished game";
        ClickControl(*HudElement("Resign"));
        EXPECT_FALSE(GameSection()->_visible);

        Model().Reset();
        EXPECT_FALSE(greyed()) << "a new game";
    }

    TEST_F(ChessUiTest, TheResignButtonOfTheHudIsInTheTopRowOfTheDefaultAndTheWideViewAndHiddenInPortraitAndTall) {
        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            DrawAt(shape.output.first, shape.output.second);
            const UIElement& resign = *HudElement("Resign");
            if (shape.portrait) {
                EXPECT_TRUE(resign.rect.IsEmpty()) << "no room left of Menu in the narrowest window; the menu section stays";
                continue;
            }
            ASSERT_FALSE(resign.rect.IsEmpty());
            const UIElement& menu = *HudElement("Menu");
            EXPECT_NEAR(resign.rect.w, menu.rect.w, kPixelTolerance) << "as wide as Menu";
            EXPECT_NEAR(resign.rect.h, menu.rect.h, kPixelTolerance);
            EXPECT_NEAR(resign.rect.y, menu.rect.y, kPixelTolerance) << "in the top row";
            EXPECT_LT(resign.rect.x + resign.rect.w, menu.rect.x) << "left of Menu";
            EXPECT_GT(resign.rect.x, HudElement("Turn")->rect.x + HudElement("Turn")->rect.w) << "right of Turn";
        }
    }

}  // namespace BECore::Chess::Tests
