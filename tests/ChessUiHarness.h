#pragma once

#include <BECore/Audio/AudioManager.h>
#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/ViewTransform.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/SceneView.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIImageComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UIPreview.h>
#include <BECore/UI/UIScreenContent.h>
#include <BECore/UI/UISegmented.h>
#include <BECore/UI/UIStack.h>
#include <BECore/UI/UITextComponent.h>
#include <Chess/ChessGame.h>
#include <Chess/ChessGameControl.h>
#include <Chess/Components/ChessBoardComponent.h>
#include <Chess/Components/ChessMaterialComponent.h>
#include <Chess/Components/ChessPieceComponent.h>
#include <Chess/Engine/OpponentChoice.h>
#include <Chess/Engine/OpponentText.h>
#include <Chess/Game/BoardCoordinates.h>
#include <Chess/Game/CapturedMaterial.h>
#include <Chess/Game/MaterialLayout.h>
#include <Chess/Game/MenuSections.h>
#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/MovePanel.h>
#include <Chess/Game/PromotionChoices.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Common/TempPath.h>
#include <Common/UILayoutMatrix.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Games/GameManager.h>
#include <chrono>
#include <tests/ChessTestUtil.h>
#include <tests/HudWords.h>
#include <tests/ShippedScene.h>
#include <tests/TestEngines.h>

/**
 * The shipped scene of Chess with its HUD: the file is read from the source tree (a game's content is not mounted in the test environment), the game is started
 * through GameManager like the executable does, and the screen is drawn into a RecordingRenderer. Needs the engine services.
 */
namespace BECore::Chess::Tests {

    using namespace BECore::Tests;

    namespace {

        constexpr float kViewWidth = 840.0f;
        constexpr float kViewHeight = 800.0f;
        constexpr float kBoardTop = 80.0f;  // in view units
        constexpr float kBoardBottom = 720.0f;
        const Color kResetColor{40, 90, 60, 255};
        const Color kMenuColor{45, 75, 120, 255};           // the fills of the buttons of the HUD (SceneConfig.xml): a color each, so a test can tell where each one is drawn
        const Color kPlateColor{36, 40, 52, 255};           // the plate of the opponent panel
        const Color kPromotionPlateColor{44, 38, 52, 255};  // the plate of the promotion panel
        constexpr float kMinGap = 8.0f;                     // two controls of the HUD keep this much between them, in view units
        constexpr float kButtonAir = 8.0f;                  // the text of a button keeps this much from each edge of its fill
        constexpr float kNarrowestWidth = 480.0f;           // the narrowest window of the size matrix, in pixels
        constexpr float kNarrowestHeight = 480.0f;          // the narrowest window with the default (landscape) view: 480 x 480 is a square, which keeps it, at the scale 0.571
        constexpr float kNarrowestPortraitHeight = 640.0f;  // 3:4 at that width: the Portrait view, at the scale 0.667, filling the window
        constexpr float kNarrowestTallHeight = 853.0f;      // 9:16 at that width: the Tall view, at the same scale

        // The second view of the scene (SceneConfig.xml, `viewVariants`): 720 x 960, the board at (40, 144), a square 80 units as in the default view
        constexpr float kPortraitViewWidth = 720.0f;
        constexpr float kPortraitViewHeight = 960.0f;
        constexpr float kPortraitBoardLeft = 40.0f;
        constexpr float kPortraitBoardTop = 144.0f;
        constexpr float kPortraitBoardBottom = 784.0f;
        constexpr float kPortraitHudBottom = 68.0f;   // Turn, Menu and Reset: 56 units high at y 12
        constexpr float kPortraitStatusTop = 892.0f;  // Status: 56 units high, 12 above the bottom of the view
        constexpr float kPortraitNotchUnits = 50.4f;  // the top inset of a notch (7 % of the short side) when the view fills the window: 7 % of its 720 units
        constexpr const char* kPortraitViewName = "Portrait";

        // The fourth view (SceneConfig.xml): 720 x 1204, Portrait made higher, every place of Portrait and the move panel under the board; picked up to the aspect 0.59
        constexpr float kTallViewHeight = 1204.0f;
        constexpr const char* kTallViewName = "Tall";
        constexpr float kTallMaxAspect = 0.59f;

        // The third view (SceneConfig.xml): 1120 x 800, the board and the margins of the default view and the room for the move panel at the right; picked from the aspect 1.4 up
        constexpr float kWideViewWidth = 1120.0f;
        constexpr const char* kWideViewName = "Wide";
        constexpr float kWideMinAspect = kWideViewWidth / kViewHeight;
        constexpr float kWideNarrowestWidth = 700.0f;  // 700 x 480 is the narrowest Wide window with side insets (672 x 480 is the aspect 1.4 itself, a knife edge), at the scale 0.6

        // The colors the board fills a full square with: the tokens of the theme `classic` of config/UIThemes.xml, the one the fixture leaves current
        const Color kSelectedColor{120, 200, 120, 200};
        const Color kLegalMoveColor{120, 200, 120, 120};
        const Color kCheckColor{230, 60, 60, 210};
        const Color kMateColor{150, 20, 20, 240};  // the king's square once mated
        const Color kHintFromColor{60, 130, 230, 110};
        const Color kHintToColor{60, 130, 230, 200};
        const Color kLastMoveColor{245, 205, 50, 140};  // the two squares of the last move, one color for both
        const Color kPremoveColor{150, 90, 200, 150};   // the two squares of each queued premove, one color for all

        // The tall cell of a pawn in the piece atlas (TextureLibrary.xml, assets/chess_test.png is 700 x 381).
        // The library is empty in the test environment, so the test gives a sprite its cell.
        constexpr uint32_t kAtlasWidth = 700;
        constexpr uint32_t kAtlasHeight = 381;
        constexpr float kCellX = 583.33f;
        constexpr float kCellWidth = 116.67f;
        constexpr float kCellHeight = 190.5f;
        constexpr size_t kPieceCount = 32;
        constexpr const char* kMaterialPieces[] = {"Queen", "Rook", "Bishop", "Knight", "Pawn"};  // the rows of a group of the captured material, in the order of the scene
        constexpr float kPixelTolerance = 0.01f;
        constexpr float kDragStepPixels = 20.0f;       // a first motion of a drag: well past the threshold of the board (6 view units, about 5 px here)
        constexpr float kUnderThresholdPixels = 3.0f;  // a motion of a drag that stays under that threshold
        constexpr float kPointerOffsetX = 60.0f;       // where a test holds the pointer, from the middle of the square it pressed (pixels)
        constexpr float kPointerOffsetY = -50.0f;
        constexpr float kHalfSquare = 40.0f;  // half the 80 units of a square: the piece in the hand is centered on the pointer
        constexpr int32_t kPieceLayer = 0;
        constexpr int32_t kDragLayer = 6;  // the layer of a piece in the hand: over the chips (5), under the HUD (10)
        constexpr float kGlyphTolerance =
            2.0f;  // a glyph of a label is laid out to the pixel of the font, not to the unit: at the scale of the Portrait view a narrow glyph reaches past its box by more than a pixel

        constexpr int kManyFrames = 200;  // frames that must pass with nothing happening

        constexpr float kDimInset = 8.0f;     // view units: a point of a dim just inside the corner of the view, far from every plate
        const Color kDimColor{0, 0, 0, 150};  // the dim of a modal panel (the node files)

        SDL_MouseButtonEvent Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

