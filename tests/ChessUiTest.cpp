#include "ChessTestUtil.h"
#include "HudWords.h"
#include "TestEngines.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/ViewTransform.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UILabel.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ChessPieceComponent.h>
#include <Chess/Engine/OpponentText.h>
#include <Chess/Game/MoveGenerator.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Common/UILayoutMatrix.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Games/GameManager.h>
#include <chrono>

/**
 * The shipped scene of Chess with its HUD: the file is read from the source tree (a game's content is not mounted in the test environment), the game is started
 * through GameManager like the executable does, and the screen is drawn into a RecordingRenderer. Needs the engine services.
 */
namespace BECore::Chess::Tests {

    using namespace BECore::Tests;

    namespace {

        constexpr float kViewWidth = 840.0f;
        constexpr float kViewHeight = 800.0f;
        constexpr float kBoardTop = 80.0f;      // in view units
        constexpr float kBoardBottom = 720.0f;  // 80 + 8 * 80
        const Color kResetColor{40, 90, 60, 255};
        const Color kLevelColor{45, 75, 120, 255};  // the fills of the buttons of the HUD (SceneConfig.xml): a color each, so a test can tell where each one is drawn
        const Color kSideColor{110, 75, 40, 255};
        constexpr float kMinGap = 8.0f;             // two controls of the HUD keep this much between them, in view units
        constexpr float kButtonAir = 8.0f;          // the text of a button keeps this much from each edge of its fill
        constexpr float kNarrowestWidth = 480.0f;   // the narrowest window of the size matrix, in pixels
        constexpr float kNarrowestHeight = 853.0f;  // 9:16 at that width

        // The colors the board fills a full square with (ChessBoardComponent.cpp)
        const Color kSelectedColor{120, 200, 120, 200};
        const Color kLegalMoveColor{120, 200, 120, 120};
        const Color kCheckColor{230, 60, 60, 210};

        // The tall cell of a pawn in the piece atlas (TextureLibrary.xml, assets/chess_test.png is 700 x 381).
        // The library is empty in the test environment, so the test gives a sprite its cell.
        constexpr uint32_t kAtlasWidth = 700;
        constexpr uint32_t kAtlasHeight = 381;
        constexpr float kCellX = 583.33f;
        constexpr float kCellWidth = 116.67f;
        constexpr float kCellHeight = 190.5f;
        constexpr size_t kPieceCount = 32;
        constexpr float kPixelTolerance = 0.01f;

        constexpr int kManyFrames = 200;                            // frames that must pass with nothing happening
        constexpr std::chrono::milliseconds kShutdownBudget{3000};  // a gated engine gives up after 10 s (kGateGiveUp): a shutdown that waited for it would not fit

        std::filesystem::path ShippedSceneConfig() {
            return std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "SceneConfig.xml";
        }

        SDL_MouseButtonEvent Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

        bool Play(ChessGameModel& model, eastl::string_view uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        bool SameColor(const Color& left, const Color& right) {
            return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
        }

        bool SameRect(const Rect& left, const Rect& right) {
            return std::abs(left.x - right.x) <= kPixelTolerance && std::abs(left.y - right.y) <= kPixelTolerance && std::abs(left.w - right.w) <= kPixelTolerance &&
                   std::abs(left.h - right.h) <= kPixelTolerance;
        }

        /// The two rects are closer to each other than @p gap (or overlap)
        bool Touch(const Rect& left, const Rect& right, float gap) {
            return left.x < right.x + right.w + gap && right.x < left.x + left.w + gap && left.y < right.y + right.h + gap && right.y < left.y + left.h + gap;
        }

        /// The text a label or a button shows (UIElement::Cast is an exact match, and a button is a label)
        eastl::string ShownText(const UIElement& element) {
            const UILabel* label = element.Cast<UILabel>();
            if (!label) {
                label = element.Cast<UIButton>();
            }
            if (!label) {
                return {};
            }
            const eastl::string_view shown = label->GetDisplayText();
            return eastl::string(shown.data(), shown.size());
        }

        /// The rect lies inside the other one and both have the same middle
        ::testing::AssertionResult IsCenteredIn(const Rect& inner, const Rect& outer) {
            const bool inside = inner.x >= outer.x - kPixelTolerance && inner.y >= outer.y - kPixelTolerance && inner.x + inner.w <= outer.x + outer.w + kPixelTolerance &&
                                inner.y + inner.h <= outer.y + outer.h + kPixelTolerance;
            const float offsetX = (inner.x + inner.w * 0.5f) - (outer.x + outer.w * 0.5f);
            const float offsetY = (inner.y + inner.h * 0.5f) - (outer.y + outer.h * 0.5f);
            if (inside && std::abs(offsetX) <= kPixelTolerance && std::abs(offsetY) <= kPixelTolerance) {
                return ::testing::AssertionSuccess();
            }
            return ::testing::AssertionFailure() << "rect (" << inner.x << ", " << inner.y << ", " << inner.w << ", " << inner.h << ") is not centered in (" << outer.x << ", " << outer.y << ", "
                                                 << outer.w << ", " << outer.h << ")";
        }

    }  // namespace

    class ChessUiTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
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
            EngineEnvironment::UseSceneManager(_manager);  // GameManager switches to the scene of the game through CoreManager

            GameManager::GetInstance().Initialize();
            ASSERT_NE(ChessGame::GetActiveModel(), nullptr) << "Chess is the active game";
        }

