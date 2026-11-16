#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <new>
#include <tests/ChessUiHarness.h>

/// What one frame of the shipped scene allocates on the main thread when nothing changes in it, per state of the game (engine/CLAUDE.md, "Zero allocations in a steady frame"). The
/// counts exist only in a profile build (ENGINE_PROFILER_ALLOC); elsewhere every case skips. Only the main thread is read: a search on a worker is never counted here
namespace BECore::Chess::Tests {

    namespace {

        constexpr int kWarmupFrames = 20;  // lazy caches, text layouts and the recorder's capacity settle in these

        struct FrameCost {
            uint64_t count = 0;
            uint64_t bytes = 0;
        };

        // The cost of a steady frame per case: a count and bytes, pinned exactly so that a new allocation and a removed one both fail
        constexpr FrameCost kOpeningUpdate{};
        constexpr FrameCost kOpeningFrame{};
        constexpr FrameCost kHighlightedFrame{};
        constexpr FrameCost kAnalysisFrame{};
        constexpr FrameCost kFinishedFrame{};
        constexpr FrameCost kPanelFrame{};
        constexpr FrameCost kSoundPanelFrame{};
        constexpr FrameCost kPromotionFrame{};
        constexpr FrameCost kHintFrame{};
        constexpr FrameCost kTurnedResizedFrame{};
        constexpr FrameCost kSearchingFrame{};

        /// The allocations the calling thread makes while @p frame runs
        template <typename Frame>
        FrameCost CostOf(Frame frame) {
            const AllocTracker::ThreadCounters before = AllocTracker::GetThreadCounters();
            frame();
            const AllocTracker::ThreadCounters after = AllocTracker::GetThreadCounters();
            return {after.count - before.count, after.bytes - before.bytes};
        }

        /// Keeps the optimizer from removing an allocation whose result is unused
        void Use(void* ptr) {
            static std::atomic<void*> sink;
            sink.store(ptr, std::memory_order_relaxed);
        }

        ::testing::AssertionResult CostIs(const FrameCost& found, const FrameCost& expected) {
            if (found.count == expected.count && found.bytes == expected.bytes) {
                return ::testing::AssertionSuccess();
            }
            return ::testing::AssertionFailure() << "a steady frame allocated " << found.count << " times, " << found.bytes << " bytes; the case pins " << expected.count << " times, "
                                                 << expected.bytes << " bytes";
        }

    }  // namespace

    class ChessUiAllocTest : public ChessUiTest {
    protected:
        void SetUp() override {
            ChessUiTest::SetUp();
#if !BE_PROFILER_ALLOC
            GTEST_SKIP() << "needs ENGINE_PROFILER_ALLOC: without it no allocation is counted";
#endif
        }

        /// A frame of the game as the loop makes it: the update of the scene, then the draw into the recorder
        void Frame() {
            _manager->UpdateAll();
            Draw();
        }

        /// The cost of one frame; a frame that drew nothing would cost nothing and prove nothing
        FrameCost MeasureFrame() {
            const FrameCost cost = CostOf([&] { Frame(); });
            EXPECT_FALSE(_renderer->calls.empty()) << "the frame drew nothing";
            return cost;
        }

        void WarmUp() {
            for (int frame = 0; frame < kWarmupFrames; ++frame) {
                Frame();
            }
        }
    };

    // A zero in the cases below proves nothing if the count is blind to this test's thread
    TEST_F(ChessUiAllocTest, TheCountSeesAnAllocationOfTheTestThread) {
        constexpr size_t kBlock = 1000;
        const FrameCost cost = CostOf([] {
            void* block = ::operator new(kBlock);
            Use(block);
            ::operator delete(block);
        });
        EXPECT_EQ(cost.count, 1u);
        EXPECT_EQ(cost.bytes, kBlock);
    }

    TEST_F(ChessUiAllocTest, TheUpdateOfTheOpeningPositionAllocatesNothingNew) {
        WarmUp();
        EXPECT_TRUE(CostIs(CostOf([&] { _manager->UpdateAll(); }), kOpeningUpdate));
    }

    TEST_F(ChessUiAllocTest, AFrameOfTheOpeningPositionAllocatesNothingNew) {
        WarmUp();
        EXPECT_TRUE(CostIs(MeasureFrame(), kOpeningFrame));
    }

    TEST_F(ChessUiAllocTest, AFrameWithTheLastMoveAndASelectedPieceAllocatesNothingNew) {
        ClickMove("e2", "e4");
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        Click("e7");
        WarmUp();
        EXPECT_TRUE(CostIs(MeasureFrame(), kHighlightedFrame));
    }

    TEST_F(ChessUiAllocTest, AFrameWithTheEvaluationShownAllocatesNothingNew) {
        CountingAnalysisFactory factory({New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        Analyst().SetEvaluation(true);
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetEvaluation(Model()).has_value(); }));
        WarmUp();
        EXPECT_TRUE(CostIs(MeasureFrame(), kAnalysisFrame));
    }

    TEST_F(ChessUiAllocTest, AFrameOfAFinishedGameAllocatesNothingNew) {
        StartFrom(kBlackMatesOnA1);
        ASSERT_TRUE(Play(Model(), "a8a1"));
        ASSERT_TRUE(Model().IsGameOver());
        WarmUp();
        EXPECT_TRUE(CostIs(MeasureFrame(), kFinishedFrame));
    }

    TEST_F(ChessUiAllocTest, AFrameWithTheOpponentPanelOpenAllocatesNothingNew) {
        ASSERT_TRUE(OpenThePanel());
        WarmUp();
        EXPECT_TRUE(CostIs(MeasureFrame(), kPanelFrame));
    }

    TEST_F(ChessUiAllocTest, AFrameWithTheSoundPanelOpenAllocatesNothingNew) {
        ASSERT_TRUE(ClickButton(kMenuColor));
        ASSERT_TRUE(ClickMenuList("MenuSectionSound").Both());
        WarmUp();
        EXPECT_TRUE(CostIs(MeasureFrame(), kSoundPanelFrame));
    }

    TEST_F(ChessUiAllocTest, AFrameWithAPromotionPendingAllocatesNothingNew) {
        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());
        WarmUp();
        EXPECT_TRUE(CostIs(MeasureFrame(), kPromotionFrame));
    }

    TEST_F(ChessUiAllocTest, AFrameWithTheHintShownAllocatesNothingNew) {
        CountingAnalysisFactory factory({New<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(ShowTheHint());
        WarmUp();
        EXPECT_TRUE(CostIs(MeasureFrame(), kHintFrame));
    }

    // The keys of what the board and the HUD rebuild (the orientation, the window, the view) have moved by now and must not move again in an idle frame
    TEST_F(ChessUiAllocTest, AFrameAfterTheBoardWasTurnedAndTheWindowResizedAllocatesNothingNew) {
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        ASSERT_TRUE(ShowPortrait({kNarrowestWidth, kNarrowestPortraitHeight}));
        ASSERT_TRUE(BlackIsAtTheBottom());
        WarmUp();
        EXPECT_TRUE(CostIs(MeasureFrame(), kTurnedResizedFrame));
    }

    // The search is on a worker and held at its gate, so the frames are the same however long the machine takes
    TEST_F(ChessUiAllocTest, AFrameWhileTheProgramThinksAllocatesNothingNew) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = New<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; })) << "the search never started";
        ASSERT_TRUE(Opponent().IsThinking());
        WarmUp();
        EXPECT_TRUE(CostIs(MeasureFrame(), kSearchingFrame));
        EXPECT_EQ(engine->Answered(), 0) << "the search was still held";
    }

}  // namespace BECore::Chess::Tests