        bool Play(ChessGameModel& model, StringView uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        /// A game of @p plies plies that none of the end rules stops: the legal move with the index `ply * 5 + 1` modulo their count, at every ply
        bool PlayAQuietGame(ChessGameModel& model, size_t plies) {
            for (size_t ply = 0; ply < plies; ++ply) {
                const MoveList legal = MoveGenerator::GenerateLegal(model.GetPosition());
                if (legal.empty() || model.IsGameOver() || !model.TryMove(legal[(ply * 5 + 1) % legal.size()]).has_value()) {
                    return false;
                }
            }
            return !model.IsGameOver();
        }

        /// Plays the moves given in UCI one after the other; false at the first that is not legal
        bool PlayAll(ChessGameModel& model, std::initializer_list<const char*> moves) {
            for (const char* uci : moves) {
                if (!Play(model, uci)) {
                    return false;
                }
            }
            return true;
        }

        bool SameColor(const Color& left, const Color& right) {
            return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
        }

        bool SameRect(const Rect& left, const Rect& right) {
            return std::abs(left.x - right.x) <= kPixelTolerance && std::abs(left.y - right.y) <= kPixelTolerance && std::abs(left.w - right.w) <= kPixelTolerance &&
                   std::abs(left.h - right.h) <= kPixelTolerance;
        }

        /// The rect lies inside the other one, give or take @p tolerance
        bool LiesIn(const Rect& inner, const Rect& outer, float tolerance = kPixelTolerance) {
            return inner.x >= outer.x - tolerance && inner.y >= outer.y - tolerance && inner.x + inner.w <= outer.x + outer.w + tolerance && inner.y + inner.h <= outer.y + outer.h + tolerance;
        }

        /// The two rects are closer to each other than @p gap (or overlap)
        bool Touch(const Rect& left, const Rect& right, float gap) {
            return left.x < right.x + right.w + gap && right.x < left.x + left.w + gap && left.y < right.y + right.h + gap && right.y < left.y + left.h + gap;
        }

        /// The text component of a label or a button (both are a UIElement with a UITextComponent; a button also has a UIInputClicker); null for anything else
        UITextComponent* TextOf(const UIElement& element) {
            return element.GetComponent<UITextComponent>();
        }

        /// The clicker of a button; null for an element that is no button
        UIInputClicker* ClickerOf(const UIElement& element) {
            return element.GetComponent<UIInputClicker>();
        }

        /// A button: an element with a clicker
        bool IsButton(const UIElement& element) {
            return ClickerOf(element) != nullptr;
        }

        /// A label: an element with a text and no clicker
        bool IsLabel(const UIElement& element) {
            return TextOf(element) != nullptr && !IsButton(element);
        }

        /// The text a label or a button shows
        String ShownText(const UIElement& element) {
            const UITextComponent* text = TextOf(element);
            if (!text) {
                return {};
            }
            const StringView shown = text->GetDisplayText(element);
            return String(shown.data(), shown.size());
        }

        /// The rect lies inside the other one and both have the same middle
        ::testing::AssertionResult IsCenteredIn(const Rect& inner, const Rect& outer) {
            const bool inside = LiesIn(inner, outer);
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

        /// The actions that the segments of the panel emit, and that ChessOpponentPanelComponent answers
        constexpr const char* kSetModeAction = "ChessSetMode";
        constexpr const char* kSetColorAction = "ChessSetColor";
        constexpr const char* kSetEngineAction = "ChessSetEngine";
        constexpr const char* kSetLevelAction = "ChessSetLevel";
        constexpr const char* kPromoteAction = "ChessPromote";  // the actions of the promotion panel
        constexpr const char* kCancelPromotionAction = "ChessCancelPromotion";

        /// Replaces the first @p what in @p text and fails the test when it is not there (a broken copy that changed nothing would prove nothing)
        void Replace(String& text, const String& what, const String& with) {
            const size_t at = text.find(what);
            ASSERT_NE(at, String::npos) << what;
            text.replace(at, what.size(), with);
        }

        /// The `<elements>` and the `<variants>` of the canvas on the node @p nodeName of the shipped scene, as the content of a screen; empty when there is none
        String ShippedElementsXml(const char* nodeName) {
            pugi::xml_document document;
            if (!LoadShippedSceneDocument(document)) {
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
            if (const pugi::xml_node variants = node.select_node(".//component[@type='UICanvasComponent']/variants").node()) {
                variants.print(out, "", pugi::format_raw);
            }
            out << "</ui>";
            return FromStdString(out.str());
        }

        /// A canvas of the SceneView space laid out on its own, for the copies of the panel that a test breaks on purpose: the elements come from the text of the shipped file
        /// with an edit, never from a second copy of the panel
        struct CanvasCopy {
            UIScreenContent content;
            UICanvasLayout layout;

            explicit CanvasCopy(const String& xml, bool useSafeArea = true) {
                XmlDeserializer deserializer;
                EXPECT_TRUE(deserializer.LoadFromString(StringView(xml)));
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

            Vector<PoolString> _actions;
            Vector<PoolString> _values;
        };

        /// Every control with text in the tree, depth first (a stack and a choice group have none of their own)
        void CollectControls(const UIElement& element, Vector<const UIElement*>& found) {
            if (TextOf(element) != nullptr) {
                found.push_back(&element);
            }
            for (const auto& child : element.children) {
                CollectControls(*child, found);
            }
        }

        /// @p root and every element below it, depth first
        Vector<const UIElement*> AllElements(const UIElement& root) {
            Vector<const UIElement*> found;
            found.push_back(&root);
            for (const auto& child : root.children) {
                for (const UIElement* below : AllElements(*child)) {
                    found.push_back(below);
                }
            }
            return found;
        }

        /// What is wrong with a modal panel as it is laid out (the plate is the element @p plateName), one problem to a line, nothing when it is right. The text that a control cuts is
        /// checked here because the validator does not see it for an ellipsis label (a button keeps kButtonAir between its text and the edge of its fill)
        String PanelProblems(const UICanvasLayout& layout, const char* plateName = "Plate") {
            String problems;
            const float scale = layout.GetResolved().scale;
            Vector<const UIElement*> controls;
            CollectControls(*layout.root, controls);
            for (const UIElement* control : controls) {
                if (control->rect.IsEmpty()) {
                    if (control->active.visible) {
                        problems += Format("{} '{}' has an empty rect (a missing height?)\n", control->name.ToStringView(), ShownText(*control).c_str());
                    }
                    continue;
                }
                const float air = control->IsInteractive() ? kButtonAir : 0.0f;
                const UIContentFit fit = control->CheckContent(control->rect.w - 2.0f * air, control->rect.h, scale);
                if (fit.hasContent && !fit.fits) {
                    problems += Format("{} cuts '{}': {:.1f} units wide with {:.1f} of air each side, at scale {:.3f}\n", control->name.ToStringView(), ShownText(*control).c_str(), control->rect.w,
                                       air, scale);
                }
                if (control->IsInteractive() && control->rect.h < kSegmentHeight - kPixelTolerance) {
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
                // A fixed-height plate does not grow with its content: a control that sticks out of it is the sign of a plate that is too low
                for (const UIElement* control : controls) {
                    const Rect& rect = control->rect;
                    if (!rect.IsEmpty() && (rect.x < plate->rect.x - kPixelTolerance || rect.y < plate->rect.y - kPixelTolerance || rect.x + rect.w > plate->rect.x + plate->rect.w + kPixelTolerance ||
                                            rect.y + rect.h > plate->rect.y + plate->rect.h + kPixelTolerance)) {
                        problems += Format("{} sticks out of the plate ({:.1f}, {:.1f}, {:.1f}, {:.1f})\n", control->name.ToStringView(), rect.x, rect.y, rect.w, rect.h);
                    }
                }
            }
            return problems;
        }

        bool HasIssue(const Vector<UIMatrixFailure>& failures, UILayoutIssueKind kind) {
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
            String name;
            const char* group = "";
            const char* action = "";
            String word;
            String id;
        };

        String Joined(StringView prefix, StringView name) {
            return Format("{}{}", prefix, name);
        }

        /// The ten segments, all of it taken from the tables of the game (the mode and color words of OpponentText, the engines, the levels); the scene is held to this list
        Vector<PanelSegment> ExpectedPanelSegments() {
            Vector<PanelSegment> segments;
            for (const OpponentMode mode : EnumUtils<OpponentMode>::Values()) {
                const StringView id = OpponentModeId(mode);
                segments.push_back(
                    {Joined("Mode", EnumUtils<OpponentMode>::ToString(mode)), "ModeGroup", kSetModeAction, OpponentText::ModeLabel(mode == OpponentMode::Computer), String(id.data(), id.size())});
            }
            for (const PieceColor color : {PieceColor::White, PieceColor::Black}) {
                const StringView id = PieceColorId(color);
                segments.push_back({Joined("Color", EnumUtils<PieceColor>::ToString(color)), "ColorGroup", kSetColorAction, OpponentText::ColorLabel(color), String(id.data(), id.size())});
            }
            for (const OpponentEngine engine : EnumUtils<OpponentEngine>::Values()) {
                const StringView id = OpponentEngineId(engine);
                const StringView word = OpponentEngineName(engine);
                segments.push_back({Joined("Engine", EnumUtils<OpponentEngine>::ToString(engine)), "EngineGroup", kSetEngineAction, String(word.data(), word.size()), String(id.data(), id.size())});
            }
            for (const OpponentLevel& level : GetOpponentLevels()) {
                segments.push_back({Joined("Level", level.name), "LevelGroup", kSetLevelAction, String(level.name.data(), level.name.size()), String(level.id.data(), level.id.size())});
            }
            return segments;
        }

        // The squares of the board in `classic`
        const Color kLightSquareColor{240, 217, 181, 255};
        const Color kDarkSquareColor{181, 136, 99, 255};

        // The letters and numbers at the edges of the board (the node BoardCoordinates of SceneConfig.xml): a label sits in the corner of an edge cell that a piece does not reach
        constexpr float kCellSize = 80.0f;
        constexpr float kBoardLeft = 100.0f;

        /// A view of the scene as a test walks it: an output that shows it, where its board is and the narrowest window it is shown in (480 px wide, with side insets)
        struct ViewShape {
            const char* name;
            Pair<float, float> output;
            Pair<float, float> narrowest;
            float viewWidth;
            float viewHeight;
            float boardLeft;
            float boardTop;
            bool portrait;

            /// The fit scale of the view in its narrowest window: the width limits the default and the Portrait view there, the height the Wide one
            [[nodiscard]] float NarrowestScale() const {
                return std::min(narrowest.first / viewWidth, narrowest.second / viewHeight);
            }
            /// Where the safe area of that window (SideInsets: a share of the short side at each side) begins in view units, past the letterbox of the view if it has one
            [[nodiscard]] float NarrowestSafeLeft() const {
                const float scale = NarrowestScale();
                const float letterbox = (narrowest.first - viewWidth * scale) * 0.5f;
                return std::max(0.0f, (std::min(narrowest.first, narrowest.second) * UIPreview::kSideInsetShare - letterbox) / scale);
            }
            /// The width of that safe area in view units
            [[nodiscard]] float NarrowestSafeWidth() const {
                return viewWidth - 2.0f * NarrowestSafeLeft();
            }
        };

        const ViewShape kViewShapes[] = {
            {"default view", {1024.0f, 768.0f}, {kNarrowestWidth, kNarrowestHeight}, kViewWidth, kViewHeight, kBoardLeft, kBoardTop, false},
            {"Portrait view", {768.0f, 1024.0f}, {kNarrowestWidth, kNarrowestPortraitHeight}, kPortraitViewWidth, kPortraitViewHeight, kPortraitBoardLeft, kPortraitBoardTop, true},
            {"Wide view", {1280.0f, 720.0f}, {kWideNarrowestWidth, kNarrowestHeight}, kWideViewWidth, kViewHeight, kBoardLeft, kBoardTop, false},
            {"Tall view", {kNarrowestWidth, kNarrowestTallHeight}, {kNarrowestWidth, kNarrowestTallHeight}, kPortraitViewWidth, kTallViewHeight, kPortraitBoardLeft, kPortraitBoardTop, true}};
        constexpr size_t kBoardEdgeCells = kLastBoardIndex + 1;
        constexpr float kPieceWidth = kCellSize * kCellWidth / kCellHeight;  // a piece fitted by Contain is as high as its cell, as wide as the aspect of its atlas cell, and centered
        constexpr float kPieceLeft = (kCellSize - kPieceWidth) * 0.5f;       // 15.5 units from the left edge of the cell
        constexpr float kPieceRight = kPieceLeft + kPieceWidth;              // 64.5 units
        constexpr float kLabelBand = 30.0f;                                  // a rank number lies in the top 30 units of its cell, a file letter in the bottom 30 (the boxes are 2..28 and 52..78)

        /// Whether a canvas was laid out for a view with the places of Portrait (Portrait or Tall): its bounds are the view in units
        bool LaidOutForPortrait(const UICanvasLayout& layout) {
            return layout.GetResolved().bounds.w == kPortraitViewWidth;
        }

        /// Whether a canvas was laid out for the Wide view: its bounds are the view in units
        bool LaidOutForWide(const UICanvasLayout& layout) {
            return layout.GetResolved().bounds.w == kWideViewWidth;
        }

        /// Whether a canvas was laid out for the Tall view: its bounds are the view in units
        bool LaidOutForTall(const UICanvasLayout& layout) {
            return layout.GetResolved().bounds.h == kTallViewHeight;
        }

        /// One label of the board coordinates: its name and the drawn cell it sits in (the rank labels are in the left column, one per row; the file labels in the bottom row, one per column)
        struct BoardLabel {
            String name;
            bool file = false;
            int column = 0;
            int row = 0;
        };

        Vector<BoardLabel> ExpectedBoardLabels() {
            Vector<BoardLabel> labels;
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

        /// What is wrong with the labels of the board as they are laid out, one problem to a line, nothing when it is right: a glyph the box cuts (the validator does not see it for a label
        /// that clips), a box that is not in the corner of its edge cell (the top left, left of the piece, for a rank; the bottom right, right of it, for a file) and a text color that does not
        /// read on the cell (the tokens resolved in the current theme, which the fixture keeps `classic`)
        String BoardLabelProblems(const UICanvasLayout& layout) {
            String problems;
            const float scale = layout.GetResolved().scale;
            // The board of the view the canvas was laid out for: its bounds are that view in units
            const bool portrait = LaidOutForPortrait(layout);
            const float boardLeft = portrait ? kPortraitBoardLeft : kBoardLeft;
            const float boardTop = portrait ? kPortraitBoardTop : kBoardTop;
            for (const BoardLabel& spec : ExpectedBoardLabels()) {
                UIElement* element = layout.root->FindByName(PoolString::Intern(spec.name));
                if (element == nullptr) {
                    problems += Format("{} is missing\n", spec.name.c_str());
                    continue;
                }
                element->ResolveTheme(CoreManager::GetUIThemes());  // a copy of the canvas is never drawn: its tokens are still unresolved
                const Rect& rect = element->rect;
                if (rect.IsEmpty()) {
                    problems += Format("{} has an empty rect\n", spec.name.c_str());
                    continue;
                }
                const UIContentFit fit = element->CheckContent(rect.w, rect.h, scale);
                if (fit.hasContent && !fit.fits) {
                    problems += Format("{} cuts '{}': {:.1f} x {:.1f} units at scale {:.3f}\n", spec.name.c_str(), ShownText(*element).c_str(), rect.w, rect.h, scale);
                }
                const float cellX = boardLeft + static_cast<float>(spec.column) * kCellSize;
                const float cellY = boardTop + static_cast<float>(spec.row) * kCellSize;
                const float low = spec.file ? cellX + kPieceRight : cellX;
                const float high = spec.file ? cellX + kCellSize : cellX + kPieceLeft;
                const float top = spec.file ? cellY + kCellSize - kLabelBand : cellY;
                const float bottom = spec.file ? cellY + kCellSize : cellY + kLabelBand;
                if (rect.x < low - kPixelTolerance || rect.x + rect.w > high + kPixelTolerance || rect.y < top - kPixelTolerance || rect.y + rect.h > bottom + kPixelTolerance) {
                    problems += Format("{} ({:.1f}, {:.1f}, {:.1f}, {:.1f}) is not in the corner x {:.1f}..{:.1f}, y {:.1f}..{:.1f} of its cell\n", spec.name.c_str(), rect.x, rect.y, rect.w, rect.h,
                                       low, high, top, bottom);
                }
                const UITextComponent* label = IsLabel(*element) ? TextOf(*element) : nullptr;
                const Color& expected = ContrastColor(spec.column, spec.row);
                if (label == nullptr || !SameColor(label->textColor, expected)) {
                    problems += Format("{} has not the color {},{},{} that reads on its cell\n", spec.name.c_str(), expected.r, expected.g, expected.b);
                }
            }
            return problems;
        }

        // ---- the node Material of SceneConfig.xml: the layout checks (the behavior tests are further down) ----
        constexpr float kMaterialMarginRight = 100.0f;  // the left margin ends where the board begins
        constexpr float kMaterialRowHeight = 40.0f;     // a row is at least this high
        constexpr float kMaterialRowWidth = 64.0f;      // and this wide, in both views
        constexpr size_t kMaterialRounds = 99;          // one round per sign, "+1" to "+99"
        constexpr int kMostCaptured = 8;                // the longest count word is "x8": the most a row can show is the eight pawns (a test holds it to the summary of two bare kings)
        constexpr const char* kMaterialGroups[] = {"Top", "Bottom"};
        constexpr const char* kMaterialMarginSet = "";      // the word of a set in its names: Material<Group>... is the margin set (above and below the board in Portrait)
        constexpr const char* kMaterialPanelSet = "Panel";  // MaterialPanel<Group>... the set in the bands of the move panel
        constexpr const char* kMaterialSets[] = {kMaterialMarginSet, kMaterialPanelSet};
        constexpr float kMaterialPanelRowWidth = 52.0f;  // a chip of the panel set is at least this wide (and kMaterialRowHeight high)
        // The bands of the move panel the panel set lies in (ChessMovePanel.xml): the top one, and the one under the list of its rows
        constexpr float kPanelBandGap = 6.0f;  // the spacing of MovePanelBox
        const Rect kPanelTopBand{kMovePanelLeft, kMovePanelTop, kMovePanelRight - kMovePanelLeft, 56.0f};
        const Rect kPanelBottomBand{kMovePanelLeft, kMovePanelTop + 56.0f + kPanelBandGap + static_cast<float>(kMovePanelRows) * 56.0f + kPanelBandGap, kMovePanelRight - kMovePanelLeft, 56.0f};

        // The longest words of the move list (the move panel and the menu section "Moves" bind the same variables)
        constexpr const char* kLongestMoves[] = {"Qh4xe1+", "exd8=Q#", "O-O-O"};  // the longest move of each kind of text the list can hold
        constexpr const char* kLongestNumber = "100.";
        constexpr size_t kMovePanelWordRounds = std::size(kLongestMoves);

        /// Sets the variables of the rows to round @p round: the three longest moves and the longest number go round the cells, so each one stands in a White and in a Black cell
        void PublishMovePanelRound(size_t round) {
            UIVars& vars = CoreManager::GetUIVars();
            for (size_t row = 0; row < kMovePanelRows; ++row) {
                vars.Set(PoolString::Intern(Format("movePanelNumber{}", row).c_str()), kLongestNumber);
                vars.Set(PoolString::Intern(Format("movePanelWhite{}", row).c_str()), kLongestMoves[(round + row) % std::size(kLongestMoves)]);
                vars.Set(PoolString::Intern(Format("movePanelBlack{}", row).c_str()), kLongestMoves[(round + row + 1) % std::size(kLongestMoves)]);
            }
        }

        /// Sets the twelve variables of the display to the words of round @p round: every count word "x1".."x8" (kMostCaptured) stands at every row and every sign "+1".."+99" at both balances once
        /// over the kMaterialRounds rounds
        void PublishMaterialRound(size_t round) {
            UIVars& vars = CoreManager::GetUIVars();
            size_t row = 0;
            for (const char* group : kMaterialGroups) {
                for (const char* piece : kMaterialPieces) {
                    const String word = Format("x{}", static_cast<int>((round + row++) % kMostCaptured) + 1);
                    vars.Set(PoolString::Intern(Format("material{}{}", group, piece).c_str()), StringView(word));
                }
                const size_t shift = group == kMaterialGroups[0] ? 0 : kMaterialRounds / 2;
                const String sign = BalanceText(static_cast<int>((round + shift) % kMaterialRounds) + 1);
                vars.Set(PoolString::Intern(Format("material{}Balance", group).c_str()), StringView(sign));
            }
        }

        /// Shows the lists and the balances of the set @p set and hides those of the other set, as the component leaves them
        void ShowMaterialSet(UICanvasComponent& canvas, const char* set) {
            for (const char* each : kMaterialSets) {
                const bool shown = StringView(each) == set;
                for (const char* group : kMaterialGroups) {
                    EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern(Format("Material{}{}List", each, group).c_str()), shown)) << each << group;
                    EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern(Format("Material{}{}Balance", each, group).c_str()), shown)) << each << group;
                }
            }
        }

        /// Shows the set @p set alone (ShowMaterialSet) with its ten rows (the component shows only the rows of the pieces that are gone), so the layout holds the lists at their full size
        void ShowEveryMaterialRow(UICanvasComponent& canvas, const char* set = kMaterialMarginSet) {
            ShowMaterialSet(canvas, set);
            for (const char* group : kMaterialGroups) {
                for (const char* piece : kMaterialPieces) {
                    EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern(Format("Material{}{}{}Row", set, group, piece).c_str()), true)) << set << group << piece;
                }
            }
        }

