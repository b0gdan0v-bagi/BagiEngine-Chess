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

        /// Shows what the component shows in the state: Resign and Draw, or the question line and Yes / No (the layout holds a hidden row at no height; Yes and No follow their row here, or
        /// PanelProblems() would take their empty rects for a missing height)
        void ShowGameState(UICanvasComponent& canvas, GameQuestion question) {
            const bool asking = question != GameQuestion::None;
            CoreManager::GetUIVars().Set("gameQuestion"_intern, StringView(question == GameQuestion::Resign ? StatusText::ResignQuestion() : StatusText::DrawQuestion()));
            EXPECT_TRUE(canvas.SetElementVisible("GameSectionQuestion"_intern, asking));
            EXPECT_TRUE(canvas.SetElementVisible("GameSectionAnswers"_intern, asking));
            EXPECT_TRUE(canvas.SetElementVisible("GameSectionYes"_intern, asking));
            EXPECT_TRUE(canvas.SetElementVisible("GameSectionNo"_intern, asking));
            EXPECT_TRUE(canvas.SetElementVisible("GameSectionResign"_intern, !asking));
            EXPECT_TRUE(canvas.SetElementVisible("GameSectionDraw"_intern, !asking));
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
        for (const GameQuestion question : {GameQuestion::None, GameQuestion::Resign, GameQuestion::Draw}) {
            ShowGameState(*canvas, question);
            const auto failures = ValidateOnMatrix(layout, options, ViewContext(), SceneViews());
            if (!failures.empty() && reported++ < kReportedCases) {
                report += Format("{}: {}", EnumUtils<GameQuestion>::ToString(question), DescribeMatrixFailures(failures).c_str());
            }
            // The validator does not see a Clip label's cut, nor a plate too low for what it holds
            OnEveryCaseOfTheMatrix(
                layout,
                [&](const String& label, const UICanvasLayout& laidOut) {
                    const String problems = PanelProblems(laidOut, kSectionPlate);
                    if (!problems.empty() && reported++ < kReportedCases) {
                        report += Format("{}, {}:\n{}", EnumUtils<GameQuestion>::ToString(question), label.c_str(), problems.c_str());
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

}  // namespace BECore::Chess::Tests
