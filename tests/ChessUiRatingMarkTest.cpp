#include <Chess/Game/RatingMark.h>
#include <tests/ChessUiHarness.h>

/// The mark of a brilliant move: the node RatingMark (config/nodes/ChessRatingMark.xml) and what ChessRatingMarkComponent draws. The scene, the helpers and the fixture ChessUiTest are
/// tests/ChessUiHarness.h; the analyst's engine is a double that names one best move in every position, so the move a test plays is brilliant exactly where it is that move.
namespace BECore::Chess::Tests {

    namespace {

        // The two fills of ChessRatingMarkComponent
        const Color kBadgeColor{250, 204, 51, 255};
        const Color kGlyphColor{34, 26, 10, 255};
        constexpr size_t kGlyphRects = kRatingMarkRectCount - 1;
        constexpr int kLayerOfTheMark = 1;

        ChessEnginePtr EngineThatPrefers(const char* uci) {
            return New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv(uci));
        }

        /// The parts of the mark on a square drawn as @p cell
        Array<RatingMarkRect, kRatingMarkRectCount> MarkAt(const Rect& cell) {
            return BrilliantMarkRects(cell.x, cell.y, cell.w);
        }

        void ExpectDrawn(const Vector<const DrawCall*>& calls, const RatingMarkRect& expected, const char* what) {
            ASSERT_FALSE(calls.empty()) << what;
            const Rect want{expected.x, expected.y, expected.w, expected.h};
            bool found = false;
            for (const DrawCall* call : calls) {
                found = found || SameRect(call->rect, want);
            }
            EXPECT_TRUE(found) << what << ": no fill at (" << expected.x << ", " << expected.y << ", " << expected.w << ", " << expected.h << ")";
        }

    }  // namespace

    TEST_F(ChessUiTest, NothingIsMarkedWithTheRatingsOffAndNothingIsSearchedForThem) {
        CountingAnalysisFactory factory({EngineThatPrefers("e2e4")});
        Analyst().SetEngineFactory(factory.AsFactory());
        Draw();
        EXPECT_TRUE(RectCalls(kBadgeColor).empty()) << "no move yet";
        ASSERT_TRUE(Play(Model(), "e2e4"));
        IdleFrames(5);
        Draw();
        EXPECT_FALSE(Analyst().AreRatingsOn());
        EXPECT_TRUE(RectCalls(kBadgeColor).empty());
        EXPECT_TRUE(RectCalls(kGlyphColor).empty());
        EXPECT_EQ(factory.Calls(), 0) << "the ratings are off: the engine is not even made";
    }

    TEST_F(ChessUiTest, ABrilliantMoveIsMarkedInTheTopRightCornerOfItsSquareWhileTheRatingsAreOn) {
        CountingAnalysisFactory factory({EngineThatPrefers("e2e4")});
        Analyst().SetEngineFactory(factory.AsFactory());
        Analyst().SetRatings(true);
        ASSERT_TRUE(Play(Model(), "e2e4"));
        ASSERT_TRUE(WaitForRatings());
        ASSERT_TRUE(Analyst().GetRating(Model(), 0)->IsBrilliant());
        Draw();

        const Rect square = SquareRect("e4");
        const auto expected = MarkAt(square);
        const auto badge = RectCalls(kBadgeColor);
        ASSERT_EQ(badge.size(), 1u);
        ExpectDrawn(badge, expected[0], "the badge");
        const auto glyph = RectCalls(kGlyphColor);
        ASSERT_EQ(glyph.size(), kGlyphRects);
        for (size_t i = 1; i < expected.size(); ++i) {
            ExpectDrawn(glyph, expected[i], "a stroke of the glyph");
        }
        EXPECT_TRUE(LiesIn(badge[0]->rect, square));
        EXPECT_GT(badge[0]->rect.x, square.x + square.w * 0.5f) << "the right half";
        EXPECT_LT(badge[0]->rect.y, square.y + square.h * 0.5f) << "the top half";
    }

    TEST_F(ChessUiTest, TheMarkGoesWithTheSwitchAndTheRatingsStayWhileItIsOff) {
        CountingAnalysisFactory factory({EngineThatPrefers("e2e4")});
        Analyst().SetEngineFactory(factory.AsFactory());
        Analyst().SetRatings(true);
        ASSERT_TRUE(Play(Model(), "e2e4"));
        ASSERT_TRUE(WaitForRatings());
        Draw();
        ASSERT_EQ(RectCalls(kBadgeColor).size(), 1u);

        Analyst().SetRatings(false);
        Draw();
        EXPECT_TRUE(RectCalls(kBadgeColor).empty());
        EXPECT_TRUE(RectCalls(kGlyphColor).empty());
        EXPECT_TRUE(Analyst().GetRating(Model(), 0).has_value()) << "off only hides the mark: the ratings stay";

        Analyst().SetRatings(true);
        Draw();
        EXPECT_EQ(RectCalls(kBadgeColor).size(), 1u);
    }

    TEST_F(ChessUiTest, AMoveThatIsNotTheEnginesBestIsNotMarked) {
        CountingAnalysisFactory factory({EngineThatPrefers("e2e4")});
        Analyst().SetEngineFactory(factory.AsFactory());
        Analyst().SetRatings(true);
        ASSERT_TRUE(Play(Model(), "d2d4"));
        ASSERT_TRUE(WaitForRatings());
        ASSERT_EQ(Analyst().GetRating(Model(), 0)->reason, MoveRatingReason::NotBest);
        Draw();
        EXPECT_TRUE(RectCalls(kBadgeColor).empty());
        EXPECT_TRUE(RectCalls(kGlyphColor).empty());
    }

    TEST_F(ChessUiTest, AMoveThatIsNotRatedYetIsNotMarkedAndGetsItsMarkWhenTheRatingArrives) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"));
        engine->Gate(true);
        CountingAnalysisFactory factory({engine});
        Analyst().SetEngineFactory(factory.AsFactory());
        Analyst().SetRatings(true);
        ASSERT_TRUE(Play(Model(), "e2e4"));
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; })) << "the search for the move is under way and held";
        Draw();
        EXPECT_FALSE(Analyst().GetRating(Model(), 0).has_value());
        EXPECT_TRUE(RectCalls(kBadgeColor).empty());

        engine->Open();
        ASSERT_TRUE(WaitForRatings());
        Draw();
        EXPECT_EQ(RectCalls(kBadgeColor).size(), 1u);
    }

    TEST_F(ChessUiTest, TheMarkIsAtTheTurnedSquareWhenTheProgramPlaysWhite) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory opponentFactory({New<GatedEngine>(true)});  // the program opens and holds its answer: the board only turns
        Opponent().SetEngineFactory(opponentFactory.AsFactory());
        CountingAnalysisFactory factory({EngineThatPrefers("e2e4")});
        Analyst().SetEngineFactory(factory.AsFactory());
        Analyst().SetRatings(true);
        _manager->UpdateAll();
        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        ASSERT_TRUE(Play(Model(), "e2e4"));
        ASSERT_TRUE(WaitForRatings());
        Draw();

        const Rect square = SquareRect("e4");
        const auto badge = RectCalls(kBadgeColor);
        ASSERT_EQ(badge.size(), 1u);
        ExpectDrawn(badge, MarkAt(square)[0], "the badge on the turned board");
        EXPECT_TRUE(LiesIn(badge[0]->rect, square));
        EXPECT_FALSE(LiesIn(badge[0]->rect, CellRect(4, 4))) << "e4 is not where it is with White at the bottom";
    }

    TEST_F(ChessUiTest, AnEarlierPositionThatIsViewedShowsThatMovesMarkAndTheLastMoveShowsItsOwn) {
        CountingAnalysisFactory factory({EngineThatPrefers("e2e4")});
        Analyst().SetEngineFactory(factory.AsFactory());
        Analyst().SetRatings(true);
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        ASSERT_TRUE(WaitForRatings());
        ASSERT_FALSE(Analyst().GetRating(Model(), 1)->IsBrilliant());
        Draw();
        EXPECT_TRUE(RectCalls(kBadgeColor).empty()) << "the last move is not brilliant";

        Model().ShowMoves(1);
        Draw();
        ASSERT_EQ(RectCalls(kBadgeColor).size(), 1u);
        ExpectDrawn(RectCalls(kBadgeColor), MarkAt(SquareRect("e4"))[0], "the mark of the move that is shown");

        Model().ShowMoves(2);
        Draw();
        EXPECT_TRUE(RectCalls(kBadgeColor).empty());

        Model().ShowMoves(1);
        Model().EndViewing();
        Draw();
        EXPECT_TRUE(RectCalls(kBadgeColor).empty()) << "back at the game's last move";

        Model().ShowMoves(0);
        Draw();
        EXPECT_TRUE(RectCalls(kBadgeColor).empty()) << "the position before the first move has no move";
    }

    TEST_F(ChessUiTest, ADraftOfThePositionEditorHasNoMark) {
        CountingAnalysisFactory factory({EngineThatPrefers("e2e4")});
        Analyst().SetEngineFactory(factory.AsFactory());
        Analyst().SetRatings(true);
        ASSERT_TRUE(Play(Model(), "e2e4"));
        ASSERT_TRUE(WaitForRatings());
        Draw();
        ASSERT_EQ(RectCalls(kBadgeColor).size(), 1u);

        ChessGame::GetActive()->BeginEditing(EditStart::Current);
        ASSERT_TRUE(Model().IsDrafting());
        Draw();
        EXPECT_TRUE(RectCalls(kBadgeColor).empty());
        EXPECT_TRUE(RectCalls(kGlyphColor).empty());
    }

    TEST_F(ChessUiTest, WithNoGameActiveNothingIsDrawnForTheMark) {
        CountingAnalysisFactory factory({EngineThatPrefers("e2e4")});
        Analyst().SetEngineFactory(factory.AsFactory());
        Analyst().SetRatings(true);
        ASSERT_TRUE(Play(Model(), "e2e4"));
        ASSERT_TRUE(WaitForRatings());
        GameManager::GetInstance().Shutdown();
        ASSERT_EQ(ChessGame::GetActive(), nullptr);
        Draw();
        EXPECT_TRUE(RectCalls(kBadgeColor).empty());
        EXPECT_TRUE(RectCalls(kGlyphColor).empty());
    }

    TEST_F(ChessUiTest, TheMarkIsOnTheLayerOverThePiecesAndBeforeTheResultNodeSoTheDimOfAFinishedGameCoversIt) {
        EXPECT_EQ(NodeLayer("RatingMark"), kLayerOfTheMark);
        EXPECT_GT(NodeLayer("RatingMark"), kPieceLayer);
        EXPECT_EQ(NodeLayer("Result"), kLayerOfTheMark) << "the same layer draws in the order of the scene";
        size_t markAt = 0;
        size_t resultAt = 0;
        size_t index = 0;
        for (const auto& node : GetScene().GetRootNode().GetChildren()) {
            markAt = node->GetName() == "RatingMark"_intern ? index : markAt;
            resultAt = node->GetName() == "Result"_intern ? index : resultAt;
            ++index;
        }
        EXPECT_LT(markAt, resultAt);

        // Fool's mate: the engine's best move at its end is the mate, so it is brilliant, and the finished game dims the board over its mark
        CountingAnalysisFactory factory({EngineThatPrefers("d8h4")});
        Analyst().SetEngineFactory(factory.AsFactory());
        Analyst().SetRatings(true);
        ASSERT_TRUE(PlayAll(Model(), {"f2f3", "e7e5", "g2g4", "d8h4"}));
        ASSERT_TRUE(Model().IsGameOver());
        ASSERT_TRUE(WaitForRatings());
        ASSERT_TRUE(Analyst().GetRating(Model(), 3)->IsBrilliant());
        Draw();

        const auto badge = RectCalls(kBadgeColor);
        ASSERT_EQ(badge.size(), 1u);
        const auto dim = RectCalls(kBoardDimColor);
        ASSERT_EQ(dim.size(), 1u);
        EXPECT_LT(badge[0], dim[0]) << "the mark is recorded before the dim, so the dim is drawn over it";
        EXPECT_TRUE(LiesIn(badge[0]->rect, dim[0]->rect));
    }

}  // namespace BECore::Chess::Tests