        void TearDown() override {
            GameManager::GetInstance().Shutdown();
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
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

        IntrusivePtr<UICanvasComponent> Hud() const {
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern("Hud")) {
                    return node->GetComponent<UICanvasComponent>();
                }
            }
            return {};
        }

        UILabel& Label(const char* name) const {
            UIElement* element = Hud()->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            UILabel* label = element ? element->Cast<UILabel>() : nullptr;
            EXPECT_NE(label, nullptr) << name;
            return *label;
        }

        /// An element of the HUD by name, null when there is none
        UIElement* HudElement(const char* name) const {
            return Hud()->GetRoot().FindByName(PoolString::Intern(name));
        }

        /// The context the HUD is laid out in: the view of the scene, and nothing of the window (a case of the matrix sets that)
        UILayoutContext ViewContext() const {
            UILayoutContext context;
            context.sceneViewWidth = GetScene().GetViewWidth();
            context.sceneViewHeight = GetScene().GetViewHeight();
            return context;
        }

        /// The opponent of the game that is running
        OpponentController& Opponent() const {
            OpponentController* opponent = ChessGame::GetActiveOpponent();
            EXPECT_NE(opponent, nullptr);
            return *opponent;
        }

        /// The text of a UI variable after a frame was drawn: the board publishes the words of the HUD when it draws
        eastl::string Var(const char* name) {
            Draw();
            const eastl::string_view value = CoreManager::GetUIVars().Get(PoolString::Intern(name));
            return eastl::string(value.data(), value.size());
        }

        /// What a button of the HUD does when it is clicked: the action it emits
        static void Press(const char* action) {
            InputEvents::ActionEvent::Emit(PoolString::Intern(action));
        }

        /// A press of the mouse on the middle of a square, where it is drawn: the way a click reaches the board (the output size it is mapped back from is the one of the last draw)
        void Click(const char* square) {
            Draw();
            const auto at = SquareCenter(square);
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.first, at.second, true));
        }

        /// A move of the person: a click on the piece, a click on the target
        void ClickMove(const char* from, const char* to) {
            Click(from);
            Click(to);
        }

        /// What a game loop does: one frame (the update of every scene) after the other until @p done, or until the time limit; false when it ran out
        template <typename Condition>
        bool FramesUntil(Condition done) {
            return WaitUntil([&] {
                _manager->UpdateAll();
                return done();
            });
        }

        /// Frames in which nothing is meant to happen
        void IdleFrames(int count = kManyFrames) {
            for (int frame = 0; frame < count; ++frame) {
                _manager->UpdateAll();
            }
        }

        /// The program plays Black with an engine that fails (the factory made it, so the pick succeeds), the person moves e2e4, and the failure of the first search stands
        void ProvokeAnEngineError(CountingFactory& factory) {
            Opponent().SetEngineFactory(factory.AsFactory());
            _manager->UpdateAll();
            ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
            ClickMove("e2", "e4");
            ASSERT_EQ(Model().GetMoves().size(), 1u);
            ASSERT_TRUE(FramesUntil([&] { return Opponent().GetError().has_value(); })) << "the engine was asked and failed";
            ASSERT_EQ(Opponent().GetError()->kind, EngineErrorKind::LaunchFailed);
        }

        void Draw() {
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        /// The middle, in output pixels, of the fill that a button was drawn with in the last frame; none when no fill of that color was drawn (a hidden button draws nothing)
        std::optional<std::pair<float, float>> DrawnCenter(const Color& fill) const {
            const auto fills = RectCalls(fill);
            if (fills.empty()) {
                return std::nullopt;
            }
            const Rect& rect = fills.back()->rect;
            return std::pair{rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
        }

        /// A click the way a mouse makes it, on the button that is drawn with this fill: a press and a release. False when no such button is on the screen or the HUD did not take both
        bool ClickButton(const Color& fill) {
            Draw();
            const auto at = DrawnCenter(fill);
            if (!at) {
                return false;
            }
            const bool pressed = CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at->first, at->second, true));
            const bool released = CoreManager::GetUIInput().DispatchMouseButtonUp(Button(at->first, at->second, false));
            return pressed && released;
        }

        /// Lays the canvas out on every case of the size matrix and calls @p check(label, layout) after each. A SceneView canvas ignores the scale mode and the user scale, so one
        /// run per case is all there is; the layout is left to be laid out again afterwards
        template <typename Check>
        void OnEveryCaseOfTheMatrix(UICanvasLayout& layout, Check check) const {
            for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
                UILayoutContext context = ViewContext();
                context.outputWidth = matrixCase.context.outputWidth;
                context.outputHeight = matrixCase.context.outputHeight;
                context.safeArea = matrixCase.context.safeArea;
                layout.Update(context);
                check(matrixCase.label, layout);
            }
            layout.Invalidate();
        }

        /// What is wrong with the HUD as it is laid out, one problem to a line, nothing when it is right: text that its control cuts, and two controls closer than kMinGap.
        /// The validator of the matrix does not see the first (a label with an ellipsis "can shrink", so TextDoesNotFit is not reported), which is why it is checked here, on the
        /// text that is shown; a button must also keep kButtonAir between its text and the edge of the fill. A hidden control has an empty rect and counts for nothing
        static eastl::string HudProblems(const UICanvasLayout& layout) {
            eastl::string problems;
            const float scale = layout.GetResolved().scale;
            const auto& elements = layout.root->children;
            for (size_t i = 0; i < elements.size(); ++i) {
                const UIElement& element = *elements[i];
                if (element.rect.IsEmpty()) {
                    continue;
                }
                const float air = element.interactive ? kButtonAir : 0.0f;
                const UIContentFit fit = element.CheckContent(element.rect.w - 2.0f * air, element.rect.h, scale);
                if (fit.hasContent && !fit.fits) {
                    problems +=
                        Format("{} cuts '{}': {:.1f} units wide with {:.1f} of air each side, at scale {:.3f}\n", element.name.ToStringView(), ShownText(element).c_str(), element.rect.w, air, scale);
                }
                for (size_t j = i + 1; j < elements.size(); ++j) {
                    const UIElement& other = *elements[j];
                    if (!other.rect.IsEmpty() && Touch(element.rect, other.rect, kMinGap)) {
                        problems += Format("{} and {} are closer than {:.0f} units\n", element.name.ToStringView(), other.name.ToStringView(), kMinGap);
                    }
                }
            }
            return problems;
        }

        /// The HUD with two people playing or with the program as the opponent, as the board leaves it after a frame, through the size matrix with every word it can show: the
        /// validator's findings, and HudProblems()
        void ExpectTheHudSurvivesTheMatrix(bool programPlays) {
            constexpr size_t kReportedFailures = 4;
            CountingFactory factory({eastl::make_shared<InstantEngine>()});
            Opponent().SetEngineFactory(factory.AsFactory());
            _manager->UpdateAll();
            if (programPlays) {
                Press("ChessCycleSide");
            }
            Draw();  // the board publishes the words and shows or hides Level

            auto canvas = Hud();
            ASSERT_TRUE(canvas);
            const UIElement* level = canvas->GetRoot().FindByName(PoolString::Intern("Level"));
            ASSERT_NE(level, nullptr);
            ASSERT_EQ(level->style.visible, programPlays);
            canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update

            const UILayoutContext base = ViewContext();
            ASSERT_FLOAT_EQ(base.sceneViewWidth, kViewWidth);
            ASSERT_FLOAT_EQ(base.sceneViewHeight, kViewHeight);
            UILayoutValidatorOptions options;
            options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
            const HudWords words = AllHudWords();
            ASSERT_GE(words.Rounds(), 2u);

            size_t failed = 0;
            eastl::string report;
            for (size_t round = 0; round < words.Rounds(); ++round) {
                words.Publish(round);
                const auto failures = ValidateOnMatrix(canvas->GetLayout(), options, base);
                if (!failures.empty() && ++failed <= kReportedFailures) {
                    report += Format("round {}: {}", round, DescribeMatrixFailures(failures).c_str());
                }
                OnEveryCaseOfTheMatrix(canvas->GetLayout(), [&](const eastl::string& label, const UICanvasLayout& layout) {
                    eastl::string problems = HudProblems(layout);
                    if (level->rect.IsEmpty() == programPlays) {
                        problems += programPlays ? "Level is hidden while the program plays\n" : "Level is shown while two people play\n";
                    }
                    if (!problems.empty() && ++failed <= kReportedFailures) {
                        report += Format("round {}, {}:\n{}", round, label.c_str(), problems.c_str());
                    }
                });
            }
            EXPECT_EQ(failed, 0u) << report.c_str();
        }

        void DrawAt(float width, float height) {
            _renderer->width = width;
            _renderer->height = height;
            Draw();
        }

        /// A square of the board in output pixels, for the fit of the view into the current output
        Rect SquareRect(const char* square) const {
            const Square s = Sq(square);
            const ViewTransform fit = ViewTransform::Fit(kViewWidth, kViewHeight, _renderer->width, _renderer->height);
            return fit.ToOutput(100.0f + static_cast<float>(s.file) * 80.0f, kBoardTop + static_cast<float>(7 - s.rank) * 80.0f, 80.0f, 80.0f);
        }

        /// The middle of a square of the board in output pixels
        std::pair<float, float> SquareCenter(const char* square) const {
            const Rect cell = SquareRect(square);
            return {cell.x + cell.w * 0.5f, cell.y + cell.h * 0.5f};
        }

        /// The sprite of a piece node by the node's name ("WhitePawn_e2"), null when the scene has no such piece
        IntrusivePtr<SpriteRendererComponent> PieceSprite(const char* nodeName) const {
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(nodeName)) {
                    return node->GetComponent<SpriteRendererComponent>();
                }
            }
            return {};
        }

        /// Gives the sprite of a piece the texture and the cell of the atlas that the library gives it in the game (the library is empty here), and returns the texture
        IntrusivePtr<ITexture> GiveAtlasCell(const char* nodeName) const {
            auto sprite = PieceSprite(nodeName);
            EXPECT_TRUE(sprite) << nodeName;
            const eastl::vector<uint8_t> pixels(static_cast<size_t>(kAtlasWidth) * kAtlasHeight * 4, 0);
            IntrusivePtr<ITexture> texture = _renderer->CreateTexture(pixels.data(), kAtlasWidth, kAtlasHeight);
            if (sprite) {
                sprite->SetTexture(texture);
                sprite->_srcRect = Rect{kCellX, 0.0f, kCellWidth, kCellHeight};
            }
            return texture;
        }

        /// The recorded call that drew the texture, null when none did
        const DrawCall* TextureCall(const ITexture& texture) const {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.texture == &texture) {
                    return &call;
                }
            }
            return nullptr;
        }

        /// The recorded rect fills of one color, in drawing order
        eastl::vector<const DrawCall*> RectCalls(const Color& color) const {
            eastl::vector<const DrawCall*> found;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && SameColor(call.color, color)) {
                    found.push_back(&call);
                }
            }
            return found;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    // ---- the screen -------------------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, TheHudPassesTheSizeMatrixWithTheLongestWordsWhileTwoPeoplePlay) {
        ExpectTheHudSurvivesTheMatrix(false);
    }

    TEST_F(ChessUiTest, TheHudPassesTheSizeMatrixWithTheLongestWordsWhileTheProgramPlays) {
        ExpectTheHudSurvivesTheMatrix(true);
    }

    TEST_F(ChessUiTest, NoWordOfTheHudIsCutInTheNarrowestWindow) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        Press("ChessCycleSide");  // all five controls are shown
        Draw();
        auto canvas = Hud();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);

        // 480 px wide, with the side insets of a phone held sideways: the narrowest the matrix has. The view is 840 units wide, so the scale is 0.57 and the safe area
        // about 689 units, which is what every row of the HUD has to fit in
        UILayoutContext context = ViewContext();
        context.outputWidth = kNarrowestWidth;
        context.outputHeight = kNarrowestHeight;
        context.safeArea = UIPreview::SimulatedSafeArea(SafeAreaSimulation::SideInsets, kNarrowestWidth, kNarrowestHeight);
        UICanvasLayout& layout = canvas->GetLayout();
        layout.Update(context);
        const float scale = kNarrowestWidth / kViewWidth;
        EXPECT_NEAR(layout.GetResolved().scale, scale, 0.01f);
        EXPECT_NEAR(layout.root->rect.w, kNarrowestWidth * (1.0f - 2.0f * UIPreview::kSideInsetShare) / scale, 0.5f) << "the safe area, in view units";

        const HudWords words = AllHudWords();
        for (size_t round = 0; round < words.Rounds(); ++round) {
            words.Publish(round);
            const eastl::string problems = HudProblems(layout);
            EXPECT_TRUE(problems.empty()) << "round " << round << ":\n" << problems.c_str();
        }
        for (const char* name : {"Turn", "Level", "Reset", "Side", "Status"}) {
            const UIElement* element = layout.root->FindByName(PoolString::Intern(name));
            ASSERT_NE(element, nullptr) << name;
            EXPECT_FALSE(element->rect.IsEmpty()) << name << " is shown";
        }
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, TheHudStaysOutsideTheBoardInEveryShape) {
        auto canvas = Hud();
        ASSERT_TRUE(canvas);
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        Press("ChessCycleSide");  // Level is shown only while the program plays

        for (const auto& size : {std::pair{1280.0f, 720.0f}, std::pair{720.0f, 1280.0f}, std::pair{1024.0f, 768.0f}, std::pair{800.0f, 800.0f}, std::pair{2560.0f, 1080.0f}}) {
            DrawAt(size.first, size.second);
            for (const char* name : {"Turn", "Level", "Reset"}) {
                const UIElement* element = canvas->GetRoot().FindByName(PoolString::Intern(name));
                ASSERT_NE(element, nullptr);
                ASSERT_FALSE(element->rect.IsEmpty()) << name << " is shown at " << size.first << "x" << size.second;
                EXPECT_LE(element->rect.y + element->rect.h, kBoardTop + 0.01f) << name << " at " << size.first << "x" << size.second;
            }
            for (const char* name : {"Side", "Status"}) {
                const UIElement* element = canvas->GetRoot().FindByName(PoolString::Intern(name));
                ASSERT_NE(element, nullptr);
                ASSERT_FALSE(element->rect.IsEmpty()) << name << " is shown at " << size.first << "x" << size.second;
                EXPECT_GE(element->rect.y, kBoardBottom - 0.01f) << name << " at " << size.first << "x" << size.second;
            }
        }
    }

    // ---- the words --------------------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, TheHudSaysWhoseMoveItIs) {
        Draw();
        EXPECT_EQ(Label("Turn").GetDisplayText(), "White to move");
        EXPECT_EQ(Label("Status").GetDisplayText(), "");

        ASSERT_TRUE(Play(Model(), "e2e4"));
        Draw();
        EXPECT_EQ(Label("Turn").GetDisplayText(), "Black to move");
    }

    TEST_F(ChessUiTest, TheHudNamesACheckAndTheEnd) {
        Model().SetPosition(PositionFromFen("4k3/4r3/8/8/8/8/8/4K3 w - - 0 1"));
        Draw();
        EXPECT_EQ(Label("Status").GetDisplayText(), "Check");

        Model().SetPosition(PositionFromFen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"));
        ASSERT_TRUE(Play(Model(), "a1a8"));
        Draw();
        EXPECT_EQ(Label("Turn").GetDisplayText(), "White wins");
        EXPECT_EQ(Label("Status").GetDisplayText(), "Checkmate");
    }

    TEST_F(ChessUiTest, TheWordsAreDrawnAsText) {
        Draw();

        size_t glyphsInTurn = 0;
        const UILabel& turn = Label("Turn");
        const ViewTransform fit = ViewTransform::Fit(kViewWidth, kViewHeight, _renderer->width, _renderer->height);
        const Rect box = fit.ToOutput(turn.rect.x, turn.rect.y, turn.rect.w, turn.rect.h);
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

        // The view is 840 x 800: 1280 x 720 and 720 x 1280 are not its shape, so the view transform is a scale and a move
        for (const auto& size : {std::pair{1280.0f, 720.0f}, std::pair{720.0f, 1280.0f}, std::pair{2560.0f, 1080.0f}}) {
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

        for (const auto& size : {std::pair{1280.0f, 720.0f}, std::pair{720.0f, 1280.0f}}) {
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

    // ---- the Reset button -------------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, TheResetActionStartsANewGame) {
        ASSERT_TRUE(Play(Model(), "e2e4"));
        const uint64_t before = Model().GetVersion();

        InputEvents::ActionEvent::Emit(PoolString::Intern("ChessReset"));

        EXPECT_EQ(Model().GetPosition().ToFen(), eastl::string(Position::kStartFen));
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
        EXPECT_NE(Model().GetPosition().ToFen(), eastl::string(Position::kStartFen)) << "a press alone does nothing";
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false)));

        EXPECT_EQ(Model().GetPosition().ToFen(), eastl::string(Position::kStartFen));
    }

    TEST_F(ChessUiTest, TheBoardStillTakesItsClicksUnderTheCanvas) {
        Draw();
        const auto from = SquareCenter("e2");
        const auto to = SquareCenter("e4");

        // The canvas does not consume a click that lands on no control; UIInput then gives it to the game, so the board gets it (the model selects, then moves)
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(from.first, from.second, true)));
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(to.first, to.second, true)));

        EXPECT_EQ(Model().GetPosition().ToFen(), eastl::string("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"));
    }

    // ---- the opponent -----------------------------------------------------------------------------------------------------
    // The engines are the doubles of TestEngines.h, given to the controller of the running game by SetEngineFactory: no child process, and a Stockfish on the PATH of
    // this machine is never looked for. The scene is the shipped one, so a click is a click on the board and a frame is the update of every component

    TEST_F(ChessUiTest, WithNobodyAsTheOpponentTheGamePlaysAsTwoPeople) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        ClickMove("e2", "e4");
        IdleFrames();
        EXPECT_EQ(Model().GetMoves().size(), 1u) << "black is a person: nothing answers";
        ClickMove("e7", "e5");
        IdleFrames();

        EXPECT_EQ(Model().GetMoves().size(), 2u);
        EXPECT_EQ(Model().GetTurn(), PieceColor::White);
        EXPECT_FALSE(Opponent().GetPlays().has_value());
        EXPECT_FALSE(Opponent().IsThinking());
        EXPECT_FALSE(Opponent().GetError().has_value());
        EXPECT_EQ(Opponent().GetRevision(), 0u) << "the start of the game picked nobody and changed nothing";
        EXPECT_EQ(factory.Calls(), 0) << "nothing is looked up until a side is picked";
    }

    TEST_F(ChessUiTest, TheProgramAnswersAPersonsMoveWithinAFewFrames) {
        const auto engine = eastl::make_shared<InstantEngine>();
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());

        IdleFrames();
        EXPECT_EQ(engine->Calls(), 0) << "white is the person's: the program waits";

        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));
        EXPECT_EQ(Model().GetMoves()[0].ToUci(), "e2e4");
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[1].To()).color, PieceColor::Black) << "the program's move is a black one";
        EXPECT_EQ(Model().GetTurn(), PieceColor::White);
        EXPECT_FALSE(Opponent().IsThinking());

        ClickMove("d2", "d4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 4; }));
        EXPECT_EQ(engine->Calls(), 2) << "one search per move of the person";
        EXPECT_EQ(factory.Calls(), 1);
        EXPECT_FALSE(Opponent().GetError().has_value());
    }

    TEST_F(ChessUiTest, WhileTheProgramThinksAClickOnTheBoardDoesNothing) {
        const auto engine = eastl::make_shared<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; })) << "the program has the search and waits at its gate";
        ASSERT_TRUE(Opponent().IsThinking());
        const uint64_t version = Model().GetVersion();

        Click("e7");  // a black piece, and it is black's turn: the model would select it for a person
        EXPECT_FALSE(Model().HasSelection());
        Click("e5");
        Click("d2");
        Click("a4");
        IdleFrames();

        EXPECT_FALSE(Model().HasSelection());
        EXPECT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetVersion(), version) << "the clicks did not move the game, so the search is not stale";
        EXPECT_TRUE(Opponent().IsThinking());
        EXPECT_FALSE(engine->WasCancelled());

        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the answer to the search the clicks left alone";
        Click("d2");
        EXPECT_TRUE(Model().HasSelection()) << "white is the person's again: the board takes clicks";
    }

    TEST_F(ChessUiTest, AfterTheProgramsMateAClickStartsANewGameAndTheProgramMovesAgain) {
        CountingFactory factory({eastl::make_shared<MatingEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        Model().SetPosition(PositionFromFen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"));
        ASSERT_TRUE(Opponent().SetSide(PieceColor::White).has_value());

        ASSERT_TRUE(FramesUntil([&] { return Model().IsGameOver(); }));
        EXPECT_EQ(Model().GetStatus(), GameStatus::Checkmate);
        EXPECT_EQ(Model().GetWinner(), std::optional<PieceColor>(PieceColor::White));
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves()[0].ToUci(), "a1a8") << "the program's move";
        EXPECT_FALSE(Opponent().GetError().has_value());
        Draw();
        EXPECT_EQ(Label("Turn").GetDisplayText(), "White wins") << "the HUD says it, whoever played the mate";

        const uint64_t version = Model().GetVersion();
        Click("e4");  // the game is over, so this click is the "next game" one even though White is the program

        EXPECT_FALSE(Model().IsGameOver());
        EXPECT_GT(Model().GetVersion(), version);
        EXPECT_EQ(Model().GetPosition().ToFen(), eastl::string(Position::kStartFen));
        EXPECT_FALSE(Model().HasSelection());
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 1; })) << "the new game opens with the move of the program, which plays White";
        EXPECT_EQ(Model().GetTurn(), PieceColor::Black);
        EXPECT_FALSE(Model().IsGameOver());
    }

    TEST_F(ChessUiTest, AResetWhileTheProgramThinksDropsItsAnswerAndANewSearchFollowsOnItsTurn) {
        const auto engine = eastl::make_shared<GatedEngine>(false);  // ignores its token: its answer comes late and must still be thrown away
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        ASSERT_TRUE(Opponent().IsThinking());

        InputEvents::ActionEvent::Emit(PoolString::Intern("ChessReset"));  // the Reset button
        _manager->UpdateAll();

        EXPECT_FALSE(Opponent().IsThinking()) << "the version moved: the search is not wanted any more";
        engine->Open();
        ASSERT_TRUE(WaitUntil([&] { return engine->Answered() == 1; })) << "the stale search returns its move";
        IdleFrames();
        EXPECT_TRUE(Model().GetMoves().empty()) << "and the move is never played";
        EXPECT_EQ(Model().GetPosition().ToFen(), eastl::string(Position::kStartFen));
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
        const auto engine = eastl::make_shared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
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
        const auto engine = eastl::make_shared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));

        InputEvents::ActionEvent::Emit(PoolString::Intern("ChessReset"));
        _manager->UpdateAll();

        EXPECT_FALSE(Opponent().GetError().has_value());
        EXPECT_TRUE(Model().GetMoves().empty());
        ClickMove("e2", "e4");
        EXPECT_EQ(Model().GetMoves().size(), 1u) << "the person plays again";
        ASSERT_TRUE(FramesUntil([&] { return Opponent().GetError().has_value(); })) << "the program is still Black and fails again: the person acted, so it was asked again";
        EXPECT_EQ(engine->Calls(), 2);
    }

    TEST_F(ChessUiTest, ChangingTheSideLetsThePersonOutOfAnEngineErrorAndNobodyIsTheWayOutForGood) {
        const auto engine = eastl::make_shared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
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

        ASSERT_TRUE(Opponent().CycleSide().has_value());  // nobody

        EXPECT_FALSE(Opponent().GetError().has_value());
        EXPECT_FALSE(Opponent().GetPlays().has_value());
        ClickMove("d2", "d4");
        ClickMove("d7", "d5");
        IdleFrames();
        EXPECT_EQ(Model().GetMoves().size(), 4u) << "two people on one screen again";
        EXPECT_EQ(engine->Calls(), 2);
    }

    TEST_F(ChessUiTest, ChangingTheLevelLetsThePersonOutOfAnEngineErrorAndAsksTheNewEngineOnce) {
        const auto engine = eastl::make_shared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
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
        const auto engine = eastl::make_shared<GatedEngine>(true);
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

    TEST_F(ChessUiTest, ShuttingTheGameDownWithASearchInFlightReturnsAtOnce) {
        const auto engine = eastl::make_shared<GatedEngine>(false);  // never looks at its token: only the gate can end its search
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        ASSERT_TRUE(Opponent().IsThinking());

        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        GameManager::GetInstance().Shutdown();  // Stop() destroys the opponent, as Application::Cleanup() does before the task system goes down
        const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - start;

        EXPECT_LT(elapsed, kShutdownBudget);
        EXPECT_EQ(ChessGame::GetActive(), nullptr);
        EXPECT_EQ(engine->Answered(), 0) << "the search is still at its gate: nothing waited for it";
        EXPECT_EQ(engine->Calls(), 1);
        _manager->UpdateAll();  // a frame after the game is gone asks nothing
        EXPECT_EQ(engine->Calls(), 1);

        engine->Open();  // frees the worker: the engine is alive as long as the search needs it, and the test does not leave a thread behind
        EXPECT_TRUE(WaitUntil([&] { return engine->Answered() == 1; }));
    }

    // ---- the words and the actions of the opponent ------------------------------------------------------------------------
    // The HUD shows `side`, `level`, `turn` and `status`; the layout of its buttons is another matter, so these tests press the actions the buttons emit and read the
    // variables the board publishes. The engines are the doubles of TestEngines.h

    TEST_F(ChessUiTest, TheOpponentWordsStartWithTwoPlayersAtTheMediumLevel) {
        EXPECT_EQ(Var("side"), "Two players");
        EXPECT_EQ(Var("level"), "Level: Medium");
        EXPECT_EQ(Var("turn"), "White to move");
        EXPECT_EQ(Var("status"), "");
    }

    TEST_F(ChessUiTest, TheSideActionWalksThreeWordsAndBackAndTheProgramPlaysTheSideTheWordSays) {
        const auto engine = eastl::make_shared<InstantEngine>();
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        Press("ChessCycleSide");
        EXPECT_EQ(Var("side"), "You: White");
        EXPECT_TRUE(Opponent().PlaysFor(PieceColor::Black));
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[1].To()).color, PieceColor::Black) << "the person has White, the program answers with a black piece";

        Press("ChessCycleSide");
        EXPECT_EQ(Var("side"), "You: Black");
        EXPECT_TRUE(Opponent().PlaysFor(PieceColor::White));
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 3; })) << "it is White's turn and White is the program's now";
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[2].To()).color, PieceColor::White);

        Press("ChessCycleSide");
        EXPECT_EQ(Var("side"), "Two players");
        EXPECT_FALSE(Opponent().PlaysFor(PieceColor::White));
        EXPECT_FALSE(Opponent().PlaysFor(PieceColor::Black));
        const MoveList legal = MoveGenerator::GenerateLegal(Model().GetPosition());
        ASSERT_FALSE(legal.empty());
        ASSERT_TRUE(Model().TryMove(legal.front()).has_value()) << "Black's move, made by a person";
        IdleFrames();
        EXPECT_EQ(Model().GetMoves().size(), 4u) << "nobody answers it";
        EXPECT_EQ(engine->Calls(), 2) << "one search for each of the two sides, none for two people";

        Press("ChessCycleSide");
        EXPECT_EQ(Var("side"), "You: White") << "and round again";
        EXPECT_EQ(factory.Calls(), 3) << "a pick for each side, and none for nobody";
    }

    TEST_F(ChessUiTest, TheLevelActionWalksTheLevelsAndWraps) {
        const auto levels = GetOpponentLevels();
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        for (size_t step = 1; step <= levels.size(); ++step) {
            Press("ChessCycleLevel");
            EXPECT_EQ(Var("level"), OpponentText::LevelLabel(levels[(kDefaultOpponentLevel + step) % levels.size()])) << "step " << step;
        }
        EXPECT_EQ(Var("level"), "Level: Medium") << "after the last level comes the first of the walk again";
        EXPECT_EQ(Opponent().GetLevel(), kDefaultOpponentLevel);
        EXPECT_EQ(factory.Calls(), 0) << "with nobody as the opponent a level is only a choice: no engine is asked for";

        Press("ChessCycleSide");
        Press("ChessCycleLevel");
        EXPECT_EQ(Var("level"), OpponentText::LevelLabel(levels[(kDefaultOpponentLevel + 1) % levels.size()]));
        ASSERT_EQ(factory.Calls(), 2) << "one engine for the pick, another for the level";
        EXPECT_EQ(factory.Levels()[0], levels[kDefaultOpponentLevel].id);
        EXPECT_EQ(factory.Levels()[1], levels[(kDefaultOpponentLevel + 1) % levels.size()].id);
    }

    TEST_F(ChessUiTest, WhileTheProgramThinksTheTurnSaysSoAndTheAnswerBringsTheUsualWordsBack) {
        const auto engine = eastl::make_shared<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        Press("ChessCycleSide");
        ASSERT_TRUE(Opponent().PlaysFor(PieceColor::Black));

        ClickMove("e2", "e4");
        EXPECT_EQ(Var("turn"), "Black to move") << "the person has moved, the search has not started yet";
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        ASSERT_TRUE(Opponent().IsThinking());
        const uint64_t version = Model().GetVersion();

        EXPECT_EQ(Var("turn"), "Computer is thinking") << "the search began without a move of the game: the words follow the search, not only the version";
        EXPECT_EQ(Model().GetVersion(), version);

        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));
        EXPECT_FALSE(Opponent().IsThinking());
        EXPECT_EQ(Var("turn"), "White to move");
    }

    TEST_F(ChessUiTest, ThePickOfAnEngineThatIsNotFoundIsSaidInTheStatusAndTheNextMoveTakesItAway) {
        CountingFactory factory(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        Press("ChessCycleSide");

        EXPECT_EQ(Var("status"), "Engine not found");
        EXPECT_EQ(Var("side"), "Two players") << "a pick that found nothing leaves two people on the screen";
        EXPECT_EQ(Var("turn"), "White to move");

        ClickMove("e2", "e4");
        _manager->UpdateAll();

        EXPECT_EQ(Var("status"), "") << "the person has played on: the sentence has been seen";
        EXPECT_EQ(Var("side"), "Two players");
        EXPECT_EQ(factory.Calls(), 1);
    }

    TEST_F(ChessUiTest, TheSentenceOfASearchErrorIsInTheStatusUntilSomeoneMovesTheGame) {
        const auto engine = eastl::make_shared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));

        EXPECT_EQ(Var("status"), "Engine not found");
        EXPECT_EQ(Var("side"), "You: White") << "the program is still Black, only the game waits";
        IdleFrames();
        EXPECT_EQ(Var("status"), "Engine not found") << "frames do not take it away";

        ASSERT_TRUE(Play(Model(), "e7e5")) << "a move of someone else, the way the control API plays";
        _manager->UpdateAll();

        EXPECT_EQ(Var("status"), "");
        EXPECT_EQ(Var("turn"), "White to move");
    }

    TEST_F(ChessUiTest, TheSentenceOfASearchErrorWinsOverCheckAndTheResetButtonTakesItAway) {
        const auto engine = eastl::make_shared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        Model().SetPosition(PositionFromFen("4k3/8/8/8/8/8/4R3/4K3 b - - 0 1"));  // Black, the program's side, is in check
        ASSERT_TRUE(Model().IsInCheck());
        ASSERT_TRUE(FramesUntil([&] { return Opponent().GetError().has_value(); }));

        EXPECT_EQ(Var("status"), "Engine not found") << "the game is stalled by the error: it is what the person has to read, not 'Check'";
        EXPECT_TRUE(Model().IsInCheck());

        Press("ChessReset");
        _manager->UpdateAll();

        EXPECT_EQ(Var("status"), "");
        EXPECT_EQ(Var("turn"), "White to move");
    }

    TEST_F(ChessUiTest, TheLevelButtonIsShownOnlyWhileTheProgramPlays) {
        const UIElement* level = HudElement("Level");
        ASSERT_NE(level, nullptr) << "the HUD of the shipped scene has the Level button";
        CountingFactory failing(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
        CountingFactory working({eastl::make_shared<InstantEngine>()});
        _manager->UpdateAll();

        Draw();
        EXPECT_FALSE(level->style.visible) << "the first publication: nobody plays";
        EXPECT_TRUE(level->rect.IsEmpty()) << "a hidden button has no place";
        EXPECT_FALSE(DrawnCenter(kLevelColor).has_value()) << "and draws nothing";

        Opponent().SetEngineFactory(working.AsFactory());
        Press("ChessCycleSide");
        Draw();
        EXPECT_TRUE(level->style.visible) << "the program plays Black";
        EXPECT_FALSE(level->rect.IsEmpty());
        const auto where = DrawnCenter(kLevelColor);
        ASSERT_TRUE(where.has_value()) << "and it is drawn";
        Press("ChessCycleSide");
        Draw();
        EXPECT_TRUE(level->style.visible) << "the program plays White";
        EXPECT_FALSE(level->rect.IsEmpty());
        Press("ChessCycleSide");
        Draw();
        EXPECT_FALSE(level->style.visible) << "two people again";
        EXPECT_TRUE(level->rect.IsEmpty());
        EXPECT_FALSE(DrawnCenter(kLevelColor).has_value());

        // Where the button was drawn there is nothing to press now: the HUD takes neither the press nor the release, and the level stays
        const size_t levelBefore = Opponent().GetLevel();
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(where->first, where->second, true)));
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(where->first, where->second, false)));
        EXPECT_EQ(Opponent().GetLevel(), levelBefore);
        EXPECT_EQ(working.Calls(), 2) << "one engine for each pick of a side, none for the click on nothing";

        Opponent().SetEngineFactory(failing.AsFactory());
        Press("ChessCycleSide");
        Draw();
        EXPECT_FALSE(level->style.visible) << "a pick that found no engine leaves nobody: no strength to choose";
        EXPECT_TRUE(level->rect.IsEmpty());
    }

    // ---- the buttons of the opponent, clicked where they are drawn ---------------------------------------------------------------
    // The fill of each button has a color of its own in SceneConfig.xml, which is how a test finds where the screen shows it (the way ClickingTheResetButtonOnTheScreen... does)

    TEST_F(ChessUiTest, ClickingTheSideButtonOnTheScreenCyclesTheSide) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        Draw();
        ASSERT_EQ(Label("Turn").GetDisplayText(), "White to move");

        const auto at = DrawnCenter(kSideColor);
        ASSERT_TRUE(at.has_value()) << "the Side button is drawn while nobody is the opponent: it is how a side is picked";
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at->first, at->second, true))) << "the button takes the click";
        EXPECT_FALSE(Opponent().GetPlays().has_value()) << "a press alone does nothing";
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(at->first, at->second, false)));
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::Black));
        EXPECT_EQ(Var("side"), "You: White");

        EXPECT_TRUE(ClickButton(kSideColor));
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::White));
        EXPECT_EQ(Var("side"), "You: Black");

        EXPECT_TRUE(ClickButton(kSideColor));
        EXPECT_FALSE(Opponent().GetPlays().has_value());
        EXPECT_EQ(Var("side"), "Two players");
        EXPECT_TRUE(DrawnCenter(kSideColor).has_value()) << "the way back to a pick is still there";
    }

    TEST_F(ChessUiTest, TheSideButtonSaysWhatItDoesAndThePersonThenPlaysTheColorItNames) {
        const auto engine = eastl::make_shared<InstantEngine>();
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        ASSERT_TRUE(ClickButton(kSideColor));
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Side")), "You: White");
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the person has White and moved: the program answers";
        EXPECT_EQ(engine->Calls(), 1);
    }

    TEST_F(ChessUiTest, ClickingTheLevelButtonOnTheScreenCyclesTheLevel) {
        const auto levels = GetOpponentLevels();
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        Draw();
        EXPECT_FALSE(ClickButton(kLevelColor)) << "no button, no click: the level is a choice that means something once the program plays";
        EXPECT_EQ(Opponent().GetLevel(), kDefaultOpponentLevel);

        ASSERT_TRUE(ClickButton(kSideColor));  // the program plays Black
        for (size_t step = 1; step <= levels.size(); ++step) {
            EXPECT_TRUE(ClickButton(kLevelColor)) << "step " << step;
            const size_t expected = (kDefaultOpponentLevel + step) % levels.size();
            EXPECT_EQ(Opponent().GetLevel(), expected) << "step " << step;
            EXPECT_EQ(Var("level"), OpponentText::LevelLabel(levels[expected])) << "step " << step;
            EXPECT_EQ(ShownText(*HudElement("Level")), OpponentText::LevelLabel(levels[expected]));
        }
        EXPECT_EQ(Opponent().GetLevel(), kDefaultOpponentLevel) << "round again";
        EXPECT_EQ(factory.Calls(), 1 + static_cast<int>(levels.size())) << "an engine for the pick of the side and one for each new level";
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::Black)) << "the side stays";
    }

    TEST_F(ChessUiTest, TheResetButtonStillResetsWithTheOpponentButtonsAround) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(ClickButton(kSideColor));
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));

        EXPECT_TRUE(ClickButton(kResetColor));

        EXPECT_EQ(Model().GetPosition().ToFen(), eastl::string(Position::kStartFen));
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::Black)) << "a new game keeps the opponent";
    }

    TEST_F(ChessUiTest, ThePressOfAHudButtonDoesNotReachTheBoardAndTheBoardStillGetsItsOwnClicks) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(ClickButton(kSideColor));
        Draw();

        // A click on the board is not taken by the HUD (it has no control there) and the board selects the piece
        const auto onTheBoard = SquareCenter("e2");
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(onTheBoard.first, onTheBoard.second, true)));
        EXPECT_TRUE(Model().HasSelection()) << "the person has White: the board took the click";
        EXPECT_TRUE(Model().GetSelected() == Sq("e2"));

        // And a press on a button of the HUD is the HUD's: the selection is not changed by it
        EXPECT_TRUE(ClickButton(kLevelColor));
        EXPECT_TRUE(Model().HasSelection());
        EXPECT_TRUE(Model().GetSelected() == Sq("e2"));
    }

}  // namespace BECore::Chess::Tests
