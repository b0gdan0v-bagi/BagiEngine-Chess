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
#include <BECore/UI/UIScreenContent.h>
#include <BECore/UI/UISegmented.h>
#include <BECore/UI/UIStack.h>
#include <Chess/ChessGame.h>
#include <Chess/ChessGameControl.h>
#include <Chess/Components/ChessPieceComponent.h>
#include <Chess/Engine/OpponentChoice.h>
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
        const Color kOpponentColor{45, 75, 120, 255};  // the fills of the buttons of the HUD (SceneConfig.xml): a color each, so a test can tell where each one is drawn
        const Color kSideColor{110, 75, 40, 255};
        const Color kEngineColor{80, 60, 115, 255};
        const Color kPlateColor{36, 40, 52, 255};   // the plate of the opponent panel
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

        constexpr float kSegmentHeight = 56.0f;  // a touch target at the smallest fit scale of the matrix (0.571)
        constexpr size_t kPanelSegments = 10;    // two modes, two colors, two engines, four levels
        constexpr size_t kReportedCases = 4;     // how many cases of a matrix a failure message lists

        /// The actions that the segments of the panel emit, and that ChessBoardComponent answers
        constexpr const char* kSetModeAction = "ChessSetMode";
        constexpr const char* kSetColorAction = "ChessSetColor";
        constexpr const char* kSetEngineAction = "ChessSetEngine";
        constexpr const char* kSetLevelAction = "ChessSetLevel";

        /// Replaces the first @p what in @p text and fails the test when it is not there (a broken copy that changed nothing would prove nothing)
        void Replace(std::string& text, const std::string& what, const std::string& with) {
            const size_t at = text.find(what);
            ASSERT_NE(at, std::string::npos) << what;
            text.replace(at, what.size(), with);
        }

        /// The `<elements>` of the canvas on the node @p nodeName of the shipped scene, as the content of a screen; empty when there is none
        std::string ShippedElementsXml(const char* nodeName) {
            pugi::xml_document document;
            if (!document.load_file(ShippedSceneConfig().string().c_str())) {
                return {};
            }
            const pugi::xml_node node = document.select_node(Format("//node[@name='{}']", nodeName).c_str()).node();
            const pugi::xml_node elements = node.select_node(".//component[@type='UICanvasComponent']/elements").node();
            if (!elements) {
                return {};
            }
            std::ostringstream out;
            out << "<ui>";
            elements.print(out, "", pugi::format_raw);
            out << "</ui>";
            return out.str();
        }

        /// A canvas of the SceneView space laid out on its own, for the copies of the panel that a test breaks on purpose: the elements come from the text of the shipped file
        /// with an edit, never from a second copy of the panel
        struct CanvasCopy {
            UIScreenContent content;
            UICanvasLayout layout;

            explicit CanvasCopy(const std::string& xml) {
                XmlDeserializer deserializer;
                EXPECT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
                content.Deserialize(deserializer);
                EXPECT_FALSE(deserializer.HasErrors());
                layout.root->children = content.elements;
                layout.variants = content.variants;
                layout.params.space = CanvasSpace::SceneView;
                layout.params.scaleMode = CanvasScaleMode::ScaleWithScreen;
                layout.params.referenceWidth = 1280.0f;
                layout.params.referenceHeight = 720.0f;
                layout.params.match = 0.5f;
                layout.params.minScale = 0.7f;
                layout.params.useSafeArea = true;
                layout.Invalidate();
            }
        };

        /// What reaches the actions: records every ActionEvent, the way the board sees them
        class ActionLog : public SubscriptionHolder {
        public:
            ActionLog() {
                Subscribe<InputEvents::ActionEvent, &ActionLog::OnAction>(this);
            }

            [[nodiscard]] size_t Total() const {
                return _actions.size();
            }
            [[nodiscard]] PoolString LastAction() const {
                return _actions.empty() ? PoolString{} : _actions.back();
            }
            [[nodiscard]] PoolString LastValue() const {
                return _values.empty() ? PoolString{} : _values.back();
            }

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                _actions.push_back(event.action);
                _values.push_back(event.value);
            }

            eastl::vector<PoolString> _actions;
            eastl::vector<PoolString> _values;
        };

        /// Every control with text in the tree, depth first (a stack and a choice group have none of their own)
        void CollectControls(const UIElement& element, eastl::vector<const UIElement*>& found) {
            if (element.Cast<UILabel>() || element.Cast<UIButton>()) {
                found.push_back(&element);
            }
            for (const auto& child : element.children) {
                CollectControls(*child, found);
            }
        }

        /// What is wrong with the opponent panel as it is laid out, one problem to a line, nothing when it is right: a control that is not shown at all (an empty rect: a segment
        /// without a `height`), text that its control cuts (the validator does not see it for an ellipsis label; a button keeps kButtonAir between its text and the edge of its
        /// fill), a button under kSegmentHeight, and a plate that leaves the safe area
        eastl::string PanelProblems(const UICanvasLayout& layout) {
            eastl::string problems;
            const float scale = layout.GetResolved().scale;
            eastl::vector<const UIElement*> controls;
            CollectControls(*layout.root, controls);
            for (const UIElement* control : controls) {
                if (control->rect.IsEmpty()) {
                    if (control->active.visible) {
                        problems += Format("{} '{}' has an empty rect (a missing height?)\n", control->name.ToStringView(), ShownText(*control).c_str());
                    }
                    continue;
                }
                const float air = control->interactive ? kButtonAir : 0.0f;
                const UIContentFit fit = control->CheckContent(control->rect.w - 2.0f * air, control->rect.h, scale);
                if (fit.hasContent && !fit.fits) {
                    problems += Format("{} cuts '{}': {:.1f} units wide with {:.1f} of air each side, at scale {:.3f}\n", control->name.ToStringView(), ShownText(*control).c_str(), control->rect.w,
                                       air, scale);
                }
                if (control->interactive && control->rect.h < kSegmentHeight - kPixelTolerance) {
                    problems += Format("{} is {:.1f} units high, under {:.0f}\n", control->name.ToStringView(), control->rect.h, kSegmentHeight);
                }
            }
            if (const UIElement* plate = layout.root->FindByName(PoolString::Intern("Plate"))) {
                const Rect& safe = layout.root->rect;
                if (plate->rect.x < safe.x - kPixelTolerance || plate->rect.y < safe.y - kPixelTolerance || plate->rect.x + plate->rect.w > safe.x + safe.w + kPixelTolerance ||
                    plate->rect.y + plate->rect.h > safe.y + safe.h + kPixelTolerance) {
                    problems += Format("the plate ({:.1f}, {:.1f}, {:.1f}, {:.1f}) leaves the safe area ({:.1f}, {:.1f}, {:.1f}, {:.1f})\n", plate->rect.x, plate->rect.y, plate->rect.w, plate->rect.h,
                                       safe.x, safe.y, safe.w, safe.h);
                }
            }
            return problems;
        }

        bool HasIssue(const eastl::vector<UIMatrixFailure>& failures, UILayoutIssueKind kind) {
            for (const auto& failure : failures) {
                for (const UILayoutIssue& issue : failure.issues) {
                    if (issue.kind == kind) {
                        return true;
                    }
                }
            }
            return false;
        }

        /// A segment of the panel as the tables of the game say it must be: the element, its group and action, the word on it and the id it emits
        struct PanelSegment {
            eastl::string name;
            const char* group = "";
            const char* action = "";
            eastl::string word;
            eastl::string id;
        };

        eastl::string Joined(eastl::string_view prefix, eastl::string_view name) {
            return Format("{}{}", prefix, name);
        }

        /// The ten segments, all of it taken from the tables of the game (the mode and color words of OpponentText, the engines, the levels); the scene is held to this list
        eastl::vector<PanelSegment> ExpectedPanelSegments() {
            eastl::vector<PanelSegment> segments;
            for (const OpponentMode mode : EnumUtils<OpponentMode>::Values()) {
                const eastl::string_view id = OpponentModeId(mode);
                segments.push_back({Joined("Mode", EnumUtils<OpponentMode>::ToString(mode)), "ModeGroup", kSetModeAction, OpponentText::ModeLabel(mode == OpponentMode::Computer),
                                    eastl::string(id.data(), id.size())});
            }
            for (const PieceColor color : {PieceColor::White, PieceColor::Black}) {
                const eastl::string_view id = PieceColorId(color);
                segments.push_back({Joined("Color", EnumUtils<PieceColor>::ToString(color)), "ColorGroup", kSetColorAction, OpponentText::ColorLabel(color), eastl::string(id.data(), id.size())});
            }
            for (const OpponentEngine engine : EnumUtils<OpponentEngine>::Values()) {
                const eastl::string_view id = OpponentEngineId(engine);
                const eastl::string_view word = OpponentEngineName(engine);
                segments.push_back(
                    {Joined("Engine", EnumUtils<OpponentEngine>::ToString(engine)), "EngineGroup", kSetEngineAction, eastl::string(word.data(), word.size()), eastl::string(id.data(), id.size())});
            }
            for (const OpponentLevel& level : GetOpponentLevels()) {
                segments.push_back({Joined("Level", level.name), "LevelGroup", kSetLevelAction, eastl::string(level.name.data(), level.name.size()), eastl::string(level.id.data(), level.id.size())});
            }
            return segments;
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

        /// The canvas of the opponent panel: the node `OpponentPanel`, a modal canvas that starts hidden
        IntrusivePtr<UICanvasComponent> Panel() const {
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern("OpponentPanel")) {
                    return node->GetComponent<UICanvasComponent>();
                }
            }
            return {};
        }

        /// An element of the panel by name (the test fails when it is not there)
        UIElement& PanelElement(const char* name) const {
            const auto panel = Panel();
            UIElement* element = panel ? panel->GetRoot().FindByName(PoolString::Intern(name)) : nullptr;
            if (element == nullptr) {
                ADD_FAILURE() << name << " is not in the panel";
                std::abort();  // a reference to nothing cannot be returned; the failure above is printed first
            }
            return *element;
        }

        /// A control of the panel by name, as the type it must be (the test fails when it is not there)
        template <typename T>
        T& PanelControl(const char* name) const {
            const auto panel = Panel();
            UIElement* element = panel ? panel->GetRoot().FindByName(PoolString::Intern(name)) : nullptr;
            T* typed = element ? element->Cast<T>() : nullptr;
            if (typed == nullptr) {
                ADD_FAILURE() << name << (element ? " is not the expected control" : " is not in the panel");
                std::abort();  // a reference to nothing cannot be returned; the failure above is printed first
            }
            return *typed;
        }

        /// The middle of an element as it was laid out in the last draw, in output pixels (the view is fitted into the output)
        std::pair<float, float> ScreenCenter(const UIElement& element) const {
            const ViewTransform fit = ViewTransform::Fit(kViewWidth, kViewHeight, _renderer->width, _renderer->height);
            const Rect cell = fit.ToOutput(element.rect.x, element.rect.y, element.rect.w, element.rect.h);
            return {cell.x + cell.w * 0.5f, cell.y + cell.h * 0.5f};
        }

        struct Taken {
            bool pressed = false;
            bool released = false;

            [[nodiscard]] bool Both() const {
                return pressed && released;
            }
        };

        /// A press and its release at a point of the screen, the way a mouse makes a click; what the UI took of the two
        static Taken ClickAtPixel(float x, float y) {
            Taken taken;
            taken.pressed = CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true));
            taken.released = CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false));
            return taken;
        }

        /// A click on the middle of a control where the last frame laid it out (a frame is drawn first, so the layout and the selection are current)
        Taken ClickControl(const UIElement& element) {
            Draw();
            const auto [x, y] = ScreenCenter(element);
            return ClickAtPixel(x, y);
        }

        /// A click on a segment (or any control) of the panel, by name
        Taken ClickPanel(const char* name) {
            return ClickControl(PanelElement(name));
        }

        /// The Opponent button of the HUD, clicked where it is drawn; whether the panel is shown afterwards
        bool OpenThePanel() {
            if (!ClickButton(kOpponentColor)) {
                return false;
            }
            return Panel() && Panel()->_visible;
        }

        /// The Ok button of the panel, clicked where it is laid out; whether the panel is hidden afterwards
        bool CloseThePanel() {
            return ClickPanel("Ok").Both() && Panel() && !Panel()->_visible;
        }

        /// The text a label or a button of the panel shows after a frame
        eastl::string PanelText(const char* name) {
            Draw();
            return ShownText(PanelElement(name));
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

        /// What a segment of a choice group of the opponent panel does when it is clicked: its action with its value (the id of the choice)
        static void PressWith(const char* action, const char* value) {
            InputEvents::ActionEvent::Emit(PoolString::Intern(action), PoolString::Intern(value));
        }

        /// The "opponent" object of get_state of the control API, the way a client reads it
        Json ControlOpponent() const {
            const auto state = ChessGame::GetActive()->AsControllable()->GetState();
            EXPECT_TRUE(state.has_value());
            const Json* opponent = state ? FindJsonMember(*state, "opponent") : nullptr;
            EXPECT_NE(opponent, nullptr);
            return opponent ? *opponent : Json();
        }

        /// set_state {"opponent": {"plays": ..}} of the control API: "white", "black" or null for two people
        void ControlPlays(const char* plays) const {
            Json opponent = Json::object();
            opponent["plays"] = plays ? Json(plays) : Json();
            Json request = Json::object();
            request["opponent"] = opponent;
            const auto result = ChessGame::GetActive()->AsControllable()->SetState(request);
            EXPECT_TRUE(result.has_value()) << DumpJson(request).c_str();
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

        /// The HUD as the board leaves it after a frame, through the size matrix with every word it can show: the validator's findings, and HudProblems()
        void ExpectTheHudSurvivesTheMatrix() {
            constexpr size_t kReportedFailures = 4;
            Draw();  // the board publishes the words

            auto canvas = Hud();
            ASSERT_TRUE(canvas);
            ASSERT_NE(HudElement("Opponent"), nullptr);
            ASSERT_EQ(HudElement("Level"), nullptr) << "the cycle button Level is gone: the panel has the level";
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
                    const eastl::string problems = HudProblems(layout);
                    if (!problems.empty() && ++failed <= kReportedFailures) {
                        report += Format("round {}, {}:\n{}", round, label.c_str(), problems.c_str());
                    }
                });
            }
            EXPECT_EQ(failed, 0u) << report.c_str();
        }

        /// The validator on the whole matrix for each round of words (the error line of the panel changes with the round): every finding of every round
        eastl::vector<UIMatrixFailure> ValidateWithWords(UICanvasLayout& layout) const {
            UILayoutValidatorOptions options;
            options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
            const HudWords words = AllHudWords();
            eastl::vector<UIMatrixFailure> all;
            for (size_t round = 0; round < words.Rounds(); ++round) {
                words.Publish(round);
                const auto failures = ValidateOnMatrix(layout, options, ViewContext());
                all.insert(all.end(), failures.begin(), failures.end());
            }
            return all;
        }

        /// PanelProblems() on every case of the matrix for each round of words: the lines of the first @p reportedCases cases that have any
        eastl::string PanelProblemsOnMatrix(UICanvasLayout& layout, size_t reportedCases = kReportedCases) const {
            eastl::string report;
            size_t reported = 0;
            const HudWords words = AllHudWords();
            for (size_t round = 0; round < words.Rounds(); ++round) {
                words.Publish(round);
                OnEveryCaseOfTheMatrix(layout, [&](const eastl::string& label, const UICanvasLayout& laidOut) {
                    const eastl::string problems = PanelProblems(laidOut);
                    if (!problems.empty() && reported++ < reportedCases) {
                        report += Format("round {}, {}:\n{}", round, label.c_str(), problems.c_str());
                    }
                });
            }
            return report;
        }

        /// The canvas alone in the narrowest window of the matrix: 480 px wide with the side insets of a phone held sideways
        void LayOutInTheNarrowestWindow(UICanvasLayout& layout) const {
            UILayoutContext context = ViewContext();
            context.outputWidth = kNarrowestWidth;
            context.outputHeight = kNarrowestHeight;
            context.safeArea = UIPreview::SimulatedSafeArea(SafeAreaSimulation::SideInsets, kNarrowestWidth, kNarrowestHeight);
            layout.Update(context);
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

    TEST_F(ChessUiTest, TheHudPassesTheSizeMatrixWithTheLongestWords) {
        // The Opponent button is always shown and says the state ({side}, three words): there is no second state of the HUD to walk
        ExpectTheHudSurvivesTheMatrix();
    }

    TEST_F(ChessUiTest, NoWordOfTheHudIsCutInTheNarrowestWindow) {
        Draw();
        auto canvas = Hud();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);

        // 480 px wide, with the side insets of a phone held sideways: the narrowest the matrix has. The view is 840 units wide, so the scale is 0.57 and the safe area
        // about 689 units, which is what every row of the HUD has to fit in
        UICanvasLayout& layout = canvas->GetLayout();
        LayOutInTheNarrowestWindow(layout);
        const float scale = kNarrowestWidth / kViewWidth;
        EXPECT_NEAR(layout.GetResolved().scale, scale, 0.01f);
        EXPECT_NEAR(layout.root->rect.w, kNarrowestWidth * (1.0f - 2.0f * UIPreview::kSideInsetShare) / scale, 0.5f) << "the safe area, in view units";

        const HudWords words = AllHudWords();
        for (size_t round = 0; round < words.Rounds(); ++round) {
            words.Publish(round);
            const eastl::string problems = HudProblems(layout);
            EXPECT_TRUE(problems.empty()) << "round " << round << ":\n" << problems.c_str();
        }
        for (const char* name : {"Turn", "Opponent", "Reset", "Side", "Engine", "Status"}) {
            const UIElement* element = layout.root->FindByName(PoolString::Intern(name));
            ASSERT_NE(element, nullptr) << name;
            EXPECT_FALSE(element->rect.IsEmpty()) << name << " is shown";
        }
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, TheHudStaysOutsideTheBoardWithoutInsets) {
        auto canvas = Hud();
        ASSERT_TRUE(canvas);
        _manager->UpdateAll();

        for (const auto& size : {std::pair{1280.0f, 720.0f}, std::pair{720.0f, 1280.0f}, std::pair{1024.0f, 768.0f}, std::pair{800.0f, 800.0f}, std::pair{2560.0f, 1080.0f}}) {
            DrawAt(size.first, size.second);
            for (const char* name : {"Turn", "Opponent", "Reset"}) {
                const UIElement* element = canvas->GetRoot().FindByName(PoolString::Intern(name));
                ASSERT_NE(element, nullptr);
                ASSERT_FALSE(element->rect.IsEmpty()) << name << " is shown at " << size.first << "x" << size.second;
                EXPECT_LE(element->rect.y + element->rect.h, kBoardTop + 0.01f) << name << " at " << size.first << "x" << size.second;
            }
            for (const char* name : {"Side", "Engine", "Status"}) {
                const UIElement* element = canvas->GetRoot().FindByName(PoolString::Intern(name));
                ASSERT_NE(element, nullptr);
                ASSERT_FALSE(element->rect.IsEmpty()) << name << " is shown at " << size.first << "x" << size.second;
                EXPECT_GE(element->rect.y, kBoardBottom - 0.01f) << name << " at " << size.first << "x" << size.second;
            }
        }

        // The panel is not a part of the margins (it covers the board while it is open) but its plate stays inside the view, in every one of the shapes
        ASSERT_TRUE(OpenThePanel());
        for (const auto& size : {std::pair{1280.0f, 720.0f}, std::pair{720.0f, 1280.0f}, std::pair{1024.0f, 768.0f}, std::pair{800.0f, 800.0f}, std::pair{2560.0f, 1080.0f}}) {
            DrawAt(size.first, size.second);
            const UIElement& plate = PanelControl<UIStack>("Plate");
            ASSERT_FALSE(plate.rect.IsEmpty()) << "the plate is shown at " << size.first << "x" << size.second;
            EXPECT_GE(plate.rect.x, -kPixelTolerance) << size.first << "x" << size.second;
            EXPECT_GE(plate.rect.y, -kPixelTolerance) << size.first << "x" << size.second;
            EXPECT_LE(plate.rect.x + plate.rect.w, kViewWidth + kPixelTolerance) << size.first << "x" << size.second;
            EXPECT_LE(plate.rect.y + plate.rect.h, kViewHeight + kPixelTolerance) << size.first << "x" << size.second;
            EXPECT_NEAR(plate.rect.x + plate.rect.w * 0.5f, kViewWidth * 0.5f, 0.5f) << "the plate is centered in the view, at " << size.first << "x" << size.second;
        }
    }

    // ---- the opponent panel: the layout ------------------------------------------------------------------------------------------
    // The panel is the node OpponentPanel of the shipped scene (a modal canvas, hidden at the start). The checks follow engine/tests/BECore/UISelectorScreenTest.cpp, the template of
    // a choice panel: the validator on the size matrix, then CheckContent() of every control (an ellipsis label can shrink, so the validator alone misses a cut word), once per
    // case of the matrix and for the narrowest window alone, with every word the game can show

    TEST_F(ChessUiTest, ThePanelPassesTheSizeMatrix) {
        auto panel = Panel();
        ASSERT_TRUE(panel) << "the shipped scene has the node OpponentPanel";
        panel->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        const auto failures = ValidateWithWords(panel->GetLayout());
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(ChessUiTest, NoWordOfThePanelIsCutOnAnyCaseOfTheMatrix) {
        auto panel = Panel();
        ASSERT_TRUE(panel);
        panel->UpdateLayout(1280.0f, 720.0f);
        const eastl::string problems = PanelProblemsOnMatrix(panel->GetLayout());
        EXPECT_TRUE(problems.empty()) << problems.c_str();
    }

    TEST_F(ChessUiTest, NoWordOfThePanelIsCutInTheNarrowestWindowAndEverySegmentIsATouchTarget) {
        auto panel = Panel();
        ASSERT_TRUE(panel);
        panel->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = panel->GetLayout();
        LayOutInTheNarrowestWindow(layout);

        const float scale = kNarrowestWidth / kViewWidth;
        EXPECT_NEAR(layout.GetResolved().scale, scale, 0.01f);
        EXPECT_NEAR(layout.root->rect.w, kNarrowestWidth * (1.0f - 2.0f * UIPreview::kSideInsetShare) / scale, 0.5f) << "the safe area, in view units: about 689";

        const HudWords words = AllHudWords();
        for (size_t round = 0; round < words.Rounds(); ++round) {
            words.Publish(round);
            const eastl::string problems = PanelProblems(layout);
            EXPECT_TRUE(problems.empty()) << "round " << round << ":\n" << problems.c_str();
        }

        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        size_t segments = 0;
        for (const PanelSegment& segment : ExpectedPanelSegments()) {
            const UIButton& button = PanelControl<UIButton>(segment.name.c_str());
            ++segments;
            EXPECT_GE(button.rect.h, kSegmentHeight - kPixelTolerance) << segment.name.c_str();
            EXPECT_GE(std::min(button.rect.w, button.rect.h) * layout.GetResolved().scale, minTouch - kPixelTolerance) << segment.name.c_str() << " is a touch target on the smallest window";
            const UIContentFit fit = button.CheckContent(button.rect.w - 2.0f * kButtonAir, button.rect.h, layout.GetResolved().scale);
            EXPECT_TRUE(fit.hasContent && fit.fits) << segment.name.c_str() << " '" << ShownText(button).c_str() << "' in " << button.rect.w << " units";
        }
        EXPECT_EQ(segments, kPanelSegments) << "two + two + two + four";
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, EveryErrorSentenceFitsTheErrorLineOfThePanelInTheNarrowestWindow) {
        auto panel = Panel();
        ASSERT_TRUE(panel);
        panel->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = panel->GetLayout();
        LayOutInTheNarrowestWindow(layout);
        const UILabel& line = PanelControl<UILabel>("OpponentError");
        ASSERT_FALSE(line.rect.IsEmpty());
        const float scale = layout.GetResolved().scale;

        size_t sentences = 0;
        for (const EngineErrorKind kind : EnumUtils<EngineErrorKind>::Values()) {
            const eastl::string sentence = OpponentText::Error(EngineError{kind, {}});
            CoreManager::GetUIVars().Set("opponentError"_intern, eastl::string_view(sentence.data(), sentence.size()));
            const UIContentFit fit = line.CheckContent(line.rect.w, line.rect.h, scale);
            EXPECT_TRUE(fit.hasContent && fit.fits) << "'" << sentence.c_str() << "' in " << line.rect.w << " units";
            ++sentences;
        }
        EXPECT_GE(sentences, 1u);
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, TheRowsAreLaidOutAsTheCommentOfTheSceneSays) {
        ASSERT_TRUE(OpenThePanel());
        Draw();
        const UIElement& plate = PanelControl<UIStack>("Plate");
        EXPECT_LE(plate.rect.w, 640.0f + kPixelTolerance);
        EXPECT_NEAR(plate.rect.h, 460.0f, kPixelTolerance) << "16 + 44 + 4 x 56 + 32 + 56 + 6 x 12 + 16";
        for (const char* row : {"Mode", "Color", "Engine", "Level"}) {
            const UIElement& caption = PanelElement(Format("{}Caption", row).c_str());
            const UIElement& group = PanelElement(Format("{}Group", row).c_str());
            EXPECT_LT(caption.rect.x + caption.rect.w, group.rect.x + kPixelTolerance) << row << ": the caption is beside its group";
            EXPECT_NEAR(caption.rect.y, group.rect.y, kPixelTolerance) << row;
            EXPECT_NEAR(group.rect.h, kSegmentHeight, kPixelTolerance) << row;
        }
        const UIElement& level = PanelControl<UISegmented>("LevelGroup");
        float left = level.rect.x;
        float width = -1.0f;
        for (const PanelSegment& segment : ExpectedPanelSegments()) {
            if (std::string_view(segment.group) != "LevelGroup") {
                continue;
            }
            const UIElement& button = PanelControl<UIButton>(segment.name.c_str());
            EXPECT_NEAR(button.rect.x, left, kPixelTolerance) << segment.name.c_str();
            width = width < 0.0f ? button.rect.w : width;
            EXPECT_NEAR(button.rect.w, width, kPixelTolerance) << segment.name.c_str() << ": equal shares by flex";
            left = button.rect.x + button.rect.w + 4.0f;
        }
        EXPECT_NEAR(left - 4.0f, level.rect.x + level.rect.w, kPixelTolerance) << "the four segments fill the group";
    }

    // ---- the opponent panel: the words and the ids come from the tables ------------------------------------------------------------

    TEST_F(ChessUiTest, TheTextAndTheValueOfEverySegmentAreThoseOfTheTables) {
        const eastl::vector<PanelSegment> expected = ExpectedPanelSegments();
        ASSERT_EQ(expected.size(), kPanelSegments);

        for (const PanelSegment& want : expected) {
            SCOPED_TRACE(want.name.c_str());
            const UIButton& button = PanelControl<UIButton>(want.name.c_str());
            EXPECT_EQ(ShownText(button), want.word) << "the word on the segment is the one of the table";
            EXPECT_EQ(eastl::string(button.value.ToStringView().data(), button.value.ToStringView().size()), want.id);
            EXPECT_EQ(button.action, PoolString::Intern(want.action));
            EXPECT_EQ(button.selected, false) << "nothing is selected before a frame";
        }

        // The id of a segment is a valid id of the table behind its group, and the group holds exactly the segments of the table (a new level or engine without a segment fails here)
        for (const char* groupName : {"ModeGroup", "ColorGroup", "EngineGroup", "LevelGroup"}) {
            const UISegmented& group = PanelControl<UISegmented>(groupName);
            size_t inTable = 0;
            for (const PanelSegment& want : expected) {
                inTable += std::string_view(want.group) == groupName ? 1u : 0u;
            }
            size_t buttons = 0;
            for (const auto& child : group.children) {
                const UIButton* button = child->Cast<UIButton>();
                ASSERT_NE(button, nullptr) << groupName << ": only segments";
                ++buttons;
                const eastl::string_view value = button->value.ToStringView();
                const std::string_view groupId = groupName;
                if (groupId == "ModeGroup") {
                    EXPECT_TRUE(ParseMode(value).has_value()) << value.data();
                } else if (groupId == "ColorGroup") {
                    EXPECT_TRUE(ParseColor(value).has_value()) << value.data();
                } else if (groupId == "EngineGroup") {
                    EXPECT_TRUE(FindOpponentEngine(value).has_value()) << value.data();
                } else {
                    EXPECT_TRUE(FindOpponentLevel(value).has_value()) << value.data();
                }
            }
            EXPECT_EQ(buttons, inTable) << groupName;
        }
    }

    TEST_F(ChessUiTest, TheGroupsAreBoundToTheVariablesTheBoardPublishesAndTheirNamesDoNotCollideWithTheHud) {
        const struct {
            const char* group;
            const char* variable;
        } bound[] = {{"ModeGroup", "opponentMode"}, {"ColorGroup", "opponentColor"}, {"EngineGroup", "opponentEngine"}, {"LevelGroup", "opponentLevel"}};
        for (const auto& [group, variable] : bound) {
            EXPECT_EQ(PanelControl<UISegmented>(group).bind, PoolString::Intern(variable)) << group;
        }
        // Elements are found by name in every canvas of the scene: the panel must not reuse a name of the HUD (nor the old Level and Engine)
        for (const char* name : {"Turn", "Opponent", "Reset", "Side", "Engine", "Status", "Level"}) {
            const auto hudHas = HudElement(name) != nullptr;
            const auto panelHas = Panel()->GetRoot().FindByName(PoolString::Intern(name)) != nullptr;
            EXPECT_FALSE(hudHas && panelHas) << name << " is in both canvases";
        }
        EXPECT_EQ(Panel()->GetRoot().FindByName(PoolString::Intern("Level")), nullptr);
        EXPECT_EQ(Panel()->GetRoot().FindByName(PoolString::Intern("Engine")), nullptr);
        EXPECT_EQ(Panel()->GetRoot().FindByName(PoolString::Intern("Mode")), nullptr);
    }

    TEST_F(ChessUiTest, EveryActionOfThePanelIsOneTheBoardAnswers) {
        // Press each action with the id of a segment through the board, as the segment would: the controller reads the pick back (the unknown action is ignored by the board)
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        const struct {
            const char* action;
            const char* value;
        } picks[] = {{kSetEngineAction, "own"}, {kSetLevelAction, "max"}};
        for (const auto& [action, value] : picks) {
            PressWith(action, value);
        }
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        EXPECT_EQ(Opponent().GetLevel(), *FindOpponentLevel("max"));
        PressWith(kSetColorAction, "black");
        PressWith(kSetModeAction, "computer");
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::White));

        for (const PanelSegment& segment : ExpectedPanelSegments()) {
            const PoolString action = PanelControl<UIButton>(segment.name.c_str()).action;
            const bool known = action == PoolString::Intern(kSetModeAction) || action == PoolString::Intern(kSetColorAction) || action == PoolString::Intern(kSetEngineAction) ||
                               action == PoolString::Intern(kSetLevelAction);
            EXPECT_TRUE(known) << segment.name.c_str() << " emits " << action.CStr() << ", which the board does not answer";
        }
        EXPECT_EQ(PanelControl<UIButton>("Ok").action, PoolString::Intern("ChessCloseOpponent"));
        EXPECT_EQ(HudElement("Opponent")->Cast<UIButton>()->action, PoolString::Intern("ChessOpenOpponent"));
    }

    // ---- the opponent panel: broken copies, so that the checks can fail --------------------------------------------------------------
    // Each copy is the text of the shipped panel with one edit (ShippedElementsXml), laid out alone

    TEST_F(ChessUiTest, AWordCutByAFontThatIsTooBigIsCaughtByTheFitCheckAndNotByTheValidator) {
        std::string xml = ShippedElementsXml("OpponentPanel");
        ASSERT_FALSE(xml.empty());
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(text="Own engine" fontSize="24")", R"(text="Own engine" fontSize="44")"));
        CanvasCopy copy(xml);

        EXPECT_FALSE(HasIssue(ValidateWithWords(copy.layout), UILayoutIssueKind::TextDoesNotFit)) << "an ellipsis label can shrink: the validator does not report it";
        const eastl::string problems = PanelProblemsOnMatrix(copy.layout);
        EXPECT_NE(problems.find("EngineOwn cuts 'Own engine'"), eastl::string::npos) << problems.c_str();

        // With the clip the validator reports it too
        std::string clipped = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(clipped, R"(text="Own engine" fontSize="24")", R"(text="Own engine" fontSize="44" overflow="Clip")"));
        CanvasCopy clippedCopy(clipped);
        EXPECT_TRUE(HasIssue(ValidateWithWords(clippedCopy.layout), UILayoutIssueKind::TextDoesNotFit));
    }

    TEST_F(ChessUiTest, ALongerWordInTheNarrowestWindowIsCaught) {
        // A word that fits in the plate of a wide window but not in a quarter of the Level row on the narrowest one
        std::string xml = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(text="Medium")", R"(text="Intermediate")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        const eastl::string problems = PanelProblems(copy.layout);
        EXPECT_NE(problems.find("LevelMedium cuts 'Intermediate'"), eastl::string::npos) << problems.c_str();
    }

    TEST_F(ChessUiTest, ASegmentUnderTheTouchSizeIsCaught) {
        std::string xml = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(name="LevelMax" flex="1" height="56")", R"(name="LevelMax" flex="1" height="40")"));
        CanvasCopy copy(xml);
        EXPECT_TRUE(HasIssue(ValidateWithWords(copy.layout), UILayoutIssueKind::TouchTargetTooSmall));
        EXPECT_NE(PanelProblemsOnMatrix(copy.layout).find("LevelMax is 40.0 units high"), eastl::string::npos);
    }

    TEST_F(ChessUiTest, ASegmentWithoutAnExplicitHeightHasNoRectAndIsCaughtHere) {
        // The gotcha of the widget: flex shares the width, not the height, so a segment without `height` gets a rect of height 0. It is not drawn, a click cannot find it
        // (`ui_click` answers "hidden") and the validator skips an empty rect, so only PanelProblems() sees it
        std::string xml = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(name="LevelHard" flex="1" height="56")", R"(name="LevelHard" flex="1")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        const UIElement* hard = copy.layout.root->FindByName(PoolString::Intern("LevelHard"));
        ASSERT_NE(hard, nullptr);
        EXPECT_NEAR(hard->rect.h, 0.0f, kPixelTolerance);
        const eastl::string problems = PanelProblems(copy.layout);
        EXPECT_NE(problems.find("LevelHard 'Hard' has an empty rect"), eastl::string::npos) << problems.c_str();
    }

    TEST_F(ChessUiTest, AnErrorSentenceWiderThanThePlateIsCaught) {
        // A plate 200 units wide cannot hold the longest sentences of the error line; the same copy with the shipped width is clean
        std::string xml = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(maxWidth="640")", R"(maxWidth="200")"));
        CanvasCopy copy(xml);
        const eastl::string problems = PanelProblemsOnMatrix(copy.layout, 100000);
        EXPECT_NE(problems.find("OpponentError cuts"), eastl::string::npos) << problems.c_str();

        CanvasCopy shipped(ShippedElementsXml("OpponentPanel"));
        EXPECT_TRUE(PanelProblemsOnMatrix(shipped.layout).empty()) << PanelProblemsOnMatrix(shipped.layout).c_str();
    }

    TEST_F(ChessUiTest, APlateTallerThanTheSafeAreaIsCaught) {
        std::string xml = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(height="460")", R"(height="1200")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        EXPECT_NE(PanelProblems(copy.layout).find("leaves the safe area"), eastl::string::npos);
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

    TEST_F(ChessUiTest, TheLevelActionPicksEveryLevelAndTheFactoryIsAskedOnlyWhenTheProgramPlays) {
        // Was TheLevelActionWalksTheLevelsAndWraps (the cycle is gone with the Level button): every level segment of the panel emits ChessSetLevel with its id
        const auto levels = GetOpponentLevels();
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        for (size_t index = 0; index < levels.size(); ++index) {
            const eastl::string id(levels[index].id.data(), levels[index].id.size());
            PressWith(kSetLevelAction, id.c_str());
            EXPECT_EQ(Opponent().GetLevel(), index) << id.c_str();
            EXPECT_EQ(Var("opponentLevel"), id) << id.c_str();
        }
        EXPECT_EQ(factory.Calls(), 0) << "with nobody as the opponent a level is only a choice: no engine is asked for";

        PressWith(kSetModeAction, "computer");
        PressWith(kSetLevelAction, "easy");
        EXPECT_EQ(Var("opponentLevel"), "easy");
        ASSERT_EQ(factory.Calls(), 2) << "one engine for the pick, another for the level";
        EXPECT_EQ(factory.Levels()[0], levels[levels.size() - 1].id) << "the pick used the level that was picked last";
        EXPECT_EQ(factory.Levels()[1], "easy");
    }

    TEST_F(ChessUiTest, TheEngineWordStartsAsStockfishAndTheActionWalksTheEnginesAndWraps) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        EXPECT_EQ(Var("engine"), OpponentText::EngineLabel(OpponentEngine::Stockfish));
        EXPECT_EQ(Var("engine"), "Stockfish");

        Press("ChessCycleEngine");
        EXPECT_EQ(Var("engine"), "Own engine");
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        Press("ChessCycleEngine");
        EXPECT_EQ(Var("engine"), "Stockfish") << "and round again";
        EXPECT_EQ(factory.Calls(), 0) << "with nobody as the opponent an engine is only a choice: none is asked for";

        Press("ChessCycleSide");
        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Engines()[0], "stockfish");
        Press("ChessCycleEngine");
        EXPECT_EQ(Var("engine"), "Own engine");
        ASSERT_EQ(factory.Calls(), 2) << "the program plays: a new engine for the new choice";
        EXPECT_EQ(factory.Engines()[1], "own");
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::Black)) << "the side stays";
        EXPECT_EQ(Var("side"), "You: White");
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

    TEST_F(ChessUiTest, TheEngineButtonIsShownWhetherOrNotTheProgramPlays) {
        const UIElement* engine = HudElement("Engine");
        ASSERT_NE(engine, nullptr) << "the HUD of the shipped scene has the Engine button";
        CountingFactory working({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(working.AsFactory());
        _manager->UpdateAll();

        Draw();
        EXPECT_TRUE(engine->style.visible) << "nobody plays: it is how the person gets to the own engine";
        EXPECT_FALSE(engine->rect.IsEmpty());
        EXPECT_TRUE(DrawnCenter(kEngineColor).has_value());

        for (const char* side : {"You: White", "You: Black", "Two players"}) {
            Press("ChessCycleSide");
            Draw();
            EXPECT_EQ(Var("side"), side);
            EXPECT_TRUE(engine->style.visible) << side;
            EXPECT_FALSE(engine->rect.IsEmpty()) << side;
            EXPECT_TRUE(DrawnCenter(kEngineColor).has_value()) << side;
        }

        CountingFactory failing(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
        Opponent().SetEngineFactory(failing.AsFactory());
        Press("ChessCycleSide");
        Draw();
        EXPECT_EQ(Var("status"), "Engine not found");
        EXPECT_TRUE(engine->style.visible) << "a failed pick leaves nobody, and the button is what the person needs then";
        EXPECT_TRUE(DrawnCenter(kEngineColor).has_value());
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

    TEST_F(ChessUiTest, ClickingTheEngineButtonOnTheScreenCyclesTheEngineAndAPressAloneDoesNothing) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        Draw();
        ASSERT_EQ(ShownText(*HudElement("Engine")), "Stockfish");

        const auto at = DrawnCenter(kEngineColor);
        ASSERT_TRUE(at.has_value());
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at->first, at->second, true))) << "the button takes the click";
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Stockfish) << "a press alone does nothing";
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(at->first, at->second, false)));
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        EXPECT_EQ(Var("engine"), "Own engine");
        EXPECT_EQ(ShownText(*HudElement("Engine")), "Own engine") << "the text on the button follows";

        ASSERT_TRUE(ClickButton(kSideColor));  // the program plays Black
        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Engines()[0], "own");
        EXPECT_TRUE(ClickButton(kEngineColor)) << "also while the program plays";
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Stockfish);
        EXPECT_EQ(Var("engine"), "Stockfish");
        EXPECT_EQ(ShownText(*HudElement("Engine")), "Stockfish");
        ASSERT_EQ(factory.Calls(), 2);
        EXPECT_EQ(factory.Engines()[1], "stockfish");
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::Black)) << "the side stays";
    }

    TEST_F(ChessUiTest, APressOnTheEngineButtonDoesNotChangeTheSelection) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        Click("e2");
        ASSERT_TRUE(Model().HasSelection());

        EXPECT_TRUE(ClickButton(kEngineColor));

        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        EXPECT_TRUE(Model().HasSelection());
        EXPECT_TRUE(Model().GetSelected() == Sq("e2"));
    }

    TEST_F(ChessUiTest, WhenNoStockfishIsFoundTheEngineButtonLeadsToTheOwnEngineAndTheProgramPlays) {
        // The user's path: Stockfish is not installed, the pick says so, the person presses Engine, then Side, and the own engine answers. The factory stands in for "no
        // Stockfish found" and nothing else: it never makes the own engine for a Stockfish pick, so the program cannot have fallen back by itself
        eastl::vector<eastl::string> asked;
        Opponent().SetEngineFactory([&asked](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            const eastl::string_view id = OpponentEngineId(engine);
            asked.emplace_back(id.data(), id.size());
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
            }
            return eastl::make_shared<InstantEngine>();
        });
        _manager->UpdateAll();

        EXPECT_TRUE(ClickButton(kSideColor));
        EXPECT_EQ(Var("status"), "Engine not found");
        EXPECT_EQ(Var("side"), "Two players");
        EXPECT_EQ(Var("engine"), "Stockfish") << "the pick failed and the choice is still Stockfish: no silent switch";
        ASSERT_EQ(asked.size(), 1u);
        EXPECT_EQ(asked[0], "stockfish");

        EXPECT_TRUE(ClickButton(kEngineColor));
        EXPECT_EQ(Var("engine"), "Own engine");
        EXPECT_EQ(Var("status"), "") << "a new pick: the person has seen the sentence";
        EXPECT_EQ(asked.size(), 1u) << "with nobody playing the choice only changes";

        EXPECT_TRUE(ClickButton(kSideColor));
        EXPECT_EQ(Var("side"), "You: White");
        EXPECT_EQ(Var("status"), "");
        ASSERT_EQ(asked.size(), 2u);
        EXPECT_EQ(asked[1], "own");

        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the own engine, behind the double, answers the person's move";
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[1].To()).color, PieceColor::Black);
        EXPECT_FALSE(Opponent().GetError().has_value());
    }

    TEST_F(ChessUiTest, TheOwnEngineOfTheDefaultFactoryPlaysThroughTheButtons) {
        // No factory is set: the controller makes the real OwnEngine (no process, no settings) at the easy level, so the test needs no Stockfish and no fake program
        ASSERT_TRUE(ClickButton(kEngineColor));
        ASSERT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        const std::optional<size_t> easy = FindOpponentLevel("easy");
        ASSERT_TRUE(easy.has_value());
        ASSERT_TRUE(ClickButton(kSideColor));
        EXPECT_EQ(Var("side"), "You: White");
        ASSERT_TRUE(OpenThePanel());  // the level is a segment of the panel now
        ASSERT_TRUE(ClickPanel("LevelEasy").Both());
        ASSERT_EQ(Opponent().GetLevel(), *easy);
        EXPECT_EQ(Var("opponentLevel"), "easy");
        EXPECT_FALSE(Opponent().GetError().has_value());
        ASSERT_TRUE(CloseThePanel());

        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the own engine searched and its move was played";
        EXPECT_EQ(Model().GetMoves()[0].ToUci(), "e2e4");
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[1].To()).color, PieceColor::Black) << "a legal reply for Black (TryMove accepts nothing else)";
        ASSERT_TRUE(FramesUntil([&] { return !Opponent().IsThinking(); }));
        EXPECT_EQ(Model().GetTurn(), PieceColor::White);
        EXPECT_FALSE(Opponent().GetError().has_value());
        EXPECT_EQ(Var("turn"), "White to move");
    }

    TEST_F(ChessUiTest, TheResetButtonKeepsTheEngine) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(ClickButton(kEngineColor));
        ASSERT_TRUE(ClickButton(kSideColor));
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));

        EXPECT_TRUE(ClickButton(kResetColor));

        EXPECT_EQ(Model().GetPosition().ToFen(), eastl::string(Position::kStartFen));
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own) << "a new game keeps the engine";
        EXPECT_EQ(Var("engine"), "Own engine");
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::Black));
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
        EXPECT_TRUE(ClickButton(kOpponentColor));
        EXPECT_TRUE(Model().HasSelection());
        EXPECT_TRUE(Model().GetSelected() == Sq("e2"));
    }

    // ---- the choice of the opponent panel: the actions with a value and the ids --------------------------------------------
    // The segments of the panel emit ChessSetMode / ChessSetColor / ChessSetEngine / ChessSetLevel with the id as `value`; the board answers through OpponentChoice and
    // publishes the ids. These tests press the actions the segments will emit (the panel itself is in the scene from WP 3) and read the variables

    TEST_F(ChessUiTest, TheIdsOfThePanelStartAsTwoWhiteStockfishMediumAndNoError) {
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_EQ(Var("opponentColor"), "white");
        EXPECT_EQ(Var("opponentEngine"), "stockfish");
        EXPECT_EQ(Var("opponentLevel"), "medium");
        EXPECT_EQ(Var("opponentError"), "");
    }

    TEST_F(ChessUiTest, EachActionOfThePanelPicksWhatTheControlApiWouldRead) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        const size_t hard = *FindOpponentLevel("hard");

        // Nobody plays: each of the three is only a choice, no engine is asked for
        PressWith("ChessSetEngine", "own");
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        EXPECT_EQ(Var("opponentEngine"), "own");
        PressWith("ChessSetLevel", "hard");
        EXPECT_EQ(Opponent().GetLevel(), hard);
        EXPECT_EQ(Var("opponentLevel"), "hard");
        PressWith("ChessSetColor", "black");
        EXPECT_FALSE(Opponent().GetPlays().has_value()) << "the color of the person is remembered, nobody plays";
        EXPECT_EQ(Var("opponentColor"), "black");
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_EQ(factory.Calls(), 0);

        // Computer: the program plays the color the person does not, with the engine and the level that were picked, one engine made
        PressWith("ChessSetMode", "computer");
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::White)) << "the person has Black";
        EXPECT_EQ(Var("opponentMode"), "computer");
        EXPECT_EQ(Var("opponentColor"), "black");
        EXPECT_EQ(Var("side"), "You: Black");
        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Engines()[0], "own");
        EXPECT_EQ(factory.Levels()[0], "hard");

        // While the program plays each pick remakes the engine once
        PressWith("ChessSetEngine", "stockfish");
        ASSERT_EQ(factory.Calls(), 2);
        EXPECT_EQ(factory.Engines()[1], "stockfish");
        EXPECT_EQ(Var("opponentEngine"), "stockfish");
        PressWith("ChessSetLevel", "easy");
        ASSERT_EQ(factory.Calls(), 3);
        EXPECT_EQ(factory.Levels()[2], "easy");
        EXPECT_EQ(Var("opponentLevel"), "easy");
        PressWith("ChessSetColor", "white");
        ASSERT_EQ(factory.Calls(), 4);
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::Black));
        EXPECT_EQ(Var("opponentColor"), "white");
        EXPECT_EQ(Var("side"), "You: White");

        PressWith("ChessSetMode", "two");
        EXPECT_FALSE(Opponent().GetPlays().has_value());
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_EQ(Var("opponentColor"), "white");
        EXPECT_EQ(factory.Calls(), 4) << "nobody plays: nothing is asked";
    }

    TEST_F(ChessUiTest, ComputerAndThenBlackMakesTheProgramOpenAsWhite) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        PressWith("ChessSetMode", "computer");
        EXPECT_TRUE(Opponent().PlaysFor(PieceColor::Black));
        PressWith("ChessSetColor", "black");
        EXPECT_TRUE(Opponent().PlaysFor(PieceColor::White));
        EXPECT_FALSE(Opponent().PlaysFor(PieceColor::Black));

        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 1; })) << "it is White's turn and White is the program's";
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[0].To()).color, PieceColor::White);
        EXPECT_EQ(Var("opponentMode"), "computer");
        EXPECT_EQ(Var("opponentColor"), "black");
    }

    TEST_F(ChessUiTest, ThePersonsColorSurvivesTwoPlayersAndComesBack) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        PressWith("ChessSetColor", "black");
        PressWith("ChessSetMode", "computer");
        PressWith("ChessSetMode", "two");
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_EQ(Var("opponentColor"), "black") << "the program is gone and the person still has the color";
        PressWith("ChessSetMode", "computer");
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::White));
        EXPECT_EQ(Var("opponentColor"), "black");
    }

    TEST_F(ChessUiTest, TheColorPickedWhileTwoPlayIsPublishedWithNoRevisionOfTheOpponent) {
        // The controller knows nothing of the color then, so its revision does not move; the board still has to say the new word
        EXPECT_EQ(Var("opponentColor"), "white");
        const uint64_t revision = Opponent().GetRevision();

        PressWith("ChessSetColor", "black");

        EXPECT_EQ(Opponent().GetRevision(), revision);
        EXPECT_EQ(Var("opponentColor"), "black");
        PressWith("ChessSetColor", "white");
        EXPECT_EQ(Var("opponentColor"), "white");
    }

    TEST_F(ChessUiTest, AnActionOfThePanelWithABadValueChangesNothing) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        PressWith("ChessSetMode", "computer");
        ASSERT_EQ(factory.Calls(), 1);
        const uint64_t revision = Opponent().GetRevision();
        const uint64_t version = Model().GetVersion();

        for (const char* action : {"ChessSetMode", "ChessSetColor", "ChessSetEngine", "ChessSetLevel"}) {
            for (const char* value : {"", "White", "Computer", "OWN", "own ", "bl", "nonsense"}) {
                PressWith(action, value);
            }
            Press(action);  // a plain button press: no value at all
        }

        EXPECT_EQ(factory.Calls(), 1) << "no engine is asked for an id that is in no table";
        EXPECT_EQ(Opponent().GetRevision(), revision);
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::Black));
        EXPECT_EQ(Var("opponentMode"), "computer");
        EXPECT_EQ(Var("opponentColor"), "white");
        EXPECT_EQ(Var("opponentEngine"), "stockfish");
        EXPECT_EQ(Var("opponentLevel"), "medium");
        EXPECT_EQ(Var("opponentError"), "");
    }

    TEST_F(ChessUiTest, WhenNoStockfishIsFoundTheEngineRowLeadsToTheOwnEngineAndComputerPlays) {
        // The user's path through the panel's actions: Stockfish is not installed, Computer says so and leaves two people, Own engine and Computer again let the program play.
        // The factory fails for Stockfish and nothing else, so the program cannot have fallen back by itself
        eastl::vector<eastl::string> asked;
        Opponent().SetEngineFactory([&asked](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            const eastl::string_view id = OpponentEngineId(engine);
            asked.emplace_back(id.data(), id.size());
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
            }
            return eastl::make_shared<InstantEngine>();
        });
        _manager->UpdateAll();

        PressWith("ChessSetMode", "computer");

        EXPECT_EQ(Var("opponentMode"), "two") << "the screen shows the truth: nobody plays";
        EXPECT_EQ(Var("opponentError"), "Engine not found");
        EXPECT_EQ(Var("status"), "Engine not found");
        EXPECT_EQ(Var("opponentEngine"), "stockfish") << "no silent switch";
        EXPECT_EQ(Var("side"), "Two players");
        ASSERT_EQ(asked.size(), 1u);

        PressWith("ChessSetEngine", "own");

        EXPECT_EQ(Var("opponentEngine"), "own");
        EXPECT_EQ(Var("opponentError"), "") << "a new pick: the person has seen the sentence";
        EXPECT_EQ(Var("status"), "");
        EXPECT_EQ(asked.size(), 1u) << "with nobody playing the choice only changes";

        PressWith("ChessSetMode", "computer");

        EXPECT_EQ(Var("opponentMode"), "computer");
        EXPECT_EQ(Var("side"), "You: White");
        EXPECT_EQ(Var("opponentError"), "");
        ASSERT_EQ(asked.size(), 2u);
        EXPECT_EQ(asked[1], "own");
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the program answers the person's move";
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[1].To()).color, PieceColor::Black);
        EXPECT_FALSE(Opponent().GetError().has_value());
    }

    TEST_F(ChessUiTest, AFailedSearchIsInTheErrorVariableToo) {
        const auto engine = eastl::make_shared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));

        EXPECT_EQ(Var("opponentError"), OpponentText::Error(*Opponent().GetError()));
        EXPECT_EQ(Var("opponentMode"), "computer") << "the pick succeeded, only the first search failed";

        Press("ChessReset");
        _manager->UpdateAll();

        EXPECT_EQ(Var("opponentError"), "") << "the version moved: the error rule clears it";
    }

    TEST_F(ChessUiTest, TheIdsAgreeWithWhatTheControlApiReportsAfterASetState) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        const auto agree = [&](const char* mode, const char* personColor, const char* plays) {
            const Json opponent = ControlOpponent();
            const auto reported = GetJsonString(opponent, "plays");
            if (plays == nullptr) {
                EXPECT_FALSE(reported.has_value());
            } else {
                ASSERT_TRUE(reported.has_value());
                EXPECT_EQ(std::string(reported->c_str(), reported->size()), plays);
            }
            EXPECT_EQ(Var("opponentMode"), mode);
            EXPECT_EQ(Var("opponentColor"), personColor) << "the person has the opposite of what the program plays";
            const auto engine = GetJsonString(opponent, "engine");
            const auto level = GetJsonString(opponent, "level");
            ASSERT_TRUE(engine.has_value());
            ASSERT_TRUE(level.has_value());
            EXPECT_EQ(Var("opponentEngine"), *engine);
            EXPECT_EQ(Var("opponentLevel"), *level);
        };

        agree("two", "white", nullptr);

        ControlPlays("black");
        _manager->UpdateAll();
        agree("computer", "white", "black");

        ControlPlays("white");
        _manager->UpdateAll();
        agree("computer", "black", "white");

        ControlPlays(nullptr);
        _manager->UpdateAll();
        agree("two", "black", nullptr);  // the color the person had stays

        // A pick of the panel is read back by the API in the program's color
        PressWith("ChessSetMode", "computer");
        agree("computer", "black", "white");
        PressWith("ChessSetColor", "white");
        agree("computer", "white", "black");
    }

    // ---- the opponent panel, clicked where it is drawn ---------------------------------------------------------------------------
    // The segments are found where the layout put them (their rect, mapped to output pixels) and clicked through CoreManager::GetUIInput(), never through Press: what the
    // screen takes, what reaches the board and what the groups show are the point. The engines are the doubles of TestEngines.h

    TEST_F(ChessUiTest, ThePanelIsHiddenAtTheStartDrawsNothingAndTakesNoClick) {
        auto panel = Panel();
        ASSERT_TRUE(panel);
        EXPECT_FALSE(panel->_visible);
        EXPECT_TRUE(panel->_modal);
        EXPECT_FALSE(Hud()->_modal);
        Draw();
        EXPECT_TRUE(RectCalls(kPlateColor).empty()) << "hidden: not even the plate is drawn";

        // Where a segment will be, the panel takes nothing and the board under it gets the click. Show it once to learn the place, then hide it again
        ASSERT_TRUE(OpenThePanel());
        Draw();
        const auto place = ScreenCenter(PanelControl<UIButton>("LevelMedium"));
        ASSERT_TRUE(CloseThePanel());
        Draw();
        EXPECT_TRUE(RectCalls(kPlateColor).empty());

        ActionLog log;
        const Taken taken = ClickAtPixel(place.first, place.second);
        EXPECT_FALSE(taken.pressed);
        EXPECT_FALSE(taken.released);
        EXPECT_EQ(log.Total(), 0u);
        EXPECT_EQ(Opponent().GetLevel(), kDefaultOpponentLevel);
        const auto e2 = SquareCenter("e2");
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(e2.first, e2.second, true)));
        EXPECT_TRUE(Model().HasSelection()) << "the board under the hidden panel gets its clicks";
    }

    TEST_F(ChessUiTest, TheOpponentButtonOpensThePanelAndOkClosesIt) {
        ActionLog log;
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Opponent")), "Two players");
        ASSERT_TRUE(ClickButton(kOpponentColor));
        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessOpenOpponent"));
        ASSERT_TRUE(Panel()->_visible);
        Draw();
        EXPECT_FALSE(RectCalls(kPlateColor).empty()) << "shown: the plate is drawn";

        // The Opponent button is under the dim now: the panel takes the press first, and nothing is emitted
        const size_t actions = log.Total();
        const auto button = ScreenCenter(*HudElement("Opponent"));
        const Taken second = ClickAtPixel(button.first, button.second);
        EXPECT_TRUE(second.Both()) << "the modal canvas takes it";
        EXPECT_EQ(log.Total(), actions) << "and it is not the Opponent button's";

        ASSERT_TRUE(CloseThePanel());
        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessCloseOpponent"));
        Draw();
        EXPECT_TRUE(RectCalls(kPlateColor).empty());

        // Closed: the board gets its clicks again and the button works
        const auto e2 = SquareCenter("e2");
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(e2.first, e2.second, true)));
        EXPECT_TRUE(Model().HasSelection());
        ASSERT_TRUE(ClickButton(kOpponentColor));
        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessOpenOpponent"));
        EXPECT_TRUE(Panel()->_visible);
    }

    TEST_F(ChessUiTest, WhileThePanelIsShownEveryPressIsItsAndTheBoardAndTheResetButtonUnderItSeeNone) {
        ASSERT_TRUE(OpenThePanel());
        Draw();
        ActionLog log;
        const uint64_t version = Model().GetVersion();
        ASSERT_TRUE(Play(Model(), "e2e4"));  // something for Reset to undo
        const uint64_t afterMove = Model().GetVersion();
        ASSERT_GT(afterMove, version);
        Draw();

        // On a segment, on the plate (its title), on the dim over the board (a1, below the plate) and over the Reset button: all of them are the panel's
        const std::pair<float, float> points[] = {ScreenCenter(PanelControl<UIButton>("EngineOwn")),
                                                  ScreenCenter(PanelElement("Title")),
                                                  SquareCenter("a1"),
                                                  SquareCenter("h8"),
                                                  ScreenCenter(*HudElement("Reset")),
                                                  ScreenCenter(*HudElement("Turn"))};
        for (const auto& [x, y] : points) {
            const Taken taken = ClickAtPixel(x, y);
            EXPECT_TRUE(taken.Both()) << x << "," << y;
        }
        EXPECT_FALSE(Model().HasSelection()) << "no click reached the board";
        EXPECT_EQ(Model().GetVersion(), afterMove) << "Reset under the dim was not pressed, the board did not move";
        EXPECT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(log.Total(), 1u) << "only the click on EngineOwn is an action; the others hit the plate or the dim";
        EXPECT_EQ(log.LastAction(), PoolString::Intern(kSetEngineAction));
    }

    TEST_F(ChessUiTest, EverySegmentEmitsItsActionAndValueAndTheSelectionFollowsTheController) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        ActionLog log;
        const eastl::vector<PanelSegment> segments = ExpectedPanelSegments();

        // Two people play first: only the mode and the engine groups take clicks. Computer is picked in the middle, which wakes the other two; then back to two
        const auto clickGroup = [&](const char* group) {
            for (const PanelSegment& segment : segments) {
                if (std::string_view(segment.group) != group) {
                    continue;
                }
                SCOPED_TRACE(segment.name.c_str());
                const size_t before = log.Total();
                const Taken taken = ClickPanel(segment.name.c_str());
                EXPECT_TRUE(taken.Both());
                ASSERT_EQ(log.Total(), before + 1);
                EXPECT_EQ(log.LastAction(), PoolString::Intern(segment.action));
                EXPECT_EQ(log.LastValue(), PoolString::Intern(segment.id.c_str()));

                Draw();  // the board answered with the variable; the next draw moves the selection
                for (const PanelSegment& other : segments) {
                    if (std::string_view(other.group) == group) {
                        EXPECT_EQ(PanelControl<UIButton>(other.name.c_str()).selected, other.name == segment.name) << other.name.c_str();
                    }
                }
                if (std::string_view(group) == "ModeGroup") {
                    EXPECT_EQ(Opponent().GetPlays().has_value(), segment.id == "computer");
                } else if (std::string_view(group) == "ColorGroup") {
                    EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(segment.id == "white" ? PieceColor::Black : PieceColor::White)) << "the program plays the other color";
                } else if (std::string_view(group) == "EngineGroup") {
                    EXPECT_EQ(Opponent().GetEngine(), *FindOpponentEngine(segment.id));
                } else {
                    EXPECT_EQ(Opponent().GetLevel(), *FindOpponentLevel(segment.id));
                }
            }
        };
        clickGroup("EngineGroup");
        clickGroup("ModeGroup");  // ends with Computer
        ASSERT_TRUE(Opponent().GetPlays().has_value());
        clickGroup("ColorGroup");
        clickGroup("LevelGroup");
        ASSERT_TRUE(ClickPanel("ModeTwo").Both());
        Draw();
        EXPECT_FALSE(Opponent().GetPlays().has_value());
        EXPECT_TRUE(PanelControl<UIButton>("ModeTwo").selected);
        EXPECT_TRUE(PanelControl<UIButton>("LevelMax").selected) << "the level that was picked stays shown while two people play";
        EXPECT_TRUE(PanelControl<UIButton>("ColorBlack").selected) << "and so does the person's color";
        EXPECT_EQ(log.Total(), 11u) << "ten segments, and ModeTwo again";
    }

    TEST_F(ChessUiTest, AClickWithoutAnAnswerLeavesTheGroupAlone) {
        // The group owns no choice: it only reports. A pick the controller refuses leaves the segment that was selected selected
        CountingFactory failing(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
        Opponent().SetEngineFactory(failing.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        Draw();
        ASSERT_TRUE(PanelControl<UIButton>("ModeTwo").selected);

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();

        EXPECT_TRUE(PanelControl<UIButton>("ModeTwo").selected);
        EXPECT_FALSE(PanelControl<UIButton>("ModeComputer").selected);
    }

    TEST_F(ChessUiTest, TheColorAndLevelGroupsAreDisabledWhileTwoPeoplePlayAndTheEngineGroupNever) {
        // Was TheLevelButtonIsShownOnlyWhileTheProgramPlays (the Level button hidden for two people): the greying of UISegmented::disabled is the rule now
        CountingFactory working({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(working.AsFactory());
        _manager->UpdateAll();
        Draw();  // the first publication

        const auto disabled = [this](const char* group) { return PanelControl<UISegmented>(group).disabled; };
        EXPECT_FALSE(disabled("ModeGroup"));
        EXPECT_TRUE(disabled("ColorGroup")) << "two people play: the person's color means nothing yet";
        EXPECT_FALSE(disabled("EngineGroup")) << "never greyed: the person needs it when the Stockfish pick failed";
        EXPECT_TRUE(disabled("LevelGroup"));

        ASSERT_TRUE(OpenThePanel());
        Draw();  // a hidden canvas is not drawn, so its segments take the group's state on the first draw once it is shown
        for (const char* name : {"ColorWhite", "ColorBlack", "LevelEasy", "LevelMedium", "LevelHard", "LevelMax"}) {
            const UIButton& button = PanelControl<UIButton>(name);
            EXPECT_TRUE(button.groupDisabled) << name;
            EXPECT_LT(button.GetCurrentFill().a, 255) << name << " is drawn grey";
        }
        for (const char* name : {"ModeTwo", "ModeComputer", "EngineStockfish", "EngineOwn"}) {
            const UIButton& button = PanelControl<UIButton>(name);
            EXPECT_FALSE(button.groupDisabled) << name;
            EXPECT_EQ(button.GetCurrentFill().a, 255) << name;
        }

        // A click on a segment of a disabled group changes nothing, and it emits nothing
        ActionLog log;
        const uint64_t revision = Opponent().GetRevision();
        ClickPanel("ColorBlack");
        ClickPanel("LevelHard");
        EXPECT_EQ(log.Total(), 0u);
        EXPECT_EQ(Opponent().GetRevision(), revision);
        EXPECT_EQ(Var("opponentColor"), "white");
        EXPECT_EQ(Var("opponentLevel"), "medium");
        EXPECT_EQ(Opponent().GetLevel(), kDefaultOpponentLevel);

        // The engine group works while two people play
        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        EXPECT_EQ(log.Total(), 1u);
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        EXPECT_FALSE(disabled("EngineGroup"));

        // Computer wakes the other two
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        EXPECT_FALSE(disabled("ModeGroup"));
        EXPECT_FALSE(disabled("ColorGroup"));
        EXPECT_FALSE(disabled("EngineGroup"));
        EXPECT_FALSE(disabled("LevelGroup"));
        EXPECT_FALSE(PanelControl<UIButton>("LevelHard").groupDisabled);
        EXPECT_EQ(PanelControl<UIButton>("LevelHard").GetCurrentFill().a, 255);
        ASSERT_TRUE(ClickPanel("LevelHard").Both());
        EXPECT_EQ(Opponent().GetLevel(), *FindOpponentLevel("hard"));
        ASSERT_TRUE(ClickPanel("ColorBlack").Both());
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::White));

        // And back to two people: they are greyed again, with the choice still shown
        ASSERT_TRUE(ClickPanel("ModeTwo").Both());
        Draw();
        EXPECT_TRUE(disabled("ColorGroup"));
        EXPECT_TRUE(disabled("LevelGroup"));
        EXPECT_FALSE(disabled("EngineGroup"));
        EXPECT_TRUE(PanelControl<UIButton>("ColorBlack").groupDisabled);
        EXPECT_TRUE(PanelControl<UIButton>("LevelHard").selected);
        EXPECT_TRUE(PanelControl<UIButton>("ColorBlack").selected);
        const size_t total = log.Total();
        ClickPanel("LevelEasy");
        EXPECT_EQ(log.Total(), total) << "greyed again: no click";

        // A pick that finds no engine leaves nobody, so the two groups stay grey; the engine group stays usable
        CountingFactory failing(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
        Opponent().SetEngineFactory(failing.AsFactory());
        ASSERT_TRUE(ClickPanel("EngineStockfish").Both());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        EXPECT_FALSE(Opponent().GetPlays().has_value());
        EXPECT_TRUE(disabled("ColorGroup"));
        EXPECT_TRUE(disabled("LevelGroup"));
        EXPECT_FALSE(disabled("EngineGroup"));
    }

    TEST_F(ChessUiTest, TheDisabledFlagFollowsAnOpponentSetFromOutside) {
        // The control API picks the program's side without the panel: the groups wake up (and sleep) with the opponent, not with the clicks
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        Draw();
        ASSERT_TRUE(PanelControl<UISegmented>("LevelGroup").disabled);

        ControlPlays("black");
        Draw();
        EXPECT_FALSE(PanelControl<UISegmented>("ColorGroup").disabled);
        EXPECT_FALSE(PanelControl<UISegmented>("LevelGroup").disabled);

        ControlPlays(nullptr);
        Draw();
        EXPECT_TRUE(PanelControl<UISegmented>("ColorGroup").disabled);
        EXPECT_TRUE(PanelControl<UISegmented>("LevelGroup").disabled);
    }

    TEST_F(ChessUiTest, TheOpponentButtonSaysWhatTheProgramPlaysAsThePanelChanges) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        EXPECT_EQ(ShownText(*HudElement("Opponent")), "Two players");

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Opponent")), "You: White") << "the person has White, the program Black";
        EXPECT_EQ(Var("side"), "You: White");

        ASSERT_TRUE(ClickPanel("ColorBlack").Both());
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Opponent")), "You: Black");

        ASSERT_TRUE(ClickPanel("ColorWhite").Both());
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Opponent")), "You: White");

        ASSERT_TRUE(ClickPanel("ModeTwo").Both());
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Opponent")), "Two players");
    }

    TEST_F(ChessUiTest, ThePersonPlaysTheColorThePanelNamesAndTheProgramTheOther) {
        const auto engine = eastl::make_shared<InstantEngine>();
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        ASSERT_TRUE(CloseThePanel());

        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the person has White: the program answers with a black piece";
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[1].To()).color, PieceColor::Black);

        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("ColorBlack").Both());
        ASSERT_TRUE(CloseThePanel());
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 3; })) << "the person has Black now: White is the program's and it is White's turn";
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[2].To()).color, PieceColor::White);
    }

    TEST_F(ChessUiTest, WhenNoStockfishIsFoundThePanelLeadsToTheOwnEngineAndTheProgramPlays) {
        // Was WhenNoStockfishIsFoundTheEngineButtonLeadsToTheOwnEngine... (the action-level twin is WhenNoStockfishIsFoundTheEngineRowLeadsToTheOwnEngineAndComputerPlays). The
        // factory stands in for "no Stockfish found" and nothing else: it never makes the own engine for a Stockfish pick, so the program cannot have fallen back by itself
        eastl::vector<eastl::string> asked;
        Opponent().SetEngineFactory([&asked](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            const eastl::string_view id = OpponentEngineId(engine);
            asked.emplace_back(id.data(), id.size());
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
            }
            return eastl::make_shared<InstantEngine>();
        });
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        EXPECT_EQ(PanelText("OpponentError"), "") << "no error line to start with";

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        EXPECT_EQ(PanelText("OpponentError"), "Engine not found") << "the panel says it where the person looks";
        EXPECT_EQ(Label("Status").GetDisplayText(), "Engine not found");
        EXPECT_TRUE(PanelControl<UIButton>("ModeTwo").selected) << "the screen shows the truth: nobody plays";
        EXPECT_FALSE(PanelControl<UIButton>("ModeComputer").selected);
        EXPECT_TRUE(PanelControl<UIButton>("EngineStockfish").selected) << "the pick failed and the choice is still Stockfish: no silent switch";
        EXPECT_EQ(ShownText(*HudElement("Opponent")), "Two players");
        ASSERT_EQ(asked.size(), 1u);
        EXPECT_EQ(asked[0], "stockfish");

        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        EXPECT_EQ(PanelText("OpponentError"), "") << "a new pick: the person has seen the sentence";
        EXPECT_TRUE(PanelControl<UIButton>("EngineOwn").selected);
        EXPECT_EQ(asked.size(), 1u) << "with nobody playing the choice only changes";

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        EXPECT_TRUE(PanelControl<UIButton>("ModeComputer").selected);
        EXPECT_EQ(PanelText("OpponentError"), "");
        EXPECT_EQ(ShownText(*HudElement("Opponent")), "You: White");
        ASSERT_EQ(asked.size(), 2u);
        EXPECT_EQ(asked[1], "own");

        ASSERT_TRUE(CloseThePanel());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the own engine, behind the double, answers the person's move";
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[1].To()).color, PieceColor::Black);
        EXPECT_FALSE(Opponent().GetError().has_value());
    }

    TEST_F(ChessUiTest, ASearchErrorIsOnTheErrorLineAndAnyPickOfThePanelLetsThePersonOut) {
        // Twin of ChangingTheSideLetsThePersonOut... and ChangingTheLevelLetsThePersonOut... through the segments: the error line empties and the new engine is asked once
        const auto engine = eastl::make_shared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));
        ASSERT_TRUE(OpenThePanel());
        EXPECT_EQ(PanelText("OpponentError"), "Engine not found") << "the first search failed";
        EXPECT_TRUE(PanelControl<UIButton>("ModeComputer").selected) << "the pick succeeded, only the search failed";

        ASSERT_TRUE(ClickPanel("LevelHard").Both());

        EXPECT_FALSE(Opponent().GetError().has_value());
        EXPECT_EQ(PanelText("OpponentError"), "");
        ASSERT_EQ(factory.Calls(), 2);
        EXPECT_EQ(factory.Levels()[1], "hard");
        ASSERT_TRUE(FramesUntil([&] { return Opponent().GetError().has_value(); })) << "black is still the program's and to move: the new engine is asked, and fails";
        IdleFrames();
        EXPECT_EQ(engine->Calls(), 2) << "once per act of the person, not per frame";

        ASSERT_TRUE(ClickPanel("ModeTwo").Both());  // nobody is the way out for good
        EXPECT_FALSE(Opponent().GetError().has_value());
        EXPECT_EQ(PanelText("OpponentError"), "");
    }

    TEST_F(ChessUiTest, TheSentencesOfTheErrorLineAreTheOnesOfTheOpponentText) {
        // The line shows OpponentText::Error of the error that stands, and nothing else
        const auto engine = eastl::make_shared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));
        ASSERT_TRUE(OpenThePanel());
        EXPECT_EQ(PanelText("OpponentError"), OpponentText::Error(*Opponent().GetError()));
        EXPECT_EQ(PanelText("OpponentError"), Var("status"));
    }

}  // namespace BECore::Chess::Tests
