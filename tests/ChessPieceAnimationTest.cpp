#include "ChessTestUtil.h"
#include "TestEngines.h"

#include <BECore/Animation/Easing.h>
#include <BECore/Animation/Lerp.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/ViewTransform.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ChessBoardComponent.h>
#include <Chess/Components/ChessPieceComponent.h>
#include <Chess/Game/MoveGenerator.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Games/GameManager.h>

/**
 * The slide of a piece to its new square (ChessPieceComponent) on the shipped scene, with a clock the test drives: a frame is what OnGameCycle does, the time
 * advances, then the tweens, then the scenes. Frame time 1/32 and a slide of 0.125 s (powers of two, so the sums are exact): a move is seen by frame 1 (which still
 * draws the old square), the tween moves from frame 2 on, and the fourth step after the first frame ends it.
 */
namespace BECore::Chess::Tests {

    using namespace BECore::Tests;

    namespace {

        constexpr double kFrame = 1.0 / 32.0;     // seconds of one frame
        constexpr float kDuration = 0.125f;       // the slide, four frames
        constexpr float kPixelTolerance = 0.01f;  // in view units
        constexpr size_t kPieceCount = 32;
        constexpr float kHidden = -10000.0f;     // where a captured piece waits (ChessPieceComponent.cpp)
        constexpr float kQuarter = 0.25f;        // progress after one tween step
        constexpr float kHalf = 0.5f;            // progress after two
        constexpr float kThreeQuarters = 0.75f;  // progress after three
        constexpr float kDefaultSlide = 0.15f;   // what SceneConfig.xml gets from the code
        constexpr int kStepsToEnd = 4;           // tween steps (frames after the first) until the slide ends
        constexpr float kViewWidth = 840.0f;     // the view of the scene (SceneConfig.xml) and half a square, to turn a square into the output pixel of its middle
        constexpr float kViewHeight = 800.0f;
        constexpr float kHalfSquare = 40.0f;
        constexpr float kDragStepPixels = 20.0f;  // a first motion of a drag, well past the threshold of the board
        constexpr int32_t kPieceLayer = 0;        // the layer of a piece node in the scene
        constexpr int32_t kDragLayer = 6;         // the layer of a piece in the hand (ChessPieceComponent::kDragLayer, written out here to pin it)

        const char* const kCaptureFen = "4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1";    // e4xd5
        const char* const kEnPassantFen = "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1";  // e5xd6 en passant
        const char* const kCastlingFen = "4k3/8/8/8/8/8/8/4K2R w K - 0 1";      // e1g1 and the rook to f1
        const char* const kPromotionFen = "7k/P7/8/8/8/8/8/K7 w - - 0 1";       // a7a8q

        std::filesystem::path ShippedSceneConfig() {
            return std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "SceneConfig.xml";
        }

        bool Play(ChessGameModel& model, StringView uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        using Origin = std::pair<float, float>;

    }  // namespace

    class ChessPieceAnimationTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            // The clock and the tween manager are shared by all tests of a process
            CoreManager::GetTweens().Clear();
            CoreManager::GetTime().Reset();
            CoreManager::GetFontAtlasLibrary().Clear();
            CoreManager::GetUIVars().Clear();
            _renderer = New<RecordingRenderer>();
            _renderer->width = 1280.0f;
            _renderer->height = 720.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromFile(ShippedSceneConfig())) << ShippedSceneConfig().string();
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            EngineEnvironment::UseSceneManager(_manager);

