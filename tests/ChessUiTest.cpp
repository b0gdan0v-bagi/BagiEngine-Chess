#include "ChessTestUtil.h"
#include "HudWords.h"
#include "TestEngines.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/ViewTransform.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
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
#include <Chess/Game/BoardCoordinates.h>
#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/PromotionChoices.h>
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
        const Color kMenuColor{45, 75, 120, 255};           // the fills of the buttons of the HUD (SceneConfig.xml): a color each, so a test can tell where each one is drawn
        const Color kPlateColor{36, 40, 52, 255};           // the plate of the opponent panel
        const Color kPromotionPlateColor{44, 38, 52, 255};  // the plate of the promotion panel
        constexpr float kMinGap = 8.0f;                     // two controls of the HUD keep this much between them, in view units
        constexpr float kButtonAir = 8.0f;                  // the text of a button keeps this much from each edge of its fill
        constexpr float kNarrowestWidth = 480.0f;           // the narrowest window of the size matrix, in pixels
        constexpr float kNarrowestHeight = 853.0f;          // 9:16 at that width

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
        constexpr const char* kPromoteAction = "ChessPromote";  // the actions of the promotion panel
        constexpr const char* kCancelPromotionAction = "ChessCancelPromotion";

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

            explicit CanvasCopy(const std::string& xml, bool useSafeArea = true) {
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
                layout.params.useSafeArea = useSafeArea;
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

        /// What is wrong with a modal panel as it is laid out (the plate is the element @p plateName), one problem to a line, nothing when it is right: a control that is not shown at all (an empty
        /// rect: a segment without a `height`), text that its control cuts (the validator does not see it for an ellipsis label; a button keeps kButtonAir between its text and the edge of its fill),
        /// a button under kSegmentHeight, and a plate that leaves the safe area
        eastl::string PanelProblems(const UICanvasLayout& layout, const char* plateName = "Plate") {
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
            if (const UIElement* plate = layout.root->FindByName(PoolString::Intern(plateName))) {
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

        // The squares of the board (ChessBoardComponent.cpp)
        const Color kLightSquareColor{240, 217, 181, 255};
        const Color kDarkSquareColor{181, 136, 99, 255};

        // The letters and numbers at the edges of the board (the node BoardCoordinates of SceneConfig.xml): a label sits in the corner of an edge cell that a piece does not reach
        constexpr float kCellSize = 80.0f;
        constexpr float kBoardLeft = 100.0f;
        constexpr size_t kBoardEdgeCells = kLastBoardIndex + 1;
        constexpr float kPieceWidth = kCellSize * kCellWidth / kCellHeight;  // a piece fitted by Contain is as high as its cell, as wide as the aspect of its atlas cell, and centered
        constexpr float kPieceLeft = (kCellSize - kPieceWidth) * 0.5f;       // 15.5 units from the left edge of the cell
        constexpr float kPieceRight = kPieceLeft + kPieceWidth;              // 64.5 units
        constexpr float kLabelBand = 30.0f;                                  // a rank number lies in the top 30 units of its cell, a file letter in the bottom 30 (the boxes are 2..28 and 52..78)

        /// One label of the board coordinates: its name and the drawn cell it sits in (the rank labels are in the left column, one per row; the file labels in the bottom row, one per column)
        struct BoardLabel {
            eastl::string name;
            bool file = false;
            int column = 0;
            int row = 0;
        };

        eastl::vector<BoardLabel> ExpectedBoardLabels() {
            eastl::vector<BoardLabel> labels;
            for (int row = 0; row <= kLastBoardIndex; ++row) {
                labels.push_back({Format("RankLabel{}", row), false, kLabelColumn, row});
            }
            for (int column = 0; column <= kLastBoardIndex; ++column) {
                labels.push_back({Format("FileLabel{}", column), true, column, kLabelRow});
            }
            return labels;
        }

        /// The color of the text of a label: the other color of the board than its cell has, so it reads on the cell; (column + row) even is a light cell, a1 (column 0, row 7) is dark, the
        /// same in both orientations
        const Color& ContrastColor(int column, int row) {
            return (column + row) % 2 == 0 ? kDarkSquareColor : kLightSquareColor;
        }

        /// What is wrong with the labels of the board as they are laid out, one problem to a line, nothing when it is right: a label that is missing or not shown, a glyph the box cuts
        /// (the validator does not see it for a label that clips), a box that is not in the corner of its edge cell (the top left, left of the piece, for a rank; the bottom right, right of it, for a
        /// file) or leaves the board, and a text color that does not read on the cell
        eastl::string BoardLabelProblems(const UICanvasLayout& layout) {
            eastl::string problems;
            const float scale = layout.GetResolved().scale;
            for (const BoardLabel& spec : ExpectedBoardLabels()) {
                const UIElement* element = layout.root->FindByName(PoolString::Intern(spec.name));
                if (element == nullptr) {
                    problems += Format("{} is missing\n", spec.name.c_str());
                    continue;
                }
                const Rect& rect = element->rect;
                if (rect.IsEmpty()) {
                    problems += Format("{} has an empty rect\n", spec.name.c_str());
                    continue;
                }
                const UIContentFit fit = element->CheckContent(rect.w, rect.h, scale);
                if (fit.hasContent && !fit.fits) {
                    problems += Format("{} cuts '{}': {:.1f} x {:.1f} units at scale {:.3f}\n", spec.name.c_str(), ShownText(*element).c_str(), rect.w, rect.h, scale);
                }
                const float cellX = kBoardLeft + static_cast<float>(spec.column) * kCellSize;
                const float cellY = kBoardTop + static_cast<float>(spec.row) * kCellSize;
                const float low = spec.file ? cellX + kPieceRight : cellX;
                const float high = spec.file ? cellX + kCellSize : cellX + kPieceLeft;
                const float top = spec.file ? cellY + kCellSize - kLabelBand : cellY;
                const float bottom = spec.file ? cellY + kCellSize : cellY + kLabelBand;
                if (rect.x < low - kPixelTolerance || rect.x + rect.w > high + kPixelTolerance || rect.y < top - kPixelTolerance || rect.y + rect.h > bottom + kPixelTolerance) {
                    problems += Format("{} ({:.1f}, {:.1f}, {:.1f}, {:.1f}) is not in the corner x {:.1f}..{:.1f}, y {:.1f}..{:.1f} of its cell\n", spec.name.c_str(), rect.x, rect.y, rect.w, rect.h,
                                       low, high, top, bottom);
                }
                const UILabel* label = element->Cast<UILabel>();
                const Color& expected = ContrastColor(spec.column, spec.row);
                if (label == nullptr || !SameColor(label->textColor, expected)) {
                    problems += Format("{} has not the color {},{},{} that reads on its cell\n", spec.name.c_str(), expected.r, expected.g, expected.b);
                }
            }
            return problems;
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

        /// The canvas of a modal panel of the scene: the node of that name, a canvas that starts hidden
        IntrusivePtr<UICanvasComponent> CanvasOf(const char* nodeName) const {
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(nodeName)) {
                    return node->GetComponent<UICanvasComponent>();
                }
            }
            return {};
        }

        /// An element of the canvas of the node @p nodeName by name (the test fails when it is not there)
        UIElement& ElementOf(const char* nodeName, const char* name) const {
            const auto canvas = CanvasOf(nodeName);
            UIElement* element = canvas ? canvas->GetRoot().FindByName(PoolString::Intern(name)) : nullptr;
            if (element == nullptr) {
                ADD_FAILURE() << name << " is not in " << nodeName;
                std::abort();  // a reference to nothing cannot be returned; the failure above is printed first
            }
            return *element;
        }

        /// The canvas of the opponent panel: the node `OpponentPanel`
        IntrusivePtr<UICanvasComponent> Panel() const {
            return CanvasOf("OpponentPanel");
        }

        /// An element of the opponent panel by name (the test fails when it is not there)
        UIElement& PanelElement(const char* name) const {
            return ElementOf("OpponentPanel", name);
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

        /// The Menu button of the HUD, clicked where it is drawn; whether the panel is shown afterwards
        bool OpenThePanel() {
            if (!ClickButton(kMenuColor)) {
                return false;
            }
            return Panel() && Panel()->_visible;
        }

        /// The Ok button of the panel, clicked where it is laid out; whether the panel is hidden afterwards
        bool CloseThePanel() {
            return ClickPanel("Ok").Both() && Panel() && !Panel()->_visible;
        }

        /// The canvas of the promotion panel: the node `PromotionPanel`
        IntrusivePtr<UICanvasComponent> Promotion() const {
            return CanvasOf("PromotionPanel");
        }

        /// Whether the promotion panel is shown (false when the scene has none)
        bool PromotionShown() const {
            const auto panel = Promotion();
            return panel && panel->_visible;
        }

        /// A control of the promotion panel by name (the test fails when it is not there)
        UIElement& PromotionElement(const char* name) const {
            return ElementOf("PromotionPanel", name);
        }

        /// A control of the promotion panel by name, as the type it must be (the test fails when it is not there)
        template <typename T>
        T& PromotionControl(const char* name) const {
            T* typed = PromotionElement(name).template Cast<T>();
            if (typed == nullptr) {
                ADD_FAILURE() << name << " is not the expected control";
                std::abort();
            }
            return *typed;
        }

        /// A click on a control of the promotion panel where it is laid out
        Taken ClickPromotion(const char* name) {
            return ClickControl(PromotionElement(name));
        }

        /// Starts a game from @p fen and lets the scene deal the pieces out
        void StartFrom(const char* fen) {
            Model().SetPosition(PositionFromFen(fen));
            _manager->UpdateAll();
        }

        /// The piece node whose handle stands on @p square in the model, null when none does
        const ChessPieceComponent* PieceNodeAt(const char* square) const {
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                const auto piece = node->GetComponent<ChessPieceComponent>();
                if (piece && Model().GetSquareOf(piece.Get()) == Sq(square)) {
                    return piece.Get();
                }
            }
            return nullptr;
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

        /// The letters or the numbers at the edge of the board as the 16 variables read after a frame: coordFile0..7 (files, by column) or coordRank0..7 (ranks, by row from the top), joined
        eastl::string Coordinates(bool files) {
            Draw();
            eastl::string text;
            for (int index = 0; index <= kLastBoardIndex; ++index) {
                const eastl::string name = files ? "coordFile{}"_format(index) : "coordRank{}"_format(index);
                const eastl::string_view value = CoreManager::GetUIVars().Get(PoolString::Intern(eastl::string_view(name.data(), name.size())));
                text.append(value.data(), value.size());
            }
            return text;
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

        /// One string member of "opponent" of get_state ("plays", "level", "engine"); "<missing>" for null or absent
        std::string ControlOpponentString(const char* key) const {
            const auto value = GetJsonString(ControlOpponent(), key);
            return value ? std::string(value->c_str(), value->size()) : std::string("<missing>");
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

        /// A press of the mouse on the middle of a cell of the drawn board, named by its place on the screen and not by its square
        void ClickCell(int column, int rowFromTop) {
            Draw();
            const Rect cell = CellRect(column, rowFromTop);
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(cell.x + cell.w * 0.5f, cell.y + cell.h * 0.5f, true));
        }

        /// Where the transform of a piece node is, in view units: the cell the piece is drawn in (ChessPieceComponent writes it in an update)
        std::pair<float, float> PieceOrigin(const char* nodeName) const {
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(nodeName)) {
                    const auto transform = node->GetComponent<TransformComponent>();
                    EXPECT_TRUE(transform) << nodeName;
                    return transform ? std::pair<float, float>{transform->_x, transform->_y} : std::pair<float, float>{};
                }
            }
            ADD_FAILURE() << "no piece node " << nodeName;
            return {};
        }

        /// Every piece node that stands on a square is at the cell that square has in the orientation asked for (after an update)
        void ExpectPiecesAtTheCellsOf(bool blackAtTheBottom) const {
            size_t checked = 0;
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                const auto piece = node->GetComponent<ChessPieceComponent>();
                const auto transform = node->GetComponent<TransformComponent>();
                if (!piece || !transform) {
                    continue;
                }
                const Square square = Model().GetSquareOf(piece.Get());
                if (!square.IsInside()) {
                    continue;
                }
                const auto [column, row] = CellOfSquare(square, blackAtTheBottom);
                const auto [x, y] = CellOrigin(column, row);
                EXPECT_NEAR(transform->_x, x, kPixelTolerance) << node->GetName().CStr();
                EXPECT_NEAR(transform->_y, y, kPixelTolerance) << node->GetName().CStr();
                ++checked;
            }
            EXPECT_GT(checked, 0u);
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
            ASSERT_NE(HudElement("Menu"), nullptr);
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
        eastl::string PanelProblemsOnMatrix(UICanvasLayout& layout, size_t reportedCases = kReportedCases, const char* plateName = "Plate") const {
            eastl::string report;
            size_t reported = 0;
            const HudWords words = AllHudWords();
            for (size_t round = 0; round < words.Rounds(); ++round) {
                words.Publish(round);
                OnEveryCaseOfTheMatrix(layout, [&](const eastl::string& label, const UICanvasLayout& laidOut) {
                    const eastl::string problems = PanelProblems(laidOut, plateName);
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

        /// The canvas of the letters and numbers at the edges of the board: the node `BoardCoordinates`
        IntrusivePtr<UICanvasComponent> Coordinate() const {
            return CanvasOf("BoardCoordinates");
        }

        /// A label of the board coordinates by name; null, with a failure recorded, when it is not a label (the caller stops with `ASSERT_NE`, so one test fails and the rest run)
        UILabel* CoordinateLabel(const char* name) const {
            const auto canvas = Coordinate();
            UIElement* element = canvas ? canvas->GetRoot().FindByName(PoolString::Intern(name)) : nullptr;
            UILabel* label = element != nullptr ? element->Cast<UILabel>() : nullptr;
            if (label == nullptr) {
                ADD_FAILURE() << name << " is not a label of BoardCoordinates";
            }
            return label;
        }

        /// A rect of the view in output pixels, for the fit of the view into the current output
        Rect OutputRect(const Rect& view) const {
            return ViewTransform::Fit(kViewWidth, kViewHeight, _renderer->width, _renderer->height).ToOutput(view.x, view.y, view.w, view.h);
        }

        /// Sets the 16 variables of the labels to round @p round of the 8: every letter and every digit stands at every label once over the rounds, so the widest one is laid out in each box
        static void PublishCoordinateRound(size_t round) {
            const eastl::string_view letters = kFileLetters;
            const eastl::string_view digits = kRankDigits;
            for (int index = 0; index <= kLastBoardIndex; ++index) {
                const size_t at = (static_cast<size_t>(index) + round) % letters.size();
                CoreManager::GetUIVars().Set(PoolString::Intern(eastl::string("coordFile{}"_format(index))), letters.substr(at, 1));
                CoreManager::GetUIVars().Set(PoolString::Intern(eastl::string("coordRank{}"_format(index))), digits.substr(at, 1));
            }
        }

        /// BoardLabelProblems() on every case of the matrix for each of the 8 rounds of letters: the lines of the first @p reportedCases cases that have any
        eastl::string BoardLabelProblemsOnMatrix(UICanvasLayout& layout, size_t reportedCases = kReportedCases) const {
            eastl::string report;
            size_t reported = 0;
            for (size_t round = 0; round < kBoardEdgeCells; ++round) {
                PublishCoordinateRound(round);
                OnEveryCaseOfTheMatrix(layout, [&](const eastl::string& label, const UICanvasLayout& laidOut) {
                    const eastl::string problems = BoardLabelProblems(laidOut);
                    if (!problems.empty() && reported++ < reportedCases) {
                        report += Format("round {}, {}:\n{}", round, label.c_str(), problems.c_str());
                    }
                });
            }
            return report;
        }

        void DrawAt(float width, float height) {
            _renderer->width = width;
            _renderer->height = height;
            Draw();
        }

        /// The color at the bottom of the board as the opponent says it: Black exactly when the program plays White (taken from the controller, as the board does)
        bool BlackIsAtTheBottom() const {
            const OpponentController* opponent = ChessGame::GetActiveOpponent();
            return opponent != nullptr && opponent->GetPlays() == std::optional<PieceColor>(PieceColor::White);
        }

        /// The top left corner of a cell of the board in view units: @p column from the left, @p rowFromTop from the top
        static std::pair<float, float> CellOrigin(int column, int rowFromTop) {
            return {100.0f + static_cast<float>(column) * 80.0f, kBoardTop + static_cast<float>(rowFromTop) * 80.0f};
        }

        /// The cell a square is drawn in, written out here and not taken from BoardOrientation.h: White at the bottom has a1 at the bottom left, Black at the bottom has the grid turned by 180 degrees
        static std::pair<int, int> CellOfSquare(const Square& s, bool blackAtTheBottom) {
            return blackAtTheBottom ? std::pair<int, int>{7 - s.file, s.rank} : std::pair<int, int>{s.file, 7 - s.rank};
        }

        /// A cell of the board in output pixels, for the fit of the view into the current output
        Rect CellRect(int column, int rowFromTop) const {
            const ViewTransform fit = ViewTransform::Fit(kViewWidth, kViewHeight, _renderer->width, _renderer->height);
            const auto [x, y] = CellOrigin(column, rowFromTop);
            return fit.ToOutput(x, y, 80.0f, 80.0f);
        }

        /// A square of the board in output pixels, for the fit of the view into the current output and the orientation the opponent gives
        Rect SquareRect(const char* square) const {
            const auto [column, row] = CellOfSquare(Sq(square), BlackIsAtTheBottom());
            return CellRect(column, row);
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
        // The Menu button is always shown and says "Menu": there is no second state of the HUD to walk
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
        for (const char* name : {"Turn", "Menu", "Reset", "Status"}) {
            const UIElement* element = layout.root->FindByName(PoolString::Intern(name));
            ASSERT_NE(element, nullptr) << name;
            EXPECT_FALSE(element->rect.IsEmpty()) << name << " is shown";
        }
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, TheMenuButtonIsAFixedWordThatOpensThePanelAndTheOldOpponentButtonIsGone) {
        Draw();
        const UIElement* element = HudElement("Menu");
        ASSERT_NE(element, nullptr);
        const UIButton* menu = element->Cast<UIButton>();
        ASSERT_NE(menu, nullptr) << "the Menu button is a UIButton";
        EXPECT_EQ(menu->text, "Menu") << "a literal: not bound to a variable";
        EXPECT_EQ(ShownText(*menu), "Menu");
        EXPECT_EQ(menu->action, PoolString::Intern("ChessOpenOpponent"));
        EXPECT_NEAR(menu->rect.w, 110.0f, 0.5f) << "as wide as Reset";
        EXPECT_NEAR(menu->rect.h, 56.0f, 0.5f);
        EXPECT_EQ(HudElement("Opponent"), nullptr) << "the button was called Opponent when it showed the state; the name is not used any more";
        EXPECT_EQ(Panel()->GetRoot().FindByName(PoolString::Intern("Menu")), nullptr) << "the name Menu is the HUD's alone";
    }

    TEST_F(ChessUiTest, TheHudStaysOutsideTheBoardWithoutInsets) {
        auto canvas = Hud();
        ASSERT_TRUE(canvas);
        _manager->UpdateAll();

        for (const auto& size : {std::pair{1280.0f, 720.0f}, std::pair{720.0f, 1280.0f}, std::pair{1024.0f, 768.0f}, std::pair{800.0f, 800.0f}, std::pair{2560.0f, 1080.0f}}) {
            DrawAt(size.first, size.second);
            for (const char* name : {"Turn", "Menu", "Reset"}) {
                const UIElement* element = canvas->GetRoot().FindByName(PoolString::Intern(name));
                ASSERT_NE(element, nullptr);
                ASSERT_FALSE(element->rect.IsEmpty()) << name << " is shown at " << size.first << "x" << size.second;
                EXPECT_LE(element->rect.y + element->rect.h, kBoardTop + 0.01f) << name << " at " << size.first << "x" << size.second;
            }
            const UIElement* status = canvas->GetRoot().FindByName(PoolString::Intern("Status"));
            ASSERT_NE(status, nullptr);
            ASSERT_FALSE(status->rect.IsEmpty()) << "Status is shown at " << size.first << "x" << size.second;
            EXPECT_GE(status->rect.y, kBoardBottom - 0.01f) << "Status at " << size.first << "x" << size.second;
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
        for (const char* name : {"Turn", "Menu", "Reset", "Side", "Engine", "Status", "Level"}) {
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
        EXPECT_EQ(HudElement("Menu")->Cast<UIButton>()->action, PoolString::Intern("ChessOpenOpponent"));
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

    // ---- the orientation of the board ---------------------------------------------------------------------------------------------
    // The board is drawn from Black's side exactly when the program plays White. The expected cells are written out in the fixture (CellOfSquare), not taken from BoardOrientation.h,
    // and the clicks go through the real mouse path

    namespace {

        const Color kBoardDimColor{0, 0, 0, 140};      // the dim of the result (ChessResultComponent.cpp)
        const Color kResultBarColor{25, 25, 25, 245};  // the bar of the winner (ChessResultComponent.cpp)

        constexpr const char* kBlackToMoveAfterE4 = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
        constexpr const char* kFourRooks = "r3k2r/8/8/8/8/8/4K3/r6r b - - 0 1";  // Black to move, a rook in each corner of the board
        constexpr const char* kBlackInCheck = "4k3/8/8/8/8/8/8/4RK2 b - - 0 1";
        constexpr const char* kBlackPromotesOnE1 = "7k/8/8/8/8/8/4p3/K7 b - - 0 1";
        constexpr const char* kBlackMatesOnA1 = "r5k1/8/8/8/8/8/5PPP/6K1 b - - 0 1";  // Ra1 is mate

    }  // namespace

    TEST_F(ChessUiTest, WithTheProgramAsWhiteTheBoardAndThePiecesAreTurnedBy180Degrees) {
        CountingFactory factory({eastl::make_shared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (std::pair<float, float>{100.0f, 640.0f})) << "two people: a1 at the bottom left";

        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());

        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (std::pair<float, float>{660.0f, 80.0f})) << "a1 at the top right";
        EXPECT_EQ(PieceOrigin("WhiteRook_h1"), (std::pair<float, float>{100.0f, 80.0f})) << "h1 at the top left";
        EXPECT_EQ(PieceOrigin("BlackRook_a8"), (std::pair<float, float>{660.0f, 640.0f})) << "a8 at the bottom right";
        ExpectPiecesAtTheCellsOf(true);

        // The squares of the board: the cell of a1 is the one of h8 in the other orientation, with the color of a1 (dark) in both
        Draw();
        const Rect a1 = SquareRect("a1");
        const Rect h8 = SquareRect("h8");
        EXPECT_GT(a1.x, h8.x);
        EXPECT_LT(a1.y, h8.y);
        const auto fillOf = [this](const Rect& cell) -> std::optional<Color> {
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

    TEST_F(ChessUiTest, AClickOnTheDrawnSquareSelectsThatSquareInTheTurnedBoard) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
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
        CountingFactory factory({eastl::make_shared<GatedEngine>(true)});
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
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
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
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
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

    TEST_F(ChessUiTest, APromotionInTheTurnedBoardAsksInAPlateThatDoesNotMove) {
        const auto plateOf = [this]() -> std::optional<Rect> {
            Draw();
            const auto plate = RectCalls(kPromotionPlateColor);
            return plate.empty() ? std::nullopt : std::optional<Rect>(plate.front()->rect);
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

        CountingFactory factory({eastl::make_shared<InstantEngine>()});
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

    TEST_F(ChessUiTest, TheDimAndTheResultBarCoverTheTurnedBoardTheSame) {
        const auto dimOf = [this]() -> std::optional<Rect> {
            Draw();
            const auto dim = RectCalls(kBoardDimColor);
            return dim.empty() ? std::nullopt : std::optional<Rect>(dim.front()->rect);
        };

        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        StartFrom(kBlackMatesOnA1);
        ASSERT_TRUE(BlackIsAtTheBottom());

        ClickMove("a8", "a1");

        ASSERT_TRUE(Model().IsGameOver());
        EXPECT_EQ(Model().GetWinner(), std::optional<PieceColor>(PieceColor::Black));
        const auto dim = dimOf();
        ASSERT_TRUE(dim.has_value());
        const ViewTransform fit = ViewTransform::Fit(kViewWidth, kViewHeight, _renderer->width, _renderer->height);
        EXPECT_TRUE(SameRect(*dim, fit.ToOutput(100.0f, kBoardTop, 640.0f, 640.0f))) << "the whole board, as with White at the bottom";
        EXPECT_FALSE(RectCalls(kResultBarColor).empty()) << "the bar of the winner";
    }

    TEST_F(ChessUiTest, TheBoardTurnsBackWhenNobodyOrBlackIsTheProgram) {
        CountingFactory factory({eastl::make_shared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(false);

        ControlPlays("white");
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(true);
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (std::pair<float, float>{660.0f, 80.0f}));

        ControlPlays(nullptr);
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(false);
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (std::pair<float, float>{100.0f, 640.0f})) << "nobody plays: White at the bottom again";

        ControlPlays("black");
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(false);
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (std::pair<float, float>{100.0f, 640.0f})) << "the program plays Black: the person has White at the bottom";
        EXPECT_FALSE(BlackIsAtTheBottom());
    }

    TEST_F(ChessUiTest, TheColorSegmentsAndTheModeTurnTheBoardAtTheNextFrame) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        PressWith("ChessSetMode", "computer");  // the person has White, the program plays Black
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (std::pair<float, float>{100.0f, 640.0f})) << "a1 at the bottom left";

        PressWith("ChessSetColor", "black");
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (std::pair<float, float>{660.0f, 80.0f})) << "the person has Black: the board is turned";
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 1; })) << "and the program, now White, opens";
        _manager->UpdateAll();  // the pieces follow the move of this frame in the next one
        ExpectPiecesAtTheCellsOf(true);

        PressWith("ChessSetColor", "white");
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (std::pair<float, float>{100.0f, 640.0f})) << "the person has White again";
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2 && !Opponent().IsThinking(); })) << "the program, now Black, answers the opening";
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(false);  // every piece, the moved ones too
    }

    TEST_F(ChessUiTest, ABlackPickedWhileTwoPlayDoesNotTurnTheBoardUntilComputerIsPicked) {
        CountingFactory factory({eastl::make_shared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        PressWith("ChessSetColor", "black");
        _manager->UpdateAll();
        EXPECT_EQ(Var("opponentColor"), "black");
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (std::pair<float, float>{100.0f, 640.0f})) << "nobody plays yet: nothing turns";

        PressWith("ChessSetMode", "computer");
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (std::pair<float, float>{660.0f, 80.0f})) << "now the program plays White";

        PressWith("ChessSetMode", "two");
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (std::pair<float, float>{100.0f, 640.0f})) << "two people: White at the bottom";
        EXPECT_EQ(Var("opponentColor"), "black") << "the color stays remembered";
    }

    TEST_F(ChessUiTest, ARefusedPickLeavesTheOrientationAsItWas) {
        eastl::vector<eastl::string> asked;
        Opponent().SetEngineFactory([&asked](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            const eastl::string_view id = OpponentEngineId(engine);
            asked.emplace_back(id.data(), id.size());
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
            }
            return eastl::make_shared<GatedEngine>(true);
        });
        _manager->UpdateAll();

        // Computer with a Stockfish that is not there changes nothing, a color picked before it included
        PressWith("ChessSetColor", "black");
        PressWith("ChessSetMode", "computer");
        _manager->UpdateAll();
        ASSERT_EQ(asked.size(), 1u);
        EXPECT_EQ(Var("opponentMode"), "two") << "the pick was refused";
        EXPECT_FALSE(BlackIsAtTheBottom());
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (std::pair<float, float>{100.0f, 640.0f}));

        // The way on is the own engine; the board turns with Computer. A refused Stockfish pick afterwards leaves it turned
        PressWith("ChessSetEngine", "own");
        PressWith("ChessSetMode", "computer");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (std::pair<float, float>{660.0f, 80.0f}));
        PressWith("ChessSetEngine", "stockfish");
        _manager->UpdateAll();
        EXPECT_EQ(Var("opponentEngine"), "own") << "refused";
        EXPECT_TRUE(BlackIsAtTheBottom());
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (std::pair<float, float>{660.0f, 80.0f}));
    }

    TEST_F(ChessUiTest, ResetAndSetStateKeepTheTurnedBoardAndDealThePiecesToItsCells) {
        CountingFactory factory({eastl::make_shared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());

        Press("ChessReset");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom()) << "a reset keeps the opponent, so the orientation";
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (std::pair<float, float>{660.0f, 80.0f}));
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
        CountingFactory factory({eastl::make_shared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        EXPECT_EQ(Coordinates(true), "hgfedcba");
        EXPECT_EQ(Coordinates(false), "12345678");
    }

    TEST_F(ChessUiTest, TheCoordinateVariablesTurnBackWithTwoPeopleAndWithTheProgramAsBlack) {
        CountingFactory factory({eastl::make_shared<GatedEngine>(true)});
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
        CountingFactory factory({eastl::make_shared<GatedEngine>(true)});
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
        CountingFactory factory({eastl::make_shared<GatedEngine>(true)});
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
    // The node BoardCoordinates of the shipped scene is a canvas of 16 UILabels at the edges of the board (SceneConfig.xml has the reasons for its numbers). They are checked the way the HUD
    // is: the validator and CheckContent() on the size matrix, then the geometry against the board and the pieces, the colors against the squares that are drawn, the order of a frame, and that
    // a click goes through them to the board

    namespace {

        /// A glyph of a label: a texture drawn with one of the two colors of the squares as its tint (the pieces are drawn white, and the text of the HUD too)
        bool IsLabelGlyph(const DrawCall& call) {
            return call.kind == DrawKind::Texture && (SameColor(call.color, kLightSquareColor) || SameColor(call.color, kDarkSquareColor));
        }

        bool Inside(const Rect& inner, const Rect& outer) {
            return inner.x >= outer.x - 1.0f && inner.y >= outer.y - 1.0f && inner.x + inner.w <= outer.x + outer.w + 1.0f && inner.y + inner.h <= outer.y + outer.h + 1.0f;
        }

    }  // namespace

    TEST_F(ChessUiTest, TheBoardCoordinatesPassTheSizeMatrixWithEveryLetterAndDigit) {
        Draw();  // the board publishes the variables
        auto canvas = Coordinate();
        ASSERT_TRUE(canvas) << "the shipped scene has the node BoardCoordinates";
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update

        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        for (size_t round = 0; round < kBoardEdgeCells; ++round) {
            PublishCoordinateRound(round);
            const auto failures = ValidateOnMatrix(canvas->GetLayout(), options, ViewContext());
            EXPECT_TRUE(failures.empty()) << "round " << round << ": " << DescribeMatrixFailures(failures).c_str();
        }
        const eastl::string problems = BoardLabelProblemsOnMatrix(canvas->GetLayout());
        EXPECT_TRUE(problems.empty()) << problems.c_str();
    }

    TEST_F(ChessUiTest, NoCoordinateLabelIsCutInTheNarrowestWindow) {
        Draw();
        auto canvas = Coordinate();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = canvas->GetLayout();
        LayOutInTheNarrowestWindow(layout);
        EXPECT_NEAR(layout.GetResolved().scale, kNarrowestWidth / kViewWidth, 0.01f) << "about 10 px for a font of 18 units";

        for (size_t round = 0; round < kBoardEdgeCells; ++round) {
            PublishCoordinateRound(round);
            const eastl::string problems = BoardLabelProblems(layout);
            EXPECT_TRUE(problems.empty()) << "round " << round << ":\n" << problems.c_str();
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
            EXPECT_FALSE(element->interactive) << spec.name.c_str();
            EXPECT_NE(element->Cast<UILabel>(), nullptr) << spec.name.c_str();
        }
        EXPECT_EQ(labels, 2 * kBoardEdgeCells);
        EXPECT_EQ(canvas->GetRoot().children.size(), 2 * kBoardEdgeCells) << "nothing else is in the canvas";
    }

    TEST_F(ChessUiTest, EveryCoordinateLabelIsInTheCornerOfItsEdgeCellOnTheShippedScene) {
        Draw();
        for (const BoardLabel& spec : ExpectedBoardLabels()) {
            SCOPED_TRACE(spec.name.c_str());
            const UILabel* found = CoordinateLabel(spec.name.c_str());
            ASSERT_NE(found, nullptr);
            const UILabel& label = *found;
            const float cellX = kBoardLeft + static_cast<float>(spec.column) * kCellSize;
            const float cellY = kBoardTop + static_cast<float>(spec.row) * kCellSize;
            const auto [expectedX, expectedY] = CellOrigin(spec.column, spec.row);
            EXPECT_FLOAT_EQ(cellX, expectedX);
            EXPECT_FLOAT_EQ(cellY, expectedY);
            EXPECT_GE(label.rect.x, spec.file ? cellX + kPieceRight : cellX) << "the rank numbers are left of the piece, the file letters right of it";
            EXPECT_LE(label.rect.x + label.rect.w, spec.file ? cellX + kCellSize : cellX + kPieceLeft);
            EXPECT_GE(label.rect.y, spec.file ? cellY + kCellSize - kLabelBand : cellY) << "the rank numbers are at the top of the cell, the file letters at the bottom";
            EXPECT_LE(label.rect.y + label.rect.h, spec.file ? cellY + kCellSize : cellY + kLabelBand);
            EXPECT_GE(label.rect.x, kBoardLeft) << "inside the board";
            EXPECT_LE(label.rect.x + label.rect.w, kBoardLeft + static_cast<float>(kBoardEdgeCells) * kCellSize);
            EXPECT_GE(label.rect.y, kBoardTop);
            EXPECT_LE(label.rect.y + label.rect.h, kBoardBottom);
        }
        EXPECT_EQ(BoardLabelProblems(Coordinate()->GetLayout()), "") << "the same checks the matrix runs";
    }

    TEST_F(ChessUiTest, NoCoordinateGlyphReachesAPieceInEitherOrientation) {
        // The width the labels keep clear is the width of a piece that Contain fits into a cell: the real draw of a rook says so
        const auto whiteA1 = GiveAtlasCell("WhiteRook_a1");
        const auto whiteH1 = GiveAtlasCell("WhiteRook_h1");
        const auto blackA8 = GiveAtlasCell("BlackRook_a8");
        const auto blackH8 = GiveAtlasCell("BlackRook_h8");
        const ITexture* const rooks[] = {whiteA1.Get(), whiteH1.Get(), blackA8.Get(), blackH8.Get()};
        CountingFactory factory({eastl::make_shared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());

        for (const bool turned : {false, true}) {
            SCOPED_TRACE(turned ? "the program plays White: the board is turned" : "two people");
            ControlPlays(turned ? "white" : nullptr);
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

    TEST_F(ChessUiTest, TheCoordinateColorsReadOnTheSquaresThatAreDrawnInBothOrientations) {
        CountingFactory factory({eastl::make_shared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());

        for (const bool turned : {false, true}) {
            SCOPED_TRACE(turned ? "the program plays White: the board is turned" : "two people");
            ControlPlays(turned ? "white" : nullptr);
            _manager->UpdateAll();
            Draw();

            for (const BoardLabel& spec : ExpectedBoardLabels()) {
                SCOPED_TRACE(spec.name.c_str());
                const UILabel* found = CoordinateLabel(spec.name.c_str());
                ASSERT_NE(found, nullptr);
                const UILabel& label = *found;
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
                EXPECT_TRUE(SameColor(label.textColor, light ? kDarkSquareColor : kLightSquareColor)) << "dark brown on a light cell, cream on a dark one";

                // and the glyph that is drawn in the label's box has that color
                const Rect box = OutputRect(label.rect);
                size_t inBox = 0;
                for (const DrawCall& call : _renderer->calls) {
                    if (call.kind == DrawKind::Texture && Inside(call.rect, box)) {
                        ++inBox;
                        EXPECT_TRUE(SameColor(call.color, label.textColor));
                        EXPECT_TRUE(Inside(call.rect, cell)) << "inside the cell it labels";
                    }
                }
                EXPECT_EQ(inBox, 1u) << "one glyph in the box";
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
        Draw();
        const auto canvas = Coordinate();
        ASSERT_TRUE(canvas);
        for (const BoardLabel& spec : ExpectedBoardLabels()) {
            const UILabel* label = CoordinateLabel(spec.name.c_str());
            ASSERT_NE(label, nullptr);
            const Rect& rect = label->rect;  // canvas units: the view units of a SceneView canvas
            EXPECT_EQ(canvas->GetLayout().HitTest(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f), nullptr) << spec.name.c_str() << " is not found by a hit test";
        }

        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        StartFrom(kFourRooks);  // a rook of Black in each corner, Black to move
        ASSERT_EQ(Model().GetTurn(), PieceColor::Black);
        const struct {
            bool turned;
            const char* label;
            const char* square;
        } presses[] = {{false, "RankLabel0", "a8"}, {false, "FileLabel0", "a1"}, {false, "FileLabel7", "h1"}, {false, "RankLabel7", "a1"},
                       {true, "RankLabel0", "h1"},  {true, "FileLabel0", "h8"},  {true, "FileLabel7", "a8"},  {true, "RankLabel7", "h8"}};
        for (const auto& press : presses) {
            SCOPED_TRACE(Format("{} with the board {}", press.label, press.turned ? "turned" : "upright").c_str());
            ControlPlays(press.turned ? "white" : nullptr);
            _manager->UpdateAll();
            Model().ClearSelection();
            Draw();
            const UILabel* label = CoordinateLabel(press.label);
            ASSERT_NE(label, nullptr);
            const auto [x, y] = ScreenCenter(*label);
            EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true))) << "the canvas does not take the press";
            EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false)));
            ASSERT_TRUE(Model().HasSelection()) << "the board got it";
            EXPECT_EQ(Model().GetSelected(), Sq(press.square)) << "the square drawn in that cell";
        }
    }

    TEST_F(ChessUiTest, TheCoordinateLabelsTurnTheirTextAndKeepTheirPlaceOnTheScreen) {
        CountingFactory factory({eastl::make_shared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        const auto read = [&](bool files) {
            Draw();
            eastl::string text;
            for (int index = 0; index <= kLastBoardIndex; ++index) {
                const UILabel* label = CoordinateLabel((files ? "FileLabel{}"_format(index) : "RankLabel{}"_format(index)).c_str());
                if (label != nullptr) {
                    text += ShownText(*label);
                }
            }
            return text;
        };
        const auto rects = [&] {
            eastl::vector<Rect> found;
            for (const BoardLabel& spec : ExpectedBoardLabels()) {
                const UILabel* label = CoordinateLabel(spec.name.c_str());
                found.push_back(label != nullptr ? label->rect : Rect{});
            }
            return found;
        };

        EXPECT_EQ(read(true), "abcdefgh") << "White at the bottom: a..h from the left";
        EXPECT_EQ(read(false), "87654321") << "8 at the top";
        const eastl::vector<Rect> upright = rects();

        ControlPlays("white");
        _manager->UpdateAll();
        EXPECT_EQ(read(true), "hgfedcba") << "the board is turned by 180 degrees: h..a from the left";
        EXPECT_EQ(read(false), "12345678") << "1 at the top";
        const eastl::vector<Rect> turned = rects();
        ASSERT_EQ(turned.size(), upright.size());
        for (size_t i = 0; i < turned.size(); ++i) {
            EXPECT_TRUE(SameRect(turned[i], upright[i])) << "label " << i << " is fixed on the screen";
        }

        ControlPlays("black");
        _manager->UpdateAll();
        EXPECT_EQ(read(true), "abcdefgh");
        EXPECT_EQ(read(false), "87654321");
    }

    TEST_F(ChessUiTest, TheCoordinateColorsAreTheSameWhenTheBoardIsTurned) {
        // The turn by 180 degrees keeps the color of every cell, which is why the colors of the labels are fixed in the XML
        CountingFactory factory({eastl::make_shared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        Draw();
        eastl::vector<Color> before;
        for (const BoardLabel& spec : ExpectedBoardLabels()) {
            const UILabel* label = CoordinateLabel(spec.name.c_str());
            ASSERT_NE(label, nullptr);
            before.push_back(label->textColor);
        }
        ControlPlays("white");
        _manager->UpdateAll();
        Draw();
        size_t index = 0;
        for (const BoardLabel& spec : ExpectedBoardLabels()) {
            const UILabel* label = CoordinateLabel(spec.name.c_str());
            ASSERT_NE(label, nullptr);
            EXPECT_TRUE(SameColor(label->textColor, before[index++])) << spec.name.c_str();
        }
    }

    // ---- the board coordinates: broken copies, so that the checks can fail ---------------------------------------------------------------------

    TEST_F(ChessUiTest, ABrokenCopyOfTheCoordinateLabelsIsCaughtByTheChecks) {
        Draw();  // the variables the labels show
        const std::string shipped = ShippedElementsXml("BoardCoordinates");
        ASSERT_FALSE(shipped.empty());
        const auto problemsOf = [&](const std::string& xml) {
            CanvasCopy copy(xml, false);
            return BoardLabelProblemsOnMatrix(copy.layout, 100000);
        };

        EXPECT_EQ(problemsOf(shipped), "") << "the same checks pass on the shipped labels";

        std::string bigFont = shipped;
        Replace(bigFont, "fontSize=\"18\"", "fontSize=\"60\"");
        EXPECT_NE(problemsOf(bigFont).find("RankLabel0 cuts"), eastl::string::npos) << "a font of 60 does not fit the box";

        std::string narrow = shipped;
        Replace(narrow, "name=\"RankLabel0\" x=\"102\" y=\"82\" width=\"13\"", "name=\"RankLabel0\" x=\"102\" y=\"82\" width=\"3\"");
        EXPECT_NE(problemsOf(narrow).find("RankLabel0 cuts"), eastl::string::npos) << "a box of 3 units cuts the glyph";

        std::string wide = shipped;
        Replace(wide, "name=\"RankLabel0\" x=\"102\" y=\"82\" width=\"13\"", "name=\"RankLabel0\" x=\"102\" y=\"82\" width=\"30\"");
        EXPECT_NE(problemsOf(wide).find("RankLabel0 (102.0, 82.0, 30.0, 26.0) is not in the corner"), eastl::string::npos) << "a box that reaches the piece";

        std::string inside = shipped;
        Replace(inside, "name=\"RankLabel3\" x=\"102\"", "name=\"RankLabel3\" x=\"182\"");
        EXPECT_NE(problemsOf(inside).find("RankLabel3 (182.0"), eastl::string::npos) << "a label under a cell that is not at the edge";

        std::string rankAtTheBottom = shipped;
        Replace(rankAtTheBottom, "name=\"RankLabel0\" x=\"102\" y=\"82\"", "name=\"RankLabel0\" x=\"102\" y=\"132\"");
        EXPECT_NE(problemsOf(rankAtTheBottom).find("RankLabel0 (102.0, 132.0"), eastl::string::npos) << "a rank number in the bottom left of its cell is not in the corner";

        std::string fileAtTheTop = shipped;
        Replace(fileAtTheTop, "name=\"FileLabel7\" x=\"725\" y=\"692\"", "name=\"FileLabel7\" x=\"725\" y=\"642\"");
        EXPECT_NE(problemsOf(fileAtTheTop).find("FileLabel7 (725.0, 642.0"), eastl::string::npos) << "a file letter in the top right of its cell is not in the corner";

        std::string wrongColor = shipped;
        Replace(wrongColor, "textColor=\"181,136,99,255\"", "textColor=\"240,217,181,255\"");
        EXPECT_NE(problemsOf(wrongColor).find("RankLabel0 has not the color"), eastl::string::npos) << "cream on a light cell";

        std::string missing = shipped;
        Replace(missing, "name=\"FileLabel7\"", "name=\"FileLabelX\"");
        EXPECT_NE(problemsOf(missing).find("FileLabel7 is missing"), eastl::string::npos);
    }

    TEST_F(ChessUiTest, TheHudStillStaysOutsideTheBoardWithTheCoordinatesOnIt) {
        // The labels are over the board and the HUD is in its margins; neither is moved by the other (the HUD tests above are unchanged)
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

    TEST_F(ChessUiTest, TheStateOfTheControlApiIsTheSameWhateverSideIsAtTheBottom) {
        CountingFactory factory({eastl::make_shared<GatedEngine>(true)});
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
            EXPECT_EQ((*board)[row].get_ref<const std::string&>(), expected[row]) << "row " << row << ": rank 8 first, white upper case";
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
    // The HUD shows `turn` and `status` and the panel the ids of the choice; the layout is another matter, so these tests press the actions the segments emit (or
    // click the segments where they are drawn) and read the variables the board publishes. The engines are the doubles of TestEngines.h

    TEST_F(ChessUiTest, TheOpponentWordsStartWithTwoPlayersAtTheMediumLevel) {
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_EQ(Var("opponentLevel"), "medium");
        EXPECT_EQ(Var("turn"), "White to move");
        EXPECT_EQ(Var("status"), "");
    }

    TEST_F(ChessUiTest, TheBoardDoesNotPublishASideVariable) {
        // The board publishes no variable named `side`, at the start or after a pick
        Draw();
        EXPECT_TRUE(CoreManager::GetUIVars().Has(PoolString::Intern("opponentMode"))) << "the board publishes its variables: the check below is not about a missing frame";
        EXPECT_FALSE(CoreManager::GetUIVars().Has(PoolString::Intern("side")));
        PressWith("ChessSetLevel", "hard");  // a pick that needs no engine while nobody plays
        Draw();
        EXPECT_FALSE(CoreManager::GetUIVars().Has(PoolString::Intern("side")));
    }

    TEST_F(ChessUiTest, ModeAndColorThroughThePanelPickTheSideTheProgramPlays) {
        // Was TheSideActionWalksThreeWordsAndBackAndTheProgramPlaysTheSideTheWordSays (the cycle is gone with the Side button): the mode and the color are two choices now
        const auto engine = eastl::make_shared<InstantEngine>();
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        EXPECT_EQ(Var("opponentMode"), "computer");
        EXPECT_TRUE(Opponent().PlaysFor(PieceColor::Black));
        ASSERT_TRUE(CloseThePanel());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[1].To()).color, PieceColor::Black) << "the person has White, the program answers with a black piece";

        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("ColorBlack").Both());
        EXPECT_EQ(Var("opponentColor"), "black");
        EXPECT_TRUE(Opponent().PlaysFor(PieceColor::White));
        ASSERT_TRUE(CloseThePanel());
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 3; })) << "it is White's turn and White is the program's now";
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[2].To()).color, PieceColor::White);

        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("ModeTwo").Both());
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_FALSE(Opponent().PlaysFor(PieceColor::White));
        EXPECT_FALSE(Opponent().PlaysFor(PieceColor::Black));
        ASSERT_TRUE(CloseThePanel());
        const MoveList legal = MoveGenerator::GenerateLegal(Model().GetPosition());
        ASSERT_FALSE(legal.empty());
        ASSERT_TRUE(Model().TryMove(legal.front()).has_value()) << "Black's move, made by a person";
        IdleFrames();
        EXPECT_EQ(Model().GetMoves().size(), 4u) << "nobody answers it";
        EXPECT_EQ(engine->Calls(), 2) << "one search for each of the two sides, none for two people";

        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        EXPECT_EQ(Var("opponentColor"), "black") << "and round again: the color the person had is kept while two people played";
        ASSERT_TRUE(ClickPanel("ColorWhite").Both());
        EXPECT_EQ(Var("opponentColor"), "white");
        EXPECT_EQ(factory.Calls(), 4) << "an engine for each pick that makes the program play (Computer, Black, Computer, White), none for two people";
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

    TEST_F(ChessUiTest, TheEngineSegmentsPickBothEnginesAndTheDefaultIsStockfish) {
        // Was TheEngineWordStartsAsStockfishAndTheActionWalksTheEnginesAndWraps (the cycle and the `engine` word are gone with the Engine button)
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        EXPECT_EQ(Var("opponentEngine"), "stockfish");
        EXPECT_TRUE(PanelControl<UIButton>("EngineStockfish").selected) << "the default";
        EXPECT_FALSE(PanelControl<UIButton>("EngineOwn").selected);

        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        EXPECT_EQ(Var("opponentEngine"), "own");
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        EXPECT_TRUE(PanelControl<UIButton>("EngineOwn").selected);
        EXPECT_FALSE(PanelControl<UIButton>("EngineStockfish").selected);
        ASSERT_TRUE(ClickPanel("EngineStockfish").Both());
        EXPECT_EQ(Var("opponentEngine"), "stockfish") << "and back";
        EXPECT_TRUE(PanelControl<UIButton>("EngineStockfish").selected);
        EXPECT_EQ(factory.Calls(), 0) << "with nobody as the opponent an engine is only a choice: none is asked for";

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Engines()[0], "stockfish");
        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        EXPECT_EQ(Var("opponentEngine"), "own");
        ASSERT_EQ(factory.Calls(), 2) << "the program plays: a new engine for the new choice";
        EXPECT_EQ(factory.Engines()[1], "own");
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::Black)) << "the side stays";
    }

    TEST_F(ChessUiTest, WhileTheProgramThinksTheTurnSaysSoAndTheAnswerBringsTheUsualWordsBack) {
        const auto engine = eastl::make_shared<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        PressWith(kSetModeAction, "computer");
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

        PressWith(kSetModeAction, "computer");

        EXPECT_EQ(Var("status"), "Engine not found");
        EXPECT_EQ(Var("opponentMode"), "two") << "a pick that found nothing leaves two people on the screen";
        EXPECT_EQ(Var("turn"), "White to move");

        ClickMove("e2", "e4");
        _manager->UpdateAll();

        EXPECT_EQ(Var("status"), "") << "the person has played on: the sentence has been seen";
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_EQ(factory.Calls(), 1);
    }

    TEST_F(ChessUiTest, TheSentenceOfASearchErrorIsInTheStatusUntilSomeoneMovesTheGame) {
        const auto engine = eastl::make_shared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));

        EXPECT_EQ(Var("status"), "Engine not found");
        EXPECT_EQ(Var("opponentMode"), "computer") << "the program is still Black, only the game waits";
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

    TEST_F(ChessUiTest, TheEngineGroupTakesClicksWhetherOrNotTheProgramPlays) {
        // Was TheEngineButtonIsShownWhetherOrNotTheProgramPlays: the Engine button was never hidden; no group of the panel is greyed, and the EngineGroup works in every state
        CountingFactory working({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(working.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());

        const auto engineGroupWorks = [&](const char* state) {
            SCOPED_TRACE(state);
            Draw();
            EXPECT_FALSE(PanelControl<UISegmented>("EngineGroup").disabled);
            EXPECT_FALSE(PanelControl<UIButton>("EngineOwn").rect.IsEmpty());
            ASSERT_TRUE(ClickPanel("EngineOwn").Both());
            EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
            ASSERT_TRUE(ClickPanel("EngineStockfish").Both());
            EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Stockfish);
        };
        engineGroupWorks("nobody plays: it is how the person gets to the own engine");
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        engineGroupWorks("the program plays Black");
        ASSERT_TRUE(ClickPanel("ColorBlack").Both());
        engineGroupWorks("the program plays White");
        ASSERT_TRUE(ClickPanel("ModeTwo").Both());
        engineGroupWorks("two people again");

        CountingFactory failing(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
        Opponent().SetEngineFactory(failing.AsFactory());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        EXPECT_EQ(Var("status"), "Engine not found");
        engineGroupWorks("after a refused pick (nobody was playing, and nothing changed): the group is what the person needs then");
    }

    // ---- the buttons of the opponent, clicked where they are drawn ---------------------------------------------------------------
    // The fill of each button has a color of its own in SceneConfig.xml, which is how a test finds where the screen shows it (the way ClickingTheResetButtonOnTheScreen... does)

    TEST_F(ChessUiTest, ClickingTheModeAndColorSegmentsOnTheScreenPicksTheSideAndAPressAloneDoesNothing) {
        // Was ClickingTheSideButtonOnTheScreenCyclesTheSide
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        Draw();
        ASSERT_EQ(Label("Turn").GetDisplayText(), "White to move");

        const auto at = ScreenCenter(PanelElement("ModeComputer"));
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.first, at.second, true))) << "the segment takes the click";
        EXPECT_FALSE(Opponent().GetPlays().has_value()) << "a press alone does nothing";
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(at.first, at.second, false)));
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::Black));

        EXPECT_TRUE(ClickPanel("ColorBlack").Both());
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::White));

        EXPECT_TRUE(ClickPanel("ModeTwo").Both());
        EXPECT_FALSE(Opponent().GetPlays().has_value());
        EXPECT_FALSE(PanelControl<UIButton>("ModeComputer").rect.IsEmpty()) << "the way back to a pick is still there";
    }

    TEST_F(ChessUiTest, ClickingAnEngineSegmentOnTheScreenPicksTheEngineAndAPressAloneDoesNothing) {
        // Was ClickingTheEngineButtonOnTheScreenCyclesTheEngineAndAPressAloneDoesNothing
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        Draw();
        ASSERT_EQ(ShownText(PanelElement("EngineStockfish")), "Stockfish");
        ASSERT_EQ(ShownText(PanelElement("EngineOwn")), "Own engine");

        const auto at = ScreenCenter(PanelElement("EngineOwn"));
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.first, at.second, true))) << "the segment takes the click";
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Stockfish) << "a press alone does nothing";
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(at.first, at.second, false)));
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        EXPECT_EQ(Var("opponentEngine"), "own");
        EXPECT_TRUE(PanelControl<UIButton>("EngineOwn").selected) << "the selected segment follows";

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());  // the program plays Black
        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Engines()[0], "own");
        EXPECT_TRUE(ClickPanel("EngineStockfish").Both()) << "also while the program plays";
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Stockfish);
        EXPECT_EQ(Var("opponentEngine"), "stockfish");
        EXPECT_TRUE(PanelControl<UIButton>("EngineStockfish").selected);
        ASSERT_EQ(factory.Calls(), 2);
        EXPECT_EQ(factory.Engines()[1], "stockfish");
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::Black)) << "the side stays";
    }

    TEST_F(ChessUiTest, APressOnASegmentDoesNotChangeTheBoardSelection) {
        // Was APressOnTheEngineButtonDoesNotChangeTheSelection
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        Click("e2");
        ASSERT_TRUE(Model().HasSelection());

        ASSERT_TRUE(OpenThePanel());
        EXPECT_TRUE(ClickPanel("EngineOwn").Both());
        EXPECT_TRUE(ClickPanel("ModeComputer").Both());  // the program takes Black, which is not the side to move: the half-made move of the person stays

        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        EXPECT_TRUE(Model().HasSelection());
        EXPECT_TRUE(Model().GetSelected() == Sq("e2"));
        ASSERT_TRUE(CloseThePanel());
        EXPECT_TRUE(Model().HasSelection());
    }

    TEST_F(ChessUiTest, TheOwnEngineOfTheDefaultFactoryPlaysThroughThePanel) {
        // Was TheOwnEngineOfTheDefaultFactoryPlaysThroughTheButtons. No factory is set: the controller makes the real OwnEngine (no process, no settings) at the easy level, so the
        // test needs no Stockfish and no fake program
        const std::optional<size_t> easy = FindOpponentLevel("easy");
        ASSERT_TRUE(easy.has_value());
        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        ASSERT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        EXPECT_EQ(Var("opponentMode"), "computer");
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
        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        ASSERT_TRUE(CloseThePanel());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));

        EXPECT_TRUE(ClickButton(kResetColor));

        EXPECT_EQ(Model().GetPosition().ToFen(), eastl::string(Position::kStartFen));
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own) << "a new game keeps the engine";
        EXPECT_EQ(Var("opponentEngine"), "own");
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::Black));
    }

    TEST_F(ChessUiTest, TheResetButtonStillResetsWithTheMenuButtonAround) {
        // Reset keeps the opponent chosen in the panel; the Menu button beside it does not change that
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        ASSERT_TRUE(CloseThePanel());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));

        EXPECT_TRUE(ClickButton(kResetColor));

        EXPECT_EQ(Model().GetPosition().ToFen(), eastl::string(Position::kStartFen));
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::Black)) << "a new game keeps the opponent";
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
    }

    TEST_F(ChessUiTest, ThePressOfAHudButtonDoesNotReachTheBoardAndTheBoardStillGetsItsOwnClicks) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        ASSERT_TRUE(CloseThePanel());
        Draw();

        // A click on the board is not taken by the HUD (it has no control there) and the board selects the piece
        const auto onTheBoard = SquareCenter("e2");
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(onTheBoard.first, onTheBoard.second, true)));
        EXPECT_TRUE(Model().HasSelection()) << "the person has White: the board took the click";
        EXPECT_TRUE(Model().GetSelected() == Sq("e2"));

        // And a press on a button of the HUD is the HUD's: the selection is not changed by it
        EXPECT_TRUE(ClickButton(kMenuColor));
        EXPECT_TRUE(Panel()->_visible);
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
        ASSERT_EQ(asked.size(), 1u);

        PressWith("ChessSetEngine", "own");

        EXPECT_EQ(Var("opponentEngine"), "own");
        EXPECT_EQ(Var("opponentError"), "") << "a new pick: the person has seen the sentence";
        EXPECT_EQ(Var("status"), "");
        EXPECT_EQ(asked.size(), 1u) << "with nobody playing the choice only changes";

        PressWith("ChessSetMode", "computer");

        EXPECT_EQ(Var("opponentMode"), "computer");
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

    TEST_F(ChessUiTest, TheMenuButtonOpensThePanelAndOkClosesIt) {
        ActionLog log;
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        ASSERT_TRUE(ClickButton(kMenuColor));
        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessOpenOpponent"));
        ASSERT_TRUE(Panel()->_visible);
        Draw();
        EXPECT_FALSE(RectCalls(kPlateColor).empty()) << "shown: the plate is drawn";

        // The Menu button is under the dim now: the panel takes the press first, and nothing is emitted
        const size_t actions = log.Total();
        const auto button = ScreenCenter(*HudElement("Menu"));
        const Taken second = ClickAtPixel(button.first, button.second);
        EXPECT_TRUE(second.Both()) << "the modal canvas takes it";
        EXPECT_EQ(log.Total(), actions) << "and it is not the Menu button's";

        ASSERT_TRUE(CloseThePanel());
        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessCloseOpponent"));
        Draw();
        EXPECT_TRUE(RectCalls(kPlateColor).empty());

        // Closed: the board gets its clicks again and the button works
        const auto e2 = SquareCenter("e2");
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(e2.first, e2.second, true)));
        EXPECT_TRUE(Model().HasSelection());
        ASSERT_TRUE(ClickButton(kMenuColor));
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

    TEST_F(ChessUiTest, NoGroupOfThePanelIsGreyedAndALevelAndAColorPickedForTwoPeopleApplyAtComputer) {
        // Replaces TheColorAndLevelGroupsAreDisabledWhileTwoPeoplePlayAndTheEngineGroupNever (and Was TheLevelButtonIsShownOnlyWhileTheProgramPlays): a greyed group read as a dead
        // button, so every group takes clicks in every mode and the pick is remembered until Computer is picked
        CountingFactory working({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(working.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        Draw();

        const auto expectNoGreying = [this](const char* state) {
            SCOPED_TRACE(state);
            for (const char* group : {"ModeGroup", "ColorGroup", "EngineGroup", "LevelGroup"}) {
                EXPECT_FALSE(PanelControl<UISegmented>(group).disabled) << group;
            }
            for (const char* name : {"ModeTwo", "ModeComputer", "ColorWhite", "ColorBlack", "EngineStockfish", "EngineOwn", "LevelEasy", "LevelMedium", "LevelHard", "LevelMax"}) {
                const UIButton& button = PanelControl<UIButton>(name);
                EXPECT_FALSE(button.groupDisabled) << name;
                EXPECT_EQ(button.GetCurrentFill().a, 255) << name << " is drawn whole";
            }
        };
        expectNoGreying("two people play");

        // Two people play: a level and a color are picked on the drawn panel
        ActionLog log;
        const uint64_t revision = Opponent().GetRevision();
        ASSERT_TRUE(ClickPanel("LevelMax").Both());
        ASSERT_TRUE(ClickPanel("ColorBlack").Both());
        Draw();
        EXPECT_EQ(log.Total(), 2u) << "both clicks emitted their action";
        EXPECT_GT(Opponent().GetRevision(), revision);
        EXPECT_TRUE(PanelControl<UIButton>("LevelMax").selected);
        EXPECT_FALSE(PanelControl<UIButton>("LevelMedium").selected);
        EXPECT_TRUE(PanelControl<UIButton>("ColorBlack").selected);
        EXPECT_FALSE(PanelControl<UIButton>("ColorWhite").selected);
        EXPECT_TRUE(PanelControl<UIButton>("ModeTwo").selected);
        EXPECT_EQ(Var("opponentLevel"), "max");
        EXPECT_EQ(Var("opponentColor"), "black");
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_EQ(ControlOpponentString("level"), "max") << "get_state of the control API reads the remembered level";
        EXPECT_EQ(ControlOpponentString("plays"), "<missing>") << "null: nobody plays";
        EXPECT_EQ(working.Calls(), 0) << "nobody plays: nothing is asked for";
        expectNoGreying("two people play, a level and a color picked");

        // Computer applies both: the person has Black, so the program plays White at the level that was picked
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        ASSERT_EQ(working.Calls(), 1);
        EXPECT_EQ(working.Levels()[0], "max");
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::White));
        EXPECT_EQ(ControlOpponentString("plays"), "white");
        EXPECT_EQ(ControlOpponentString("level"), "max");
        EXPECT_TRUE(PanelControl<UIButton>("LevelMax").selected);
        EXPECT_TRUE(PanelControl<UIButton>("ColorBlack").selected);
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        expectNoGreying("the program plays");

        // Back to two people: nothing grey, the choice still shown, and the groups still take clicks
        ASSERT_TRUE(ClickPanel("ModeTwo").Both());
        Draw();
        EXPECT_TRUE(PanelControl<UIButton>("LevelMax").selected);
        EXPECT_TRUE(PanelControl<UIButton>("ColorBlack").selected);
        expectNoGreying("two people again");
        const size_t total = log.Total();
        ASSERT_TRUE(ClickPanel("LevelEasy").Both());
        EXPECT_EQ(log.Total(), total + 1) << "a click in Two players mode is a pick, not a dead button";
        EXPECT_EQ(Opponent().GetLevel(), *FindOpponentLevel("easy"));
    }

    TEST_F(ChessUiTest, NoGroupOfThePanelIsGreyedWhateverTheControlApiSets) {
        // Replaces TheDisabledFlagFollowsAnOpponentSetFromOutside: the control API picks the program's side without the panel and the groups stay whole while the choice follows it
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());  // a hidden canvas is not drawn, so its segments take the choice only once it is shown
        Draw();

        const auto wholePanel = [this] {
            for (const char* group : {"ModeGroup", "ColorGroup", "EngineGroup", "LevelGroup"}) {
                EXPECT_FALSE(PanelControl<UISegmented>(group).disabled) << group;
            }
        };
        wholePanel();

        ControlPlays("black");
        Draw();
        wholePanel();
        EXPECT_EQ(Var("opponentColor"), "white");
        EXPECT_TRUE(PanelControl<UIButton>("ColorWhite").selected);

        ControlPlays("white");
        Draw();
        wholePanel();
        EXPECT_EQ(Var("opponentColor"), "black");
        EXPECT_TRUE(PanelControl<UIButton>("ColorBlack").selected);

        ControlPlays(nullptr);
        Draw();
        wholePanel();
        EXPECT_TRUE(PanelControl<UIButton>("ModeTwo").selected);
        EXPECT_TRUE(PanelControl<UIButton>("ColorBlack").selected) << "the person's color is kept when the program leaves";
    }

    TEST_F(ChessUiTest, ARefusedStockfishPickOnThePanelKeepsComputerTheOwnEngineAndTheLevelAndTheProgramStillAnswers) {
        // The owner's report: choosing Stockfish switched the mode to Two players. An engine that cannot be had cannot be saved: nothing changes and the error is said
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
        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        ASSERT_TRUE(ClickPanel("LevelHard").Both());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        ASSERT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::Black));
        ASSERT_EQ(asked.size(), 1u);

        ASSERT_TRUE(ClickPanel("EngineStockfish").Both());
        Draw();

        ASSERT_EQ(asked.size(), 2u);
        EXPECT_EQ(asked[1], "stockfish");
        EXPECT_TRUE(PanelControl<UIButton>("ModeComputer").selected) << "the mode does not move";
        EXPECT_FALSE(PanelControl<UIButton>("ModeTwo").selected);
        EXPECT_TRUE(PanelControl<UIButton>("EngineOwn").selected) << "the engine that plays stays picked";
        EXPECT_FALSE(PanelControl<UIButton>("EngineStockfish").selected);
        EXPECT_TRUE(PanelControl<UIButton>("LevelHard").selected);
        EXPECT_TRUE(PanelControl<UIButton>("ColorWhite").selected);
        EXPECT_EQ(PanelText("OpponentError"), "Engine not found");
        EXPECT_EQ(Label("Status").GetDisplayText(), "Engine not found");
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_EQ(ControlOpponentString("engine"), "own");
        EXPECT_EQ(ControlOpponentString("plays"), "black");

        ASSERT_TRUE(CloseThePanel());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the program that played before still answers";
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves()[1].To()).color, PieceColor::Black);
        EXPECT_EQ(asked.size(), 2u) << "no frame asked again";
        Draw();
        EXPECT_EQ(Var("opponentError"), "") << "the move of the person took the sentence away (the controller test pins that the program's own move does not)";
    }

    TEST_F(ChessUiTest, TheMenuButtonSaysMenuWhateverIsPickedAndThePanelNamesTheColors) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_EQ(Opponent().GetPlays(), std::nullopt);

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu") << "the button is not a state: the panel shows the pick";
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::Black)) << "the person has White, the program Black";
        EXPECT_EQ(Var("opponentColor"), "white");

        ASSERT_TRUE(ClickPanel("ColorBlack").Both());
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::White));
        EXPECT_EQ(Var("opponentColor"), "black");

        ASSERT_TRUE(ClickPanel("ColorWhite").Both());
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_EQ(Opponent().GetPlays(), std::optional<PieceColor>(PieceColor::Black));
        EXPECT_EQ(Var("opponentColor"), "white");

        ASSERT_TRUE(ClickPanel("ModeTwo").Both());
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_EQ(Opponent().GetPlays(), std::nullopt);
        EXPECT_EQ(Var("opponentMode"), "two");
    }

    TEST_F(ChessUiTest, ThePersonPlaysTheColorThePanelNamesAndTheProgramTheOther) {
        // The person plays the color the PANEL names (the Menu button names nothing)
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
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
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
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
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

    // ---- the old cycle buttons are gone ------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, NoSideEngineOrLevelElementAndNoCycleActionIsLeftInTheScene) {
        // A leftover would be a dead button: the board no longer answers the cycle actions, and the words {level} and {engine} are no longer published
        pugi::xml_document document;
        ASSERT_TRUE(document.load_file(ShippedSceneConfig().string().c_str()));
        EXPECT_TRUE(document.select_nodes("//element[@name='Side' or @name='Engine' or @name='Level']").empty()) << "the old HUD elements are gone, and nothing else takes their names";
        constexpr std::string_view kRemovedActions = "ChessCy"
                                                     "cle";  // split, so that a search of the sources for the removed actions finds no leftover
        for (const pugi::xpath_node found : document.select_nodes("//element[@action]")) {
            const std::string_view action = found.node().attribute("action").value();
            EXPECT_EQ(action.find(kRemovedActions), std::string_view::npos) << found.node().attribute("name").value() << " emits " << action;
            const bool known = action == "ChessReset" || action == "ChessOpenOpponent" || action == "ChessCloseOpponent" || action == kSetModeAction || action == kSetColorAction ||
                               action == kSetEngineAction || action == kSetLevelAction || action == kPromoteAction || action == kCancelPromotionAction;
            EXPECT_TRUE(known) << found.node().attribute("name").value() << " emits " << action << ", which the board does not answer";
        }
        for (const pugi::xpath_node found : document.select_nodes("//element[@text]")) {
            const std::string_view text = found.node().attribute("text").value();
            EXPECT_TRUE(text.find("{level}") == std::string_view::npos && text.find("{engine}") == std::string_view::npos) << found.node().attribute("name").value() << ": " << text;
        }

        // The same in the loaded scene: the HUD is Turn, Menu, Reset and Status, and nothing sits at the bottom left
        const auto hud = Hud();
        ASSERT_TRUE(hud);
        size_t elements = 0;
        for (const auto& child : hud->GetRoot().children) {
            ++elements;
            EXPECT_NE(child->style.rect.anchor, UIAnchor::BottomLeft) << child->name.CStr() << ": the bottom left of the HUD is empty";
        }
        EXPECT_EQ(elements, 4u) << "Turn, Menu, Reset, Status";
        for (const char* name : {"Side", "Engine", "Level"}) {
            EXPECT_EQ(HudElement(name), nullptr) << name;
            EXPECT_EQ(Panel()->GetRoot().FindByName(PoolString::Intern(name)), nullptr) << name;
        }

        // And the variables of the old words are not published any more
        Draw();
        EXPECT_TRUE(CoreManager::GetUIVars().Get("level"_intern).empty());
        EXPECT_TRUE(CoreManager::GetUIVars().Get("engine"_intern).empty());
    }

    // ---- the panel is drawn over everything -----------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, ThePanelIsDrawnOverTheHudAndTheResultBar) {
        const auto layerOf = [this](const char* name) {
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(name)) {
                    return node->GetLayer();
                }
            }
            ADD_FAILURE() << name << " is not a node of the scene";
            return 0;
        };
        EXPECT_GT(layerOf("OpponentPanel"), layerOf("Hud"));
        EXPECT_GT(layerOf("Hud"), layerOf("Result"));

        // Drawn: a mate shows the bar of the result, then the panel is opened over it; the plate comes after the Reset button and the edge of the bar in the draw order
        ASSERT_TRUE(Play(Model(), "f2f3"));
        ASSERT_TRUE(Play(Model(), "e7e5"));
        ASSERT_TRUE(Play(Model(), "g2g4"));
        ASSERT_TRUE(Play(Model(), "d8h4"));
        ASSERT_TRUE(Model().IsGameOver());
        ASSERT_TRUE(OpenThePanel());
        Draw();
        const auto indexOf = [this](const Color& color) {
            for (size_t i = 0; i < _renderer->calls.size(); ++i) {
                if (_renderer->calls[i].kind == DrawKind::Rect && SameColor(_renderer->calls[i].color, color)) {
                    return i;
                }
            }
            return _renderer->calls.size();
        };
        const Color barEdge{205, 165, 60, 255};  // ChessResultComponent.cpp
        const size_t bar = indexOf(barEdge);
        const size_t reset = indexOf(kResetColor);
        const size_t plate = indexOf(kPlateColor);
        ASSERT_LT(bar, _renderer->calls.size()) << "the result bar is drawn once the game is over";
        ASSERT_LT(reset, _renderer->calls.size());
        ASSERT_LT(plate, _renderer->calls.size());
        EXPECT_LT(bar, plate) << "the plate is over the result bar";
        EXPECT_LT(reset, plate) << "and over the HUD";
    }

    // ---- the promotion panel -------------------------------------------------------------------------------------------------------
    // A click on a promotion target leaves the promotion pending in the model; the board shows the modal PromotionPanel for as long as it is. The buttons are clicked where
    // they are drawn (CoreManager::GetUIInput()), the board by a press on its square

    namespace {

        constexpr const char* kPromotionFen = "8/P6k/8/8/8/8/8/K7 w - - 0 1";  // a white pawn on a7, the way to a8 is free
        constexpr const char* kBlackPromotionFen = "k7/8/8/8/8/8/p6K/8 b - - 0 1";
        constexpr const char* kMatingPromotionFen = "7k/P7/6K1/8/8/8/8/8 w - - 0 1";  // a8 gives mate with a queen or a rook

        /// The name of the button of a promotion piece in the scene: PromoteQueen, PromoteRook, ...
        eastl::string PromoteButton(PieceType piece) {
            return Format("Promote{}", PromotionLabel(piece));
        }

        constexpr const char* kPromotionPlate = "PromotionPlate";
        constexpr float kPromotionPlateHeight = 224.0f;  // 16 + 44 + 56 + 56 + 3 x 12 + 16, the comment of the scene

        /// Every element name of a tree, depth first
        void CollectNames(const UIElement& element, eastl::vector<PoolString>& names) {
            if (!element.name.ToStringView().empty()) {
                names.push_back(element.name);
            }
            for (const auto& child : element.children) {
                CollectNames(*child, names);
            }
        }

    }  // namespace

    TEST_F(ChessUiTest, ThePromotionPanelIsHiddenAtTheStartAndDrawsNothing) {
        auto panel = Promotion();
        ASSERT_TRUE(panel);
        EXPECT_FALSE(panel->_visible);
        EXPECT_TRUE(panel->_modal);
        Draw();
        EXPECT_TRUE(RectCalls(kPromotionPlateColor).empty());
        _manager->UpdateAll();
        EXPECT_FALSE(PromotionShown()) << "an update with nothing pending shows nothing";
    }

    TEST_F(ChessUiTest, AClickOnAPromotionTargetOpensThePanelAndPlaysNothing) {
        StartFrom(kPromotionFen);
        const uint64_t version = Model().GetVersion();
        const eastl::string fen = Model().GetPosition().ToFen();

        Click("a7");
        EXPECT_FALSE(PromotionShown()) << "selecting the pawn is not a promotion";
        Click("a8");

        EXPECT_TRUE(PromotionShown());
        EXPECT_TRUE(Model().HasPendingPromotion());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_EQ(Model().GetPosition().ToFen(), fen) << "nothing has moved";
        Draw();
        EXPECT_FALSE(RectCalls(kPromotionPlateColor).empty()) << "shown: the plate is drawn";
        EXPECT_TRUE(RectCalls(kPlateColor).empty()) << "and it is not the opponent panel";
        EXPECT_FALSE(Panel()->_visible);
    }

    TEST_F(ChessUiTest, EachPieceButtonPlaysItsPromotionClosesThePanelAndTheHandleReportsThePiece) {
        for (const PieceType piece : PromotionPieces()) {
            SCOPED_TRACE(eastl::string(PromotionLabel(piece)).c_str());
            StartFrom(kPromotionFen);
            const uint64_t version = Model().GetVersion();
            const ChessGameModel::PieceHandle pawn = PieceNodeAt("a7");
            ASSERT_NE(pawn, nullptr);
            ClickMove("a7", "a8");
            ASSERT_TRUE(PromotionShown());

            const Taken taken = ClickPromotion(PromoteButton(piece).c_str());
            EXPECT_TRUE(taken.Both());

            ASSERT_EQ(Model().GetMoves().size(), 1u);
            EXPECT_EQ(Model().GetMoves().back().ToUci().substr(0, 4), eastl::string("a7a8"));
            EXPECT_TRUE(Model().GetMoves().back().IsPromotion());
            EXPECT_EQ(Model().GetPieceAt(Sq("a8")).type, piece);
            EXPECT_EQ(Model().GetPieceAt(Sq("a8")).color, PieceColor::White);
            EXPECT_GT(Model().GetVersion(), version);
            EXPECT_FALSE(PromotionShown());
            EXPECT_FALSE(Model().HasPendingPromotion());
            EXPECT_EQ(Model().GetSquareOf(pawn), Sq("a8"));
            EXPECT_EQ(Model().GetPieceOf(pawn).type, piece) << "the handle of the pawn reports the piece";
            EXPECT_EQ(Model().GetTurn(), PieceColor::Black);
        }
    }

    TEST_F(ChessUiTest, TheLetterOfEachPromotionIsTheOneOfItsButton) {
        const std::pair<PieceType, const char*> letters[] = {{PieceType::Queen, "a7a8q"}, {PieceType::Rook, "a7a8r"}, {PieceType::Bishop, "a7a8b"}, {PieceType::Knight, "a7a8n"}};
        for (const auto& [piece, uci] : letters) {
            SCOPED_TRACE(uci);
            StartFrom(kPromotionFen);
            ClickMove("a7", "a8");
            ASSERT_TRUE(ClickPromotion(PromoteButton(piece).c_str()).Both());
            ASSERT_EQ(Model().GetMoves().size(), 1u);
            EXPECT_EQ(Model().GetMoves().back().ToUci(), eastl::string(uci));
        }
    }

    TEST_F(ChessUiTest, CancelClosesThePanelKeepsThePawnSelectedAndTheSameTargetOpensItAgain) {
        StartFrom(kPromotionFen);
        const uint64_t version = Model().GetVersion();
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());
        ActionLog log;

        EXPECT_TRUE(ClickPromotion("PromotionCancel").Both());

        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessCancelPromotion"));
        EXPECT_FALSE(PromotionShown());
        EXPECT_FALSE(Model().HasPendingPromotion());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("a7"));
        EXPECT_EQ(Model().GetPieceAt(Sq("a7")).type, PieceType::Pawn);
        Draw();
        EXPECT_TRUE(RectCalls(kPromotionPlateColor).empty());

        Click("a8");
        EXPECT_TRUE(PromotionShown()) << "the pawn is still selected, so the same target asks again";
        ASSERT_TRUE(ClickPromotion("PromoteKnight").Both());
        EXPECT_EQ(Model().GetPieceAt(Sq("a8")).type, PieceType::Knight);
    }

    TEST_F(ChessUiTest, WhileThePromotionPanelIsShownEveryPressIsItsAndNothingUnderItSeesOne) {
        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());
        Draw();
        ActionLog log;
        const uint64_t version = Model().GetVersion();

        // The board (two corners and the target), the dim, the Reset and the Menu button of the HUD: the panel takes all of them
        const std::pair<float, float> points[] = {
            SquareCenter("a1"), SquareCenter("h8"), SquareCenter("a8"), ScreenCenter(*HudElement("Reset")), ScreenCenter(*HudElement("Menu")), ScreenCenter(PromotionElement("PromotionTitle"))};
        for (const auto& [x, y] : points) {
            EXPECT_TRUE(ClickAtPixel(x, y).Both()) << x << "," << y;
        }

        EXPECT_EQ(log.Total(), 0u) << "no action was emitted: Reset and Opponent got no press";
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("a7"));
        EXPECT_TRUE(Model().HasPendingPromotion());
        EXPECT_TRUE(PromotionShown());
        EXPECT_FALSE(Panel()->_visible) << "the opponent panel stays closed";
    }

    TEST_F(ChessUiTest, AResetASetPositionOrAMoveOfSomeoneElseClosesThePanelAtTheNextFrame) {
        const auto open = [&] {
            StartFrom(kPromotionFen);
            ClickMove("a7", "a8");
            ASSERT_TRUE(PromotionShown());
        };

        open();
        Model().Reset();
        _manager->UpdateAll();
        EXPECT_FALSE(PromotionShown()) << "Reset";
        EXPECT_FALSE(Model().HasPendingPromotion());

        open();
        Model().SetPosition(PositionFromFen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"));
        _manager->UpdateAll();
        EXPECT_FALSE(PromotionShown()) << "SetPosition";

        open();
        ASSERT_TRUE(Play(Model(), "a1b1"));  // the control API plays a move of its own
        _manager->UpdateAll();
        EXPECT_FALSE(PromotionShown()) << "a move of someone else";
        EXPECT_FALSE(Model().HasPendingPromotion());

        // The Reset button cannot be pressed under the panel, but the action can be emitted (the control API's reset does the same to the model)
        open();
        Press("ChessReset");
        _manager->UpdateAll();
        EXPECT_FALSE(PromotionShown()) << "the action ChessReset";
    }

    TEST_F(ChessUiTest, APromotionThatMatesEndsTheGameAndClosesThePanel) {
        StartFrom(kMatingPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());

        ASSERT_TRUE(ClickPromotion("PromoteQueen").Both());

        EXPECT_FALSE(PromotionShown());
        EXPECT_EQ(Model().GetStatus(), GameStatus::Checkmate);
        EXPECT_EQ(Model().GetWinner(), std::optional<PieceColor>(PieceColor::White));
        EXPECT_EQ(Var("turn"), "White wins");
        EXPECT_EQ(Var("status"), "Checkmate");

        // The knight does not mate (king and knight against a king is a draw): the same position, the other choice, ends the game another way
        StartFrom(kMatingPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(ClickPromotion("PromoteKnight").Both());
        EXPECT_EQ(Model().GetStatus(), GameStatus::DrawInsufficientMaterial);
        EXPECT_FALSE(PromotionShown());
    }

    TEST_F(ChessUiTest, AnIdThatIsInNoTableChangesNothingAndLeavesThePanelOpen) {
        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());
        const uint64_t version = Model().GetVersion();

        for (const char* value : {"", "Queen", "QUEEN", "queen ", "pawn", "king", "que", "nonsense"}) {
            PressWith("ChessPromote", value);
        }
        Press("ChessPromote");  // no value at all
        _manager->UpdateAll();

        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_TRUE(Model().HasPendingPromotion());
        EXPECT_TRUE(PromotionShown());
        EXPECT_EQ(Model().GetPieceAt(Sq("a7")).type, PieceType::Pawn);
    }

    TEST_F(ChessUiTest, APromotionPressWithNothingPendingChangesNothing) {
        StartFrom(kPromotionFen);
        const uint64_t version = Model().GetVersion();
        Press("ChessCancelPromotion");
        PressWith("ChessPromote", "queen");
        _manager->UpdateAll();
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_FALSE(PromotionShown());
        EXPECT_EQ(Model().GetPieceAt(Sq("a7")).type, PieceType::Pawn);
    }

    TEST_F(ChessUiTest, AwayFromTheLastRankAClickNeverOpensThePanel) {
        ClickMove("e2", "e4");
        EXPECT_FALSE(PromotionShown());
        EXPECT_EQ(Model().GetMoves().size(), 1u);
    }

    TEST_F(ChessUiTest, WhileTheProgramPlaysTheOtherColorThePersonsPromotionWorksAndTheProgramAnswersIt) {
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());

        ASSERT_TRUE(ClickPromotion("PromoteRook").Both());

        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().front().ToUci(), eastl::string("a7a8r"));
        EXPECT_FALSE(PromotionShown());
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the program answers the promotion";
        EXPECT_EQ(Model().GetTurn(), PieceColor::White);
        EXPECT_FALSE(PromotionShown());
    }

    TEST_F(ChessUiTest, TheProgramTakingTheTurnClosesThePanel) {
        // The person selected the pawn and asked for the promotion; the program (switched on in the meantime) takes the turn and drops the half-made move
        CountingFactory factory({eastl::make_shared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());

        ASSERT_TRUE(Opponent().SetSide(PieceColor::White).has_value());
        _manager->UpdateAll();

        EXPECT_FALSE(Model().HasPendingPromotion());
        EXPECT_FALSE(PromotionShown());
    }

    TEST_F(ChessUiTest, ABlackPromotionWorks) {
        StartFrom(kBlackPromotionFen);
        ClickMove("a2", "a1");
        ASSERT_TRUE(PromotionShown());

        ASSERT_TRUE(ClickPromotion("PromoteKnight").Both());

        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), eastl::string("a2a1n"));
        EXPECT_EQ(Model().GetPieceAt(Sq("a1")).type, PieceType::Knight);
        EXPECT_EQ(Model().GetPieceAt(Sq("a1")).color, PieceColor::Black);
        EXPECT_FALSE(PromotionShown());
        EXPECT_EQ(Model().GetTurn(), PieceColor::White);
    }

    TEST_F(ChessUiTest, ThePromotedPieceIsDrawnWithItsSprite) {
        StartFrom(kPromotionFen);
        const auto* node = PieceNodeAt("a7");
        ASSERT_NE(node, nullptr);
        ClickMove("a7", "a8");
        ASSERT_TRUE(ClickPromotion("PromoteBishop").Both());
        _manager->UpdateAll();  // ChessPieceComponent switches the sprite in an update

        const auto sprite = node->GetNode().GetComponent<SpriteRendererComponent>();
        ASSERT_TRUE(sprite);
        EXPECT_EQ(sprite->_spriteId, PoolString::Intern("WhiteBishop"));
    }

    // ---- the promotion panel: the layout ---------------------------------------------------------------------------------------------
    // The checks of the opponent panel on the node PromotionPanel: the validator on the size matrix, no word cut on any case and in the narrowest window, the touch targets, the plate
    // inside the view, the words and ids held to the table of PromotionChoices, the names of the four canvases apart and broken copies. The panel has no HTML + CSS twin in
    // ChessHudWebTest: that file holds the HUD and the opponent panel, and the web front end has no entry for this panel yet

    TEST_F(ChessUiTest, ThePromotionPanelPassesTheSizeMatrix) {
        auto panel = Promotion();
        ASSERT_TRUE(panel) << "the shipped scene has the node PromotionPanel";
        panel->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        const auto failures = ValidateWithWords(panel->GetLayout());
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(ChessUiTest, NoWordOfThePromotionPanelIsCutOnAnyCaseOfTheMatrix) {
        auto panel = Promotion();
        ASSERT_TRUE(panel);
        panel->UpdateLayout(1280.0f, 720.0f);
        const eastl::string problems = PanelProblemsOnMatrix(panel->GetLayout(), kReportedCases, kPromotionPlate);
        EXPECT_TRUE(problems.empty()) << problems.c_str();
    }

    TEST_F(ChessUiTest, NoWordOfThePromotionPanelIsCutInTheNarrowestWindowAndEveryButtonIsATouchTarget) {
        auto panel = Promotion();
        ASSERT_TRUE(panel);
        panel->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = panel->GetLayout();
        LayOutInTheNarrowestWindow(layout);

        const float scale = kNarrowestWidth / kViewWidth;
        EXPECT_NEAR(layout.GetResolved().scale, scale, 0.01f);
        EXPECT_NEAR(layout.root->rect.w, kNarrowestWidth * (1.0f - 2.0f * UIPreview::kSideInsetShare) / scale, 0.5f) << "the safe area, in view units: about 689";
        const eastl::string problems = PanelProblems(layout, kPromotionPlate);
        EXPECT_TRUE(problems.empty()) << problems.c_str();

        // 94% of the safe area is more than maxWidth, so the plate is 640 here and in every other window: 608 inside the padding, 146 a piece button
        EXPECT_NEAR(PromotionElement(kPromotionPlate).rect.w, 640.0f, kPixelTolerance);

        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        eastl::vector<eastl::string> buttons{"PromotionCancel"};
        for (const PieceType piece : PromotionPieces()) {
            buttons.push_back(PromoteButton(piece));
        }
        ASSERT_EQ(buttons.size(), 5u) << "four pieces and Cancel";
        for (const eastl::string& name : buttons) {
            const UIButton& button = PromotionControl<UIButton>(name.c_str());
            EXPECT_GE(button.rect.h, kSegmentHeight - kPixelTolerance) << name.c_str();
            EXPECT_GE(std::min(button.rect.w, button.rect.h) * layout.GetResolved().scale, minTouch - kPixelTolerance) << name.c_str() << " is a touch target on the smallest window";
            const UIContentFit fit = button.CheckContent(button.rect.w - 2.0f * kButtonAir, button.rect.h, layout.GetResolved().scale);
            EXPECT_TRUE(fit.hasContent && fit.fits) << name.c_str() << " '" << ShownText(button).c_str() << "' in " << button.rect.w << " units";
        }
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, ThePromotionPlateIsInsideTheViewCenteredAndLaidOutAsTheCommentOfTheSceneSays) {
        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());

        for (const auto& size : {std::pair{1280.0f, 720.0f}, std::pair{720.0f, 1280.0f}, std::pair{1024.0f, 768.0f}, std::pair{800.0f, 800.0f}, std::pair{2560.0f, 1080.0f}}) {
            SCOPED_TRACE(Format("{}x{}", size.first, size.second).c_str());
            DrawAt(size.first, size.second);
            const UIElement& plate = PromotionElement(kPromotionPlate);
            ASSERT_FALSE(plate.rect.IsEmpty()) << "the plate is shown";
            EXPECT_GE(plate.rect.x, -kPixelTolerance);
            EXPECT_GE(plate.rect.y, -kPixelTolerance);
            EXPECT_LE(plate.rect.x + plate.rect.w, kViewWidth + kPixelTolerance);
            EXPECT_LE(plate.rect.y + plate.rect.h, kViewHeight + kPixelTolerance);
            EXPECT_NEAR(plate.rect.x + plate.rect.w * 0.5f, kViewWidth * 0.5f, 0.5f) << "the plate is centered in the view";
            EXPECT_LE(plate.rect.w, 640.0f + kPixelTolerance);
            EXPECT_NEAR(plate.rect.h, kPromotionPlateHeight, kPixelTolerance);

            // The four pieces share one row of equal buttons that fills it; Cancel is the row below, the title the one above
            const UIElement& row = PromotionControl<UIStack>("PromotionPieces");
            float left = row.rect.x;
            float width = -1.0f;
            for (const PieceType piece : PromotionPieces()) {
                const UIElement& button = PromotionElement(PromoteButton(piece).c_str());
                EXPECT_NEAR(button.rect.x, left, kPixelTolerance) << PromoteButton(piece).c_str();
                EXPECT_NEAR(button.rect.y, row.rect.y, kPixelTolerance) << PromoteButton(piece).c_str();
                EXPECT_NEAR(button.rect.h, kSegmentHeight, kPixelTolerance) << PromoteButton(piece).c_str();
                width = width < 0.0f ? button.rect.w : width;
                EXPECT_NEAR(button.rect.w, width, kPixelTolerance) << PromoteButton(piece).c_str() << ": equal shares by flex";
                left = button.rect.x + button.rect.w + 8.0f;
            }
            EXPECT_NEAR(left - 8.0f, row.rect.x + row.rect.w, kPixelTolerance) << "the four buttons fill the row";
            const UIElement& cancel = PromotionElement("PromotionCancel");
            const UIElement& title = PromotionElement("PromotionTitle");
            EXPECT_GE(cancel.rect.y, row.rect.y + row.rect.h + 12.0f - kPixelTolerance) << "Cancel is under the row";
            EXPECT_LE(title.rect.y + title.rect.h, row.rect.y - 12.0f + kPixelTolerance) << "the title is over the row";
        }
    }

    // ---- the promotion panel: the words and the ids come from the table -----------------------------------------------------------------

    TEST_F(ChessUiTest, TheTextValueAndActionOfEveryPieceButtonAreThoseOfThePromotionTable) {
        const UIStack& row = PromotionControl<UIStack>("PromotionPieces");
        const auto pieces = PromotionPieces();
        ASSERT_EQ(row.children.size(), pieces.size()) << "exactly the pieces of the table, no more and no fewer";
        ASSERT_EQ(pieces.size(), 4u);
        EXPECT_EQ(pieces[0], PieceType::Queen) << "queen first";

        for (size_t i = 0; i < pieces.size(); ++i) {
            const PieceType piece = pieces[i];
            SCOPED_TRACE(PromoteButton(piece).c_str());
            const UIButton* button = row.children[i]->Cast<UIButton>();
            ASSERT_NE(button, nullptr);
            EXPECT_EQ(button->name, PoolString::Intern(PromoteButton(piece).c_str())) << "the buttons are in the order of the table";
            const eastl::string_view label = PromotionLabel(piece);
            const eastl::string_view id = PromotionId(piece);
            EXPECT_EQ(ShownText(*button), eastl::string(label.data(), label.size()));
            EXPECT_EQ(eastl::string(button->value.ToStringView().data(), button->value.ToStringView().size()), eastl::string(id.data(), id.size()));
            EXPECT_EQ(button->action, PoolString::Intern(kPromoteAction));
            EXPECT_EQ(FindPromotion(button->value.ToStringView()), std::optional<PieceType>(piece)) << "the value is an id the board accepts";
        }

        const UIButton& cancel = PromotionControl<UIButton>("PromotionCancel");
        EXPECT_EQ(ShownText(cancel), "Cancel");
        EXPECT_EQ(cancel.action, PoolString::Intern(kCancelPromotionAction));
    }

    TEST_F(ChessUiTest, EveryActionOfThePromotionPanelIsOneTheBoardAnswers) {
        // Every button of the canvas emits ChessPromote or ChessCancelPromotion, and the board answers both (pressed here as the buttons would)
        auto panel = Promotion();
        ASSERT_TRUE(panel);
        eastl::vector<const UIElement*> controls;
        CollectControls(panel->GetRoot(), controls);
        size_t buttons = 0;
        for (const UIElement* control : controls) {
            if (const UIButton* button = control->Cast<UIButton>()) {
                ++buttons;
                EXPECT_TRUE(button->action == PoolString::Intern(kPromoteAction) || button->action == PoolString::Intern(kCancelPromotionAction))
                    << button->name.CStr() << " emits " << button->action.CStr() << ", which the board does not answer";
            }
        }
        EXPECT_EQ(buttons, 5u);

        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(Model().HasPendingPromotion());
        Press(kCancelPromotionAction);
        EXPECT_FALSE(Model().HasPendingPromotion()) << kCancelPromotionAction;
        ClickMove("a7", "a8");
        ASSERT_TRUE(Model().HasPendingPromotion());
        PressWith(kPromoteAction, "queen");
        EXPECT_FALSE(Model().HasPendingPromotion()) << kPromoteAction;
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "a7a8q");
    }

    TEST_F(ChessUiTest, NoElementNameIsInTwoCanvasesOfTheScene) {
        // Elements are found by name in every canvas of the scene (SetVisibleByName and the lookups of the tests): the HUD, the board coordinates, the opponent panel and the promotion panel keep
        // their own names
        const struct {
            const char* node;
            IntrusivePtr<UICanvasComponent> canvas;
        } canvases[] = {{"Hud", Hud()}, {"BoardCoordinates", Coordinate()}, {"OpponentPanel", Panel()}, {"PromotionPanel", Promotion()}};
        constexpr size_t kCanvasCount = std::size(canvases);
        eastl::vector<PoolString> names[kCanvasCount];
        for (size_t i = 0; i < kCanvasCount; ++i) {
            ASSERT_TRUE(canvases[i].canvas) << canvases[i].node;
            CollectNames(canvases[i].canvas->GetRoot(), names[i]);
            EXPECT_FALSE(names[i].empty()) << canvases[i].node;
        }
        for (size_t first = 0; first < kCanvasCount; ++first) {
            for (size_t second = first + 1; second < kCanvasCount; ++second) {
                for (const PoolString& name : names[first]) {
                    EXPECT_EQ(eastl::find(names[second].begin(), names[second].end(), name), names[second].end())
                        << name.CStr() << " is in both " << canvases[first].node << " and " << canvases[second].node;
                }
            }
        }
    }

    // ---- the promotion panel: broken copies, so that the checks can fail ---------------------------------------------------------------------

    TEST_F(ChessUiTest, APromotionButtonUnderTheTouchSizeIsCaught) {
        // The row stretches its buttons to its own height (crossAlign), so the row is what makes the four piece buttons low; Cancel has a height of its own
        std::string cancel = ShippedElementsXml("PromotionPanel");
        ASSERT_FALSE(cancel.empty());
        ASSERT_NO_FATAL_FAILURE(Replace(cancel, R"(name="PromotionCancel" height="56")", R"(name="PromotionCancel" height="40")"));
        CanvasCopy lowCancel(cancel);
        EXPECT_TRUE(HasIssue(ValidateWithWords(lowCancel.layout), UILayoutIssueKind::TouchTargetTooSmall));
        EXPECT_NE(PanelProblemsOnMatrix(lowCancel.layout, kReportedCases, kPromotionPlate).find("PromotionCancel is 40.0 units high"), eastl::string::npos);

        std::string row = ShippedElementsXml("PromotionPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(row, R"(name="PromotionPieces" height="56")", R"(name="PromotionPieces" height="40")"));
        CanvasCopy lowRow(row);
        EXPECT_TRUE(HasIssue(ValidateWithWords(lowRow.layout), UILayoutIssueKind::TouchTargetTooSmall));
        EXPECT_NE(PanelProblemsOnMatrix(lowRow.layout, kReportedCases, kPromotionPlate).find("PromoteBishop is 40.0 units high"), eastl::string::npos);

        CanvasCopy shipped(ShippedElementsXml("PromotionPanel"));
        EXPECT_TRUE(PanelProblemsOnMatrix(shipped.layout, kReportedCases, kPromotionPlate).empty()) << "the same checks pass on the shipped panel";
        EXPECT_FALSE(HasIssue(ValidateWithWords(shipped.layout), UILayoutIssueKind::TouchTargetTooSmall));
    }

    TEST_F(ChessUiTest, APromotionWordCutByAFontThatIsTooBigIsCaught) {
        std::string xml = ShippedElementsXml("PromotionPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(text="Knight" fontSize="26")", R"(text="Knight" fontSize="60")"));
        CanvasCopy copy(xml);
        const eastl::string problems = PanelProblemsOnMatrix(copy.layout, kReportedCases, kPromotionPlate);
        EXPECT_NE(problems.find("PromoteKnight cuts 'Knight'"), eastl::string::npos) << problems.c_str();
    }

    TEST_F(ChessUiTest, APromotionPlateTooNarrowForTheFourButtonsIsCaught) {
        // A plate of 300 units leaves about 61 for a button, which cannot hold "Knight" with its air; the shipped 640 can
        std::string xml = ShippedElementsXml("PromotionPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(maxWidth="640")", R"(maxWidth="300")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        const eastl::string problems = PanelProblems(copy.layout, kPromotionPlate);
        EXPECT_NE(problems.find("cuts '"), eastl::string::npos) << problems.c_str();
    }

    TEST_F(ChessUiTest, APromotionPlateTallerThanTheSafeAreaIsCaught) {
        std::string xml = ShippedElementsXml("PromotionPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(height="224")", R"(height="1200")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        EXPECT_NE(PanelProblems(copy.layout, kPromotionPlate).find("leaves the safe area"), eastl::string::npos);
    }

}  // namespace BECore::Chess::Tests
