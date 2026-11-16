#include <Chess/Sound/SoundCue.h>
#include <tests/ChessUiHarness.h>

/// The sound of the board (ChessSoundComponent on the node Sound): one request to the audio manager per played move, whoever plays it, with the clip of its cue, and none for anything
/// that is not a move. ChessTests has no audio backend, so what the game asked for is the record of the manager (GetRecentPlays()). The scene, the helpers and the fixture ChessUiTest
/// are tests/ChessUiHarness.h; the rule of the cue is SoundCueTest's.
namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kCaptureFen = "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2";
        constexpr const char* kCastleFen = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
        constexpr const char* kCheckFen = "4k3/8/8/8/8/8/8/R3K3 w Q - 0 1";

        /// Exactly one request, for @p cue: its clip on the bus and at the volume of the table
        void ExpectOneRequestFor(const Vector<AudioPlayRecord>& requests, SoundCue cue) {
            ASSERT_EQ(requests.size(), 1u);
            EXPECT_EQ(requests[0].clip, ClipPath(cue));
            EXPECT_EQ(requests[0].bus, kSoundCueBus);
            EXPECT_EQ(requests[0].volume, CueVolume(cue));
        }

    }  // namespace

    TEST_F(ChessUiTest, TheFirstFrameOfANewGameAsksForNoSound) {
        const uint64_t before = AudioRequests();
        _manager->UpdateAll();
        EXPECT_EQ(AudioRequests(), before);
    }

    TEST_F(ChessUiTest, AMoveByClickAsksForItsClipOnce) {
        _manager->UpdateAll();
        const uint64_t before = AudioRequests();

        ClickMove("e2", "e4");
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(AudioRequests(), before) << "the sound comes with the next frame, not with the click";
        _manager->UpdateAll();

        ExpectOneRequestFor(AudioRequestsSince(before), SoundCue::Move);
        _manager->UpdateAll();
        EXPECT_EQ(AudioRequests(), before + 1) << "once";
    }

    TEST_F(ChessUiTest, AMoveByTryMoveAndOneByTheControlAdapterAskForTheirClips) {
        _manager->UpdateAll();
        uint64_t before = AudioRequests();

        ASSERT_TRUE(Model().TryMove(Mv("e2e4")).has_value());
        _manager->UpdateAll();
        ExpectOneRequestFor(AudioRequestsSince(before), SoundCue::Move);

        before = AudioRequests();
        Json action = Json::object();
        action["type"] = "move";
        action["uci"] = "d7d5";
        ASSERT_TRUE(ChessGame::GetActive()->AsControllable()->PerformAction(action).has_value());
        _manager->UpdateAll();
        ExpectOneRequestFor(AudioRequestsSince(before), SoundCue::Move);
    }

    TEST_F(ChessUiTest, TheMoveOfTheProgramAsksForItsClipToo) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({New<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        const uint64_t before = AudioRequests();

        ClickMove("e2", "e4");
        _manager->UpdateAll();
        ExpectOneRequestFor(AudioRequestsSince(before), SoundCue::Move);

        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));
        _manager->UpdateAll();
        const Vector<AudioPlayRecord> requests = AudioRequestsSince(before);
        ASSERT_EQ(requests.size(), 2u) << "the program's move is a move like the person's";
        EXPECT_EQ(requests[1].clip, ClipPath(SoundCue::Move));
    }

    TEST_F(ChessUiTest, ACaptureAsksForTheCaptureClip) {
        StartFrom(kCaptureFen);
        const uint64_t before = AudioRequests();
        ClickMove("e4", "d5");
        _manager->UpdateAll();
        ExpectOneRequestFor(AudioRequestsSince(before), SoundCue::Capture);
    }

    TEST_F(ChessUiTest, CastlingAsksForTheCastleClip) {
        StartFrom(kCastleFen);
        const uint64_t before = AudioRequests();
        ASSERT_TRUE(Play(Model(), "e1g1"));
        _manager->UpdateAll();
        ExpectOneRequestFor(AudioRequestsSince(before), SoundCue::Castle);
    }

    TEST_F(ChessUiTest, ACheckAsksForTheCheckClip) {
        StartFrom(kCheckFen);
        const uint64_t before = AudioRequests();
        ASSERT_TRUE(Play(Model(), "a1a8"));
        _manager->UpdateAll();
        ExpectOneRequestFor(AudioRequestsSince(before), SoundCue::Check);
    }

    TEST_F(ChessUiTest, APromotionAsksForTheClipWhenThePieceIsChosenNotBefore) {
        StartFrom(kPromotionFen);
        const uint64_t before = AudioRequests();

        ClickMove("a7", "a8");
        ASSERT_TRUE(Model().HasPendingPromotion());
        _manager->UpdateAll();
        EXPECT_EQ(AudioRequests(), before) << "a pending promotion is no move yet";

        ASSERT_TRUE(Model().ChoosePromotion(PieceType::Queen).has_value());
        _manager->UpdateAll();
        ExpectOneRequestFor(AudioRequestsSince(before), SoundCue::Promotion);
    }

    TEST_F(ChessUiTest, ACheckmateAsksForTheEndOfTheGameClip) {
        _manager->UpdateAll();
        ASSERT_TRUE(PlayAll(Model(), {"f2f3", "e7e5", "g2g4"}));
        _manager->UpdateAll();
        const uint64_t before = AudioRequests();

        ASSERT_TRUE(Play(Model(), "d8h4"));
        ASSERT_TRUE(Model().IsGameOver());
        _manager->UpdateAll();
        ExpectOneRequestFor(AudioRequestsSince(before), SoundCue::GameEnd);
    }

    TEST_F(ChessUiTest, TwoMovesInOneFrameAskOnceWithTheCueOfTheLastOne) {
        _manager->UpdateAll();
        const uint64_t before = AudioRequests();
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "d7d5", "e4d5"}));
        _manager->UpdateAll();
        ExpectOneRequestFor(AudioRequestsSince(before), SoundCue::Capture);
    }

    TEST_F(ChessUiTest, ANewGameATakebackAndAPositionFromAFenAskForNothing) {
        _manager->UpdateAll();
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5", "g1f3"}));
        _manager->UpdateAll();
        uint64_t before = AudioRequests();

        ASSERT_EQ(Model().TakeBack(1), 1u);
        _manager->UpdateAll();
        EXPECT_EQ(AudioRequests(), before) << "takeback";

        Json state = Json::object();
        state["fen"] = MakeJsonString(kCaptureFen);
        ASSERT_TRUE(ChessGame::GetActive()->AsControllable()->SetState(state).has_value());
        _manager->UpdateAll();
        EXPECT_EQ(AudioRequests(), before) << "set_state with a FEN";

        ASSERT_TRUE(Play(Model(), "e4d5"));
        _manager->UpdateAll();
        before = AudioRequests();
        Model().Reset();
        _manager->UpdateAll();
        EXPECT_EQ(AudioRequests(), before) << "reset";
    }

    TEST_F(ChessUiTest, ViewingAnEarlierPositionAndSelectingAskForNothing) {
        _manager->UpdateAll();
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        _manager->UpdateAll();
        const uint64_t before = AudioRequests();

        ASSERT_TRUE(Model().ShowMoves(1));
        _manager->UpdateAll();
        Model().EndViewing();
        _manager->UpdateAll();
        EXPECT_EQ(AudioRequests(), before) << "viewing";

        Click("g1");
        ASSERT_TRUE(Model().HasSelection());
        _manager->UpdateAll();
        Click("e5");
        _manager->UpdateAll();
        EXPECT_EQ(AudioRequests(), before) << "a selection, and a click on a square that is no target";

        Click("d4");
        _manager->UpdateAll();
        EXPECT_EQ(AudioRequests(), before) << "a click on an empty square";
    }

    TEST_F(ChessUiTest, AGameThatHoldsMovesBeforeTheFirstFrameIsSilent) {
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        const uint64_t before = AudioRequests();
        _manager->UpdateAll();
        EXPECT_EQ(AudioRequests(), before) << "a restored game is not heard again";

        ASSERT_TRUE(Play(Model(), "g1f3"));
        _manager->UpdateAll();
        ExpectOneRequestFor(AudioRequestsSince(before), SoundCue::Move);
    }

    TEST_F(ChessUiTest, SteadyFramesAndAFrameAfterTheGameIsGoneAskForNothing) {
        _manager->UpdateAll();
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "e7e5"}));
        _manager->UpdateAll();
        const uint64_t before = AudioRequests();

        IdleFrames();
        EXPECT_EQ(AudioRequests(), before);

        GameManager::GetInstance().Shutdown();
        ASSERT_EQ(ChessGame::GetActiveModel(), nullptr);
        _manager->UpdateAll();
        EXPECT_EQ(AudioRequests(), before);
    }

}  // namespace BECore::Chess::Tests