        /// What is wrong with the canvas Material as it is laid out with all ten rows shown, one problem to a line, nothing when it is right. The text that a count or a sign cuts is checked
        /// here because the validator does not see a Clip label's cut; the lists must lie inside the safe area only while their box does (the rule of Game/MaterialLayout.h, so the rows would
        /// be shown), a balance box always. A group stays in its place of the view it was laid out for: the left margin of the default view, the band above the board (the upper group) or
        /// below it (the lower one) in Portrait, where the rows go along x and not along y
        String MaterialProblems(const UICanvasLayout& layout) {
            String problems;
            const float scale = layout.GetResolved().scale;
            const Rect safe = layout.GetResolved().safeBounds;
            const bool portrait = LaidOutForPortrait(layout);
            const bool listsFit = MaterialListFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h, portrait ? MaterialView::Portrait : MaterialView::Default);
            const Rect margin{0.0f, kBoardTop, kMaterialMarginRight, kBoardBottom - kBoardTop};
            const Rect upperBand{0.0f, kPortraitHudBottom, kPortraitViewWidth, kPortraitBoardTop - kPortraitHudBottom};
            const Rect lowerBand{0.0f, kPortraitBoardBottom, kPortraitViewWidth, kPortraitStatusTop - kPortraitBoardBottom};