            GameManager::GetInstance().Initialize();
            ASSERT_NE(ChessGame::GetActiveModel(), nullptr) << "Chess is the active game";
        }

        void TearDown() override {
            GameManager::GetInstance().Shutdown();
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            CoreManager::GetTweens().Clear();
            CoreManager::GetTime().Reset();
            CoreManager::GetUIVars().Clear();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        ChessGameModel& Model() const {
            ChessGameModel* model = ChessGame::GetActiveModel();
            EXPECT_NE(model, nullptr);
            return *model;
        }

        Scene& GetScene() const {
            return *_manager->GetSceneByName(PoolString::Intern("ChessScene"));
        }

        /// One frame of the game loop, in its order: the clock, the tweens (stage 2b), the scenes
        void Frame(double seconds = kFrame) {
            CoreManager::GetTime().Advance(seconds);
            CoreManager::GetTweens().Advance(CoreManager::GetTime().GetDeltaTime());
            _manager->UpdateAll();
        }

        void Frames(int count) {
            for (int frame = 0; frame < count; ++frame) {
                Frame();
            }
        }

        /// Every piece node the scene has
        Vector<IntrusivePtr<SceneNode>> PieceNodes() const {
            Vector<IntrusivePtr<SceneNode>> nodes;
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                if (node->GetComponent<ChessPieceComponent>()) {
                    nodes.push_back(node);
                }
            }
            return nodes;
        }

        void SetSlideDuration(float seconds) const {
            for (const auto& node : PieceNodes()) {
                node->GetComponent<ChessPieceComponent>()->_slideDuration = seconds;
            }
        }

        /// The first frame registers the pieces; with the slide of the tests set, the pieces are on the squares of the game
        void Settle() {
            SetSlideDuration(kDuration);
            Frames(2);
        }

        void StartFrom(const char* fen) {
            Model().SetPosition(PositionFromFen(fen));
            Frames(2);
        }

        /// The node whose piece the model has on @p square now
        IntrusivePtr<SceneNode> NodeAt(const char* square) const {
            for (const auto& node : PieceNodes()) {
                if (Model().GetSquareOf(node->GetComponent<ChessPieceComponent>().Get()) == Sq(square)) {
                    return node;
                }
            }
            ADD_FAILURE() << "no piece on " << square;
            return {};
        }

        /// The cell of a square in the orientation the board has now
        static Origin PixelOf(const char* square) {
            const ChessBoardComponent* board = ChessBoardComponent::GetActive();
            EXPECT_NE(board, nullptr);
            float x = 0.0f;
            float y = 0.0f;
            if (board) {
                board->SquareToPixel(Sq(square).file, Sq(square).rank, x, y);
            }
            return {x, y};
        }

        static Origin OriginOf(const IntrusivePtr<SceneNode>& node) {
            const auto transform = node->GetComponent<TransformComponent>();
            EXPECT_TRUE(transform);
            return transform ? Origin{transform->_x, transform->_y} : Origin{};
        }

        static void ExpectOrigin(const IntrusivePtr<SceneNode>& node, Origin expected, const char* what) {
            const Origin at = OriginOf(node);
            EXPECT_NEAR(at.first, expected.first, kPixelTolerance) << what << " " << node->GetName().CStr();
            EXPECT_NEAR(at.second, expected.second, kPixelTolerance) << what << " " << node->GetName().CStr();
        }

        static void ExpectOn(const IntrusivePtr<SceneNode>& node, const char* square, const char* what = "on its square") {
            ExpectOrigin(node, PixelOf(square), what);
        }

        /// The QuadOut point of the slide between two squares, @p progress being the share of the time that passed
        static void ExpectSliding(const IntrusivePtr<SceneNode>& node, const char* from, const char* to, float progress, const char* what = "mid-slide") {
            const float eased = Ease(EaseType::QuadOut, progress);
            const Origin start = PixelOf(from);
            const Origin end = PixelOf(to);
            ExpectOrigin(node, {Lerp(start.first, end.first, eased), Lerp(start.second, end.second, eased)}, what);
        }

        static void ExpectHidden(const IntrusivePtr<SceneNode>& node) {
            ExpectOrigin(node, {kHidden, kHidden}, "hidden");
        }

        /// Every piece that stands on a square is drawn on it
        void ExpectEveryPieceOnItsSquare(const char* what = "on its square") const {
            size_t checked = 0;
            for (const auto& node : PieceNodes()) {
                const Square square = Model().GetSquareOf(node->GetComponent<ChessPieceComponent>().Get());
                if (!square.IsInside()) {
                    ExpectHidden(node);
                    continue;
                }
                float x = 0.0f;
                float y = 0.0f;
                ChessBoardComponent::GetActive()->SquareToPixel(square.file, square.rank, x, y);
                ExpectOrigin(node, {x, y}, what);
                ++checked;
            }
            EXPECT_GT(checked, 0u);
        }

        /// Where every piece node is now, by node name
        Vector<std::pair<PoolString, Origin>> Origins() const {
            Vector<std::pair<PoolString, Origin>> origins;
            for (const auto& node : PieceNodes()) {
                origins.emplace_back(node->GetName(), OriginOf(node));
            }
            return origins;
        }

        /// The pieces other than @p moving have not left the origins in @p before
        void ExpectOthersStill(const Vector<std::pair<PoolString, Origin>>& before, const IntrusivePtr<SceneNode>& moving) const {
            const auto now = Origins();
            ASSERT_EQ(now.size(), before.size());
            for (size_t i = 0; i < now.size(); ++i) {
                if (now[i].first == moving->GetName()) {
                    continue;
                }
                EXPECT_NEAR(now[i].second.first, before[i].second.first, kPixelTolerance) << now[i].first.CStr();
                EXPECT_NEAR(now[i].second.second, before[i].second.second, kPixelTolerance) << now[i].first.CStr();
            }
        }

        PoolString SpriteOf(const IntrusivePtr<SceneNode>& node) const {
            const auto sprite = node->GetComponent<SpriteRendererComponent>();
            EXPECT_TRUE(sprite);
            return sprite ? sprite->_spriteId : PoolString();
        }

        OpponentController& Opponent() const {
            OpponentController* opponent = ChessGame::GetActiveOpponent();
            EXPECT_NE(opponent, nullptr);
            return *opponent;
        }

        /// The program plays White, so the board is turned by 180 degrees (a gated engine never answers: nothing moves by itself)
        void TurnTheBoard() {
            Opponent().SetEngineFactory(_factory.AsFactory());
            ASSERT_TRUE(Opponent().SetSide(PieceColor::White).has_value());
            Frame(0.0);
        }

        /// Plays @p uci and then the frames; the node of the piece is taken before the move
        IntrusivePtr<SceneNode> PlayAndShow(const char* from, const char* uci) {
            const IntrusivePtr<SceneNode> node = NodeAt(from);
            EXPECT_TRUE(Play(Model(), uci)) << uci;
            Frame();
            return node;
        }

        /// The middle of a square in output pixels, for the fit of the view into the output and the orientation the board has now
        Origin ScreenOf(const char* square) const {
            const Origin cell = PixelOf(square);
            const Rect at = ViewTransform::Fit(kViewWidth, kViewHeight, _renderer->width, _renderer->height).ToOutput(cell.first + kHalfSquare, cell.second + kHalfSquare, 0.0f, 0.0f);
            return {at.x, at.y};
        }

        static SDL_MouseButtonEvent Button(Origin at, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = at.first;
            event.y = at.second;
            return event;
        }

        static void MoveHeld(Origin at) {
            SDL_MouseMotionEvent event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.state = SDL_BUTTON_LMASK;
            event.x = at.first;
            event.y = at.second;
            CoreManager::GetUIInput().DispatchMouseMotion(event);
        }

        /// The output is mapped back to the view of the last draw, so the scene is drawn once before the first event
        void PressOn(const char* square) {
            _manager->DrawAll();
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(ScreenOf(square), true));
        }

        void ReleaseOn(const char* square) const {
            CoreManager::GetUIInput().DispatchMouseButtonUp(Button(ScreenOf(square), false));
        }

        /// A press on the piece, a motion past the threshold and a frame that sees the piece in the hand
        void Lift(const char* square) {
            PressOn(square);
            const Origin at = ScreenOf(square);
            MoveHeld({at.first + kDragStepPixels, at.second});
            Frame();
        }

        /// The pointer goes to @p square and the button is released there; no frame in between
        void DropOn(const char* square) const {
            MoveHeld(ScreenOf(square));
            ReleaseOn(square);
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        CountingFactory _factory{eastl::make_shared<GatedEngine>(true)};
    };

    TEST_F(ChessPieceAnimationTest, TheShippedSlideTimeIsTheDefaultOfTheCode) {
        const auto nodes = PieceNodes();
        ASSERT_EQ(nodes.size(), kPieceCount);
        for (const auto& node : nodes) {
            EXPECT_FLOAT_EQ(node->GetComponent<ChessPieceComponent>()->_slideDuration, kDefaultSlide) << node->GetName().CStr();
        }
    }

    TEST_F(ChessPieceAnimationTest, TheFirstSightingPlacesEveryPieceAtOnce) {
        SetSlideDuration(kDuration);
        Frame();
        ExpectEveryPieceOnItsSquare();
        Frames(kStepsToEnd);
        ExpectEveryPieceOnItsSquare();
    }

    TEST_F(ChessPieceAnimationTest, MovesPlayedBeforeTheFirstFrameAreNotSlid) {
        SetSlideDuration(kDuration);
        ASSERT_TRUE(Play(Model(), "e2e4"));
        ASSERT_TRUE(Play(Model(), "e7e5"));

        // The pieces register one by one in the first frame and each registration deals the game again, so the second frame is the first with every piece on its square
        Frames(2);
        ExpectEveryPieceOnItsSquare("at once");
        EXPECT_EQ(Model().GetPieceAt(Sq("e4")).type, PieceType::Pawn);
        Frames(kStepsToEnd);
        ExpectEveryPieceOnItsSquare();
    }

    TEST_F(ChessPieceAnimationTest, APlayedMoveSlidesWithQuadOutAndEndsOnItsSquare) {
        Settle();
        const auto pawn = NodeAt("e2");
        const auto before = Origins();
        const uint64_t version = Model().GetVersion();

        ASSERT_TRUE(Play(Model(), "e2e4"));
        EXPECT_EQ(Model().GetVersion(), version + 1);
        Frame();
        ExpectOn(pawn, "e2", "the frame that sees the move still draws the old square");
        ExpectOthersStill(before, pawn);

        Frame();
        ExpectSliding(pawn, "e2", "e4", kQuarter);
        Frame();
        ExpectSliding(pawn, "e2", "e4", kHalf);
        EXPECT_NE(OriginOf(pawn), PixelOf("e2"));
        EXPECT_NE(OriginOf(pawn), PixelOf("e4"));
        Frame();
        ExpectSliding(pawn, "e2", "e4", kThreeQuarters);
        ExpectOthersStill(before, pawn);

        Frame();
        ExpectOn(pawn, "e4", "the end of the slide");
        Frames(kStepsToEnd);
        ExpectOn(pawn, "e4");
        ExpectOthersStill(before, pawn);
        EXPECT_EQ(Model().GetVersion(), version + 1) << "the animation is the component's only: the model moved once";
    }

    TEST_F(ChessPieceAnimationTest, ACapturedPieceStaysOnItsSquareDuringTheSlideAndThenGoesOffScreen) {
        Settle();
        StartFrom(kCaptureFen);
        const auto victim = NodeAt("d5");
        const auto capturer = PlayAndShow("e4", "e4d5");
        // PlayAndShow took the node before the move, so the capturer is the one that stood on e4
        ExpectOn(victim, "d5", "the first frame");
        ExpectOn(capturer, "e4", "the first frame");

        Frames(2);
        ExpectOn(victim, "d5", "waits for the slide");
        ExpectSliding(capturer, "e4", "d5", kHalf);

        Frames(kStepsToEnd);
        ExpectHidden(victim);
        ExpectOn(capturer, "d5");
    }

    TEST_F(ChessPieceAnimationTest, ThePawnTakenEnPassantStaysUntilTheSlideEnds) {
        Settle();
        StartFrom(kEnPassantFen);
        const auto victim = NodeAt("d5");
        const auto capturer = PlayAndShow("e5", "e5d6");
        ExpectOn(victim, "d5");

        Frames(2);
        ExpectOn(victim, "d5", "the passed pawn waits on its own square");
        ExpectSliding(capturer, "e5", "d6", kHalf);

        Frames(kStepsToEnd);
        ExpectHidden(victim);
        ExpectOn(capturer, "d6");
    }

    TEST_F(ChessPieceAnimationTest, CastlingSlidesTheKingAndTheRook) {
        Settle();
        StartFrom(kCastlingFen);
        const auto rook = NodeAt("h1");
        const auto king = PlayAndShow("e1", "e1g1");
        ExpectOn(king, "e1");
        ExpectOn(rook, "h1");

        Frames(2);
        ExpectSliding(king, "e1", "g1", kHalf);
        ExpectSliding(rook, "h1", "f1", kHalf);

        Frames(kStepsToEnd);
        ExpectOn(king, "g1");
        ExpectOn(rook, "f1");
    }

    TEST_F(ChessPieceAnimationTest, ThePromotedPawnChangesItsSpriteWhenTheSlideEnds) {
        Settle();
        StartFrom(kPromotionFen);
        const auto pawn = PlayAndShow("a7", "a7a8q");
        const PoolString pawnSprite = PoolString::Intern("WhitePawn");
        const PoolString queenSprite = PoolString::Intern("WhiteQueen");
        EXPECT_EQ(SpriteOf(pawn), pawnSprite);

        Frames(kStepsToEnd - 1);
        ExpectSliding(pawn, "a7", "a8", kThreeQuarters);
        EXPECT_EQ(SpriteOf(pawn), pawnSprite) << "still a pawn on its way";

        Frame();
        ExpectOn(pawn, "a8");
        EXPECT_EQ(SpriteOf(pawn), queenSprite);
    }

    TEST_F(ChessPieceAnimationTest, AResetPlacesEveryPieceAtOnceAndEndsASlideUnderWay) {
        Settle();
        ASSERT_TRUE(Play(Model(), "e2e4"));
        Frames(kStepsToEnd + 1);
        ASSERT_TRUE(Play(Model(), "e7e5"));
        Frames(2);  // the pawn of Black is half way

        Model().Reset();
        Frame();
        ExpectEveryPieceOnItsSquare("after a reset");
        EXPECT_EQ(Model().GetMoves().size(), 0u);
        Frames(kStepsToEnd);
        ExpectEveryPieceOnItsSquare();
    }

    TEST_F(ChessPieceAnimationTest, ANewPositionPlacesEveryPieceAtOnce) {
        Settle();
        ASSERT_TRUE(Play(Model(), "e2e4"));
        Frames(2);

        Model().SetPosition(PositionFromFen(kCaptureFen));
        Frame();
        ExpectEveryPieceOnItsSquare("after set_state");
        Frames(kStepsToEnd);
        ExpectEveryPieceOnItsSquare();
    }

    TEST_F(ChessPieceAnimationTest, AResetFollowedByMovesBetweenTwoFramesIsNotTakenForOneMove) {
        Settle();
        ASSERT_TRUE(Play(Model(), "e2e4"));
        Frames(kStepsToEnd + 1);

        // The count of moves is one more than the component saw, but a reset lies between (the control API can do this in one gap of frames)
        Model().Reset();
        ASSERT_TRUE(Play(Model(), "e2e4"));
        ASSERT_TRUE(Play(Model(), "e7e5"));
        ASSERT_EQ(Model().GetMoves().size(), 2u);
        Frame();
        ExpectEveryPieceOnItsSquare("no slide");
    }

    TEST_F(ChessPieceAnimationTest, AFrameWithNoTimeShowsAMoveAtOnce) {
        Settle();
        const auto pawn = NodeAt("e2");
        ASSERT_TRUE(Play(Model(), "e2e4"));
        Frame(0.0);
        ExpectOn(pawn, "e4");
        Frames(kStepsToEnd);
        ExpectOn(pawn, "e4");
    }

    TEST_F(ChessPieceAnimationTest, ASlideUnderWayStaysWhereItIsInAFrameWithNoTime) {
        Settle();
        const auto pawn = NodeAt("e2");
        ASSERT_TRUE(Play(Model(), "e2e4"));
        Frames(2);
        ExpectSliding(pawn, "e2", "e4", kQuarter);

        Frame(0.0);
        ExpectSliding(pawn, "e2", "e4", kQuarter, "the tween does not advance");

        Frame();
        ExpectSliding(pawn, "e2", "e4", kHalf);
    }

    TEST_F(ChessPieceAnimationTest, ASlideTimeOfZeroMeansNoSlide) {
        Settle();
        SetSlideDuration(0.0f);
        const auto pawn = NodeAt("e2");
        ASSERT_TRUE(Play(Model(), "e2e4"));
        Frame();
        ExpectOn(pawn, "e4");
    }

    TEST_F(ChessPieceAnimationTest, ASlideCutOffByTheTweenManagerDoesNotLeavePieceOnItsOldSquare) {
        Settle();
        const auto pawn = NodeAt("e2");
        ASSERT_TRUE(Play(Model(), "e2e4"));
        Frames(2);
        ExpectSliding(pawn, "e2", "e4", kQuarter);

        CoreManager::GetTweens().Clear();  // what a shutdown or a test teardown does: the tween is gone, its callbacks never run
        Frame();
        ExpectOn(pawn, "e4", "no tween, no slide");
    }

    TEST_F(ChessPieceAnimationTest, WithTheProgramAsWhiteAMoveSlidesBetweenTheTurnedCells) {
        SKIP_WITHOUT_WORKER_THREADS();
        Settle();
        TurnTheBoard();
        EXPECT_EQ(PixelOf("a1"), (Origin{660.0f, 80.0f})) << "the board is turned: a1 at the top right";
        ExpectEveryPieceOnItsSquare("turned");

        const auto pawn = NodeAt("e2");
        ASSERT_TRUE(Play(Model(), "e2e4"));
        Frame();
        ExpectOn(pawn, "e2");
        Frames(2);
        ExpectSliding(pawn, "e2", "e4", kHalf);
        EXPECT_NEAR(OriginOf(pawn).first, (PixelOf("e2").first + PixelOf("e4").first) * 0.5f, 80.0f * 0.5f);
        Frames(kStepsToEnd);
        ExpectOn(pawn, "e4");
        ExpectEveryPieceOnItsSquare();
    }

    TEST_F(ChessPieceAnimationTest, ASlideUnderWayWhenTheBoardTurnsGoesBetweenTheTurnedCells) {
        Settle();
        const auto pawn = NodeAt("e2");
        ASSERT_TRUE(Play(Model(), "e2e4"));
        Frames(2);
        ExpectSliding(pawn, "e2", "e4", kQuarter);
        const Origin upright = OriginOf(pawn);

        TurnTheBoard();  // a frame with no time: the tween stands, the pixels come from the turned board
        ExpectSliding(pawn, "e2", "e4", kQuarter, "turned");
        EXPECT_NE(OriginOf(pawn), upright);

        Frame();
        ExpectSliding(pawn, "e2", "e4", kHalf, "turned");
        Frames(kStepsToEnd);
        ExpectOn(pawn, "e4");
        ExpectEveryPieceOnItsSquare();
    }

    TEST_F(ChessPieceAnimationTest, ClicksAndASecondMoveDuringASlideWorkAsWithoutAnimation) {
        // The same moves with the slide and without it end in the same game, the same version steps and the same pieces on the same squares
        const auto play = [this](float duration) {
            Model().Reset();
            Frames(2);
            SetSlideDuration(duration);
            const uint64_t version = Model().GetVersion();
            const auto pawn = NodeAt("e2");

            Model().OnSquareClicked(Sq("e2"));
            Model().OnSquareClicked(Sq("e4"));
            Frames(2);
            if (duration > 0.0f) {
                ExpectSliding(pawn, "e2", "e4", kQuarter);
            }
            EXPECT_TRUE(Play(Model(), "d7d5"));  // a piece of Black moves while the pawn is on its way
            Frame();
            if (duration > 0.0f) {
                ExpectSliding(pawn, "e2", "e4", kHalf, "the slide goes on");
            }
            Model().OnSquareClicked(Sq("e4"));
            Model().OnSquareClicked(Sq("d5"));  // the same pawn again, before it has arrived: its slide is replaced
            Frame();
            Frames(kStepsToEnd + 1);
            ExpectEveryPieceOnItsSquare();
            ExpectOn(pawn, "d5");

            String moves;
            for (const Move move : Model().GetMoves()) {
                moves += String(move.ToUci().c_str()) + " ";
            }
            return std::tuple{Model().GetVersion() - version, moves, String(Model().GetPosition().ToFen().c_str())};
        };

        const auto animated = play(kDuration);
        const auto plain = play(0.0f);
        EXPECT_EQ(std::get<0>(animated), 3u);
        EXPECT_EQ(animated, plain);
    }

    TEST_F(ChessPieceAnimationTest, AMoveDroppedByADragStandsOnTheTargetInTheFirstFrameAfterTheRelease) {
        Settle();
        const auto pawn = NodeAt("e2");
        Lift("e2");
        EXPECT_EQ(pawn->GetLayer(), kDragLayer);
        EXPECT_NE(OriginOf(pawn), PixelOf("e2")) << "the piece is in the hand";
        const uint64_t version = Model().GetVersion();

        DropOn("e4");
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetVersion(), version + 1);
        for (int frame = 0; frame <= kStepsToEnd + 1; ++frame) {
            Frame();
            ExpectOn(pawn, "e4", "dropped: never between e2 and e4");
            EXPECT_EQ(pawn->GetLayer(), kPieceLayer) << "frame " << frame;
        }
        ExpectEveryPieceOnItsSquare();
    }

    TEST_F(ChessPieceAnimationTest, ADropInTheTurnedBoardStandsOnTheTargetAtOnceToo) {
        SKIP_WITHOUT_WORKER_THREADS();
        TurnTheBoard();
        Settle();
        StartFrom("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
        SetSlideDuration(kDuration);
        const auto pawn = NodeAt("e7");
        Lift("e7");
        DropOn("e5");
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        Frame();
        ExpectOn(pawn, "e5", "the turned cell of e5");
        EXPECT_EQ(pawn->GetLayer(), kPieceLayer);
    }

    TEST_F(ChessPieceAnimationTest, ADropThatPlaysNothingPutsThePieceBackAtOnce) {
        Settle();
        const auto pawn = NodeAt("e2");
        Lift("e2");
        DropOn("e5");  // not a target of the pawn
        EXPECT_TRUE(Model().GetMoves().empty());
        Frame();
        ExpectOn(pawn, "e2", "back on its square");
        EXPECT_EQ(pawn->GetLayer(), kPieceLayer);
        ExpectEveryPieceOnItsSquare();
    }

    TEST_F(ChessPieceAnimationTest, TheMoveOfTheOtherSideAfterADragSlidesAsUsual) {
        Settle();
        const auto pawn = NodeAt("e2");
        const auto reply = NodeAt("e7");
        Lift("e2");
        DropOn("e4");
        Frame();
        ExpectOn(pawn, "e4");

        ASSERT_TRUE(Play(Model(), "e7e5"));  // what the answer of the program is
        Frame();
        ExpectOn(reply, "e7", "the frame that sees the move still draws the old square");
        Frame();
        ExpectSliding(reply, "e7", "e5", kQuarter);
        ExpectOn(pawn, "e4");
        Frame();
        ExpectSliding(reply, "e7", "e5", kHalf);
        Frames(kStepsToEnd);
        ExpectOn(reply, "e5");
        ExpectEveryPieceOnItsSquare();
    }

    TEST_F(ChessPieceAnimationTest, TheSameMoveByTwoMouseClicksStillSlides) {
        Settle();
        const auto pawn = NodeAt("e2");
        PressOn("e2");
        ReleaseOn("e2");
        PressOn("e4");
        ReleaseOn("e4");
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        Frame();
        ExpectOn(pawn, "e2", "the frame that sees the move");
        Frame();
        ExpectSliding(pawn, "e2", "e4", kQuarter);
        Frames(kStepsToEnd);
        ExpectOn(pawn, "e4");
    }

    TEST_F(ChessPieceAnimationTest, ADragWithNoSlideTimeIsUnchanged) {
        Settle();
        SetSlideDuration(0.0f);
        const auto pawn = NodeAt("e2");
        Lift("e2");
        EXPECT_EQ(pawn->GetLayer(), kDragLayer);
        DropOn("e4");
        Frame();
        ExpectOn(pawn, "e4");
        EXPECT_EQ(pawn->GetLayer(), kPieceLayer);
    }

    TEST_F(ChessPieceAnimationTest, ACaptureByADropLandsAtOnceAndTheTakenPieceStaysUnderItForTheSlideTime) {
        Settle();
        StartFrom(kCaptureFen);
        SetSlideDuration(kDuration);
        const auto taker = NodeAt("e4");
        const auto taken = NodeAt("d5");
        Lift("e4");
        DropOn("d5");
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        Frame();
        ExpectOn(taker, "d5", "dropped on the square of the taken piece");
        ExpectOn(taken, "d5", "the taken piece waits there for the slide time, as before");
        Frames(kStepsToEnd);
        ExpectOn(taker, "d5");
        ExpectHidden(taken);
    }

}  // namespace BECore::Chess::Tests