            const auto find = [&](const String& name) -> const UIElement* {
                const UIElement* element = layout.root->FindByName(PoolString::Intern(name.c_str()));
                if (element == nullptr) {
                    problems += Format("{} is missing\n", name.c_str());
                } else if (element->rect.IsEmpty()) {
                    problems += Format("{} has an empty rect\n", name.c_str());
                    return nullptr;
                }
                return element;
            };
            const auto place = [&](const UIElement& element, const char* group, bool inSafeArea) {
                const Rect& where = !portrait ? margin : (group == kMaterialGroups[0] ? upperBand : lowerBand);
                if (!LiesIn(element.rect, where)) {
                    problems += Format("{} ({:.1f}, {:.1f}, {:.1f}, {:.1f}) leaves its place (x {:.0f} to {:.0f}, y {:.0f} to {:.0f})\n", element.name.ToStringView(), element.rect.x, element.rect.y,
                                       element.rect.w, element.rect.h, where.x, where.x + where.w, where.y, where.y + where.h);
                }
                if (inSafeArea && !LiesIn(element.rect, safe)) {
                    problems += Format("{} ({:.1f}, {:.1f}, {:.1f}, {:.1f}) leaves the safe area ({:.1f}, {:.1f}, {:.1f}, {:.1f})\n", element.name.ToStringView(), element.rect.x, element.rect.y,
                                       element.rect.w, element.rect.h, safe.x, safe.y, safe.w, safe.h);
                }
            };
            const auto fits = [&](const UIElement& element) {
                const UIContentFit fit = element.CheckContent(element.rect.w, element.rect.h, scale);
                if (fit.hasContent && !fit.fits) {
                    problems += Format("{} cuts '{}': {:.1f} x {:.1f} units at scale {:.3f}\n", element.name.ToStringView(), ShownText(element).c_str(), element.rect.w, element.rect.h, scale);
                }
            };

            float previousListBottom = -1.0f;
            for (const char* group : kMaterialGroups) {
                const UIElement* list = find(Format("Material{}List", group));
                if (list != nullptr) {
                    place(*list, group, listsFit);
                    if (list->rect.y < previousListBottom) {
                        problems += Format("{} ({:.1f}) starts above the end of the list before it ({:.1f})\n", list->name.ToStringView(), list->rect.y, previousListBottom);
                    }
                    previousListBottom = list->rect.y + list->rect.h;
                }
                float previousRowEnd = -1.0f;  // along the list: the bottom of the row before it in the default view, its right edge in Portrait
                for (const char* piece : kMaterialPieces) {
                    const UIElement* row = find(Format("Material{}{}Row", group, piece));
                    const UIElement* icon = find(Format("Material{}{}Icon", group, piece));
                    const UIElement* count = find(Format("Material{}{}Count", group, piece));
                    if (row == nullptr) {
                        continue;
                    }
                    place(*row, group, listsFit);
                    if (row->rect.h < kMaterialRowHeight - kPixelTolerance) {
                        problems += Format("{} is {:.1f} units high, under {:.0f}\n", row->name.ToStringView(), row->rect.h, kMaterialRowHeight);
                    }
                    if (row->rect.w < kMaterialRowWidth - kPixelTolerance) {
                        problems += Format("{} is {:.1f} units wide, under {:.0f}\n", row->name.ToStringView(), row->rect.w, kMaterialRowWidth);
                    }
                    const float start = portrait ? row->rect.x : row->rect.y;
                    if (start < previousRowEnd - kPixelTolerance) {
                        problems += Format("{} starts at {:.1f}, before the end of the row before it ({:.1f})\n", row->name.ToStringView(), start, previousRowEnd);
                    }
                    previousRowEnd = portrait ? row->rect.x + row->rect.w : row->rect.y + row->rect.h;
                    if (list != nullptr && !LiesIn(row->rect, list->rect)) {
                        problems += Format("{} is not inside its list\n", row->name.ToStringView());
                    }
                    for (const UIElement* part : {icon, count}) {
                        if (part != nullptr && !LiesIn(part->rect, row->rect)) {
                            problems += Format("{} is not inside its row\n", part->name.ToStringView());
                        }
                    }
                    if (icon != nullptr && count != nullptr && icon->rect.x + icon->rect.w > count->rect.x + kPixelTolerance) {
                        problems += Format("{} and {} overlap\n", icon->name.ToStringView(), count->name.ToStringView());
                    }
                    if (icon != nullptr && icon->GetComponent<UIImageComponent>() == nullptr) {
                        problems += Format("{} has no UIImageComponent\n", icon->name.ToStringView());
                    }
                    if (count != nullptr) {
                        fits(*count);
                    }
                }
                if (const UIElement* balance = find(Format("Material{}Balance", group))) {
                    place(*balance, group, true);
                    fits(*balance);
                    if (list != nullptr && balance->rect.x < list->rect.x + list->rect.w) {
                        problems += Format("{} touches {}\n", balance->name.ToStringView(), list->name.ToStringView());
                    }
                }
            }
            return problems;
        }

        /// What is wrong with the panel set of the canvas Material as it is laid out shown with its ten rows (ShowEveryMaterialRow(canvas, kMaterialPanelSet)), in a window where the move panel is
        /// shown: one problem to a line, nothing when it is right. A group lies in its band of the panel (the upper one in the top band, the lower one in the band under the list) and in the safe
        /// area, its rows go along x and keep their size, and no count or sign is cut
        String MaterialPanelProblems(const UICanvasLayout& layout) {
            String problems;
            const float scale = layout.GetResolved().scale;
            const Rect safe = layout.GetResolved().safeBounds;
            const auto find = [&](const String& name) -> const UIElement* {
                const UIElement* element = layout.root->FindByName(PoolString::Intern(name.c_str()));
                if (element == nullptr) {
                    problems += Format("{} is missing\n", name.c_str());
                } else if (element->rect.IsEmpty()) {
                    problems += Format("{} has an empty rect\n", name.c_str());
                    return nullptr;
                }
                return element;
            };
            const auto place = [&](const UIElement& element, const Rect& band) {
                if (!LiesIn(element.rect, band) || !LiesIn(element.rect, safe)) {
                    problems += Format("{} ({:.1f}, {:.1f}, {:.1f}, {:.1f}) leaves its band (y {:.0f} to {:.0f}) or the safe area\n", element.name.ToStringView(), element.rect.x, element.rect.y,
                                       element.rect.w, element.rect.h, band.y, band.y + band.h);
                }
            };
            const auto fits = [&](const UIElement& element) {
                const UIContentFit fit = element.CheckContent(element.rect.w, element.rect.h, scale);
                if (fit.hasContent && !fit.fits) {
                    problems += Format("{} cuts '{}': {:.1f} x {:.1f} units at scale {:.3f}\n", element.name.ToStringView(), ShownText(element).c_str(), element.rect.w, element.rect.h, scale);
                }
            };
            for (const char* group : kMaterialGroups) {
                const Rect& band = group == kMaterialGroups[0] ? kPanelTopBand : kPanelBottomBand;
                const UIElement* list = find(Format("MaterialPanel{}List", group));
                if (list != nullptr) {
                    place(*list, band);
                }
                float previousRowEnd = -1.0f;
                for (const char* piece : kMaterialPieces) {
                    const UIElement* row = find(Format("MaterialPanel{}{}Row", group, piece));
                    const UIElement* icon = find(Format("MaterialPanel{}{}Icon", group, piece));
                    const UIElement* count = find(Format("MaterialPanel{}{}Count", group, piece));
                    if (row == nullptr) {
                        continue;
                    }
                    place(*row, band);
                    if (row->rect.h < kMaterialRowHeight - kPixelTolerance || row->rect.w < kMaterialPanelRowWidth - kPixelTolerance) {
                        problems += Format("{} is {:.1f} x {:.1f} units, under {:.0f} x {:.0f}\n", row->name.ToStringView(), row->rect.w, row->rect.h, kMaterialPanelRowWidth, kMaterialRowHeight);
                    }
                    if (row->rect.x < previousRowEnd - kPixelTolerance) {
                        problems += Format("{} starts at {:.1f}, before the end of the row before it ({:.1f})\n", row->name.ToStringView(), row->rect.x, previousRowEnd);
                    }
                    previousRowEnd = row->rect.x + row->rect.w;
                    if (list != nullptr && !LiesIn(row->rect, list->rect)) {
                        problems += Format("{} is not inside its list\n", row->name.ToStringView());
                    }
                    for (const UIElement* part : {icon, count}) {
                        if (part != nullptr && !LiesIn(part->rect, row->rect)) {
                            problems += Format("{} is not inside its row\n", part->name.ToStringView());
                        }
                    }
                    if (icon != nullptr && count != nullptr && icon->rect.x + icon->rect.w > count->rect.x + kPixelTolerance) {
                        problems += Format("{} and {} overlap\n", icon->name.ToStringView(), count->name.ToStringView());
                    }
                    if (icon != nullptr && icon->GetComponent<UIImageComponent>() == nullptr) {
                        problems += Format("{} has no UIImageComponent\n", icon->name.ToStringView());
                    }
                    if (count != nullptr) {
                        fits(*count);
                    }
                }
                if (const UIElement* balance = find(Format("MaterialPanel{}Balance", group))) {
                    place(*balance, band);
                    fits(*balance);
                    if (list != nullptr && balance->rect.x < list->rect.x + list->rect.w) {
                        problems += Format("{} touches {}\n", balance->name.ToStringView(), list->name.ToStringView());
                    }
                }
            }
            return problems;
        }

        /// The rects of the canvas Material that are on the screen (a row with a rect, a balance) against the controls of the canvas @p other (a label or a button with a rect): the pairs closer than
        /// @p gap, one to a line
        String MaterialClearance(const UICanvasLayout& material, const UICanvasLayout& other, float gap) {
            String problems;
            Vector<const UIElement*> mine;
            for (const char* set : kMaterialSets) {
                for (const char* group : kMaterialGroups) {
                    for (const char* piece : kMaterialPieces) {
                        mine.push_back(material.root->FindByName(PoolString::Intern(Format("Material{}{}{}Row", set, group, piece).c_str())));
                    }
                    mine.push_back(material.root->FindByName(PoolString::Intern(Format("Material{}{}Balance", set, group).c_str())));
                }
            }
            for (const UIElement* element : mine) {
                if (element == nullptr || element->rect.IsEmpty()) {
                    continue;
                }
                for (const UIElement* control : AllElements(*other.root)) {
                    if (TextOf(*control) != nullptr && !control->rect.IsEmpty() && Touch(element->rect, control->rect, gap)) {
                        problems += Format("{} ({:.1f}, {:.1f}, {:.1f}, {:.1f}) and {} ({:.1f}, {:.1f}, {:.1f}, {:.1f}) are closer than {:.0f} units\n", element->name.ToStringView(), element->rect.x,
                                           element->rect.y, element->rect.w, element->rect.h, control->name.ToStringView(), control->rect.x, control->rect.y, control->rect.w, control->rect.h, gap);
                    }
                }
            }
            return problems;
        }

        // Shared by the files of several topics
        constexpr const char* kPromotionFen = "8/P6k/8/8/8/8/8/K7 w - - 0 1";                                       // a white pawn on a7, the way to a8 is free
        constexpr const char* kWhiteTookAPawn = "rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3";       // White has taken the d pawn of Black: +1 for White
        constexpr const char* kBlackCanTakeAPawn = "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2";  // Black to move: d5e4 takes a pawn

        /// set_state {"fen": ...} of the control API; whether it was taken
        bool FenSetsTheState(const char* fen) {
            Json request = Json::object();
            request["fen"] = fen;
            return ChessGame::GetActive()->AsControllable()->SetState(request).has_value();
        }
        const Color kMaterialChipColor{232, 210, 170, 255};  // the fill of a row (SceneConfig.xml)
        constexpr float kBoardUnits = 640.0f;
        /// The outputs that decide which view is right: phones held upright and sideways, tablets, desktop windows, squares and the degenerate ones
        const Pair<float, float> kViewOutputs[] = {{480.0f, 853.0f},   {390.0f, 844.0f},   {720.0f, 1280.0f},  {768.0f, 1024.0f}, {800.0f, 800.0f},  {1024.0f, 768.0f}, {1280.0f, 720.0f},
                                                   {1920.0f, 1080.0f}, {480.0f, 480.0f},   {1.0f, 1.0f},       {0.0f, 0.0f},      {874.0f, 1000.0f}, {875.0f, 1000.0f}, {876.0f, 1000.0f},
                                                   {2560.0f, 1080.0f}, {1390.0f, 1000.0f}, {1400.0f, 1000.0f}, {480.0f, 1040.0f}, {590.0f, 1000.0f}, {600.0f, 1000.0f}, {480.0f, 640.0f}};
        const Color kBoardDimColor{0, 0, 0, 140};                                     // the dim of the result (ChessResultComponent.cpp)
        constexpr const char* kBlackMatesOnA1 = "r5k1/8/8/8/8/8/5PPP/6K1 b - - 0 1";  // Ra1 is mate
        /// A glyph of a label: a texture drawn with one of the two colors of the squares as its tint (the pieces are drawn white, and the text of the HUD too)
        bool IsLabelGlyph(const DrawCall& call) {
            return call.kind == DrawKind::Texture && (SameColor(call.color, kLightSquareColor) || SameColor(call.color, kDarkSquareColor));
        }
        /// Every element name of a tree, depth first
        void CollectNames(const UIElement& element, Vector<PoolString>& names) {
            if (!element.name.ToStringView().empty()) {
                names.push_back(element.name);
            }
            for (const auto& child : element.children) {
                CollectNames(*child, names);
            }
        }
    }  // namespace

// The panel of a build without Stockfish (CHESS_NO_STOCKFISH) hides the segment EngineStockfish (ChessOpponentPanelComponent::SyncStockfishSegment), so a case that clicks it, or counts ten
// segments, has nothing to click there; the cases at "TheStockfishSegment..." and "WithoutStockfish..." say what that build does instead
#if CHESS_NO_STOCKFISH
#define SKIP_WITHOUT_STOCKFISH_SEGMENT() GTEST_SKIP() << "the Stockfish segment of the panel is hidden (CHESS_NO_STOCKFISH)"
#else
#define SKIP_WITHOUT_STOCKFISH_SEGMENT() static_cast<void>(0)
#endif

    class ChessUiTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            // Whether Stockfish is offered must not depend on the machine: STOCKFISH names a file for the whole case (a test that wants none points it at a missing one)
            _stockfishDirectory = TestTempDirectory("ChessUiStockfish");
            std::filesystem::remove_all(_stockfishDirectory);
            std::filesystem::create_directories(_stockfishDirectory);
            _stockfishEnvironment.emplace("STOCKFISH", FoundStockfishFile().c_str());

            CoreManager::GetFontAtlasLibrary().Clear();
            CoreManager::GetUIVars().Clear();
            ASSERT_TRUE(LoadShippedThemes(CoreManager::GetUIThemes()));  // the board and its labels draw tokens
            _renderer = New<RecordingRenderer>();
            _renderer->width = 1280.0f;
            _renderer->height = 720.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            _manager = LoadShippedSceneManager();
            ASSERT_TRUE(_manager);
            _manager->Initialize();
            EngineEnvironment::UseSceneManager(_manager);  // GameManager switches to the scene of the game through CoreManager

            GameManager::GetInstance().Initialize();
            ASSERT_NE(ChessGame::GetActiveModel(), nullptr) << "Chess is the active game";
        }

        void TearDown() override {
            GameManager::GetInstance().Shutdown();
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            CoreManager::GetUIEnvironment().SetSafeAreaOverride(std::nullopt);  // a test that simulated insets leaves the shared environment as it was
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIThemes().Clear();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
            _stockfishEnvironment.reset();
            std::error_code ignored;
            std::filesystem::remove_all(_stockfishDirectory, ignored);
        }

        /// A file FindStockfish() accepts (it is never run: the doubles of a test play); the same name every time, so it can be made again after it was removed
        String FoundStockfishFile() const {
            const std::filesystem::path file = _stockfishDirectory / "stockfish-found";
            MakeStockfishFile(file);
            return FromStdString(file.string());
        }

        /// The path of a Stockfish that is not there (yet): handed to the settings it finds nothing, and PATH is not looked at then
        String MissingStockfishFile() const {
            return FromStdString((_stockfishDirectory / "stockfish-missing").string());
        }

        /// Settings with an explicit path for the controller of the game, and a new look at it
        void UseStockfishPath(const String& path) const {
            Opponent().SetSettings(OpponentSettings{path});
        }

        /// The opponent panel is open and drawn, the segment of Stockfish takes no room and Own engine fills the group, selected, and takes its click
        void ExpectOnlyTheOwnSegment() {
            const UIElement& stockfish = PanelButton("EngineStockfish");
            const UIElement& own = PanelButton("EngineOwn");
            const UISegmented& group = PanelControl<UISegmented>("EngineGroup");
            EXPECT_FALSE(stockfish.active.visible);
            EXPECT_TRUE(own.active.visible);
            ASSERT_FALSE(own.rect.IsEmpty());
            EXPECT_NEAR(own.rect.h, kSegmentHeight, kPixelTolerance);
            EXPECT_TRUE(stockfish.rect.IsEmpty()) << "a hidden segment takes no room";
            EXPECT_NEAR(own.rect.x, group.rect.x, kPixelTolerance) << "Own engine fills the group";
            EXPECT_NEAR(own.rect.w, group.rect.w, kPixelTolerance);
            EXPECT_EQ(Var("opponentEngine"), "own");
            Draw();
            EXPECT_TRUE(own.selected) << "and it is the selected one";
            EXPECT_FALSE(stockfish.selected);
            ActionLog log;
            ASSERT_TRUE(ClickPanel("EngineOwn").Both()) << "the one segment takes its click";
            EXPECT_EQ(log.Total(), 1u);
            EXPECT_EQ(log.LastValue(), "own"_intern);
        }

        ChessGameModel& Model() const {
            ChessGameModel* model = ChessGame::GetActiveModel();
            EXPECT_NE(model, nullptr);
            return *model;
        }

        Scene& GetScene() const {
            return *_manager->GetSceneByName("ChessScene"_intern);
        }

        IntrusivePtr<UICanvasComponent> Hud() const {
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                if (node->GetName() == "Hud"_intern) {
                    return node->GetComponent<UICanvasComponent>();
                }
            }
            return {};
        }

        UIElement& Label(const char* name) const {
            UIElement* element = Hud()->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            if (element == nullptr || !IsLabel(*element)) {
                ADD_FAILURE() << name << " is not a label of the HUD";
                std::abort();  // a reference to nothing cannot be returned; the failure above is printed first
            }
            return *element;
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

        /// A button of the panel by name: an element with a text and a clicker (the test fails when it is not there)
        UIElement& PanelButton(const char* name) const {
            UIElement& element = PanelElement(name);
            if (!IsButton(element) || TextOf(element) == nullptr) {
                ADD_FAILURE() << name << " is not a button";
                std::abort();
            }
            return element;
        }

        /// A label of the panel by name: an element with a text and no clicker (the test fails when it is not there)
        UIElement& PanelLabel(const char* name) const {
            UIElement& element = PanelElement(name);
            if (!IsLabel(element)) {
                ADD_FAILURE() << name << " is not a label";
                std::abort();
            }
            return element;
        }

        /// The middle of an element as it was laid out in the last draw, in output pixels (the view is fitted into the output)
        Pair<float, float> ScreenCenter(const UIElement& element) const {
            const Rect cell = ActiveFit().ToOutput(element.rect.x, element.rect.y, element.rect.w, element.rect.h);
            return {cell.x + cell.w * 0.5f, cell.y + cell.h * 0.5f};
        }

        /// Whether a point of the screen (output pixels) lies in an element as it was laid out in the last draw
        bool Covers(const UIElement& element, const Pair<float, float>& point) const {
            const Rect cell = ActiveFit().ToOutput(element.rect.x, element.rect.y, element.rect.w, element.rect.h);
            return point.first >= cell.x && point.first <= cell.x + cell.w && point.second >= cell.y && point.second <= cell.y + cell.h;
        }

        /// A point of the dim of a modal canvas that no plate covers: just inside the corner of the view (the dim is the whole canvas, so its middle is under the plate)
        Pair<float, float> DimCorner(const char* nodeName, const char* dimName) {
            Draw();
            const UIElement& dim = ElementOf(nodeName, dimName);
            return PixelOf(dim.rect.x + kDimInset, dim.rect.y + kDimInset);
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

        /// The canvas of the list of the menu: the node `MenuPanel`
        IntrusivePtr<UICanvasComponent> MenuList() const {
            return CanvasOf("MenuPanel");
        }

        /// A click on a control of the list of the menu, by name
        Taken ClickMenuList(const char* name) {
            return ClickControl(ElementOf("MenuPanel", name));
        }

        /// A button of the list of the menu by name: an element with a text and a clicker (the test fails when it is not there)
        UIElement& MenuButton(const char* name) const {
            UIElement& element = ElementOf("MenuPanel", name);
            if (!IsButton(element) || TextOf(element) == nullptr) {
                ADD_FAILURE() << name << " is not a button";
                std::abort();
            }
            return element;
        }

        /// The Menu button of the HUD and then the Opponent button of the list, clicked where they are drawn; whether the opponent panel is shown afterwards
        bool OpenThePanel() {
            if (!ClickButton(kMenuColor)) {
                return false;
            }
            if (!MenuList() || !MenuList()->_visible || !ClickMenuList("MenuSectionOpponent").Both()) {
                return false;
            }
            return Panel() && Panel()->_visible;
        }

        /// A click on the dim of the opponent panel, beside the plate (the panel has no Close button); whether the panel and the list are hidden afterwards
        bool CloseThePanel() {
            const auto corner = DimCorner("OpponentPanel", "Dim");
            return ClickAtPixel(corner.first, corner.second).Both() && Panel() && !Panel()->_visible && MenuList() && !MenuList()->_visible;
        }

        /// The canvas of the menu section "Game": the node `GameSection`
        IntrusivePtr<UICanvasComponent> GameSection() const {
            return CanvasOf("GameSection");
        }

        /// A click on a control of the Game section, by name
        Taken ClickGameSection(const char* name) {
            return ClickControl(ElementOf("GameSection", name));
        }

        /// The Menu button of the HUD and then the Resign / Draw button of the list, clicked where they are drawn; whether the section is shown afterwards
        bool OpenTheGameSection() {
            if (!ClickButton(kMenuColor)) {
                return false;
            }
            if (!MenuList() || !MenuList()->_visible || !ClickMenuList("MenuSectionGame").Both()) {
                return false;
            }
            return GameSection() && GameSection()->_visible;
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

        /// A button of the promotion panel by name: an element with a text and a clicker (the test fails when it is not there)
        UIElement& PromotionButton(const char* name) const {
            UIElement& element = PromotionElement(name);
            if (!IsButton(element) || TextOf(element) == nullptr) {
                ADD_FAILURE() << name << " is not a button";
                std::abort();
            }
            return element;
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
        String PanelText(const char* name) {
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

        OpponentController& Opponent() const {
            OpponentController* opponent = ChessGame::GetActiveOpponent();
            EXPECT_NE(opponent, nullptr);
            return *opponent;
        }

        /// The text of a UI variable after a frame was drawn: ChessHudComponent publishes the words of the HUD when it draws
        String Var(const char* name) {
            Draw();
            const StringView value = CoreManager::GetUIVars().Get(PoolString::Intern(name));
            return String(value.data(), value.size());
        }

        /// The letters or the numbers at the edge of the board as the 16 variables read after a frame: coordFile0..7 (files, by column) or coordRank0..7 (ranks, by row from the top), joined
        String Coordinates(bool files) {
            Draw();
            String text;
            for (int index = 0; index <= kLastBoardIndex; ++index) {
                const String name = files ? "coordFile{}"_format(index) : "coordRank{}"_format(index);
                const StringView value = CoreManager::GetUIVars().Get(PoolString::Intern(StringView(name)));
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

        /// A game of a build without Stockfish starts with the own engine. The cases that walk from "stockfish" (a name that the doubles of a test factory answer) pick it first, through
        /// the action of the panel; they must have given the controller a factory, or the default one refuses it. Where Stockfish is available it is the engine already and nothing is done
        static void StartFromTheStockfishName() {
            if constexpr (!kStockfishAvailable) {
                PressWith("ChessSetEngine", "stockfish");
            }
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
        String ControlOpponentString(const char* key) const {
            const auto value = GetJsonString(ControlOpponent(), key);
            return value ? String(value->c_str(), value->size()) : String("<missing>");
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
        Pair<float, float> PieceOrigin(const char* nodeName) const {
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(nodeName)) {
                    const auto transform = node->GetComponent<TransformComponent>();
                    EXPECT_TRUE(transform) << nodeName;
                    return transform ? Pair<float, float>{transform->_x, transform->_y} : Pair<float, float>{};
                }
            }
            ADD_FAILURE() << "no piece node " << nodeName;
            return {};
        }

        /// The draw layer of a node of the scene by its name
        int32_t NodeLayer(const char* nodeName) const {
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(nodeName)) {
                    return node->GetLayer();
                }
            }
            ADD_FAILURE() << "no node " << nodeName;
            return -1;
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

        /// A move of the pointer with the left button held or not, the way the window reports it (the UI first, then the game)
        static void MouseMove(float x, float y, bool held) {
            SDL_MouseMotionEvent event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.state = held ? SDL_BUTTON_LMASK : 0;
            event.x = x;
            event.y = y;
            CoreManager::GetUIInput().DispatchMouseMotion(event);
        }

        /// The release of the left button at a point of the screen
        static void Release(float x, float y) {
            CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false));
        }

        /// The release of the left button on the middle of a square
        void ReleaseOn(const char* square) const {
            const auto at = SquareCenter(square);
            Release(at.first, at.second);
        }

        /// The output pixel of a point of the view in scene units, for the fit of the view into the current output
        Pair<float, float> PixelOf(float sceneX, float sceneY) const {
            const Rect at = ActiveFit().ToOutput(sceneX, sceneY, 0.0f, 0.0f);
            return {at.x, at.y};
        }

        /// A drag between two points of the screen: the press, a held motion well past the threshold, a held motion at the target and the release there
        void DragBetween(Pair<float, float> from, Pair<float, float> to) {
            Draw();
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(from.first, from.second, true));
            MouseMove(from.first + kDragStepPixels, from.second, true);
            MouseMove(to.first, to.second, true);
            Release(to.first, to.second);
        }

        /// A drag of the piece on a square to the middle of another one
        void Drag(const char* from, const char* to) {
            Draw();  // the points are where the view this frame picks puts the squares
            DragBetween(SquareCenter(from), SquareCenter(to));
        }

        /// The same by drawn cells (column from the left, row from the top), for the turned board
        void DragCells(int fromColumn, int fromRow, int toColumn, int toRow) {
            Draw();
            const Rect from = CellRect(fromColumn, fromRow);
            const Rect to = CellRect(toColumn, toRow);
            DragBetween({from.x + from.w * 0.5f, from.y + from.h * 0.5f}, {to.x + to.w * 0.5f, to.y + to.h * 0.5f});
        }

        /// What the board holds in the pointer now: none when no piece is lifted
        static Optional<ChessBoardComponent::LiftedPiece> Lifted() {
            const ChessBoardComponent* board = ChessBoardComponent::GetActive();
            EXPECT_NE(board, nullptr);
            return board ? board->GetLiftedPiece() : std::nullopt;
        }

        /// What a game loop does: one frame (the update of every scene) after the other until @p done, or until the time limit; false when it ran out
        template <typename Condition>
        bool FramesUntil(Condition done) {
            return WaitUntil([&] {
                _manager->UpdateAll();
                return done();
            });
        }

        PositionAnalyst& Analyst() const {
            PositionAnalyst* analyst = ChessGame::GetActiveAnalyst();
            EXPECT_NE(analyst, nullptr);
            return *analyst;
        }

        /// Asks for the hint of the position on the board and runs frames until the engine has answered for it
        bool ShowTheHint() {
            if (!Analyst().RequestHint(Model())) {
                return false;
            }
            return FramesUntil([&] { return Analyst().GetHint(Model()).has_value(); });
        }

        /// The recorded fills of the hint in the last frame: the squares it leaves and goes to, in drawing order
        [[nodiscard]] Pair<Vector<const DrawCall*>, Vector<const DrawCall*>> HintFills() const {
            return {RectCalls(kHintFromColor), RectCalls(kHintToColor)};
        }

        /// The recorded fills of the last move in the last frame, in drawing order (the square it leaves, then the one it goes to)
        [[nodiscard]] Vector<const DrawCall*> LastMoveFills() const {
            return RectCalls(kLastMoveColor);
        }

        /// The two rects the last move is meant to fill (the squares of the last move of the model, where they are drawn now): from, to
        [[nodiscard]] Pair<Rect, Rect> LastMoveRects() const {
            const Move last = Model().GetMoves().back();
            return {SquareRect(last.From()), SquareRect(last.To())};
        }

        /// How many sounds the game has asked the audio manager for so far, played or not (ChessTests has no backend); read it before a step to see what the step asked for
        static uint64_t AudioRequests() {
            return CoreManager::GetAudio().GetRequestCount();
        }

        /// The sounds asked for after the count @p before was read, oldest first
        static Vector<AudioPlayRecord> AudioRequestsSince(uint64_t before) {
            Vector<AudioPlayRecord> recent;
            CoreManager::GetAudio().GetRecentPlays(recent);
            Vector<AudioPlayRecord> found;
            for (const AudioPlayRecord& record : recent) {
                if (record.index > before) {
                    found.push_back(record);
                }
            }
            return found;
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

        /// A frame of the game as it is a few frames after a window has changed its shape: when the draw picked another view of the scene, the next update announces it to the board
        /// (SceneViewChangedEvent), and the frame is drawn again with the geometry of that view
        void Draw() {
            const uint32_t viewBefore = GetScene().GetViewVersion();
            _renderer->ClearCalls();
            _manager->DrawAll();
            if (GetScene().GetViewVersion() != viewBefore) {
                _manager->UpdateAll();
                _renderer->ClearCalls();
                _manager->DrawAll();
            }
        }

        /// Whether the scene draws a view with the board and the places of Portrait now (Portrait or Tall; the last draw picked it)
        bool PortraitIsActive() const {
            return HasPortraitPlaces(GetScene().GetActiveView().name);
        }

        /// The size of the view the scene draws now, in scene units
        float ViewWidth() const {
            return GetScene().GetViewWidth();
        }
        float ViewHeight() const {
            return GetScene().GetViewHeight();
        }

        /// Draws the scene in @p output (width, height in pixels); whether it shows the Portrait view then, with the board following it
        bool ShowPortrait(const Pair<float, float>& output) {
            DrawAt(output.first, output.second);
            return PortraitIsActive();
        }

        /// The fit of the active view into the current output, as the scene draws it
        ViewTransform ActiveFit() const {
            return ViewTransform::Fit(GetScene().GetViewWidth(), GetScene().GetViewHeight(), _renderer->width, _renderer->height);
        }

        /// The middle, in output pixels, of the fill that a button was drawn with in the last frame; none when no fill of that color was drawn (a hidden button draws nothing)
        Optional<Pair<float, float>> DrawnCenter(const Color& fill) const {
            const auto fills = RectCalls(fill);
            if (fills.empty()) {
                return std::nullopt;
            }
            const Rect& rect = fills.back()->rect;
            return Pair<float, float>{rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
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
        /// With @p pickView every case takes the view the scene would draw for its output (as ValidateOnMatrix() does with the views of the scene), else the view that is active now
        template <typename Check>
        void OnEveryCaseOfTheMatrix(UICanvasLayout& layout, Check check, bool pickView = false) const {
            for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
                layout.Update(ContextOf(matrixCase, pickView));
                check(matrixCase.label, layout);
            }
            layout.Invalidate();
        }

        /// What is wrong with the HUD as it is laid out, one problem to a line, nothing when it is right: text that its control cuts, and two controls closer than kMinGap.
        /// The validator of the matrix does not see the first (a label with an ellipsis "can shrink", so TextDoesNotFit is not reported), which is why it is checked here, on the
        /// text that is shown; a button must also keep kButtonAir between its text and the edge of the fill. A hidden control has an empty rect and counts for nothing
        static String HudProblems(const UICanvasLayout& layout) {
            String problems;
            const float scale = layout.GetResolved().scale;
            const auto& elements = layout.root->children;
            for (size_t i = 0; i < elements.size(); ++i) {
                const UIElement& element = *elements[i];
                if (element.rect.IsEmpty()) {
                    continue;
                }
                const float air = element.IsInteractive() ? kButtonAir : 0.0f;
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

        /// The HUD as a frame leaves it, through the size matrix with every word it can show: the validator's findings, and HudProblems()
        void ExpectTheHudSurvivesTheMatrix() {
            constexpr size_t kReportedFailures = 4;
            Draw();  // the HUD publishes the words

            auto canvas = Hud();
            ASSERT_TRUE(canvas);
            ASSERT_NE(HudElement("Menu"), nullptr);
            ASSERT_EQ(HudElement("Level"), nullptr) << "the cycle button Level is gone: the panel has the level";
            canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update

            const UILayoutContext base = ViewContext();
            ASSERT_FLOAT_EQ(base.sceneViewWidth, kWideViewWidth) << "the 1280 x 720 of the fixture is a Wide window";
            ASSERT_FLOAT_EQ(base.sceneViewHeight, kViewHeight);
            UILayoutValidatorOptions options;
            options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
            const HudWords words = AllHudWords();
            ASSERT_GE(words.Rounds(), 2u);

            size_t failed = 0;
            String report;
            for (size_t round = 0; round < words.Rounds(); ++round) {
                words.Publish(round);
                const auto failures = ValidateOnMatrix(canvas->GetLayout(), options, base, SceneViews());
                if (!failures.empty() && ++failed <= kReportedFailures) {
                    report += Format("round {}: {}", round, DescribeMatrixFailures(failures).c_str());
                }
                OnEveryCaseOfTheMatrix(
                    canvas->GetLayout(),
                    [&](const String& label, const UICanvasLayout& layout) {
                        const String problems = HudProblems(layout);
                        if (!problems.empty() && ++failed <= kReportedFailures) {
                            report += Format("round {}, {}:\n{}", round, label.c_str(), problems.c_str());
                        }
                    },
                    true);
            }
            EXPECT_EQ(failed, 0u) << report.c_str();
        }

        /// The validator on the whole matrix for each round of words (the error line of the panel changes with the round): every finding of every round
        Vector<UIMatrixFailure> ValidateWithWords(UICanvasLayout& layout) const {
            UILayoutValidatorOptions options;
            options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
            const HudWords words = AllHudWords();
            Vector<UIMatrixFailure> all;
            for (size_t round = 0; round < words.Rounds(); ++round) {
                words.Publish(round);
                const auto failures = ValidateOnMatrix(layout, options, ViewContext(), SceneViews());
                all.insert(all.end(), failures.begin(), failures.end());
            }
            return all;
        }

        /// PanelProblems() on every case of the matrix, in the view the scene picks for it, for each round of words: the lines of the first @p reportedCases cases that have any
        String PanelProblemsOnMatrix(UICanvasLayout& layout, size_t reportedCases = kReportedCases, const char* plateName = "Plate") const {
            String report;
            size_t reported = 0;
            const HudWords words = AllHudWords();
            for (size_t round = 0; round < words.Rounds(); ++round) {
                words.Publish(round);
                OnEveryCaseOfTheMatrix(
                    layout,
                    [&](const String& label, const UICanvasLayout& laidOut) {
                        const String problems = PanelProblems(laidOut, plateName);
                        if (!problems.empty() && reported++ < reportedCases) {
                            report += Format("round {}, {}:\n{}", round, label.c_str(), problems.c_str());
                        }
                    },
                    true);
            }
            return report;
        }

        /// The views of the scene as the matrix picks from: the default size and the variants
        UIMatrixViews SceneViews() const {
            return UIMatrixViews{kViewWidth, kViewHeight, GetScene().GetViewVariants()};
        }

        /// The canvas alone in the narrowest window of a view: 480 px wide with the side insets of a phone held sideways, 480 x 480 for the default view and 480 x 853 for Portrait; the view
        /// is the one the scene picks for that window
        void LayOutInTheNarrowestWindow(UICanvasLayout& layout, const ViewShape& shape = kViewShapes[0]) const {
            UILayoutContext context = ViewContext();
            context.outputWidth = shape.narrowest.first;
            context.outputHeight = shape.narrowest.second;
            context.safeArea = UIPreview::SimulatedSafeArea(SafeAreaSimulation::SideInsets, context.outputWidth, context.outputHeight);
            const ActiveSceneView view = SceneViewSelector::Pick(kViewWidth, kViewHeight, GetScene().GetViewVariants(), context.outputWidth, context.outputHeight);
            context.sceneViewWidth = view.width;
            context.sceneViewHeight = view.height;
            layout.Update(context);
        }

        /// The canvas of the letters and numbers at the edges of the board: the node `BoardCoordinates`
        IntrusivePtr<UICanvasComponent> Coordinate() const {
            return CanvasOf("BoardCoordinates");
        }

        /// The canvas of the captured pieces and the balance: the node `Material`
        IntrusivePtr<UICanvasComponent> Material() const {
            return CanvasOf("Material");
        }

        /// The canvas of the move list: the node `MovePanel`
        IntrusivePtr<UICanvasComponent> MovePanel() const {
            return CanvasOf("MovePanel");
        }

        /// An element of the `MovePanel` canvas by name (the test fails when it is not there)
        UIElement& MovePanelElement(const char* name) const {
            return ElementOf("MovePanel", name);
        }

        /// The word of a cell of the move list after one frame: @p side is "White" or "Black"
        String MovePanelWord(size_t row, const char* side) {
            Draw();
            return ShownText(MovePanelElement(Format("MovePanel{}Text{}", side, row).c_str()));
        }

        /// The rows of the move list the screen has after one frame, shown ones only, joined by " | ": "1. e4 e5", the cell of the last move in brackets ("2. [Nf3]"), a cell with no word left out
        String ShownMovePanelRows() {
            return ShownMoveRows("MovePanel");
        }

        /// ShownMovePanelRows() of the canvas on the node @p node, whose elements are named `<node>Row<i>` and so on (the move panel and the menu section "Moves")
        String ShownMoveRows(const char* node) {
            Draw();
            String rows;
            for (size_t row = 0; row < kMovePanelRows; ++row) {
                if (!ElementOf(node, Format("{}Row{}", node, row).c_str()).active.visible) {
                    continue;
                }
                String line = ShownText(ElementOf(node, Format("{}Number{}", node, row).c_str()));
                for (const char* side : {"White", "Black"}) {
                    const String word = ShownText(ElementOf(node, Format("{}{}Text{}", node, side, row).c_str()));
                    if (word.empty()) {
                        continue;
                    }
                    const bool marked = ElementOf(node, Format("{}{}Mark{}", node, side, row).c_str()).active.visible;
                    line += marked ? Format(" [{}]", word.c_str()) : Format(" {}", word.c_str());
                }
                rows += rows.empty() ? "" : " | ";
                rows += line;
            }
            return rows;
        }

        /// An element of the `Material` canvas by name (the test fails when it is not there)
        UIElement& MaterialElement(const char* name) const {
            return ElementOf("Material", name);
        }

        /// The row of a piece in a group of the display as it was laid out in the last draw: @p group is "Top" or "Bottom", @p piece "Queen" .. "Pawn", @p set kMaterialMarginSet or kMaterialPanelSet
        UIElement& MaterialRow(const char* group, const char* piece, const char* set = kMaterialMarginSet) const {
            return MaterialElement(Format("Material{}{}{}Row", set, group, piece).c_str());
        }

        /// Whether that row of @p set is on the screen: shown by the component and given a rect by the layout
        bool RowShownIn(const char* set, const char* group, const char* piece) const {
            const UIElement& row = MaterialRow(group, piece, set);
            return row.active.visible && !row.rect.IsEmpty();
        }

        /// Whether the row is on the screen in either set (the component never shows both)
        bool RowShown(const char* group, const char* piece) const {
            return RowShownIn(kMaterialMarginSet, group, piece) || RowShownIn(kMaterialPanelSet, group, piece);
        }

        /// Which set the component shows after the last draw, from its lists and balances: "margin", "panel", or "mixed" when the eight do not agree on one set
        String MaterialPlace() const {
            bool margin = true;
            bool panel = true;
            for (const char* group : kMaterialGroups) {
                for (const char* part : {"List", "Balance"}) {
                    margin = margin && MaterialElement(Format("Material{}{}", group, part).c_str()).active.visible;
                    margin = margin && !MaterialElement(Format("MaterialPanel{}{}", group, part).c_str()).active.visible;
                    panel = panel && MaterialElement(Format("MaterialPanel{}{}", group, part).c_str()).active.visible;
                    panel = panel && !MaterialElement(Format("Material{}{}", group, part).c_str()).active.visible;
                }
            }
            return margin ? "margin" : panel ? "panel" : "mixed";
        }

        /// The rows on the screen after the last draw in either set, in the order of the scene ("TopQueen BottomPawn"), nothing when none is
        String ShownMaterialRows() const {
            String shown;
            for (const char* group : {"Top", "Bottom"}) {
                for (const char* piece : kMaterialPieces) {
                    if (RowShown(group, piece)) {
                        shown += shown.empty() ? "" : " ";
                        shown += Format("{}{}", group, piece);
                    }
                }
            }
            return shown;
        }

        /// The text of the variable that a count of the display shows, after a frame: "x3" or ""
        String MaterialCount(const char* group, const char* piece) {
            return Var(Format("material{}{}", group, piece).c_str());
        }

        /// The sprite the icon of a row has been given; a failure is recorded when the two sets do not have the same one
        String MaterialSprite(const char* group, const char* piece) const {
            String sprites[std::size(kMaterialSets)];
            for (size_t set = 0; set < std::size(kMaterialSets); ++set) {
                const UIImageComponent* icon = MaterialElement(Format("Material{}{}{}Icon", kMaterialSets[set], group, piece).c_str()).GetComponent<UIImageComponent>();
                if (icon == nullptr) {
                    ADD_FAILURE() << kMaterialSets[set] << group << piece << " has no UIImageComponent icon";
                    return {};
                }
                const StringView name = icon->sprite.ToStringView();
                sprites[set] = String(name.data(), name.size());
            }
            EXPECT_EQ(sprites[0], sprites[1]) << group << piece << ": the icons of the margin and of the panel show the same piece";
            return sprites[0];
        }

        /// The twelve variables of the display after a frame, joined: ten counts (top group queen to pawn, then the bottom group) and the two balances ("|" between them)
        String MaterialWords() {
            Draw();
            String words;
            for (const char* group : {"Top", "Bottom"}) {
                for (const char* piece : kMaterialPieces) {
                    const StringView value = CoreManager::GetUIVars().Get(PoolString::Intern(Format("material{}{}", group, piece).c_str()));
                    words += Format("{}|", value);
                }
                const StringView balance = CoreManager::GetUIVars().Get(PoolString::Intern(Format("material{}Balance", group).c_str()));
                words += Format("{}|", balance);
            }
            return words;
        }

        /// Draws the first two frames of a scene: the canvas has been laid out once only after the first, and the rows wait for that
        void Settle() {
            Draw();
            Draw();
        }

        /// A window of @p width x @p height pixels with the safe area of a phone held sideways (SafeAreaSimulation::SideInsets), drawn twice so the rule of the lists has seen it
        void ResizeWithSideInsets(float width, float height) {
            _renderer->width = width;
            _renderer->height = height;
            CoreManager::GetUIEnvironment().SetSafeAreaOverride(UIPreview::SimulatedSafeArea(SafeAreaSimulation::SideInsets, width, height));
            Settle();
        }

        /// A window of @p width x @p height pixels with no insets at all, drawn twice
        void ResizeWithoutInsets(float width, float height) {
            _renderer->width = width;
            _renderer->height = height;
            CoreManager::GetUIEnvironment().SetSafeAreaOverride(std::nullopt);
            Settle();
        }

        /// A label of the board coordinates by name; null, with a failure recorded, when it is not a label (the caller stops with `ASSERT_NE`, so one test fails and the rest run)
        UIElement* CoordinateLabel(const char* name) const {
            const auto canvas = Coordinate();
            UIElement* element = canvas ? canvas->GetRoot().FindByName(PoolString::Intern(name)) : nullptr;
            UIElement* label = element != nullptr && IsLabel(*element) ? element : nullptr;
            if (label == nullptr) {
                ADD_FAILURE() << name << " is not a label of BoardCoordinates";
            }
            return label;
        }

        /// A rect of the view in output pixels, for the fit of the view into the current output
        Rect OutputRect(const Rect& view) const {
            return ActiveFit().ToOutput(view.x, view.y, view.w, view.h);
        }

        /// Sets the 16 variables of the labels to round @p round of the 8: every letter and every digit stands at every label once over the rounds, so the widest one is laid out in each box
        static void PublishCoordinateRound(size_t round) {
            const StringView letters = kFileLetters;
            const StringView digits = kRankDigits;
            for (int index = 0; index <= kLastBoardIndex; ++index) {
                const size_t at = (static_cast<size_t>(index) + round) % letters.size();
                CoreManager::GetUIVars().Set(PoolString::Intern(String("coordFile{}"_format(index))), letters.substr(at, 1));
                CoreManager::GetUIVars().Set(PoolString::Intern(String("coordRank{}"_format(index))), digits.substr(at, 1));
            }
        }

        /// BoardLabelProblems() on every case of the matrix for each of the 8 rounds of letters: the lines of the first @p reportedCases cases that have any
        String BoardLabelProblemsOnMatrix(UICanvasLayout& layout, size_t reportedCases = kReportedCases, bool pickView = true) const {
            String report;
            size_t reported = 0;
            for (size_t round = 0; round < kBoardEdgeCells; ++round) {
                PublishCoordinateRound(round);
                OnEveryCaseOfTheMatrix(
                    layout,
                    [&](const String& label, const UICanvasLayout& laidOut) {
                        const String problems = BoardLabelProblems(laidOut);
                        if (!problems.empty() && reported++ < reportedCases) {
                            report += Format("round {}, {}:\n{}", round, label.c_str(), problems.c_str());
                        }
                    },
                    pickView);
            }
            return report;
        }

        /// The context a case of the size matrix lays a canvas out in: the view of the scene (with @p pickView the view the scene would draw for the output of the case), and the window and
        /// the safe area of the case
        UILayoutContext ContextOf(const UIMatrixCase& matrixCase, bool pickView = false) const {
            UILayoutContext context = ViewContext();
            context.outputWidth = matrixCase.context.outputWidth;
            context.outputHeight = matrixCase.context.outputHeight;
            context.safeArea = matrixCase.context.safeArea;
            if (pickView) {
                const ActiveSceneView view = SceneViewSelector::Pick(kViewWidth, kViewHeight, GetScene().GetViewVariants(), context.outputWidth, context.outputHeight);
                context.sceneViewWidth = view.width;
                context.sceneViewHeight = view.height;
            }
            return context;
        }

        /// MaterialProblems() on every case of the matrix, in the view the scene picks for it, for the first @p rounds rounds of words (rows already shown): the lines of the first
        /// @p reportedCases cases that have any
        String MaterialProblemsOnMatrix(UICanvasLayout& layout, size_t rounds = kMaterialRounds, size_t reportedCases = kReportedCases) const {
            String report;
            size_t reported = 0;
            for (size_t round = 0; round < rounds; ++round) {
                PublishMaterialRound(round);
                OnEveryCaseOfTheMatrix(
                    layout,
                    [&](const String& label, const UICanvasLayout& laidOut) {
                        const String problems = MaterialProblems(laidOut);
                        if (!problems.empty() && reported++ < reportedCases) {
                            report += Format("round {}, {}:\n{}", round, label.c_str(), problems.c_str());
                        }
                    },
                    true);
            }
            return report;
        }

        void DrawAt(float width, float height) {
            _renderer->width = width;
            _renderer->height = height;
            Draw();
        }

        /// The color at the bottom of the board as the game says it (ChessGame::BoardBottomColor(), as the board does): Black when the program plays White, or when a puzzle is on that
        /// Black solves
        bool BlackIsAtTheBottom() const {
            const ChessGame* game = ChessGame::GetActive();
            return game != nullptr && game->BoardBottomColor() == PieceColor::Black;
        }

        /// The top left corner of a cell of the board in view units: @p column from the left, @p rowFromTop from the top
        Pair<float, float> CellOrigin(int column, int rowFromTop) const {
            const float left = PortraitIsActive() ? kPortraitBoardLeft : kBoardLeft;
            const float top = PortraitIsActive() ? kPortraitBoardTop : kBoardTop;
            return {left + static_cast<float>(column) * 80.0f, top + static_cast<float>(rowFromTop) * 80.0f};
        }

        /// The cell a square is drawn in, written out here and not taken from BoardOrientation.h: White at the bottom has a1 at the bottom left, Black at the bottom has the grid turned by 180 degrees
        static Pair<int, int> CellOfSquare(const Square& s, bool blackAtTheBottom) {
            return blackAtTheBottom ? Pair<int, int>{7 - s.file, s.rank} : Pair<int, int>{s.file, 7 - s.rank};
        }

        /// A cell of the board in output pixels, for the fit of the view into the current output
        Rect CellRect(int column, int rowFromTop) const {
            const auto [x, y] = CellOrigin(column, rowFromTop);
            return ActiveFit().ToOutput(x, y, 80.0f, 80.0f);
        }

        /// A square of the board in output pixels, for the fit of the view into the current output and the orientation the opponent gives
        Rect SquareRect(const char* square) const {
            return SquareRect(Sq(square));
        }

        Rect SquareRect(const Square& square) const {
            const auto [column, row] = CellOfSquare(square, BlackIsAtTheBottom());
            return CellRect(column, row);
        }

        /// The middle of a square of the board in output pixels
        Pair<float, float> SquareCenter(const char* square) const {
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
            const Vector<uint8_t> pixels(static_cast<size_t>(kAtlasWidth) * kAtlasHeight * 4, 0);
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
        Vector<const DrawCall*> RectCalls(const Color& color) const {
            Vector<const DrawCall*> found;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && SameColor(call.color, color)) {
                    found.push_back(&call);
                }
            }
            return found;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        std::filesystem::path _stockfishDirectory;
        Optional<ScopedEnvironment> _stockfishEnvironment;
    };

}  // namespace BECore::Chess::Tests
