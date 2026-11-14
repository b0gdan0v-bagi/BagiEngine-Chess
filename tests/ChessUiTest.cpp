#include "ChessTestUtil.h"
#include "HudWords.h"
#include "TestEngines.h"

#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/ViewTransform.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
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
        const Color kHintFromColor{60, 130, 230, 110};  // the two squares of the hint: the one the piece leaves and the one it goes to
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
        constexpr float kHalfSquare = 40.0f;     // half the 80 units of a square: the piece in the hand is centered on the pointer
        constexpr int32_t kPieceLayer = 0;       // the layer of a piece node in the scene
        constexpr int32_t kDragLayer = 6;        // the layer of a piece in the hand: over the chips (5), under the HUD (10)
        constexpr float kGlyphTolerance = 1.0f;  // a glyph of a label is laid out to the pixel of the font, not to the unit

        constexpr int kManyFrames = 200;  // frames that must pass with nothing happening

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

        bool Play(ChessGameModel& model, StringView uci) {
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

        /// The actions that the segments of the panel emit, and that ChessBoardComponent answers
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

        /// The `<elements>` of the canvas on the node @p nodeName of the shipped scene, as the content of a screen; empty when there is none
        String ShippedElementsXml(const char* nodeName) {
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

        /// What is wrong with a modal panel as it is laid out (the plate is the element @p plateName), one problem to a line, nothing when it is right: a control that is not shown at all (an empty
        /// rect: a segment without a `height`), text that its control cuts (the validator does not see it for an ellipsis label; a button keeps kButtonAir between its text and the edge of its fill),
        /// a button under kSegmentHeight, and a plate that leaves the safe area
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

        /// What is wrong with the labels of the board as they are laid out, one problem to a line, nothing when it is right: a label that is missing or not shown, a glyph the box cuts
        /// (the validator does not see it for a label that clips), a box that is not in the corner of its edge cell (the top left, left of the piece, for a rank; the bottom right, right of it, for a
        /// file) or leaves the board, and a text color that does not read on the cell
        String BoardLabelProblems(const UICanvasLayout& layout) {
            String problems;
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
        constexpr size_t kMaterialRounds = 99;          // one round per sign, "+1" to "+99"
        constexpr int kMostCaptured = 8;                // the longest count word is "x8": the most a row can show is the eight pawns (a test holds it to the summary of two bare kings)
        constexpr const char* kMaterialGroups[] = {"Top", "Bottom"};

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

        /// Shows the ten rows of the canvas (the component shows only the rows of the pieces that are gone), so the layout holds the lists at their full size
        void ShowEveryMaterialRow(UICanvasComponent& canvas) {
            for (const char* group : kMaterialGroups) {
                for (const char* piece : kMaterialPieces) {
                    EXPECT_TRUE(canvas.SetElementVisible(PoolString::Intern(Format("Material{}{}Row", group, piece).c_str()), true)) << group << piece;
                }
            }
        }

        /// What is wrong with the canvas Material as it is laid out with all ten rows shown, one problem to a line, nothing when it is right: an element that is missing or has no rect, a list, row,
        /// count or balance outside the margin (x 0 to 100, the rows of the board) or on top of one another, a row under kMaterialRowHeight, a count or a sign that its box cuts (the validator
        /// does not see a Clip label's cut), and, while the box of the lists lies inside the safe area (the rule of Game/MaterialLayout.h, so the rows would be shown), a list that is not inside
        /// the safe area; a balance box must always be inside it
        String MaterialProblems(const UICanvasLayout& layout) {
            String problems;
            const float scale = layout.GetResolved().scale;
            const Rect safe = layout.GetResolved().safeBounds;
            const bool listsFit = MaterialListFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h);
            const Rect margin{0.0f, kBoardTop, kMaterialMarginRight, kBoardBottom - kBoardTop};

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
            const auto place = [&](const UIElement& element, bool inSafeArea) {
                if (!LiesIn(element.rect, margin)) {
                    problems += Format("{} ({:.1f}, {:.1f}, {:.1f}, {:.1f}) leaves the margin (x 0 to {:.0f}, y {:.0f} to {:.0f})\n", element.name.ToStringView(), element.rect.x, element.rect.y,
                                       element.rect.w, element.rect.h, margin.w, margin.y, margin.y + margin.h);
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
                    place(*list, listsFit);
                    if (list->rect.y < previousListBottom) {
                        problems += Format("{} ({:.1f}) starts above the end of the list before it ({:.1f})\n", list->name.ToStringView(), list->rect.y, previousListBottom);
                    }
                    previousListBottom = list->rect.y + list->rect.h;
                }
                float previousRowBottom = -1.0f;
                for (const char* piece : kMaterialPieces) {
                    const UIElement* row = find(Format("Material{}{}Row", group, piece));
                    const UIElement* icon = find(Format("Material{}{}Icon", group, piece));
                    const UIElement* count = find(Format("Material{}{}Count", group, piece));
                    if (row == nullptr) {
                        continue;
                    }
                    place(*row, listsFit);
                    if (row->rect.h < kMaterialRowHeight - kPixelTolerance) {
                        problems += Format("{} is {:.1f} units high, under {:.0f}\n", row->name.ToStringView(), row->rect.h, kMaterialRowHeight);
                    }
                    if (row->rect.y < previousRowBottom - kPixelTolerance) {
                        problems += Format("{} starts at {:.1f}, above the end of the row before it ({:.1f})\n", row->name.ToStringView(), row->rect.y, previousRowBottom);
                    }
                    previousRowBottom = row->rect.y + row->rect.h;
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
                    place(*balance, true);
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
            for (const char* group : kMaterialGroups) {
                for (const char* piece : kMaterialPieces) {
                    mine.push_back(material.root->FindByName(PoolString::Intern(Format("Material{}{}Row", group, piece).c_str())));
                }
                mine.push_back(material.root->FindByName(PoolString::Intern(Format("Material{}Balance", group).c_str())));
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
    }  // namespace

// The panel of a build without Stockfish (CHESS_NO_STOCKFISH) hides the segment EngineStockfish (ChessBoardComponent::HideUnavailableEngines), so a case that clicks it, or counts ten
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
            CoreManager::GetUIEnvironment().SetSafeAreaOverride(std::nullopt);  // a test that simulated insets leaves the shared environment as it was
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

        /// The canvas of the list of the menu: the node `MenuPanel`
        IntrusivePtr<UICanvasComponent> MenuList() const {
            return CanvasOf("MenuPanel");
        }

        /// A click on a control of the list of the menu, by name
        Taken ClickMenuList(const char* name) {
            return ClickControl(ElementOf("MenuPanel", name));
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

        /// The Close button of the opponent panel, clicked where it is laid out; whether the panel and the list are hidden afterwards
        bool CloseThePanel() {
            return ClickPanel("OpponentClose").Both() && Panel() && !Panel()->_visible && MenuList() && !MenuList()->_visible;
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

        /// The opponent of the game that is running
        OpponentController& Opponent() const {
            OpponentController* opponent = ChessGame::GetActiveOpponent();
            EXPECT_NE(opponent, nullptr);
            return *opponent;
        }

        /// The text of a UI variable after a frame was drawn: the board publishes the words of the HUD when it draws
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
            const Rect at = ViewTransform::Fit(kViewWidth, kViewHeight, _renderer->width, _renderer->height).ToOutput(sceneX, sceneY, 0.0f, 0.0f);
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
            DragBetween(SquareCenter(from), SquareCenter(to));
        }

        /// The same by drawn cells (column from the left, row from the top), for the turned board
        void DragCells(int fromColumn, int fromRow, int toColumn, int toRow) {
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

        /// The analyst of the game that is running
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
            String report;
            for (size_t round = 0; round < words.Rounds(); ++round) {
                words.Publish(round);
                const auto failures = ValidateOnMatrix(canvas->GetLayout(), options, base);
                if (!failures.empty() && ++failed <= kReportedFailures) {
                    report += Format("round {}: {}", round, DescribeMatrixFailures(failures).c_str());
                }
                OnEveryCaseOfTheMatrix(canvas->GetLayout(), [&](const String& label, const UICanvasLayout& layout) {
                    const String problems = HudProblems(layout);
                    if (!problems.empty() && ++failed <= kReportedFailures) {
                        report += Format("round {}, {}:\n{}", round, label.c_str(), problems.c_str());
                    }
                });
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
                const auto failures = ValidateOnMatrix(layout, options, ViewContext());
                all.insert(all.end(), failures.begin(), failures.end());
            }
            return all;
        }

        /// PanelProblems() on every case of the matrix for each round of words: the lines of the first @p reportedCases cases that have any
        String PanelProblemsOnMatrix(UICanvasLayout& layout, size_t reportedCases = kReportedCases, const char* plateName = "Plate") const {
            String report;
            size_t reported = 0;
            const HudWords words = AllHudWords();
            for (size_t round = 0; round < words.Rounds(); ++round) {
                words.Publish(round);
                OnEveryCaseOfTheMatrix(layout, [&](const String& label, const UICanvasLayout& laidOut) {
                    const String problems = PanelProblems(laidOut, plateName);
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

        /// The canvas of the captured pieces and the balance: the node `Material`
        IntrusivePtr<UICanvasComponent> Material() const {
            return CanvasOf("Material");
        }

        /// An element of the `Material` canvas by name (the test fails when it is not there)
        UIElement& MaterialElement(const char* name) const {
            return ElementOf("Material", name);
        }

        /// The row of a piece in a group of the display as it was laid out in the last draw: @p group is "Top" or "Bottom", @p piece "Queen" .. "Pawn"
        UIElement& MaterialRow(const char* group, const char* piece) const {
            return MaterialElement(Format("Material{}{}Row", group, piece).c_str());
        }

        /// Whether that row is on the screen: shown by the component and given a rect by the layout
        bool RowShown(const char* group, const char* piece) const {
            const UIElement& row = MaterialRow(group, piece);
            return row.active.visible && !row.rect.IsEmpty();
        }

        /// The rows on the screen after the last draw, in the order of the scene ("TopQueen BottomPawn"), nothing when none is
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

        /// The sprite the icon of a row has been given
        String MaterialSprite(const char* group, const char* piece) const {
            const UIImageComponent* icon = MaterialElement(Format("Material{}{}Icon", group, piece).c_str()).GetComponent<UIImageComponent>();
            if (icon == nullptr) {
                ADD_FAILURE() << group << piece << " has no UIImageComponent icon";
                return {};
            }
            const StringView name = icon->sprite.ToStringView();
            return String(name.data(), name.size());
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
            return ViewTransform::Fit(kViewWidth, kViewHeight, _renderer->width, _renderer->height).ToOutput(view.x, view.y, view.w, view.h);
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
        String BoardLabelProblemsOnMatrix(UICanvasLayout& layout, size_t reportedCases = kReportedCases) const {
            String report;
            size_t reported = 0;
            for (size_t round = 0; round < kBoardEdgeCells; ++round) {
                PublishCoordinateRound(round);
                OnEveryCaseOfTheMatrix(layout, [&](const String& label, const UICanvasLayout& laidOut) {
                    const String problems = BoardLabelProblems(laidOut);
                    if (!problems.empty() && reported++ < reportedCases) {
                        report += Format("round {}, {}:\n{}", round, label.c_str(), problems.c_str());
                    }
                });
            }
            return report;
        }

        /// The context a case of the size matrix lays a canvas out in: the view of the scene, and the window and the safe area of the case
        UILayoutContext ContextOf(const UIMatrixCase& matrixCase) const {
            UILayoutContext context = ViewContext();
            context.outputWidth = matrixCase.context.outputWidth;
            context.outputHeight = matrixCase.context.outputHeight;
            context.safeArea = matrixCase.context.safeArea;
            return context;
        }

        /// MaterialProblems() on every case of the matrix for the first @p rounds rounds of words (rows already shown): the lines of the first @p reportedCases cases that have any
        String MaterialProblemsOnMatrix(UICanvasLayout& layout, size_t rounds = kMaterialRounds, size_t reportedCases = kReportedCases) const {
            String report;
            size_t reported = 0;
            for (size_t round = 0; round < rounds; ++round) {
                PublishMaterialRound(round);
                OnEveryCaseOfTheMatrix(layout, [&](const String& label, const UICanvasLayout& laidOut) {
                    const String problems = MaterialProblems(laidOut);
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
            return opponent != nullptr && opponent->GetPlays() == Optional<PieceColor>(PieceColor::White);
        }

        /// The top left corner of a cell of the board in view units: @p column from the left, @p rowFromTop from the top
        static Pair<float, float> CellOrigin(int column, int rowFromTop) {
            return {100.0f + static_cast<float>(column) * 80.0f, kBoardTop + static_cast<float>(rowFromTop) * 80.0f};
        }

        /// The cell a square is drawn in, written out here and not taken from BoardOrientation.h: White at the bottom has a1 at the bottom left, Black at the bottom has the grid turned by 180 degrees
        static Pair<int, int> CellOfSquare(const Square& s, bool blackAtTheBottom) {
            return blackAtTheBottom ? Pair<int, int>{7 - s.file, s.rank} : Pair<int, int>{s.file, 7 - s.rank};
        }

        /// A cell of the board in output pixels, for the fit of the view into the current output
        Rect CellRect(int column, int rowFromTop) const {
            const ViewTransform fit = ViewTransform::Fit(kViewWidth, kViewHeight, _renderer->width, _renderer->height);
            const auto [x, y] = CellOrigin(column, rowFromTop);
            return fit.ToOutput(x, y, 80.0f, 80.0f);
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
            const String problems = HudProblems(layout);
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
        const UIElement* menu = element;
        ASSERT_TRUE(IsButton(*menu) && TextOf(*menu) != nullptr) << "the Menu button is a button: a text and a clicker";
        EXPECT_EQ(TextOf(*menu)->text, "Menu") << "a literal: not bound to a variable";
        EXPECT_EQ(ShownText(*menu), "Menu");
        EXPECT_EQ(ClickerOf(*menu)->action, PoolString::Intern("ChessOpenMenu"));
        EXPECT_NEAR(menu->rect.w, 110.0f, 0.5f) << "as wide as Reset";
        EXPECT_NEAR(menu->rect.h, 56.0f, 0.5f);
        EXPECT_EQ(HudElement("Opponent"), nullptr) << "the button was called Opponent when it showed the state; the name is not used any more";
        EXPECT_EQ(Panel()->GetRoot().FindByName(PoolString::Intern("Menu")), nullptr) << "the name Menu is the HUD's alone";
    }

    TEST_F(ChessUiTest, TheHudStaysOutsideTheBoardWithoutInsets) {
        auto canvas = Hud();
        ASSERT_TRUE(canvas);
        _manager->UpdateAll();

        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}, Pair<float, float>{1024.0f, 768.0f}, Pair<float, float>{800.0f, 800.0f},
                                 Pair<float, float>{2560.0f, 1080.0f}}) {
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
        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}, Pair<float, float>{1024.0f, 768.0f}, Pair<float, float>{800.0f, 800.0f},
                                 Pair<float, float>{2560.0f, 1080.0f}}) {
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
        const String problems = PanelProblemsOnMatrix(panel->GetLayout());
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
            const String problems = PanelProblems(layout);
            EXPECT_TRUE(problems.empty()) << "round " << round << ":\n" << problems.c_str();
        }

        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        size_t segments = 0;
        for (const PanelSegment& segment : ExpectedPanelSegments()) {
            const UIElement& button = PanelButton(segment.name.c_str());
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
        const UIElement& line = PanelLabel("OpponentError");
        ASSERT_FALSE(line.rect.IsEmpty());
        const float scale = layout.GetResolved().scale;

        size_t sentences = 0;
        for (const EngineErrorKind kind : EnumUtils<EngineErrorKind>::Values()) {
            const String sentence = OpponentText::Error(EngineError{kind, {}});
            CoreManager::GetUIVars().Set("opponentError"_intern, StringView(sentence));
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
            if (StringView(segment.group) != "LevelGroup") {
                continue;
            }
            const UIElement& button = PanelButton(segment.name.c_str());
            EXPECT_NEAR(button.rect.x, left, kPixelTolerance) << segment.name.c_str();
            width = width < 0.0f ? button.rect.w : width;
            EXPECT_NEAR(button.rect.w, width, kPixelTolerance) << segment.name.c_str() << ": equal shares by flex";
            left = button.rect.x + button.rect.w + 4.0f;
        }
        EXPECT_NEAR(left - 4.0f, level.rect.x + level.rect.w, kPixelTolerance) << "the four segments fill the group";
    }

    // ---- the opponent panel: the words and the ids come from the tables ------------------------------------------------------------

    TEST_F(ChessUiTest, TheTextAndTheValueOfEverySegmentAreThoseOfTheTables) {
        const Vector<PanelSegment> expected = ExpectedPanelSegments();
        ASSERT_EQ(expected.size(), kPanelSegments);

        for (const PanelSegment& want : expected) {
            SCOPED_TRACE(want.name.c_str());
            const UIElement& button = PanelButton(want.name.c_str());
            EXPECT_EQ(ShownText(button), want.word) << "the word on the segment is the one of the table";
            const StringView value = ClickerOf(button)->value.ToStringView();
            EXPECT_EQ(String(value.data(), value.size()), want.id);
            EXPECT_EQ(ClickerOf(button)->action, PoolString::Intern(want.action));
            EXPECT_EQ(button.selected, false) << "nothing is selected before a frame";
        }

        // The id of a segment is a valid id of the table behind its group, and the group holds exactly the segments of the table (a new level or engine without a segment fails here)
        for (const char* groupName : {"ModeGroup", "ColorGroup", "EngineGroup", "LevelGroup"}) {
            const UISegmented& group = PanelControl<UISegmented>(groupName);
            size_t inTable = 0;
            for (const PanelSegment& want : expected) {
                inTable += StringView(want.group) == groupName ? 1u : 0u;
            }
            size_t buttons = 0;
            for (const auto& child : group.children) {
                const UIInputClicker* button = ClickerOf(*child);
                ASSERT_NE(button, nullptr) << groupName << ": only segments";
                ++buttons;
                const StringView value = button->value.ToStringView();
                const StringView groupId = groupName;
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
        CountingFactory factory({MakeShared<InstantEngine>()});
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
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::White));

        for (const PanelSegment& segment : ExpectedPanelSegments()) {
            const PoolString action = ClickerOf(PanelButton(segment.name.c_str()))->action;
            const bool known = action == PoolString::Intern(kSetModeAction) || action == PoolString::Intern(kSetColorAction) || action == PoolString::Intern(kSetEngineAction) ||
                               action == PoolString::Intern(kSetLevelAction);
            EXPECT_TRUE(known) << segment.name.c_str() << " emits " << action.CStr() << ", which the board does not answer";
        }
        EXPECT_EQ(ClickerOf(PanelButton("OpponentBack"))->action, PoolString::Intern("ChessOpenMenu"));
        EXPECT_EQ(ClickerOf(PanelButton("OpponentClose"))->action, PoolString::Intern("ChessCloseMenu"));
        EXPECT_EQ(ClickerOf(*HudElement("Menu"))->action, PoolString::Intern("ChessOpenMenu"));
    }

    // ---- the opponent panel: broken copies, so that the checks can fail --------------------------------------------------------------
    // Each copy is the text of the shipped panel with one edit (ShippedElementsXml), laid out alone

    TEST_F(ChessUiTest, AWordCutByAFontThatIsTooBigIsCaughtByTheFitCheckAndNotByTheValidator) {
        String xml = ShippedElementsXml("OpponentPanel");
        ASSERT_FALSE(xml.empty());
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(name="EngineOwn" flex="1" height="56" fontSize="24")", R"(name="EngineOwn" flex="1" height="56" fontSize="44")"));
        CanvasCopy copy(xml);

        EXPECT_FALSE(HasIssue(ValidateWithWords(copy.layout), UILayoutIssueKind::TextDoesNotFit)) << "an ellipsis label can shrink: the validator does not report it";
        const String problems = PanelProblemsOnMatrix(copy.layout);
        EXPECT_NE(problems.find("EngineOwn cuts 'Own engine'"), String::npos) << problems.c_str();

        // With the clip the validator reports it too
        String clipped = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(clipped, R"(name="EngineOwn" flex="1" height="56" fontSize="24")", R"(name="EngineOwn" flex="1" height="56" fontSize="44")"));
        ASSERT_NO_FATAL_FAILURE(Replace(clipped, R"(text="Own engine")", R"(text="Own engine" overflow="Clip")"));
        CanvasCopy clippedCopy(clipped);
        EXPECT_TRUE(HasIssue(ValidateWithWords(clippedCopy.layout), UILayoutIssueKind::TextDoesNotFit));
    }

    TEST_F(ChessUiTest, ALongerWordInTheNarrowestWindowIsCaught) {
        // A word that fits in the plate of a wide window but not in a quarter of the Level row on the narrowest one
        String xml = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(text="Medium")", R"(text="Intermediate")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        const String problems = PanelProblems(copy.layout);
        EXPECT_NE(problems.find("LevelMedium cuts 'Intermediate'"), String::npos) << problems.c_str();
    }

    TEST_F(ChessUiTest, ASegmentUnderTheTouchSizeIsCaught) {
        String xml = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(name="LevelMax" flex="1" height="56")", R"(name="LevelMax" flex="1" height="40")"));
        CanvasCopy copy(xml);
        EXPECT_TRUE(HasIssue(ValidateWithWords(copy.layout), UILayoutIssueKind::TouchTargetTooSmall));
        EXPECT_NE(PanelProblemsOnMatrix(copy.layout).find("LevelMax is 40.0 units high"), String::npos);
    }

    TEST_F(ChessUiTest, ASegmentWithoutAnExplicitHeightHasNoRectAndIsCaughtHere) {
        // The gotcha of the widget: flex shares the width, not the height, so a segment without `height` gets a rect of height 0. It is not drawn, a click cannot find it
        // (`ui_click` answers "hidden") and the validator skips an empty rect, so only PanelProblems() sees it
        String xml = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(name="LevelHard" flex="1" height="56")", R"(name="LevelHard" flex="1")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        const UIElement* hard = copy.layout.root->FindByName(PoolString::Intern("LevelHard"));
        ASSERT_NE(hard, nullptr);
        EXPECT_NEAR(hard->rect.h, 0.0f, kPixelTolerance);
        const String problems = PanelProblems(copy.layout);
        EXPECT_NE(problems.find("LevelHard 'Hard' has an empty rect"), String::npos) << problems.c_str();
    }

    TEST_F(ChessUiTest, AnErrorSentenceWiderThanThePlateIsCaught) {
        // A plate 200 units wide cannot hold the longest sentences of the error line; the same copy with the shipped width is clean
        String xml = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(maxWidth="640")", R"(maxWidth="200")"));
        CanvasCopy copy(xml);
        const String problems = PanelProblemsOnMatrix(copy.layout, 100000);
        EXPECT_NE(problems.find("OpponentError cuts"), String::npos) << problems.c_str();

        CanvasCopy shipped(ShippedElementsXml("OpponentPanel"));
        EXPECT_TRUE(PanelProblemsOnMatrix(shipped.layout).empty()) << PanelProblemsOnMatrix(shipped.layout).c_str();
    }

    TEST_F(ChessUiTest, APlateTallerThanTheSafeAreaIsCaught) {
        String xml = ShippedElementsXml("OpponentPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(height="460")", R"(height="1200")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        EXPECT_NE(PanelProblems(copy.layout).find("leaves the safe area"), String::npos);
    }

    // ---- the words --------------------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, TheHudSaysWhoseMoveItIs) {
        Draw();
        EXPECT_EQ(ShownText(Label("Turn")), "White to move");
        EXPECT_EQ(ShownText(Label("Status")), "");

        ASSERT_TRUE(Play(Model(), "e2e4"));
        Draw();
        EXPECT_EQ(ShownText(Label("Turn")), "Black to move");
    }

    TEST_F(ChessUiTest, TheHudNamesACheckAndTheEnd) {
        Model().SetPosition(PositionFromFen("4k3/4r3/8/8/8/8/8/4K3 w - - 0 1"));
        Draw();
        EXPECT_EQ(ShownText(Label("Status")), "Check");

        Model().SetPosition(PositionFromFen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"));
        ASSERT_TRUE(Play(Model(), "a1a8"));
        Draw();
        EXPECT_EQ(ShownText(Label("Turn")), "White wins");
        EXPECT_EQ(ShownText(Label("Status")), "Checkmate");
    }

    TEST_F(ChessUiTest, TheWordsAreDrawnAsText) {
        Draw();

        size_t glyphsInTurn = 0;
        const UIElement& turn = Label("Turn");
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
        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}, Pair<float, float>{2560.0f, 1080.0f}}) {
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

        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}}) {
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
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f})) << "two people: a1 at the bottom left";

        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());

        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{660.0f, 80.0f})) << "a1 at the top right";
        EXPECT_EQ(PieceOrigin("WhiteRook_h1"), (Pair<float, float>{100.0f, 80.0f})) << "h1 at the top left";
        EXPECT_EQ(PieceOrigin("BlackRook_a8"), (Pair<float, float>{660.0f, 640.0f})) << "a8 at the bottom right";
        ExpectPiecesAtTheCellsOf(true);

        // The squares of the board: the cell of a1 is the one of h8 in the other orientation, with the color of a1 (dark) in both
        Draw();
        const Rect a1 = SquareRect("a1");
        const Rect h8 = SquareRect("h8");
        EXPECT_GT(a1.x, h8.x);
        EXPECT_LT(a1.y, h8.y);
        const auto fillOf = [this](const Rect& cell) -> Optional<Color> {
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
        CountingFactory factory({MakeShared<InstantEngine>()});
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
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true)});
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
        CountingFactory factory({MakeShared<InstantEngine>()});
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
        CountingFactory factory({MakeShared<InstantEngine>()});
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
        const auto plateOf = [this]() -> Optional<Rect> {
            Draw();
            const auto plate = RectCalls(kPromotionPlateColor);
            return plate.empty() ? std::nullopt : Optional<Rect>(plate.front()->rect);
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

        CountingFactory factory({MakeShared<InstantEngine>()});
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
        const auto dimOf = [this]() -> Optional<Rect> {
            Draw();
            const auto dim = RectCalls(kBoardDimColor);
            return dim.empty() ? std::nullopt : Optional<Rect>(dim.front()->rect);
        };

        CountingFactory factory({MakeShared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        StartFrom(kBlackMatesOnA1);
        ASSERT_TRUE(BlackIsAtTheBottom());

        ClickMove("a8", "a1");

        ASSERT_TRUE(Model().IsGameOver());
        EXPECT_EQ(Model().GetWinner(), Optional<PieceColor>(PieceColor::Black));
        const auto dim = dimOf();
        ASSERT_TRUE(dim.has_value());
        const ViewTransform fit = ViewTransform::Fit(kViewWidth, kViewHeight, _renderer->width, _renderer->height);
        EXPECT_TRUE(SameRect(*dim, fit.ToOutput(100.0f, kBoardTop, 640.0f, 640.0f))) << "the whole board, as with White at the bottom";
        EXPECT_FALSE(RectCalls(kResultBarColor).empty()) << "the bar of the winner";
    }

    TEST_F(ChessUiTest, TheBoardTurnsBackWhenNobodyOrBlackIsTheProgram) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(false);

        ControlPlays("white");
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(true);
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{660.0f, 80.0f}));

        ControlPlays(nullptr);
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(false);
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f})) << "nobody plays: White at the bottom again";

        ControlPlays("black");
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(false);
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f})) << "the program plays Black: the person has White at the bottom";
        EXPECT_FALSE(BlackIsAtTheBottom());
    }

    TEST_F(ChessUiTest, TheColorSegmentsAndTheModeTurnTheBoardAtTheNextFrame) {
        CountingFactory factory({MakeShared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        PressWith("ChessSetMode", "computer");  // the person has White, the program plays Black
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f})) << "a1 at the bottom left";

        PressWith("ChessSetColor", "black");
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{660.0f, 80.0f})) << "the person has Black: the board is turned";
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 1; })) << "and the program, now White, opens";
        _manager->UpdateAll();  // the pieces follow the move of this frame in the next one
        ExpectPiecesAtTheCellsOf(true);

        PressWith("ChessSetColor", "white");
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f})) << "the person has White again";
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2 && !Opponent().IsThinking(); })) << "the program, now Black, answers the opening";
        _manager->UpdateAll();
        ExpectPiecesAtTheCellsOf(false);  // every piece, the moved ones too
    }

    TEST_F(ChessUiTest, ABlackPickedWhileTwoPlayDoesNotTurnTheBoardUntilComputerIsPicked) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        PressWith("ChessSetColor", "black");
        _manager->UpdateAll();
        EXPECT_EQ(Var("opponentColor"), "black");
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f})) << "nobody plays yet: nothing turns";

        PressWith("ChessSetMode", "computer");
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{660.0f, 80.0f})) << "now the program plays White";

        PressWith("ChessSetMode", "two");
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f})) << "two people: White at the bottom";
        EXPECT_EQ(Var("opponentColor"), "black") << "the color stays remembered";
    }

    TEST_F(ChessUiTest, ARefusedPickLeavesTheOrientationAsItWas) {
        SKIP_WITHOUT_WORKER_THREADS();
        Vector<String> asked;
        Opponent().SetEngineFactory([&asked](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            const StringView id = OpponentEngineId(engine);
            asked.emplace_back(id.data(), id.size());
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
            }
            return MakeShared<GatedEngine>(true);
        });
        _manager->UpdateAll();
        StartFromTheStockfishName();

        // Computer with a Stockfish that is not there changes nothing, a color picked before it included
        PressWith("ChessSetColor", "black");
        PressWith("ChessSetMode", "computer");
        _manager->UpdateAll();
        ASSERT_EQ(asked.size(), 1u);
        EXPECT_EQ(Var("opponentMode"), "two") << "the pick was refused";
        EXPECT_FALSE(BlackIsAtTheBottom());
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f}));

        // The way on is the own engine; the board turns with Computer. A refused Stockfish pick afterwards leaves it turned
        PressWith("ChessSetEngine", "own");
        PressWith("ChessSetMode", "computer");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{660.0f, 80.0f}));
        PressWith("ChessSetEngine", "stockfish");
        _manager->UpdateAll();
        EXPECT_EQ(Var("opponentEngine"), "own") << "refused";
        EXPECT_TRUE(BlackIsAtTheBottom());
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{660.0f, 80.0f}));
    }

    TEST_F(ChessUiTest, ResetAndSetStateKeepTheTurnedBoardAndDealThePiecesToItsCells) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());

        Press("ChessReset");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom()) << "a reset keeps the opponent, so the orientation";
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{660.0f, 80.0f}));
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
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        EXPECT_EQ(Coordinates(true), "hgfedcba");
        EXPECT_EQ(Coordinates(false), "12345678");
    }

    TEST_F(ChessUiTest, TheCoordinateVariablesTurnBackWithTwoPeopleAndWithTheProgramAsBlack) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true)});
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
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true)});
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
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true)});
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
        const String problems = BoardLabelProblemsOnMatrix(canvas->GetLayout());
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
            const String problems = BoardLabelProblems(layout);
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
            EXPECT_FALSE(element->IsInteractive()) << spec.name.c_str();
            EXPECT_TRUE(IsLabel(*element)) << spec.name.c_str();
        }
        EXPECT_EQ(labels, 2 * kBoardEdgeCells);
        EXPECT_EQ(canvas->GetRoot().children.size(), 2 * kBoardEdgeCells) << "nothing else is in the canvas";
    }

    TEST_F(ChessUiTest, EveryCoordinateLabelIsInTheCornerOfItsEdgeCellOnTheShippedScene) {
        Draw();
        for (const BoardLabel& spec : ExpectedBoardLabels()) {
            SCOPED_TRACE(spec.name.c_str());
            const UIElement* found = CoordinateLabel(spec.name.c_str());
            ASSERT_NE(found, nullptr);
            const UIElement& label = *found;
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
        SKIP_WITHOUT_WORKER_THREADS();
        // The width the labels keep clear is the width of a piece that Contain fits into a cell: the real draw of a rook says so
        const auto whiteA1 = GiveAtlasCell("WhiteRook_a1");
        const auto whiteH1 = GiveAtlasCell("WhiteRook_h1");
        const auto blackA8 = GiveAtlasCell("BlackRook_a8");
        const auto blackH8 = GiveAtlasCell("BlackRook_h8");
        const ITexture* const rooks[] = {whiteA1.Get(), whiteH1.Get(), blackA8.Get(), blackH8.Get()};
        CountingFactory factory({MakeShared<GatedEngine>(true)});
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
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());

        for (const bool turned : {false, true}) {
            SCOPED_TRACE(turned ? "the program plays White: the board is turned" : "two people");
            ControlPlays(turned ? "white" : nullptr);
            _manager->UpdateAll();
            Draw();

            for (const BoardLabel& spec : ExpectedBoardLabels()) {
                SCOPED_TRACE(spec.name.c_str());
                const UIElement* found = CoordinateLabel(spec.name.c_str());
                ASSERT_NE(found, nullptr);
                const UIElement& label = *found;
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
                EXPECT_TRUE(SameColor(TextOf(label)->textColor, light ? kDarkSquareColor : kLightSquareColor)) << "dark brown on a light cell, cream on a dark one";

                // and the glyph that is drawn in the label's box has that color
                const Rect box = OutputRect(label.rect);
                size_t inBox = 0;
                for (const DrawCall& call : _renderer->calls) {
                    if (call.kind == DrawKind::Texture && LiesIn(call.rect, box, kGlyphTolerance)) {
                        ++inBox;
                        EXPECT_TRUE(SameColor(call.color, TextOf(label)->textColor));
                        EXPECT_TRUE(LiesIn(call.rect, cell, kGlyphTolerance)) << "inside the cell it labels";
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
        EXPECT_GT(layerOf("MenuPanel"), layerOf("Hud"));
        EXPECT_EQ(layerOf("MenuPanel"), layerOf("OpponentPanel"));
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
            const UIElement* label = CoordinateLabel(spec.name.c_str());
            ASSERT_NE(label, nullptr);
            const Rect& rect = label->rect;  // canvas units: the view units of a SceneView canvas
            EXPECT_EQ(canvas->GetLayout().HitTest(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f), nullptr) << spec.name.c_str() << " is not found by a hit test";
        }

        CountingFactory factory({MakeShared<InstantEngine>()});
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
            const UIElement* label = CoordinateLabel(press.label);
            ASSERT_NE(label, nullptr);
            const auto [x, y] = ScreenCenter(*label);
            EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true))) << "the canvas does not take the press";
            EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false)));
            ASSERT_TRUE(Model().HasSelection()) << "the board got it";
            EXPECT_EQ(Model().GetSelected(), Sq(press.square)) << "the square drawn in that cell";
        }
    }

    TEST_F(ChessUiTest, TheCoordinateLabelsTurnTheirTextAndKeepTheirPlaceOnTheScreen) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        const auto read = [&](bool files) {
            Draw();
            String text;
            for (int index = 0; index <= kLastBoardIndex; ++index) {
                const UIElement* label = CoordinateLabel((files ? "FileLabel{}"_format(index) : "RankLabel{}"_format(index)).c_str());
                if (label != nullptr) {
                    text += ShownText(*label);
                }
            }
            return text;
        };
        const auto rects = [&] {
            Vector<Rect> found;
            for (const BoardLabel& spec : ExpectedBoardLabels()) {
                const UIElement* label = CoordinateLabel(spec.name.c_str());
                found.push_back(label != nullptr ? label->rect : Rect{});
            }
            return found;
        };

        EXPECT_EQ(read(true), "abcdefgh") << "White at the bottom: a..h from the left";
        EXPECT_EQ(read(false), "87654321") << "8 at the top";
        const Vector<Rect> upright = rects();

        ControlPlays("white");
        _manager->UpdateAll();
        EXPECT_EQ(read(true), "hgfedcba") << "the board is turned by 180 degrees: h..a from the left";
        EXPECT_EQ(read(false), "12345678") << "1 at the top";
        const Vector<Rect> turned = rects();
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
        SKIP_WITHOUT_WORKER_THREADS();
        // The turn by 180 degrees keeps the color of every cell, which is why the colors of the labels are fixed in the XML
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        Draw();
        Vector<Color> before;
        for (const BoardLabel& spec : ExpectedBoardLabels()) {
            const UIElement* label = CoordinateLabel(spec.name.c_str());
            ASSERT_NE(label, nullptr);
            before.push_back(TextOf(*label)->textColor);
        }
        ControlPlays("white");
        _manager->UpdateAll();
        Draw();
        size_t index = 0;
        for (const BoardLabel& spec : ExpectedBoardLabels()) {
            const UIElement* label = CoordinateLabel(spec.name.c_str());
            ASSERT_NE(label, nullptr);
            EXPECT_TRUE(SameColor(TextOf(*label)->textColor, before[index++])) << spec.name.c_str();
        }
    }

    // ---- the board coordinates: broken copies, so that the checks can fail ---------------------------------------------------------------------

    TEST_F(ChessUiTest, ABrokenCopyOfTheCoordinateLabelsIsCaughtByTheChecks) {
        Draw();  // the variables the labels show
        const String shipped = ShippedElementsXml("BoardCoordinates");
        ASSERT_FALSE(shipped.empty());
        const auto problemsOf = [&](const String& xml) {
            CanvasCopy copy(xml, false);
            return BoardLabelProblemsOnMatrix(copy.layout, 100000);
        };

        EXPECT_EQ(problemsOf(shipped), "") << "the same checks pass on the shipped labels";

        String bigFont = shipped;
        Replace(bigFont, "fontSize=\"18\"", "fontSize=\"60\"");
        EXPECT_NE(problemsOf(bigFont).find("RankLabel0 cuts"), String::npos) << "a font of 60 does not fit the box";

        String narrow = shipped;
        Replace(narrow, "name=\"RankLabel0\" x=\"102\" y=\"82\" width=\"13\"", "name=\"RankLabel0\" x=\"102\" y=\"82\" width=\"3\"");
        EXPECT_NE(problemsOf(narrow).find("RankLabel0 cuts"), String::npos) << "a box of 3 units cuts the glyph";

        String wide = shipped;
        Replace(wide, "name=\"RankLabel0\" x=\"102\" y=\"82\" width=\"13\"", "name=\"RankLabel0\" x=\"102\" y=\"82\" width=\"30\"");
        EXPECT_NE(problemsOf(wide).find("RankLabel0 (102.0, 82.0, 30.0, 26.0) is not in the corner"), String::npos) << "a box that reaches the piece";

        String inside = shipped;
        Replace(inside, "name=\"RankLabel3\" x=\"102\"", "name=\"RankLabel3\" x=\"182\"");
        EXPECT_NE(problemsOf(inside).find("RankLabel3 (182.0"), String::npos) << "a label under a cell that is not at the edge";

        String rankAtTheBottom = shipped;
        Replace(rankAtTheBottom, "name=\"RankLabel0\" x=\"102\" y=\"82\"", "name=\"RankLabel0\" x=\"102\" y=\"132\"");
        EXPECT_NE(problemsOf(rankAtTheBottom).find("RankLabel0 (102.0, 132.0"), String::npos) << "a rank number in the bottom left of its cell is not in the corner";

        String fileAtTheTop = shipped;
        Replace(fileAtTheTop, "name=\"FileLabel7\" x=\"725\" y=\"692\"", "name=\"FileLabel7\" x=\"725\" y=\"642\"");
        EXPECT_NE(problemsOf(fileAtTheTop).find("FileLabel7 (725.0, 642.0"), String::npos) << "a file letter in the top right of its cell is not in the corner";

        String wrongColor = shipped;
        Replace(wrongColor, "textColor=\"181,136,99,255\"", "textColor=\"240,217,181,255\"");
        EXPECT_NE(problemsOf(wrongColor).find("RankLabel0 has not the color"), String::npos) << "cream on a light cell";

        String missing = shipped;
        Replace(missing, "name=\"FileLabel7\"", "name=\"FileLabelX\"");
        EXPECT_NE(problemsOf(missing).find("FileLabel7 is missing"), String::npos);
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
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true)});
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
            EXPECT_EQ((*board)[row].get_ref<const std::string&>(), expected[row]) << "row " << row << ": rank 8 first, white upper case";  //-NoStd: nlohmann string
        }
    }

    // ---- the Reset button -------------------------------------------------------------------------------------------------

    TEST_F(ChessUiTest, TheResetActionStartsANewGame) {
        ASSERT_TRUE(Play(Model(), "e2e4"));
        const uint64_t before = Model().GetVersion();

        InputEvents::ActionEvent::Emit(PoolString::Intern("ChessReset"));

        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
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
        EXPECT_NE(Model().GetPosition().ToFen(), String(Position::kStartFen)) << "a press alone does nothing";
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false)));

        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
    }

    TEST_F(ChessUiTest, TheBoardStillTakesItsClicksUnderTheCanvas) {
        Draw();
        const auto from = SquareCenter("e2");
        const auto to = SquareCenter("e4");

        // The canvas does not consume a click that lands on no control; UIInput then gives it to the game, so the board gets it (the model selects, then moves)
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(from.first, from.second, true)));
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(to.first, to.second, true)));

        EXPECT_EQ(Model().GetPosition().ToFen(), String("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"));
    }

    // ---- the opponent -----------------------------------------------------------------------------------------------------
    // The engines are the doubles of TestEngines.h, given to the controller of the running game by SetEngineFactory: no child process, and a Stockfish on the PATH of
    // this machine is never looked for. The scene is the shipped one, so a click is a click on the board and a frame is the update of every component

    TEST_F(ChessUiTest, WithNobodyAsTheOpponentTheGamePlaysAsTwoPeople) {
        CountingFactory factory({MakeShared<InstantEngine>()});
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
        const auto engine = MakeShared<InstantEngine>();
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

    TEST_F(ChessUiTest, WhileTheProgramThinksAClickOnTheBoardNeverMovesTheGameOrCancelsItsSearch) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeShared<GatedEngine>(true);
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

    // ---- premoves: the clicks of the person while the program thinks ----------------------------------------------------------------
    // The model's flow is ChessGameModelTest's and the controller's play is OpponentControllerTest's; here the clicks come through the scene

    TEST_F(ChessUiTest, WhileTheProgramThinksAClickOnAnOwnPieceSelectsItAndAClickOnItsTargetQueuesAPremove) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeShared<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; })) << "the program has the search and waits at its gate";
        const uint64_t version = Model().GetVersion();

        Click("d2");
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("d2"));
        EXPECT_EQ(Model().GetLegalMoves().size(), 4u) << "d3, d4 and the two diagonals, which a premove has with or without a piece there";
        Draw();
        EXPECT_EQ(RectCalls(kSelectedColor).size(), 1u) << "the selection is drawn as ever";
        EXPECT_EQ(RectCalls(kLegalMoveColor).size(), 4u);

        Click("d4");
        ASSERT_EQ(Model().GetPremoves().size(), 1u);
        EXPECT_EQ(Model().GetPremoves()[0], (Premove{Sq("d2"), Sq("d4")}));
        EXPECT_FALSE(Model().HasSelection());
        Draw();
        const auto marks = RectCalls(kPremoveColor);
        ASSERT_EQ(marks.size(), 2u);
        EXPECT_TRUE(SameRect(marks[0]->rect, SquareRect("d2")) || SameRect(marks[1]->rect, SquareRect("d2")));
        EXPECT_TRUE(SameRect(marks[0]->rect, SquareRect("d4")) || SameRect(marks[1]->rect, SquareRect("d4")));
        EXPECT_EQ(Model().GetVersion(), version) << "a premove is no change of the game, so the search is not stale";
        EXPECT_TRUE(Opponent().IsThinking());
        EXPECT_FALSE(engine->WasCancelled());
        EXPECT_EQ(Model().GetMoves().size(), 1u);

        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() >= 3; })) << "the answer and then the premove";
        EXPECT_EQ(Model().GetMoves()[2].ToUci(), "d2d4");
        EXPECT_TRUE(Model().GetPremoves().empty());
        Draw();
        EXPECT_TRUE(RectCalls(kPremoveColor).empty()) << "a played premove is not drawn any more";
    }

    TEST_F(ChessUiTest, ASecondPremoveMovesThePieceTheFirstOneMovedAndAnotherOwnPieceReselects) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeShared<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));

        Click("d2");
        Click("g1");
        EXPECT_EQ(Model().GetSelected(), Sq("g1")) << "another own piece takes the selection";
        Click("f3");
        ASSERT_EQ(Model().GetPremoves().size(), 1u);
        Click("f3");  // the knight is on f3 on the premove board: this click selects it
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("f3"));
        Click("g5");

        ASSERT_EQ(Model().GetPremoves().size(), 2u);
        EXPECT_EQ(Model().GetPremoves()[0], (Premove{Sq("g1"), Sq("f3")}));
        EXPECT_EQ(Model().GetPremoves()[1], (Premove{Sq("f3"), Sq("g5")}));
    }

    TEST_F(ChessUiTest, WhileTheProgramThinksAClickOnAnEmptyOrAnEnemySquareCancelsTheWholeQueue) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeShared<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        const uint64_t version = Model().GetVersion();

        ClickMove("d2", "d4");
        ClickMove("b1", "c3");
        ASSERT_EQ(Model().GetPremoves().size(), 2u);
        Click("e7");  // a piece of the program's
        EXPECT_TRUE(Model().GetPremoves().empty());
        EXPECT_FALSE(Model().GetPremoveColor().has_value());

        ClickMove("d2", "d4");
        ASSERT_EQ(Model().GetPremoves().size(), 1u);
        Click("a5");  // nothing stands there, and nothing is selected
        EXPECT_TRUE(Model().GetPremoves().empty());

        ClickMove("d2", "d4");
        Click("g1");
        Click("a5");  // a selection and a square that is none of its targets
        EXPECT_TRUE(Model().GetPremoves().empty());
        EXPECT_FALSE(Model().HasSelection());
        Draw();
        EXPECT_TRUE(RectCalls(kPremoveColor).empty());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_TRUE(Opponent().IsThinking());
        EXPECT_FALSE(engine->WasCancelled());
    }

    TEST_F(ChessUiTest, APremoveToThePromotionRankAsksNoPieceAndPlaysAsAQueen) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeShared<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom("4k3/1P6/8/8/8/8/8/4K3 b - - 0 1");  // Black, the program, is to move; White's pawn on b7 is one step from the last rank
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));

        ClickMove("b7", "b8");

        EXPECT_EQ(Model().GetPremoves().size(), 1u);
        EXPECT_FALSE(Model().HasPendingPromotion());
        EXPECT_FALSE(PromotionShown());
        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() >= 2; }));
        EXPECT_EQ(Model().GetPieceAt(Sq("b8")).type, PieceType::Queen);
        EXPECT_EQ(Model().GetPieceAt(Sq("b8")).color, PieceColor::White);
    }

    TEST_F(ChessUiTest, WithTwoPeopleAClickOnABlackPieceOnWhitesTurnIsNoPremove) {
        _manager->UpdateAll();

        Click("e7");
        Click("e5");

        EXPECT_FALSE(Model().HasSelection());
        EXPECT_TRUE(Model().GetPremoves().empty());
        EXPECT_FALSE(Model().GetPremoveColor().has_value());
        Click("e2");
        EXPECT_TRUE(Model().HasSelection()) << "an ordinary selection";
        EXPECT_TRUE(Model().GetPremoves().empty());
    }

    TEST_F(ChessUiTest, WithTheProgramAsWhiteAPremoveOfBlackIsClickedOnTheTurnedBoard) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeShared<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; })) << "the program opens and waits at its gate";
        ASSERT_EQ(Model().GetTurn(), PieceColor::White);

        Click("e7");
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("e7"));
        ClickCell(3, 4);  // the cell where e5 is drawn on this board
        ASSERT_EQ(Model().GetPremoves().size(), 1u);
        EXPECT_EQ(Model().GetPremoves()[0], (Premove{Sq("e7"), Sq("e5")}));
        EXPECT_EQ(Model().GetPremoveColor(), Optional<PieceColor>(PieceColor::Black));
        Draw();
        const auto marks = RectCalls(kPremoveColor);
        ASSERT_EQ(marks.size(), 2u);
        EXPECT_TRUE(SameRect(marks[0]->rect, CellRect(3, 6)) || SameRect(marks[1]->rect, CellRect(3, 6))) << "e7 is the second row from the bottom, the fifth column from the right";

        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() >= 2; }));
        EXPECT_EQ(Model().GetMoves()[1].ToUci(), "e7e5");
    }

    TEST_F(ChessUiTest, TheResetButtonDropsTheQueue) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeShared<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        ClickMove("d2", "d4");
        ASSERT_EQ(Model().GetPremoves().size(), 1u);

        Press("ChessReset");
        _manager->UpdateAll();

        EXPECT_TRUE(Model().GetPremoves().empty());
        EXPECT_TRUE(Model().GetMoves().empty());
        Draw();
        EXPECT_TRUE(RectCalls(kPremoveColor).empty());
    }

    TEST_F(ChessUiTest, TheProgramsMateEndsTheQueueAndAClickAfterItStartsANewGame) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto mate = MoveGenerator::ParseUci(PositionFromFen(kBlackMatesOnA1), "a8a1");
        ASSERT_TRUE(mate.has_value());
        const auto engine = MakeShared<GatedEngine>(true, *mate);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kBlackMatesOnA1);
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        ClickMove("g1", "h1");
        ASSERT_EQ(Model().GetPremoves().size(), 1u);

        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().IsGameOver(); }));

        EXPECT_EQ(Model().GetMoves().size(), 1u) << "the premove did not play after the mate";
        EXPECT_TRUE(Model().GetPremoves().empty());
        Click("e4");
        EXPECT_FALSE(Model().IsGameOver()) << "a click after the end starts the next game";
        EXPECT_TRUE(Model().GetPremoves().empty());
    }

    TEST_F(ChessUiTest, AfterTheProgramsMateAClickStartsANewGameAndTheProgramMovesAgain) {
        CountingFactory factory({MakeShared<MatingEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        Model().SetPosition(PositionFromFen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"));
        ASSERT_TRUE(Opponent().SetSide(PieceColor::White).has_value());

        ASSERT_TRUE(FramesUntil([&] { return Model().IsGameOver(); }));
        EXPECT_EQ(Model().GetStatus(), GameStatus::Checkmate);
        EXPECT_EQ(Model().GetWinner(), Optional<PieceColor>(PieceColor::White));
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves()[0].ToUci(), "a1a8") << "the program's move";
        EXPECT_FALSE(Opponent().GetError().has_value());
        Draw();
        EXPECT_EQ(ShownText(Label("Turn")), "White wins") << "the HUD says it, whoever played the mate";

        const uint64_t version = Model().GetVersion();
        Click("e4");  // the game is over, so this click is the "next game" one even though White is the program

        EXPECT_FALSE(Model().IsGameOver());
        EXPECT_GT(Model().GetVersion(), version);
        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
        EXPECT_FALSE(Model().HasSelection());
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 1; })) << "the new game opens with the move of the program, which plays White";
        EXPECT_EQ(Model().GetTurn(), PieceColor::Black);
        EXPECT_FALSE(Model().IsGameOver());
    }

    TEST_F(ChessUiTest, AResetWhileTheProgramThinksDropsItsAnswerAndANewSearchFollowsOnItsTurn) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeShared<GatedEngine>(false);  // ignores its token: its answer comes late and must still be thrown away
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
        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
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
        const auto engine = MakeShared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
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
        const auto engine = MakeShared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
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
        const auto engine = MakeShared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
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
        const auto engine = MakeShared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
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
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeShared<GatedEngine>(true);
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

    TEST_F(ChessUiTest, ShuttingTheGameDownWithASearchInFlightJoinsNothing) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeShared<GatedEngine>(false);  // never looks at its token: only the gate can end its search
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; }));
        ASSERT_TRUE(Opponent().IsThinking());

        GameManager::GetInstance().Shutdown();  // Stop() destroys the opponent, as Application::Cleanup() does before the task system goes down

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
        const auto engine = MakeShared<InstantEngine>();
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
        CountingFactory factory({MakeShared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        for (size_t index = 0; index < levels.size(); ++index) {
            const String id(levels[index].id.data(), levels[index].id.size());
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
        SKIP_WITHOUT_STOCKFISH_SEGMENT();
        CountingFactory factory({MakeShared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFromTheStockfishName();
        ASSERT_TRUE(OpenThePanel());
        EXPECT_EQ(Var("opponentEngine"), "stockfish");
        EXPECT_TRUE(PanelButton("EngineStockfish").selected) << "the default (where Stockfish is available)";
        EXPECT_FALSE(PanelButton("EngineOwn").selected);

        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        EXPECT_EQ(Var("opponentEngine"), "own");
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
        EXPECT_TRUE(PanelButton("EngineOwn").selected);
        EXPECT_FALSE(PanelButton("EngineStockfish").selected);
        ASSERT_TRUE(ClickPanel("EngineStockfish").Both());
        EXPECT_EQ(Var("opponentEngine"), "stockfish") << "and back";
        EXPECT_TRUE(PanelButton("EngineStockfish").selected);
        EXPECT_EQ(factory.Calls(), 0) << "with nobody as the opponent an engine is only a choice: none is asked for";

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Engines()[0], "stockfish");
        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        EXPECT_EQ(Var("opponentEngine"), "own");
        ASSERT_EQ(factory.Calls(), 2) << "the program plays: a new engine for the new choice";
        EXPECT_EQ(factory.Engines()[1], "own");
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black)) << "the side stays";
    }

    TEST_F(ChessUiTest, WhileTheProgramThinksTheTurnSaysSoAndTheAnswerBringsTheUsualWordsBack) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeShared<GatedEngine>(true);
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
        const auto engine = MakeShared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
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
        const auto engine = MakeShared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
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
        CountingFactory working({MakeShared<InstantEngine>()});
        Opponent().SetEngineFactory(working.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());

        const auto engineGroupWorks = [&](const char* state) {
            SCOPED_TRACE(state);
            Draw();
            EXPECT_FALSE(PanelControl<UISegmented>("EngineGroup").disabled);
            EXPECT_FALSE(PanelButton("EngineOwn").rect.IsEmpty());
            ASSERT_TRUE(ClickPanel("EngineOwn").Both());
            EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
            if constexpr (kStockfishAvailable) {
                ASSERT_TRUE(ClickPanel("EngineStockfish").Both());
                EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Stockfish);
            }
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
        CountingFactory factory({MakeShared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        Draw();
        ASSERT_EQ(ShownText(Label("Turn")), "White to move");

        const auto at = ScreenCenter(PanelElement("ModeComputer"));
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.first, at.second, true))) << "the segment takes the click";
        EXPECT_FALSE(Opponent().GetPlays().has_value()) << "a press alone does nothing";
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(at.first, at.second, false)));
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black));

        EXPECT_TRUE(ClickPanel("ColorBlack").Both());
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::White));

        EXPECT_TRUE(ClickPanel("ModeTwo").Both());
        EXPECT_FALSE(Opponent().GetPlays().has_value());
        EXPECT_FALSE(PanelButton("ModeComputer").rect.IsEmpty()) << "the way back to a pick is still there";
    }

    TEST_F(ChessUiTest, ClickingAnEngineSegmentOnTheScreenPicksTheEngineAndAPressAloneDoesNothing) {
        // Was ClickingTheEngineButtonOnTheScreenCyclesTheEngineAndAPressAloneDoesNothing
        SKIP_WITHOUT_STOCKFISH_SEGMENT();
        CountingFactory factory({MakeShared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFromTheStockfishName();
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
        EXPECT_TRUE(PanelButton("EngineOwn").selected) << "the selected segment follows";

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());  // the program plays Black
        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Engines()[0], "own");
        EXPECT_TRUE(ClickPanel("EngineStockfish").Both()) << "also while the program plays";
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Stockfish);
        EXPECT_EQ(Var("opponentEngine"), "stockfish");
        EXPECT_TRUE(PanelButton("EngineStockfish").selected);
        ASSERT_EQ(factory.Calls(), 2);
        EXPECT_EQ(factory.Engines()[1], "stockfish");
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black)) << "the side stays";
    }

    TEST_F(ChessUiTest, APressOnASegmentDoesNotChangeTheBoardSelection) {
        // Was APressOnTheEngineButtonDoesNotChangeTheSelection
        CountingFactory factory({MakeShared<InstantEngine>()});
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
        const Optional<size_t> easy = FindOpponentLevel("easy");
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
        CountingFactory factory({MakeShared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        ASSERT_TRUE(CloseThePanel());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));

        EXPECT_TRUE(ClickButton(kResetColor));

        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own) << "a new game keeps the engine";
        EXPECT_EQ(Var("opponentEngine"), "own");
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black));
    }

    TEST_F(ChessUiTest, TheResetButtonStillResetsWithTheMenuButtonAround) {
        // Reset keeps the opponent chosen in the panel; the Menu button beside it does not change that
        CountingFactory factory({MakeShared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        ASSERT_TRUE(CloseThePanel());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));

        EXPECT_TRUE(ClickButton(kResetColor));

        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black)) << "a new game keeps the opponent";
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
    }

    TEST_F(ChessUiTest, ThePressOfAHudButtonDoesNotReachTheBoardAndTheBoardStillGetsItsOwnClicks) {
        CountingFactory factory({MakeShared<InstantEngine>()});
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
        EXPECT_TRUE(MenuList()->_visible);
        EXPECT_TRUE(Model().HasSelection());
        EXPECT_TRUE(Model().GetSelected() == Sq("e2"));
    }

    // ---- the choice of the opponent panel: the actions with a value and the ids --------------------------------------------
    // The segments of the panel emit ChessSetMode / ChessSetColor / ChessSetEngine / ChessSetLevel with the id as `value`; the board answers through OpponentChoice and
    // publishes the ids. These tests press the actions the segments will emit (the panel itself is in the scene from WP 3) and read the variables

    TEST_F(ChessUiTest, TheIdsOfThePanelStartAsTwoWhiteTheDefaultEngineMediumAndNoError) {
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_EQ(Var("opponentColor"), "white");
        EXPECT_EQ(Var("opponentEngine"), kStockfishAvailable ? "stockfish" : "own");
        EXPECT_EQ(Var("opponentLevel"), "medium");
        EXPECT_EQ(Var("opponentError"), "");
    }

    TEST_F(ChessUiTest, EachActionOfThePanelPicksWhatTheControlApiWouldRead) {
        CountingFactory factory({MakeShared<InstantEngine>()});
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
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::White)) << "the person has Black";
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
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black));
        EXPECT_EQ(Var("opponentColor"), "white");

        PressWith("ChessSetMode", "two");
        EXPECT_FALSE(Opponent().GetPlays().has_value());
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_EQ(Var("opponentColor"), "white");
        EXPECT_EQ(factory.Calls(), 4) << "nobody plays: nothing is asked";
    }

    TEST_F(ChessUiTest, ComputerAndThenBlackMakesTheProgramOpenAsWhite) {
        CountingFactory factory({MakeShared<InstantEngine>()});
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
        CountingFactory factory({MakeShared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();

        PressWith("ChessSetColor", "black");
        PressWith("ChessSetMode", "computer");
        PressWith("ChessSetMode", "two");
        EXPECT_EQ(Var("opponentMode"), "two");
        EXPECT_EQ(Var("opponentColor"), "black") << "the program is gone and the person still has the color";
        PressWith("ChessSetMode", "computer");
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::White));
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
        CountingFactory factory({MakeShared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFromTheStockfishName();
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
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black));
        EXPECT_EQ(Var("opponentMode"), "computer");
        EXPECT_EQ(Var("opponentColor"), "white");
        EXPECT_EQ(Var("opponentEngine"), "stockfish");
        EXPECT_EQ(Var("opponentLevel"), "medium");
        EXPECT_EQ(Var("opponentError"), "");
    }

    TEST_F(ChessUiTest, WhenNoStockfishIsFoundTheEngineRowLeadsToTheOwnEngineAndComputerPlays) {
        // The user's path through the panel's actions: Stockfish is not installed, Computer says so and leaves two people, Own engine and Computer again let the program play.
        // The factory fails for Stockfish and nothing else, so the program cannot have fallen back by itself
        Vector<String> asked;
        Opponent().SetEngineFactory([&asked](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            const StringView id = OpponentEngineId(engine);
            asked.emplace_back(id.data(), id.size());
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
            }
            return MakeShared<InstantEngine>();
        });
        _manager->UpdateAll();
        StartFromTheStockfishName();

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
        const auto engine = MakeShared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));

        EXPECT_EQ(Var("opponentError"), OpponentText::Error(*Opponent().GetError()));
        EXPECT_EQ(Var("opponentMode"), "computer") << "the pick succeeded, only the first search failed";

        Press("ChessReset");
        _manager->UpdateAll();

        EXPECT_EQ(Var("opponentError"), "") << "the version moved: the error rule clears it";
    }

    TEST_F(ChessUiTest, TheIdsAgreeWithWhatTheControlApiReportsAfterASetState) {
        CountingFactory factory({MakeShared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        const auto agree = [&](const char* mode, const char* personColor, const char* plays) {
            const Json opponent = ControlOpponent();
            const auto reported = GetJsonString(opponent, "plays");
            if (plays == nullptr) {
                EXPECT_FALSE(reported.has_value());
            } else {
                ASSERT_TRUE(reported.has_value());
                EXPECT_EQ(String(reported->c_str(), reported->size()), plays);
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
        const auto place = ScreenCenter(PanelButton("LevelMedium"));
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

    TEST_F(ChessUiTest, TheMenuButtonOpensTheListAndTheListWalksToTheOpponentPanelAndClosesEverything) {
        ActionLog log;
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_FALSE(MenuList()->_visible);
        ASSERT_TRUE(ClickButton(kMenuColor));
        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessOpenMenu"));
        ASSERT_TRUE(MenuList()->_visible);
        EXPECT_FALSE(Panel()->_visible) << "the list shows no section";
        Draw();
        EXPECT_FALSE(RectCalls(kPlateColor).empty()) << "shown: the plate is drawn";

        // The Menu button is under the dim now: the list takes the press first, and nothing is emitted
        const size_t actions = log.Total();
        const auto button = ScreenCenter(*HudElement("Menu"));
        const Taken second = ClickAtPixel(button.first, button.second);
        EXPECT_TRUE(second.Both()) << "the modal canvas takes it";
        EXPECT_EQ(log.Total(), actions) << "and it is not the Menu button's";

        // The section replaces the list, Back brings the list back, Close hides both
        ASSERT_TRUE(ClickMenuList("MenuSectionOpponent").Both());
        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessOpenSection"));
        EXPECT_TRUE(Panel()->_visible);
        EXPECT_FALSE(MenuList()->_visible);
        ASSERT_TRUE(ClickPanel("OpponentBack").Both());
        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessOpenMenu"));
        EXPECT_TRUE(MenuList()->_visible);
        EXPECT_FALSE(Panel()->_visible);
        ASSERT_TRUE(ClickMenuList("MenuSectionOpponent").Both());
        ASSERT_TRUE(CloseThePanel());
        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessCloseMenu"));
        Draw();
        EXPECT_TRUE(RectCalls(kPlateColor).empty());

        // Closed: the board gets its clicks again and the button works; Close of the list hides it
        const auto e2 = SquareCenter("e2");
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(e2.first, e2.second, true)));
        EXPECT_TRUE(Model().HasSelection());
        ASSERT_TRUE(ClickButton(kMenuColor));
        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessOpenMenu"));
        EXPECT_TRUE(MenuList()->_visible);
        ASSERT_TRUE(ClickMenuList("MenuClose").Both());
        EXPECT_EQ(log.LastAction(), PoolString::Intern("ChessCloseMenu"));
        EXPECT_FALSE(MenuList()->_visible);
        EXPECT_FALSE(Panel()->_visible);
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
        const Pair<float, float> points[] = {ScreenCenter(PanelButton("EngineOwn")), ScreenCenter(PanelElement("Title")), SquareCenter("a1"), SquareCenter("h8"),
                                             ScreenCenter(*HudElement("Reset")),     ScreenCenter(*HudElement("Turn"))};
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
        CountingFactory factory({MakeShared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        ActionLog log;
        const Vector<PanelSegment> segments = ExpectedPanelSegments();
        // A build without Stockfish hides its segment: nine are on the screen to click
        const auto IsShown = [](const PanelSegment& segment) { return kStockfishAvailable || segment.name != "EngineStockfish"; };

        // Two people play first: only the mode and the engine groups take clicks. Computer is picked in the middle, which wakes the other two; then back to two
        const auto clickGroup = [&](const char* group) {
            for (const PanelSegment& segment : segments) {
                if (StringView(segment.group) != group || !IsShown(segment)) {
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
                    if (StringView(other.group) == group && IsShown(other)) {
                        EXPECT_EQ(PanelButton(other.name.c_str()).selected, other.name == segment.name) << other.name.c_str();
                    }
                }
                if (StringView(group) == "ModeGroup") {
                    EXPECT_EQ(Opponent().GetPlays().has_value(), segment.id == "computer");
                } else if (StringView(group) == "ColorGroup") {
                    EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(segment.id == "white" ? PieceColor::Black : PieceColor::White)) << "the program plays the other color";
                } else if (StringView(group) == "EngineGroup") {
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
        EXPECT_TRUE(PanelButton("ModeTwo").selected);
        EXPECT_TRUE(PanelButton("LevelMax").selected) << "the level that was picked stays shown while two people play";
        EXPECT_TRUE(PanelButton("ColorBlack").selected) << "and so does the person's color";
        EXPECT_EQ(log.Total(), kStockfishAvailable ? 11u : 10u) << "every segment shown (ten, nine without Stockfish), and ModeTwo again";
    }

    TEST_F(ChessUiTest, AClickWithoutAnAnswerLeavesTheGroupAlone) {
        // The group owns no choice: it only reports. A pick the controller refuses leaves the segment that was selected selected
        CountingFactory failing(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
        Opponent().SetEngineFactory(failing.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        Draw();
        ASSERT_TRUE(PanelButton("ModeTwo").selected);

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();

        EXPECT_TRUE(PanelButton("ModeTwo").selected);
        EXPECT_FALSE(PanelButton("ModeComputer").selected);
    }

    TEST_F(ChessUiTest, NoGroupOfThePanelIsGreyedAndALevelAndAColorPickedForTwoPeopleApplyAtComputer) {
        // Replaces TheColorAndLevelGroupsAreDisabledWhileTwoPeoplePlayAndTheEngineGroupNever (and Was TheLevelButtonIsShownOnlyWhileTheProgramPlays): a greyed group read as a dead
        // button, so every group takes clicks in every mode and the pick is remembered until Computer is picked
        CountingFactory working({MakeShared<InstantEngine>()});
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
                const UIElement& button = PanelButton(name);
                EXPECT_FALSE(button.groupDisabled) << name;
                EXPECT_EQ(button.GetStateLook().fill.a, 255) << name << " is drawn whole";
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
        EXPECT_TRUE(PanelButton("LevelMax").selected);
        EXPECT_FALSE(PanelButton("LevelMedium").selected);
        EXPECT_TRUE(PanelButton("ColorBlack").selected);
        EXPECT_FALSE(PanelButton("ColorWhite").selected);
        EXPECT_TRUE(PanelButton("ModeTwo").selected);
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
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::White));
        EXPECT_EQ(ControlOpponentString("plays"), "white");
        EXPECT_EQ(ControlOpponentString("level"), "max");
        EXPECT_TRUE(PanelButton("LevelMax").selected);
        EXPECT_TRUE(PanelButton("ColorBlack").selected);
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        expectNoGreying("the program plays");

        // Back to two people: nothing grey, the choice still shown, and the groups still take clicks
        ASSERT_TRUE(ClickPanel("ModeTwo").Both());
        Draw();
        EXPECT_TRUE(PanelButton("LevelMax").selected);
        EXPECT_TRUE(PanelButton("ColorBlack").selected);
        expectNoGreying("two people again");
        const size_t total = log.Total();
        ASSERT_TRUE(ClickPanel("LevelEasy").Both());
        EXPECT_EQ(log.Total(), total + 1) << "a click in Two players mode is a pick, not a dead button";
        EXPECT_EQ(Opponent().GetLevel(), *FindOpponentLevel("easy"));
    }

    TEST_F(ChessUiTest, NoGroupOfThePanelIsGreyedWhateverTheControlApiSets) {
        // Replaces TheDisabledFlagFollowsAnOpponentSetFromOutside: the control API picks the program's side without the panel and the groups stay whole while the choice follows it
        CountingFactory factory({MakeShared<InstantEngine>()});
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
        EXPECT_TRUE(PanelButton("ColorWhite").selected);

        ControlPlays("white");
        Draw();
        wholePanel();
        EXPECT_EQ(Var("opponentColor"), "black");
        EXPECT_TRUE(PanelButton("ColorBlack").selected);

        ControlPlays(nullptr);
        Draw();
        wholePanel();
        EXPECT_TRUE(PanelButton("ModeTwo").selected);
        EXPECT_TRUE(PanelButton("ColorBlack").selected) << "the person's color is kept when the program leaves";
    }

    TEST_F(ChessUiTest, ARefusedStockfishPickOnThePanelKeepsComputerTheOwnEngineAndTheLevelAndTheProgramStillAnswers) {
        // The owner's report: choosing Stockfish switched the mode to Two players. An engine that cannot be had cannot be saved: nothing changes and the error is said
        SKIP_WITHOUT_STOCKFISH_SEGMENT();  // the pick of the name is the action's in that build: WithoutStockfishThePickOfTheNameOnTheActionIsRefused...
        Vector<String> asked;
        Opponent().SetEngineFactory([&asked](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            const StringView id = OpponentEngineId(engine);
            asked.emplace_back(id.data(), id.size());
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
            }
            return MakeShared<InstantEngine>();
        });
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        ASSERT_TRUE(ClickPanel("LevelHard").Both());
        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        ASSERT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black));
        ASSERT_EQ(asked.size(), 1u);

        ASSERT_TRUE(ClickPanel("EngineStockfish").Both());
        Draw();

        ASSERT_EQ(asked.size(), 2u);
        EXPECT_EQ(asked[1], "stockfish");
        EXPECT_TRUE(PanelButton("ModeComputer").selected) << "the mode does not move";
        EXPECT_FALSE(PanelButton("ModeTwo").selected);
        EXPECT_TRUE(PanelButton("EngineOwn").selected) << "the engine that plays stays picked";
        EXPECT_FALSE(PanelButton("EngineStockfish").selected);
        EXPECT_TRUE(PanelButton("LevelHard").selected);
        EXPECT_TRUE(PanelButton("ColorWhite").selected);
        EXPECT_EQ(PanelText("OpponentError"), "Engine not found");
        EXPECT_EQ(ShownText(Label("Status")), "Engine not found");
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

    TEST_F(ChessUiTest, TheStockfishSegmentIsHiddenExactlyWhenTheBuildHasNoStockfishAndTheOwnSegmentFillsTheGroupThen) {
        // The scene file is the same in every build; ChessBoardComponent::HideUnavailableEngines hides the segment once, at the first update or draw
        ASSERT_TRUE(OpenThePanel());
        Draw();
        const UIElement& stockfish = PanelButton("EngineStockfish");
        const UIElement& own = PanelButton("EngineOwn");
        const UISegmented& group = PanelControl<UISegmented>("EngineGroup");
        EXPECT_EQ(stockfish.active.visible, kStockfishAvailable);
        EXPECT_TRUE(own.active.visible);
        ASSERT_FALSE(own.rect.IsEmpty());
        EXPECT_NEAR(own.rect.h, kSegmentHeight, kPixelTolerance);
        if constexpr (kStockfishAvailable) {
            ASSERT_FALSE(stockfish.rect.IsEmpty());
            EXPECT_NEAR(stockfish.rect.x, group.rect.x, kPixelTolerance) << "two segments share the group";
            EXPECT_NEAR(stockfish.rect.w, own.rect.w, kPixelTolerance);
            EXPECT_NEAR(own.rect.x + own.rect.w, group.rect.x + group.rect.w, kPixelTolerance);
        } else {
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
            EXPECT_EQ(log.LastValue(), PoolString::Intern("own"));
        }
    }

    TEST_F(ChessUiTest, ThePanelWithTheStockfishSegmentHiddenPassesTheMatrixAndOwnEngineIsATouchTargetInTheNarrowestWindow) {
        // The layout rules for the group of one segment, in every build: the segment is hidden by hand here, so the desktop build holds the web one's panel too
        auto panel = Panel();
        ASSERT_TRUE(panel);
        panel->UpdateLayout(1280.0f, 720.0f);
        ASSERT_TRUE(panel->SetElementVisible(PoolString::Intern("EngineStockfish"), false));
        UICanvasLayout& layout = panel->GetLayout();

        const auto failures = ValidateWithWords(layout);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        const String problems = PanelProblemsOnMatrix(layout);
        EXPECT_TRUE(problems.empty()) << problems.c_str();

        LayOutInTheNarrowestWindow(layout);
        const UIElement& own = PanelButton("EngineOwn");
        const UIElement& group = PanelControl<UISegmented>("EngineGroup");
        EXPECT_TRUE(PanelButton("EngineStockfish").rect.IsEmpty());
        EXPECT_NEAR(own.rect.x, group.rect.x, kPixelTolerance);
        EXPECT_NEAR(own.rect.w, group.rect.w, kPixelTolerance);
        EXPECT_GE(std::min(own.rect.w, own.rect.h) * layout.GetResolved().scale, CoreManager::GetUIEnvironment().GetSettings().minTouchSize - kPixelTolerance);
        const UIContentFit fit = own.CheckContent(own.rect.w - 2.0f * kButtonAir, own.rect.h, layout.GetResolved().scale);
        EXPECT_TRUE(fit.hasContent && fit.fits) << "'" << ShownText(own).c_str() << "' in " << own.rect.w << " units";
        layout.Invalidate();
    }

#if CHESS_NO_STOCKFISH
    TEST_F(ChessUiTest, WithoutStockfishThePickOfTheNameOnTheActionIsRefusedAndTheOwnEngineStaysTheWayOnIsComputer) {
        // The default factory (no SetEngineFactory): the panel has no segment for the name, but the action can still carry it (a hand-made press, the web build's scene editor)
        _manager->UpdateAll();
        EXPECT_EQ(Var("opponentEngine"), "own") << "a game starts with the own engine";

        PressWith(kSetEngineAction, "stockfish");

        EXPECT_EQ(Var("opponentEngine"), "own") << "refused as a whole, nothing falls back";
        EXPECT_EQ(Var("opponentError"), "Engine not supported here");
        EXPECT_EQ(Var("status"), "Engine not supported here");
        EXPECT_FALSE(Opponent().GetPlays().has_value());

        PressWith(kSetModeAction, "computer");

        EXPECT_EQ(Var("opponentMode"), "computer") << "the own engine plays";
        EXPECT_EQ(Var("opponentError"), "") << "a new pick: the person has seen the sentence";
        EXPECT_EQ(Opponent().GetEngine(), OpponentEngine::Own);
    }
#endif

    TEST_F(ChessUiTest, TheMenuButtonSaysMenuWhateverIsPickedAndThePanelNamesTheColors) {
        CountingFactory factory({MakeShared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(OpenThePanel());
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_EQ(Opponent().GetPlays(), std::nullopt);

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu") << "the button is not a state: the panel shows the pick";
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black)) << "the person has White, the program Black";
        EXPECT_EQ(Var("opponentColor"), "white");

        ASSERT_TRUE(ClickPanel("ColorBlack").Both());
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::White));
        EXPECT_EQ(Var("opponentColor"), "black");

        ASSERT_TRUE(ClickPanel("ColorWhite").Both());
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::Black));
        EXPECT_EQ(Var("opponentColor"), "white");

        ASSERT_TRUE(ClickPanel("ModeTwo").Both());
        Draw();
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        EXPECT_EQ(Opponent().GetPlays(), std::nullopt);
        EXPECT_EQ(Var("opponentMode"), "two");
    }

    TEST_F(ChessUiTest, ThePersonPlaysTheColorThePanelNamesAndTheProgramTheOther) {
        // The person plays the color the PANEL names (the Menu button names nothing)
        const auto engine = MakeShared<InstantEngine>();
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
        Vector<String> asked;
        Opponent().SetEngineFactory([&asked](OpponentEngine engine, const OpponentLevel&) -> MadeEngine {
            const StringView id = OpponentEngineId(engine);
            asked.emplace_back(id.data(), id.size());
            if (engine == OpponentEngine::Stockfish) {
                return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
            }
            return MakeShared<InstantEngine>();
        });
        _manager->UpdateAll();
        StartFromTheStockfishName();
        ASSERT_TRUE(OpenThePanel());
        EXPECT_EQ(PanelText("OpponentError"), "") << "no error line to start with";

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        EXPECT_EQ(PanelText("OpponentError"), "Engine not found") << "the panel says it where the person looks";
        EXPECT_EQ(ShownText(Label("Status")), "Engine not found");
        EXPECT_TRUE(PanelButton("ModeTwo").selected) << "the screen shows the truth: nobody plays";
        EXPECT_FALSE(PanelButton("ModeComputer").selected);
        if constexpr (kStockfishAvailable) {
            EXPECT_TRUE(PanelButton("EngineStockfish").selected) << "the pick failed and the choice is still Stockfish: no silent switch";
        }
        EXPECT_EQ(Var("opponentEngine"), "stockfish") << "no silent switch (the segment is hidden without Stockfish)";
        EXPECT_EQ(ShownText(*HudElement("Menu")), "Menu");
        ASSERT_EQ(asked.size(), 1u);
        EXPECT_EQ(asked[0], "stockfish");

        ASSERT_TRUE(ClickPanel("EngineOwn").Both());
        EXPECT_EQ(PanelText("OpponentError"), "") << "a new pick: the person has seen the sentence";
        EXPECT_TRUE(PanelButton("EngineOwn").selected);
        EXPECT_EQ(asked.size(), 1u) << "with nobody playing the choice only changes";

        ASSERT_TRUE(ClickPanel("ModeComputer").Both());
        Draw();
        EXPECT_TRUE(PanelButton("ModeComputer").selected);
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
        const auto engine = MakeShared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
        CountingFactory factory({engine});
        ASSERT_NO_FATAL_FAILURE(ProvokeAnEngineError(factory));
        ASSERT_TRUE(OpenThePanel());
        EXPECT_EQ(PanelText("OpponentError"), "Engine not found") << "the first search failed";
        EXPECT_TRUE(PanelButton("ModeComputer").selected) << "the pick succeeded, only the search failed";

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
        const auto engine = MakeShared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "the program cannot be started"});
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
        constexpr StringView kRemovedActions = "ChessCy"
                                               "cle";  // split, so that a search of the sources for the removed actions finds no leftover
        for (const pugi::xpath_node found : document.select_nodes("//element[@action]")) {
            const StringView action = found.node().attribute("action").value();
            EXPECT_EQ(action.find(kRemovedActions), StringView::npos) << found.node().attribute("name").value() << " emits " << ToStd(action);
            const bool known = action == "ChessReset" || action == "ChessOpenMenu" || action == "ChessOpenSection" || action == "ChessCloseMenu" || action == kSetModeAction ||
                               action == kSetColorAction || action == kSetEngineAction || action == kSetLevelAction || action == kPromoteAction || action == kCancelPromotionAction;
            EXPECT_TRUE(known) << found.node().attribute("name").value() << " emits " << ToStd(action) << ", which the board does not answer";
        }
        for (const pugi::xpath_node found : document.select_nodes("//element[@text]")) {
            const StringView text = found.node().attribute("text").value();
            EXPECT_TRUE(text.find("{level}") == StringView::npos && text.find("{engine}") == StringView::npos) << found.node().attribute("name").value() << ": " << ToStd(text);
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
        EXPECT_GT(layerOf("MenuPanel"), layerOf("Hud"));
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
        String PromoteButton(PieceType piece) {
            return Format("Promote{}", PromotionLabel(piece));
        }

        constexpr const char* kPromotionPlate = "PromotionPlate";
        constexpr float kPromotionPlateHeight = 224.0f;  // 16 + 44 + 56 + 56 + 3 x 12 + 16, the comment of the scene

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
        const String fen = Model().GetPosition().ToFen();

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
            SCOPED_TRACE(String(PromotionLabel(piece)).c_str());
            StartFrom(kPromotionFen);
            const uint64_t version = Model().GetVersion();
            const ChessGameModel::PieceHandle pawn = PieceNodeAt("a7");
            ASSERT_NE(pawn, nullptr);
            ClickMove("a7", "a8");
            ASSERT_TRUE(PromotionShown());

            const Taken taken = ClickPromotion(PromoteButton(piece).c_str());
            EXPECT_TRUE(taken.Both());

            ASSERT_EQ(Model().GetMoves().size(), 1u);
            EXPECT_EQ(Model().GetMoves().back().ToUci().substr(0, 4), String("a7a8"));
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
        const Pair<PieceType, const char*> letters[] = {{PieceType::Queen, "a7a8q"}, {PieceType::Rook, "a7a8r"}, {PieceType::Bishop, "a7a8b"}, {PieceType::Knight, "a7a8n"}};
        for (const auto& [piece, uci] : letters) {
            SCOPED_TRACE(uci);
            StartFrom(kPromotionFen);
            ClickMove("a7", "a8");
            ASSERT_TRUE(ClickPromotion(PromoteButton(piece).c_str()).Both());
            ASSERT_EQ(Model().GetMoves().size(), 1u);
            EXPECT_EQ(Model().GetMoves().back().ToUci(), String(uci));
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
        const Pair<float, float> points[] = {
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
        EXPECT_EQ(Model().GetWinner(), Optional<PieceColor>(PieceColor::White));
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
        CountingFactory factory({MakeShared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());

        ASSERT_TRUE(ClickPromotion("PromoteRook").Both());

        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().front().ToUci(), String("a7a8r"));
        EXPECT_FALSE(PromotionShown());
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; })) << "the program answers the promotion";
        EXPECT_EQ(Model().GetTurn(), PieceColor::White);
        EXPECT_FALSE(PromotionShown());
    }

    TEST_F(ChessUiTest, TheProgramTakingTheTurnClosesThePanel) {
        // The person selected the pawn and asked for the promotion; the program (switched on in the meantime) takes the turn and drops the half-made move
        CountingFactory factory({MakeShared<InstantEngine>()});
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
        EXPECT_EQ(Model().GetMoves().back().ToUci(), String("a2a1n"));
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
    // inside the view, the words and ids held to the table of PromotionChoices, the names of the five canvases apart and broken copies. The panel has no HTML + CSS twin in
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
        const String problems = PanelProblemsOnMatrix(panel->GetLayout(), kReportedCases, kPromotionPlate);
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
        const String problems = PanelProblems(layout, kPromotionPlate);
        EXPECT_TRUE(problems.empty()) << problems.c_str();

        // 94% of the safe area is more than maxWidth, so the plate is 640 here and in every other window: 608 inside the padding, 146 a piece button
        EXPECT_NEAR(PromotionElement(kPromotionPlate).rect.w, 640.0f, kPixelTolerance);

        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        Vector<String> buttons{"PromotionCancel"};
        for (const PieceType piece : PromotionPieces()) {
            buttons.push_back(PromoteButton(piece));
        }
        ASSERT_EQ(buttons.size(), 5u) << "four pieces and Cancel";
        for (const String& name : buttons) {
            const UIElement& button = PromotionButton(name.c_str());
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

        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}, Pair<float, float>{1024.0f, 768.0f}, Pair<float, float>{800.0f, 800.0f},
                                 Pair<float, float>{2560.0f, 1080.0f}}) {
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
            const UIElement& button = *row.children[i];
            const UIInputClicker* clicker = ClickerOf(button);
            ASSERT_NE(clicker, nullptr);
            EXPECT_EQ(button.name, PoolString::Intern(PromoteButton(piece).c_str())) << "the buttons are in the order of the table";
            const StringView label = PromotionLabel(piece);
            const StringView id = PromotionId(piece);
            EXPECT_EQ(ShownText(button), String(label.data(), label.size()));
            EXPECT_EQ(String(clicker->value.ToStringView().data(), clicker->value.ToStringView().size()), String(id.data(), id.size()));
            EXPECT_EQ(clicker->action, PoolString::Intern(kPromoteAction));
            EXPECT_EQ(FindPromotion(clicker->value.ToStringView()), Optional<PieceType>(piece)) << "the value is an id the board accepts";
        }

        const UIElement& cancel = PromotionButton("PromotionCancel");
        EXPECT_EQ(ShownText(cancel), "Cancel");
        EXPECT_EQ(ClickerOf(cancel)->action, PoolString::Intern(kCancelPromotionAction));
    }

    TEST_F(ChessUiTest, EveryActionOfThePromotionPanelIsOneTheBoardAnswers) {
        // Every button of the canvas emits ChessPromote or ChessCancelPromotion, and the board answers both (pressed here as the buttons would)
        auto panel = Promotion();
        ASSERT_TRUE(panel);
        Vector<const UIElement*> controls;
        CollectControls(panel->GetRoot(), controls);
        size_t buttons = 0;
        for (const UIElement* control : controls) {
            if (const UIInputClicker* button = ClickerOf(*control)) {
                ++buttons;
                EXPECT_TRUE(button->action == PoolString::Intern(kPromoteAction) || button->action == PoolString::Intern(kCancelPromotionAction))
                    << control->name.CStr() << " emits " << button->action.CStr() << ", which the board does not answer";
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
        // Elements are found by name in every canvas of the scene (SetVisibleByName and the lookups of the tests): the HUD, the board coordinates, the captured material, the menu list, the opponent
        // panel and the promotion panel keep their own names
        const struct {
            const char* node;
            IntrusivePtr<UICanvasComponent> canvas;
        } canvases[] = {{"Hud", Hud()}, {"BoardCoordinates", Coordinate()}, {"Material", Material()}, {"MenuPanel", MenuList()}, {"OpponentPanel", Panel()}, {"PromotionPanel", Promotion()}};
        constexpr size_t kCanvasCount = std::size(canvases);
        Vector<PoolString> names[kCanvasCount];
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
        String cancel = ShippedElementsXml("PromotionPanel");
        ASSERT_FALSE(cancel.empty());
        ASSERT_NO_FATAL_FAILURE(Replace(cancel, R"(name="PromotionCancel" height="56")", R"(name="PromotionCancel" height="40")"));
        CanvasCopy lowCancel(cancel);
        EXPECT_TRUE(HasIssue(ValidateWithWords(lowCancel.layout), UILayoutIssueKind::TouchTargetTooSmall));
        EXPECT_NE(PanelProblemsOnMatrix(lowCancel.layout, kReportedCases, kPromotionPlate).find("PromotionCancel is 40.0 units high"), String::npos);

        String row = ShippedElementsXml("PromotionPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(row, R"(name="PromotionPieces" height="56")", R"(name="PromotionPieces" height="40")"));
        CanvasCopy lowRow(row);
        EXPECT_TRUE(HasIssue(ValidateWithWords(lowRow.layout), UILayoutIssueKind::TouchTargetTooSmall));
        EXPECT_NE(PanelProblemsOnMatrix(lowRow.layout, kReportedCases, kPromotionPlate).find("PromoteBishop is 40.0 units high"), String::npos);

        CanvasCopy shipped(ShippedElementsXml("PromotionPanel"));
        EXPECT_TRUE(PanelProblemsOnMatrix(shipped.layout, kReportedCases, kPromotionPlate).empty()) << "the same checks pass on the shipped panel";
        EXPECT_FALSE(HasIssue(ValidateWithWords(shipped.layout), UILayoutIssueKind::TouchTargetTooSmall));
    }

    TEST_F(ChessUiTest, APromotionWordCutByAFontThatIsTooBigIsCaught) {
        String xml = ShippedElementsXml("PromotionPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(name="PromoteKnight" flex="1" height="56" fontSize="26")", R"(name="PromoteKnight" flex="1" height="56" fontSize="60")"));
        CanvasCopy copy(xml);
        const String problems = PanelProblemsOnMatrix(copy.layout, kReportedCases, kPromotionPlate);
        EXPECT_NE(problems.find("PromoteKnight cuts 'Knight'"), String::npos) << problems.c_str();
    }

    TEST_F(ChessUiTest, APromotionPlateTooNarrowForTheFourButtonsIsCaught) {
        // A plate of 300 units leaves about 61 for a button, which cannot hold "Knight" with its air; the shipped 640 can
        String xml = ShippedElementsXml("PromotionPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(maxWidth="640")", R"(maxWidth="300")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        const String problems = PanelProblems(copy.layout, kPromotionPlate);
        EXPECT_NE(problems.find("cuts '"), String::npos) << problems.c_str();
    }

    TEST_F(ChessUiTest, APromotionPlateTallerThanTheSafeAreaIsCaught) {
        String xml = ShippedElementsXml("PromotionPanel");
        ASSERT_NO_FATAL_FAILURE(Replace(xml, R"(height="224")", R"(height="1200")"));
        CanvasCopy copy(xml);
        LayOutInTheNarrowestWindow(copy.layout);
        EXPECT_NE(PanelProblems(copy.layout, kPromotionPlate).find("leaves the safe area"), String::npos);
    }

    // ---- the captured material: the node Material ---------------------------------------------------------------------------------------
    // A canvas in the left margin (SceneConfig.xml has the reasons for its numbers) that ChessMaterialComponent gives its words and its sprites. Two groups: the lower one shows what the color at
    // the bottom of the board has taken, the upper one what the other color has taken. The rows are shown while the lists lie inside the safe area (Game/MaterialLayout.h). Here: the behavior on
    // the shipped scene; the library of textures is empty in the test, so an icon has its sprite name and draws nothing

    namespace {

        constexpr const char* kWhiteTookAPawn = "rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3";       // White has taken the d pawn of Black: +1 for White
        constexpr const char* kBlackTookTheQueen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNB1KBNR w KQkq - 0 1";      // the queen of White is gone: +9 for Black
        constexpr const char* kBlackCanTakeAPawn = "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2";  // Black to move: d5e4 takes a pawn
        constexpr const char* kPawnAboutToPromote = "8/P6k/8/8/8/8/8/K7 w - - 0 1";                                 // White: a pawn; Black: a king. a7a8q makes a queen
        constexpr const char* kEightPawnsAgainstNothing = "4k3/8/8/8/8/8/PPPPPPPP/4K3 w - - 0 1";                   // White: king and eight pawns, Black: a bare king
        constexpr const char* kEmptyMaterialWords = "||||||||||||";                                                 // twelve variables, each followed by "|"
        const Color kMaterialChipColor{232, 210, 170, 255};                                                         // the fill of a row (SceneConfig.xml)

        bool FenSetsTheState(const char* fen) {
            Json request = Json::object();
            request["fen"] = fen;
            return ChessGame::GetActive()->AsControllable()->SetState(request).has_value();
        }

    }  // namespace

    TEST_F(ChessUiTest, TheMaterialCanvasIsOnItsOwnLayerOverThePiecesAndUnderTheHud) {
        const auto& nodes = GetScene().GetRootNode().GetChildren();
        const auto indexOf = [&](const char* name) -> int {
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (nodes[i]->GetName() == PoolString::Intern(name)) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        };
        for (const char* name : {"Board", "BoardCoordinates", "Material", "Result", "Hud", "MenuPanel", "OpponentPanel", "PromotionPanel"}) {
            ASSERT_GE(indexOf(name), 0) << "the scene has the node " << name;
        }
        const auto layerOf = [&](const char* name) { return nodes[static_cast<size_t>(indexOf(name))]->GetLayer(); };
        EXPECT_EQ(layerOf("Material"), 5);
        EXPECT_GT(indexOf("Material"), indexOf("BoardCoordinates")) << "written after the coordinates";
        EXPECT_GT(layerOf("Material"), 0) << "over the pieces (layer 0)";
        EXPECT_GT(layerOf("Material"), layerOf("Result")) << "and over the result bar";
        EXPECT_LT(layerOf("Material"), layerOf("Hud"));
        EXPECT_LT(layerOf("Material"), layerOf("MenuPanel"));
        EXPECT_LT(layerOf("Material"), layerOf("OpponentPanel"));
        EXPECT_LT(layerOf("Material"), layerOf("PromotionPanel"));

        // The component comes before the canvas on the node: it gives the variables their words before the canvas draws in the same frame
        const auto& components = nodes[static_cast<size_t>(indexOf("Material"))]->GetComponents();
        const auto words = nodes[static_cast<size_t>(indexOf("Material"))]->GetComponent<ChessMaterialComponent>();
        const auto canvas = Material();
        ASSERT_TRUE(words);
        ASSERT_TRUE(canvas);
        ASSERT_EQ(components.size(), 2u);
        EXPECT_EQ(components[0].Get(), static_cast<IComponent*>(words.Get())) << "the component is listed first";
        EXPECT_EQ(components[1].Get(), static_cast<IComponent*>(canvas.Get()));

        EXPECT_EQ(canvas->_space, CanvasSpace::SceneView);
        EXPECT_FALSE(canvas->_useSafeArea) << "fixed scene units like the board; the rule of the lists reads the safe area itself";
        EXPECT_FALSE(canvas->_modal);
        EXPECT_TRUE(canvas->_visible);
    }

    TEST_F(ChessUiTest, TheMaterialCanvasHasTwoGroupsOfFiveRowsAndNothingInItTakesAClick) {
        const auto canvas = Material();
        ASSERT_TRUE(canvas);
        const UIElement& root = canvas->GetRoot();
        ASSERT_EQ(root.children.size(), 4u) << "two lists and two balances";
        for (const UIElement* element : AllElements(root)) {
            EXPECT_FALSE(element->blocksInput) << element->name.CStr();
            EXPECT_FALSE(element->IsInteractive()) << element->name.CStr();
            if (element != &root) {
                EXPECT_EQ(String(element->name.CStr()).rfind("Material", 0), 0u) << element->name.CStr() << " starts with Material: elements are found by name in every canvas";
            }
        }

        for (const char* group : {"Top", "Bottom"}) {
            SCOPED_TRACE(group);
            const UIElement& list = MaterialElement(Format("Material{}List", group).c_str());
            EXPECT_NE(list.Cast<UIStack>(), nullptr);
            ASSERT_EQ(list.children.size(), 5u);
            for (size_t index = 0; index < 5; ++index) {
                const UIElement& row = *list.children[index];
                EXPECT_EQ(row.name, PoolString::Intern(Format("Material{}{}Row", group, kMaterialPieces[index]).c_str())) << "the order of the rows: queen, rook, bishop, knight, pawn";
                EXPECT_FALSE(row.style.visible) << row.name.CStr() << " starts hidden";
                ASSERT_EQ(row.children.size(), 2u);
                EXPECT_NE(row.children[0]->GetComponent<UIImageComponent>(), nullptr) << "the icon";
                const UITextComponent* count = IsLabel(*row.children[1]) ? TextOf(*row.children[1]) : nullptr;
                ASSERT_NE(count, nullptr) << "the count";
                EXPECT_EQ(count->text, Format("{{material{}{}}}", group, kMaterialPieces[index]));
            }
            const UIElement& balanceElement = MaterialElement(Format("Material{}Balance", group).c_str());
            const UITextComponent* balance = IsLabel(balanceElement) ? TextOf(balanceElement) : nullptr;
            ASSERT_NE(balance, nullptr);
            EXPECT_EQ(balance->text, Format("{{material{}Balance}}", group));
            EXPECT_EQ(balance->overflow, TextOverflow::AutoFit) << "the sign is shrunk into its box and never cut";
        }
    }

    TEST_F(ChessUiTest, TheListsAndTheBalancesLieInTheMarginOfTheBoardWhereTheLayoutRuleSaysSo) {
        Settle();
        const Rect top = MaterialElement("MaterialTopList").rect;
        const Rect bottom = MaterialElement("MaterialBottomList").rect;
        EXPECT_TRUE(SameRect(top, Rect{8.0f, 84.0f, 64.0f, 216.0f}));
        EXPECT_TRUE(SameRect(bottom, Rect{8.0f, 500.0f, 64.0f, 216.0f}));
        // the box of Game/MaterialLayout.h is the one of the two lists
        EXPECT_FLOAT_EQ(kMaterialListLeft, std::min(top.x, bottom.x));
        EXPECT_FLOAT_EQ(kMaterialListTop, top.y);
        EXPECT_FLOAT_EQ(kMaterialListRight, std::max(top.x + top.w, bottom.x + bottom.w));
        EXPECT_FLOAT_EQ(kMaterialListBottom, bottom.y + bottom.h);
        EXPECT_LE(top.y + top.h, bottom.y) << "the lists do not meet";
        const Rect topBalance = MaterialElement("MaterialTopBalance").rect;
        const Rect bottomBalance = MaterialElement("MaterialBottomBalance").rect;
        EXPECT_TRUE(SameRect(topBalance, Rect{77.0f, 84.0f, 23.0f, 28.0f}));
        EXPECT_TRUE(SameRect(bottomBalance, Rect{77.0f, 500.0f, 23.0f, 28.0f}));
        EXPECT_LE(topBalance.x + topBalance.w, kMaterialMarginRight) << "the balance box ends where the board begins";
        EXPECT_GE(topBalance.x, top.x + top.w) << "and does not touch the list";
    }

    TEST_F(ChessUiTest, TheMaterialStartsWithNoRowShownAndEmptyWords) {
        EXPECT_EQ(MaterialWords(), kEmptyMaterialWords) << "after the first frame";
        Settle();
        EXPECT_EQ(ShownMaterialRows(), "");
        EXPECT_EQ(MaterialWords(), kEmptyMaterialWords);
    }

    TEST_F(ChessUiTest, NoRowIsShownInTheFirstFrameWhateverTheGameSays) {
        // The canvas has not been laid out when the component runs in the first frame, so the safe area is not known and the rows wait: nothing flashes for a frame
        StartFrom(kWhiteTookAPawn);
        Draw();
        EXPECT_EQ(CoreManager::GetUIVars().Get(PoolString::Intern("materialBottomPawn")), "x1") << "the words are there at once";
        EXPECT_EQ(ShownMaterialRows(), "") << "the rows are not";
        Draw();
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn") << "the next frame has the safe area";
    }

    TEST_F(ChessUiTest, ACaptureByClicksShowsThePieceInTheGroupOfTheColorThatTookItAndTheLeadOfThePoints) {
        Settle();
        ClickMove("e2", "e4");
        ClickMove("d7", "d5");
        ASSERT_EQ(ShownMaterialRows(), "") << "nothing is gone yet";
        ClickMove("e4", "d5");
        ASSERT_EQ(Model().GetMoves().size(), 3u);

        EXPECT_EQ(MaterialCount("Bottom", "Pawn"), "x1") << "White is at the bottom, and White has taken a pawn";
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn");
        EXPECT_EQ(MaterialSprite("Bottom", "Pawn"), "BlackPawn") << "the piece that was taken is Black's";
        EXPECT_EQ(MaterialWords(), "||||||||||x1|+1|") << "only the pawn count and the lead of White: the other ten words are empty";
        EXPECT_EQ(Var("materialTopBalance"), "");
        EXPECT_EQ(Var("materialBottomBalance"), "+1");

        // Black takes back: both groups have a pawn and the lead is gone
        ClickMove("d8", "d5");
        ASSERT_EQ(Model().GetMoves().size(), 4u);
        EXPECT_EQ(MaterialCount("Top", "Pawn"), "x1");
        EXPECT_EQ(MaterialCount("Bottom", "Pawn"), "x1");
        EXPECT_EQ(ShownMaterialRows(), "TopPawn BottomPawn");
        EXPECT_EQ(MaterialSprite("Top", "Pawn"), "WhitePawn");
        EXPECT_EQ(Var("materialTopBalance"), "");
        EXPECT_EQ(Var("materialBottomBalance"), "") << "an equal material leads for nobody";
    }

    TEST_F(ChessUiTest, TheRowsOfAGroupFollowTheOrderOfTheTableAndTheBalanceIsInTheGroupThatIsAhead) {
        StartFrom(kBlackTookTheQueen);
        Settle();
        EXPECT_EQ(MaterialCount("Top", "Queen"), "x1") << "Black has taken the queen: the upper group, because White is at the bottom";
        EXPECT_EQ(MaterialSprite("Top", "Queen"), "WhiteQueen");
        EXPECT_EQ(ShownMaterialRows(), "TopQueen");
        EXPECT_EQ(Var("materialTopBalance"), "+9");
        EXPECT_EQ(Var("materialBottomBalance"), "");

        StartFrom(kEightPawnsAgainstNothing);
        Settle();
        EXPECT_EQ(MaterialCount("Bottom", "Queen"), "x1") << "White has taken everything of Black's but the king";
        EXPECT_EQ(MaterialCount("Bottom", "Rook"), "x2");
        EXPECT_EQ(MaterialCount("Bottom", "Bishop"), "x2");
        EXPECT_EQ(MaterialCount("Bottom", "Knight"), "x2");
        EXPECT_EQ(MaterialCount("Bottom", "Pawn"), "x8");
        EXPECT_EQ(MaterialCount("Top", "Queen"), "x1") << "and Black everything of White's but the king and the pawns";
        EXPECT_EQ(MaterialCount("Top", "Pawn"), "");
        EXPECT_EQ(ShownMaterialRows(), "TopQueen TopRook TopBishop TopKnight BottomQueen BottomRook BottomBishop BottomKnight BottomPawn");
        EXPECT_EQ(Var("materialBottomBalance"), "+8");
        EXPECT_EQ(Var("materialTopBalance"), "");
    }

    TEST_F(ChessUiTest, TheShownRowsPackAtTheTopOfTheirListAndAHiddenRowTakesNoRoom) {
        StartFrom(kEightPawnsAgainstNothing);
        Settle();
        const float step = MaterialRow("Bottom", "Rook").rect.y - MaterialRow("Bottom", "Queen").rect.y;
        EXPECT_NEAR(step, 44.0f, kPixelTolerance) << "a row is 40 units high and 4 apart";
        EXPECT_NEAR(MaterialRow("Bottom", "Queen").rect.y, 500.0f, kPixelTolerance);

        StartFrom(kWhiteTookAPawn);  // only the pawn of Black is gone
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "BottomPawn");
        EXPECT_NEAR(MaterialRow("Bottom", "Pawn").rect.y, 500.0f, kPixelTolerance) << "the first row of the list: the four above it take no room";
        EXPECT_TRUE(MaterialRow("Bottom", "Queen").rect.IsEmpty());
    }

    TEST_F(ChessUiTest, WithTheProgramAsWhiteTheGroupsSwapWithTheBoard) {
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kWhiteTookAPawn);
        Settle();
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn") << "two people: White at the bottom, and White has taken the pawn";

        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        Settle();
        EXPECT_EQ(MaterialCount("Top", "Pawn"), "x1") << "Black is at the bottom now: White's captures are in the upper group";
        EXPECT_EQ(MaterialCount("Bottom", "Pawn"), "");
        EXPECT_EQ(ShownMaterialRows(), "TopPawn");
        EXPECT_EQ(MaterialSprite("Top", "Pawn"), "BlackPawn") << "the sprite is the piece that was taken: the upper group is White's, so the pieces are Black's";
        EXPECT_EQ(MaterialSprite("Bottom", "Pawn"), "WhitePawn") << "the icons of the lower group were given the other color too";
        EXPECT_EQ(Var("materialTopBalance"), "+1") << "the lead goes with the group";
        EXPECT_EQ(Var("materialBottomBalance"), "");

        ControlPlays(nullptr);
        _manager->UpdateAll();
        Settle();
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn") << "nobody plays: White is at the bottom again";
        EXPECT_EQ(MaterialSprite("Bottom", "Pawn"), "BlackPawn");
        EXPECT_EQ(Var("materialBottomBalance"), "+1");
    }

    TEST_F(ChessUiTest, WithTheProgramAsBlackAndWithTwoPeopleWhiteStaysAtTheBottomOfTheDisplayToo) {
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kBlackTookTheQueen);
        ControlPlays("black");
        _manager->UpdateAll();
        ASSERT_FALSE(BlackIsAtTheBottom());
        Settle();
        EXPECT_EQ(ShownMaterialRows(), "TopQueen") << "the program plays Black: White stays at the bottom, and Black's captures are in the upper group";
        EXPECT_EQ(Var("materialTopBalance"), "+9");
    }

    TEST_F(ChessUiTest, ThePanelTurnsTheDisplayWithTheBoard) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kBlackTookTheQueen);
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "TopQueen");

        PressWith(kSetColorAction, "black");  // the person plays Black, but two people are playing: nothing turns
        _manager->UpdateAll();
        Settle();
        EXPECT_EQ(ShownMaterialRows(), "TopQueen");

        PressWith(kSetModeAction, "computer");  // the person has Black, the program White: Black is at the bottom
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        Settle();
        EXPECT_EQ(ShownMaterialRows(), "BottomQueen") << "Black's capture is in the lower group now";
        EXPECT_EQ(MaterialSprite("Bottom", "Queen"), "WhiteQueen");
        EXPECT_EQ(Var("materialBottomBalance"), "+9");
        EXPECT_EQ(Var("materialTopBalance"), "");
    }

    TEST_F(ChessUiTest, TheDisplayFollowsAResetASetStateAndASetPosition) {
        StartFrom(kBlackTookTheQueen);
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "TopQueen");

        Model().Reset();
        Settle();
        EXPECT_EQ(MaterialWords(), kEmptyMaterialWords) << "a new game: nothing is gone";
        EXPECT_EQ(ShownMaterialRows(), "");

        ASSERT_TRUE(FenSetsTheState(kWhiteTookAPawn)) << "set_state of the control API";
        _manager->UpdateAll();
        Settle();
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn");
        EXPECT_EQ(Var("materialBottomBalance"), "+1");

        StartFrom(kBlackTookTheQueen);  // SetPosition
        Settle();
        EXPECT_EQ(ShownMaterialRows(), "TopQueen");
        EXPECT_EQ(Var("materialBottomBalance"), "");
        EXPECT_EQ(Var("materialTopBalance"), "+9");
    }

    TEST_F(ChessUiTest, APromotionChangesTheBalanceAndTheCountOfThePawns) {
        StartFrom(kPawnAboutToPromote);
        Settle();
        EXPECT_EQ(MaterialCount("Top", "Queen"), "x1") << "White has no queen";
        EXPECT_EQ(MaterialCount("Top", "Pawn"), "x7");
        EXPECT_EQ(Var("materialBottomBalance"), "+1") << "a pawn against a bare king";

        ClickMove("a7", "a8");
        ASSERT_TRUE(Model().HasPendingPromotion());
        PressWith(kPromoteAction, "queen");
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        Settle();
        EXPECT_EQ(Var("materialBottomBalance"), "+9") << "the new queen counts: nine points against nothing";
        EXPECT_EQ(MaterialCount("Top", "Queen"), "") << "White has a queen again: none is gone";
        EXPECT_EQ(MaterialCount("Top", "Pawn"), "x8") << "and its pawn is: the position cannot tell that a pawn became the queen";
    }

    TEST_F(ChessUiTest, AMoveOfTheProgramIsSeenByTheDisplayInTheSameFrameItIsPlayed) {
        StartFrom(kBlackCanTakeAPawn);
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "");
        const auto take = MoveGenerator::ParseUci(Model().GetPosition(), "d5e4");
        ASSERT_TRUE(take.has_value());
        const auto engine = MakeShared<GatedEngine>(true, *take);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 1; })) << "the program took the pawn";
        Settle();
        EXPECT_EQ(MaterialCount("Top", "Pawn"), "x1") << "Black is the program and has taken a pawn: the upper group, White is at the bottom";
        EXPECT_EQ(MaterialSprite("Top", "Pawn"), "WhitePawn");
        EXPECT_EQ(Var("materialTopBalance"), "+1");
        EXPECT_EQ(ShownMaterialRows(), "TopPawn");
    }

    TEST_F(ChessUiTest, WithNoGameActiveTheDisplayIsEmpty) {
        StartFrom(kWhiteTookAPawn);
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "BottomPawn");

        GameManager::GetInstance().Shutdown();
        ASSERT_EQ(ChessGame::GetActive(), nullptr);
        Draw();
        Draw();
        EXPECT_EQ(MaterialWords(), kEmptyMaterialWords);
        EXPECT_EQ(ShownMaterialRows(), "");
    }

    TEST_F(ChessUiTest, TheMaterialCanvasTakesNoClickAndTheBoardStillGetsItsOwn) {
        StartFrom(kWhiteTookAPawn);
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "BottomPawn");
        const auto canvas = Material();
        ASSERT_TRUE(canvas);

        // a hit test of the canvas finds nothing at the middle of any element, whether it is shown or not
        for (const UIElement* element : AllElements(canvas->GetRoot())) {
            const Rect& rect = element->rect;
            EXPECT_EQ(canvas->GetLayout().HitTest(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f), nullptr) << element->name.CStr();
        }
        // and a press where the shown row and the balance are drawn is not taken by the UI
        for (const char* name : {"MaterialBottomPawnRow", "MaterialBottomBalance", "MaterialBottomList", "MaterialTopBalance"}) {
            const auto [x, y] = ScreenCenter(MaterialElement(name));
            EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true))) << name;
            EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false))) << name;
        }

        // the board right next to the balance boxes (their right edge is the left edge of the board) takes a press
        ASSERT_TRUE(Model().GetTurn() == PieceColor::Black);
        const Rect nextToTheBalance = OutputRect(Rect{kMaterialMarginRight + 1.0f, 100.0f, 2.0f, 2.0f});  // inside the cell of a8, a hair from the balance box
        EXPECT_FALSE(Model().HasSelection());
        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(nextToTheBalance.x + nextToTheBalance.w * 0.5f, nextToTheBalance.y + nextToTheBalance.h * 0.5f, true));
        ASSERT_TRUE(Model().HasSelection()) << "the press went through the margin to the board";
        EXPECT_EQ(Model().GetSelected(), Sq("a8"));
    }

    TEST_F(ChessUiTest, TheMaterialIsDrawnOverThePiecesAndTheResultBarAndUnderTheHudAndThePanels) {
        const auto king = GiveAtlasCell("WhiteKing_e1");
        StartFrom(kBlackMatesOnA1);
        ASSERT_TRUE(Play(Model(), "a8a1"));
        ASSERT_TRUE(Model().IsGameOver());
        _manager->UpdateAll();
        Settle();
        ASSERT_NE(ShownMaterialRows(), "") << "most of the pieces are gone";
        ASSERT_TRUE(OpenThePanel());
        Draw();

        const auto& calls = _renderer->calls;
        long lastSquare = -1;
        long firstGlyph = -1;
        long lastGlyph = -1;
        long firstPiece = -1;
        long dim = -1;
        long firstChip = -1;
        long lastChip = -1;
        long resetButton = -1;
        long plate = -1;
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
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kMaterialChipColor)) {
                firstChip = firstChip < 0 ? at : firstChip;
                lastChip = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kResetColor)) {
                resetButton = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kPlateColor)) {
                plate = at;
            }
        }
        ASSERT_GE(lastSquare, 0);
        ASSERT_GE(lastGlyph, 0);
        ASSERT_GE(firstPiece, 0) << "the king is drawn";
        ASSERT_GE(dim, 0) << "the game is over: the board is dimmed";
        ASSERT_GE(firstChip, 0) << "the rows are drawn";
        ASSERT_GE(resetButton, 0);
        ASSERT_GE(plate, 0) << "the opponent panel is open";
        EXPECT_LT(lastSquare, firstChip) << "over the squares";
        EXPECT_LT(lastGlyph, firstChip) << "and over the coordinates";
        EXPECT_LT(firstPiece, firstChip) << "over the pieces";
        EXPECT_LT(dim, firstChip) << "and over the dim of the finished game";
        EXPECT_LT(lastChip, resetButton) << "under the HUD";
        EXPECT_LT(lastChip, plate) << "and under the panels";
    }

    TEST_F(ChessUiTest, InTheNarrowestWindowWithSideInsetsTheListsAreHiddenAndTheBalanceStays) {
        StartFrom(kWhiteTookAPawn);
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "BottomPawn") << "a window with no insets: the lists are shown";

        ResizeWithSideInsets(kNarrowestWidth, kNarrowestHeight);
        const auto canvas = Material();
        ASSERT_TRUE(canvas);
        const Rect safe = canvas->GetLayout().GetResolved().safeBounds;
        EXPECT_NEAR(safe.x, 75.6f, 0.2f) << "9 % of 480 px at the fit scale 0.571";
        EXPECT_EQ(ShownMaterialRows(), "") << "the box of the lists reaches out of the safe area";
        EXPECT_EQ(Var("materialBottomPawn"), "x1") << "the words are not taken away, only the rows";
        EXPECT_EQ(Var("materialBottomBalance"), "+1") << "the balance is still there";

        const UIElement& balance = MaterialElement("MaterialBottomBalance");
        ASSERT_TRUE(IsLabel(balance));
        EXPECT_EQ(ShownText(balance), "+1");
        EXPECT_GE(balance.rect.x, safe.x) << "inside the safe area";
        EXPECT_LE(balance.rect.x + balance.rect.w, kMaterialMarginRight) << "inside the margin";
        EXPECT_FALSE(balance.rect.IsEmpty());
        EXPECT_TRUE(balance.active.visible);

        // The sign of a big lead is shrunk into its box (AutoFit), not cut (the text component does not expose its last layout, so the check is the component's own fit)
        const float scale = canvas->GetLayout().GetResolved().scale;
        EXPECT_EQ(TextOf(balance)->overflow, TextOverflow::AutoFit);
        for (const char* lead : {"+1", "+39", "+99"}) {
            CoreManager::GetUIVars().Set(PoolString::Intern("materialBottomBalance"), lead);
            Draw();
            const UIContentFit fit = balance.CheckContent(balance.rect.w, balance.rect.h, scale);
            EXPECT_TRUE(fit.hasContent) << lead;
            EXPECT_TRUE(fit.fits) << lead << " is inside its box";
        }

        ResizeWithoutInsets(1280.0f, 720.0f);  // and the other way
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn") << "the window grows again: the rows come back";
        ResizeWithSideInsets(kNarrowestWidth, kNarrowestHeight);
        EXPECT_EQ(ShownMaterialRows(), "");
    }

    TEST_F(ChessUiTest, AHiddenListStaysHiddenWhenTheGameChangesAndAppearsWhenTheWindowHasRoom) {
        StartFrom(kWhiteTookAPawn);
        ResizeWithSideInsets(kNarrowestWidth, kNarrowestHeight);
        ASSERT_EQ(ShownMaterialRows(), "");
        ASSERT_TRUE(Play(Model(), "d8d5")) << "Black takes the pawn back";
        Settle();
        EXPECT_EQ(MaterialCount("Top", "Pawn"), "x1");
        EXPECT_EQ(ShownMaterialRows(), "") << "a change of the counts does not show a row the window has no room for";
        EXPECT_EQ(Var("materialTopBalance"), "");

        ResizeWithoutInsets(1280.0f, 720.0f);
        EXPECT_EQ(ShownMaterialRows(), "TopPawn BottomPawn");
    }

    TEST_F(ChessUiTest, EveryBalanceFromPlusOneToPlusNinetyNineFitsItsBoxInTheNarrowestWindow) {
        Draw();
        auto canvas = Material();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        UICanvasLayout& layout = canvas->GetLayout();
        LayOutInTheNarrowestWindow(layout);
        EXPECT_NEAR(layout.GetResolved().scale, kNarrowestWidth / kViewWidth, 0.01f);
        const float scale = layout.GetResolved().scale;

        size_t checked = 0;
        for (const char* group : {"Top", "Bottom"}) {
            const UIElement* label = &MaterialElement(Format("Material{}Balance", group).c_str());
            ASSERT_TRUE(IsLabel(*label));
            const PoolString variable = PoolString::Intern(Format("material{}Balance", group).c_str());
            for (int points = 1; points <= 99; ++points) {
                const String text = BalanceText(points);
                CoreManager::GetUIVars().Set(variable, StringView(text));
                const UIContentFit fit = label->CheckContent(label->rect.w, label->rect.h, scale);
                ASSERT_TRUE(fit.hasContent) << text.c_str();
                EXPECT_TRUE(fit.fits) << group << ": " << text.c_str() << " is cut in a box of " << label->rect.w << " x " << label->rect.h << " units at scale " << scale;
                ++checked;
            }
        }
        EXPECT_EQ(checked, 198u);
        layout.Invalidate();
    }

    // ---- the captured material: the layout ---------------------------------------------------------------------------------------------
    // The checks of the coordinates and of the panels on the node Material: the validator on the size matrix and the content of every box, with every count word and every sign, the narrowest
    // window alone, the shown rects against the margin, the HUD and the coordinate labels, the aspect of an icon, and broken copies built from the text of the shipped XML. The rows are shown
    // by hand for the matrix (the component shows only the rows of the pieces that are gone), so the lists are laid out at their full size

    namespace {

        /// The elements of the canvas Material as the shipped XML has them, with the rows shown (the copies of the tests start from this text)
        String ShownMaterialXml() {
            String xml = ShippedElementsXml("Material");
            const String hidden = " visible=\"false\"";
            for (size_t at = xml.find(hidden); at != String::npos; at = xml.find(hidden)) {
                xml.erase(at, hidden.size());
            }
            return xml;
        }

    }  // namespace

    TEST_F(ChessUiTest, TheMaterialCanvasHasTheElementsAndTheOrderOfTheScene) {
        const auto canvas = Material();
        ASSERT_TRUE(canvas);
        Vector<String> expected;
        for (const char* group : kMaterialGroups) {
            expected.push_back(Format("Material{}List", group));
            for (const char* piece : kMaterialPieces) {
                expected.push_back(Format("Material{}{}Row", group, piece));
                expected.push_back(Format("Material{}{}Icon", group, piece));
                expected.push_back(Format("Material{}{}Count", group, piece));
            }
            expected.push_back(Format("Material{}Balance", group));
        }
        const Vector<const UIElement*> elements = AllElements(canvas->GetRoot());
        ASSERT_EQ(elements.size(), 35u) << "the root and 2 lists, 10 rows, 10 icons, 10 counts and 2 balances";
        size_t stacks = 0;
        size_t images = 0;
        size_t labels = 0;
        for (size_t index = 0; index < expected.size(); ++index) {
            const UIElement& element = *elements[index + 1];
            EXPECT_EQ(element.name, PoolString::Intern(expected[index].c_str())) << "element " << index << " in the order of the scene: list, rows (row, icon, count), balance";
            stacks += element.Cast<UIStack>() != nullptr ? 1 : 0;
            images += element.GetComponent<UIImageComponent>() != nullptr ? 1 : 0;
            labels += IsLabel(element) ? 1 : 0;
        }
        EXPECT_EQ(stacks, 12u) << "two lists and ten rows";
        EXPECT_EQ(images, 10u);
        EXPECT_EQ(labels, 12u) << "ten counts and two balances";
    }

    TEST_F(ChessUiTest, TheMaterialCanvasPassesTheSizeMatrixWithEveryCountAndEverySign) {
        Draw();
        auto canvas = Material();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        ShowEveryMaterialRow(*canvas);

        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        for (size_t round = 0; round < kMaterialRounds; ++round) {
            PublishMaterialRound(round);
            const auto failures = ValidateOnMatrix(canvas->GetLayout(), options, ViewContext());
            ASSERT_TRUE(failures.empty()) << "round " << round << ": " << DescribeMatrixFailures(failures).c_str();
        }
        const String problems = MaterialProblemsOnMatrix(canvas->GetLayout());
        EXPECT_TRUE(problems.empty()) << problems.c_str();
    }

    TEST_F(ChessUiTest, TheLongestCountWordOfTheMatrixIsTheMostARowCanShow) {
        // Two bare kings: everything else is gone. The summary is where the largest count comes from; the matrix lays out every word up to it
        const Position bare = PositionFromFen("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
        const MaterialSummary summary = SummarizeMaterial(bare);
        int most = 0;
        for (const PieceColor color : {PieceColor::White, PieceColor::Black}) {
            for (const PieceType type : CapturableTypes()) {
                most = std::max(most, summary.GetMissing(color, type));
            }
        }
        EXPECT_EQ(most, kMostCaptured) << "the pawns: the longest count word is 'x" << most << "'";
    }

    TEST_F(ChessUiTest, TheListsAreHiddenOnlyInTheWindowsWhoseSideInsetsTakeTheMarginOfTheView) {
        Draw();
        auto canvas = Material();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);
        UICanvasLayout& layout = canvas->GetLayout();

        size_t hidden = 0;
        size_t shown = 0;
        String hiddenCases;
        for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
            layout.Update(ContextOf(matrixCase));
            const Rect safe = layout.GetResolved().safeBounds;
            const bool fits = MaterialListFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h);
            const bool sideInsets = matrixCase.label.find("side insets") != String::npos;
            if (!fits) {
                EXPECT_TRUE(sideInsets) << matrixCase.label.c_str() << ": only side insets take the margin (a notch takes the top 7 % of the short side, which is above the lists)";
                EXPECT_GT(safe.x, kMaterialListLeft) << matrixCase.label.c_str();
                ++hidden;
                hiddenCases += Format("  {} (safe area from x = {:.1f})\n", matrixCase.label.c_str(), safe.x);
            } else {
                ++shown;
                EXPECT_LE(safe.x, kMaterialListLeft) << matrixCase.label.c_str();
            }
        }
        layout.Invalidate();
        // The side inset is 9 % of the short side in pixels. Where the short side is the width (1:1 and the three portrait shapes: all 19 hidden cases) the view fills the width and the inset is 9 %
        // of the 840 units, 75.6, whatever the size. In a landscape window the view is fitted to the height, the inset is 72 units at most, and the bars at the sides of the view (113 units or more,
        // at 4:3) are wider than that
        EXPECT_GT(hidden, 0u) << "the narrowest window of the matrix is one of them";
        EXPECT_GT(shown, hidden);
        std::cout << "[   INFO   ] the lists are hidden in " << hidden << " of " << hidden + shown << " cases of the size matrix:\n" << hiddenCases.c_str();
    }

    TEST_F(ChessUiTest, NoMaterialElementIsCutOrOutOfPlaceInTheNarrowestWindow) {
        Draw();
        auto canvas = Material();
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);
        ShowEveryMaterialRow(*canvas);
        UICanvasLayout& layout = canvas->GetLayout();
        LayOutInTheNarrowestWindow(layout);
        const Rect safe = layout.GetResolved().safeBounds;
        EXPECT_NEAR(safe.x, 75.6f, 0.2f) << "9 % of 480 px at the fit scale 0.571";
        EXPECT_FALSE(MaterialListFits(safe.x, safe.y, safe.x + safe.w, safe.y + safe.h)) << "the lists are hidden here: the component shows no row";

        for (size_t round = 0; round < kMaterialRounds; ++round) {
            PublishMaterialRound(round);
            const String problems = MaterialProblems(layout);
            ASSERT_TRUE(problems.empty()) << "round " << round << ":\n" << problems.c_str();
        }
        // The balance boxes are the part that stays: inside the safe area, inside the margin, clear of the board (x 100) and of the coordinate labels (x 102)
        for (const char* group : kMaterialGroups) {
            const UIElement& balance = MaterialElement(Format("Material{}Balance", group).c_str());
            EXPECT_GE(balance.rect.x, safe.x) << group;
            EXPECT_LE(balance.rect.x + balance.rect.w, kMaterialMarginRight) << group;
        }
        layout.Invalidate();
    }

    TEST_F(ChessUiTest, TheShownMaterialIsClearOfTheHudAndTheCoordinateLabelsOnTheSizeMatrix) {
        Draw();
        auto material = Material();
        auto hud = Hud();
        auto coordinates = Coordinate();
        ASSERT_TRUE(material);
        ASSERT_TRUE(hud);
        ASSERT_TRUE(coordinates);
        material->UpdateLayout(1280.0f, 720.0f);
        hud->UpdateLayout(1280.0f, 720.0f);
        coordinates->UpdateLayout(1280.0f, 720.0f);
        ShowEveryMaterialRow(*material);
        PublishMaterialRound(0);

        size_t notchCasesWithAnOverlap = 0;
        size_t notchCases = 0;
        for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
            const UILayoutContext context = ContextOf(matrixCase);
            material->GetLayout().Update(context);
            hud->GetLayout().Update(context);
            coordinates->GetLayout().Update(context);
            const String labels = MaterialClearance(material->GetLayout(), coordinates->GetLayout(), 1.0f);
            EXPECT_TRUE(labels.empty()) << matrixCase.label.c_str() << ":\n" << labels.c_str();
            const String controls = MaterialClearance(material->GetLayout(), hud->GetLayout(), kMinGap);
            if (matrixCase.label.find("notch") != String::npos) {
                ++notchCases;
                if (!controls.empty()) {
                    ++notchCasesWithAnOverlap;
                    // Not only close: the Turn label lies on top of the balance of the upper group, and of its first rows (src/Chess/Components/CLAUDE.md, "The captured material", "Known limits")
                    const String touching = MaterialClearance(material->GetLayout(), hud->GetLayout(), 0.0f);
                    EXPECT_NE(touching.find("MaterialTopBalance ("), String::npos) << matrixCase.label.c_str() << ":\n" << touching.c_str();
                    EXPECT_NE(touching.find("MaterialTopQueenRow ("), String::npos) << matrixCase.label.c_str() << ":\n" << touching.c_str();
                    EXPECT_NE(touching.find(") and Turn ("), String::npos) << matrixCase.label.c_str() << ":\n" << touching.c_str();
                }
            } else {
                EXPECT_TRUE(controls.empty()) << matrixCase.label.c_str() << ":\n" << controls.c_str();
            }
        }
        material->GetLayout().Invalidate();
        hud->GetLayout().Invalidate();
        coordinates->GetLayout().Invalidate();
        // The count is pinned: src/Chess/Components/CLAUDE.md names 32 of 46 notch cases, and each of them was asserted above
        EXPECT_EQ(notchCases, 46u);
        EXPECT_EQ(notchCasesWithAnOverlap, 32u);
        // The HUD keeps clear of the safe area, and a notch pushes its top row down by 7 % of the short side, onto the first rows and the balance of the upper group (as it does onto the board:
        // src/Chess/Components/CLAUDE.md, "The HUD")
        std::cout << "[   INFO   ] the HUD reaches the top group in " << notchCasesWithAnOverlap << " of " << notchCases << " notch cases\n";
    }

    TEST_F(ChessUiTest, TheShownMaterialStaysInTheMarginAndClearOfTheHudAndTheCoordinatesInFiveWindowShapes) {
        StartFrom("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}, Pair<float, float>{1024.0f, 768.0f}, Pair<float, float>{800.0f, 800.0f},
                                 Pair<float, float>{2560.0f, 1080.0f}}) {
            SCOPED_TRACE(Format("{}x{}", size.first, size.second).c_str());
            DrawAt(size.first, size.second);
            DrawAt(size.first, size.second);  // the rows wait for the first layout
            ASSERT_EQ(ShownMaterialRows(), "TopQueen TopRook TopBishop TopKnight TopPawn BottomQueen BottomRook BottomBishop BottomKnight BottomPawn") << "two bare kings: everything is gone";
            const UICanvasLayout& material = Material()->GetLayout();
            EXPECT_EQ(MaterialProblems(material), "");
            EXPECT_EQ(MaterialClearance(material, Hud()->GetLayout(), kMinGap), "") << "no safe area: the HUD is in the margins above and below the board";
            EXPECT_EQ(MaterialClearance(material, Coordinate()->GetLayout(), 1.0f), "");
            for (const char* group : kMaterialGroups) {
                for (const char* piece : kMaterialPieces) {
                    const Rect& rect = MaterialRow(group, piece).rect;
                    EXPECT_LE(rect.x + rect.w, kMaterialMarginRight) << group << piece << " does not reach the board";
                    EXPECT_GE(rect.x, 0.0f) << group << piece;
                }
            }
        }
    }

    TEST_F(ChessUiTest, AnIconKeepsTheAspectOfItsAtlasCellCenteredInsideItsRow) {
        // The library is empty in the test, so every icon is given the atlas of the pieces and a cell of it by hand; UIImageComponent fits the cell into its rect
        StartFrom("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
        const Vector<uint8_t> pixels(static_cast<size_t>(kAtlasWidth) * kAtlasHeight * 4, 0);
        const IntrusivePtr<ITexture> atlas = _renderer->CreateTexture(pixels.data(), kAtlasWidth, kAtlasHeight);
        for (const char* group : kMaterialGroups) {
            for (const char* piece : kMaterialPieces) {
                UIImageComponent* icon = MaterialElement(Format("Material{}{}Icon", group, piece).c_str()).GetComponent<UIImageComponent>();
                ASSERT_NE(icon, nullptr) << group << piece;
                icon->GetSprite().SetTexture(atlas, Rect{kCellX, 0.0f, kCellWidth, kCellHeight});
            }
        }

        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}}) {
            SCOPED_TRACE(Format("{}x{}", size.first, size.second).c_str());
            DrawAt(size.first, size.second);
            _renderer->calls.clear();
            DrawAt(size.first, size.second);
            ASSERT_NE(ShownMaterialRows(), "");
            Vector<const DrawCall*> drawn;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.texture == atlas.Get()) {
                    drawn.push_back(&call);
                }
            }
            ASSERT_EQ(drawn.size(), 10u) << "one picture per icon, in the order of the scene";
            size_t index = 0;
            for (const char* group : kMaterialGroups) {
                for (const char* piece : kMaterialPieces) {
                    SCOPED_TRACE(Format("{}{}", group, piece).c_str());
                    const Rect& call = drawn[index++]->rect;
                    const Rect box = OutputRect(MaterialElement(Format("Material{}{}Icon", group, piece).c_str()).rect);
                    EXPECT_NEAR(call.w / call.h, kCellWidth / kCellHeight, 0.01f) << "the aspect of the atlas cell, not of the 26-unit box";
                    EXPECT_GE(call.x, box.x - 0.5f);
                    EXPECT_GE(call.y, box.y - 0.5f);
                    EXPECT_LE(call.x + call.w, box.x + box.w + 0.5f) << "inside its box";
                    EXPECT_LE(call.y + call.h, box.y + box.h + 0.5f);
                    EXPECT_NEAR(call.x + call.w * 0.5f, box.x + box.w * 0.5f, 0.5f) << "centered";
                    EXPECT_NEAR(call.y + call.h * 0.5f, box.y + box.h * 0.5f, 0.5f);
                    EXPECT_NEAR(call.h, box.h, 0.5f) << "the cell is tall: the height of the box is the limit, and the picture fills it";
                }
            }
        }
    }

    TEST_F(ChessUiTest, ABrokenCopyOfTheMaterialCanvasIsCaughtByTheChecks) {
        Draw();
        const String shipped = ShownMaterialXml();
        ASSERT_FALSE(shipped.empty());
        auto hud = Hud();
        ASSERT_TRUE(hud);
        hud->UpdateLayout(1280.0f, 720.0f);
        const auto problemsOf = [&](const String& xml) {
            CanvasCopy copy(xml, false);
            return MaterialProblemsOnMatrix(copy.layout, 3, 100000);
        };

        EXPECT_EQ(problemsOf(shipped), "") << "the same checks pass on the shipped canvas";

        String wide = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(wide, R"(name="MaterialTopList" x="8" y="84" width="64")", R"(name="MaterialTopList" x="8" y="84" width="104")"));
        EXPECT_NE(problemsOf(wide).find("MaterialTopList (8.0, 84.0, 104.0"), String::npos) << "a list that reaches the board leaves the margin";

        String bigFont = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(bigFont, R"(name="MaterialTopQueenCount" flex="1" fontSize="20")", R"(name="MaterialTopQueenCount" flex="1" fontSize="60")"));
        EXPECT_NE(problemsOf(bigFont).find("MaterialTopQueenCount cuts"), String::npos) << "a font of 60 does not fit the count box";

        String smallBox = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(smallBox, R"(name="MaterialTopBalance" x="77" y="84" width="23")", R"(name="MaterialTopBalance" x="77" y="84" width="8")"));
        EXPECT_NE(problemsOf(smallBox).find("MaterialTopBalance cuts"), String::npos) << "a balance box of 8 units cuts the sign even at the smallest size";

        String lowRow = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(lowRow, R"(name="MaterialTopQueenRow" height="40")", R"(name="MaterialTopQueenRow" height="20")"));
        EXPECT_NE(problemsOf(lowRow).find("MaterialTopQueenRow is 20.0 units high"), String::npos);

        String missing = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(missing, R"(name="MaterialTopPawnRow")", R"(name="MaterialTopPawnRowX")"));
        EXPECT_NE(problemsOf(missing).find("MaterialTopPawnRow is missing"), String::npos);

        // A list that moved up under the first row of the HUD: the clearance against the HUD finds it, in a window without insets
        String underTheHud = shipped;
        ASSERT_NO_FATAL_FAILURE(Replace(underTheHud, R"(name="MaterialTopList" x="8" y="84")", R"(name="MaterialTopList" x="8" y="20")"));
        CanvasCopy moved(underTheHud, false);
        EXPECT_NE(problemsOf(underTheHud).find("MaterialTopList (8.0, 20.0"), String::npos) << "above the rows of the board";
        const UIMatrixCase* plain = nullptr;
        for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
            if (matrixCase.label == "16:9 1280x720 no insets") {
                plain = &matrixCase;
            }
        }
        ASSERT_NE(plain, nullptr);
        moved.layout.Update(ContextOf(*plain));
        hud->GetLayout().Update(ContextOf(*plain));
        EXPECT_NE(MaterialClearance(moved.layout, hud->GetLayout(), kMinGap).find("MaterialTopQueenRow"), String::npos) << "the first row is on the Turn label";
        CanvasCopy fine(shipped, false);
        fine.layout.Update(ContextOf(*plain));
        EXPECT_EQ(MaterialClearance(fine.layout, hud->GetLayout(), kMinGap), "");
        hud->GetLayout().Invalidate();
    }

    // ---- the hint on the board ----------------------------------------------------------------------------------------------------
    // The analyst holds the hint for one version of the position; the board fills its two squares (under the selection and the pieces) while it does. An engine double answers the
    // scripted move e2e4 through the analyst's factory, and the frames are real ones

    TEST_F(ChessUiTest, WithNoHintAskedForNoExtraSquareIsFilled) {
        CountingAnalysisFactory factory({MakeShared<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        Settle();
        EXPECT_TRUE(HintFills().first.empty());
        EXPECT_TRUE(HintFills().second.empty());
        Analyst().SetEvaluation(true);  // an evaluation alone is no hint
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetEvaluation(Model()).has_value(); }));
        Draw();
        EXPECT_TRUE(HintFills().first.empty());
        EXPECT_TRUE(HintFills().second.empty());
        EXPECT_EQ(RectCalls(kLightSquareColor).size() + RectCalls(kDarkSquareColor).size(), 64u);
    }

    TEST_F(ChessUiTest, AReadyHintFillsTheTwoSquaresOfItsMoveInTheirOwnColorsWithWhiteAtTheBottom) {
        CountingAnalysisFactory factory({MakeShared<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(ShowTheHint());
        ASSERT_FALSE(BlackIsAtTheBottom());

        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}}) {
            SCOPED_TRACE(Format("output {}x{}", size.first, size.second).c_str());
            DrawAt(size.first, size.second);
            const auto [from, to] = HintFills();
            ASSERT_EQ(from.size(), 1u);
            ASSERT_EQ(to.size(), 1u);
            EXPECT_TRUE(SameRect(from[0]->rect, SquareRect("e2"))) << "the square the piece leaves";
            EXPECT_TRUE(SameRect(to[0]->rect, SquareRect("e4"))) << "the square it goes to";
        }
    }

    TEST_F(ChessUiTest, AReadyHintFillsTheTurnedCellsWhenTheProgramPlaysWhite) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory opponentFactory({MakeShared<GatedEngine>(true)});  // the program opens and holds its answer: the board stays as it is
        Opponent().SetEngineFactory(opponentFactory.AsFactory());
        CountingAnalysisFactory factory({MakeShared<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        ASSERT_TRUE(ShowTheHint());

        Draw();
        const auto [from, to] = HintFills();
        ASSERT_EQ(from.size(), 1u);
        ASSERT_EQ(to.size(), 1u);
        EXPECT_TRUE(SameRect(from[0]->rect, SquareRect("e2")));
        EXPECT_TRUE(SameRect(to[0]->rect, SquareRect("e4")));
        // written out: e2 is the cell column 3, row 1 of the turned grid, e4 column 3, row 3
        EXPECT_TRUE(SameRect(from[0]->rect, CellRect(3, 1)));
        EXPECT_TRUE(SameRect(to[0]->rect, CellRect(3, 3)));
    }

    TEST_F(ChessUiTest, TheHintIsDrawnOverTheSquaresAndUnderThePiecesAndASelectionStaysOnTopOfIt) {
        const auto pawn = GiveAtlasCell("WhitePawn_e2");
        CountingAnalysisFactory factory({MakeShared<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(ShowTheHint());
        Model().OnSquareClicked(Sq("e2"));  // a selection is not a change of the position: the hint stays
        ASSERT_TRUE(Model().HasSelection());
        Draw();

        long lastSquare = -1;
        long hintFrom = -1;
        long hintTo = -1;
        long selection = -1;
        long legal = -1;
        long piece = -1;
        const auto& calls = _renderer->calls;
        for (size_t i = 0; i < calls.size(); ++i) {
            const DrawCall& call = calls[i];
            const long at = static_cast<long>(i);
            if (call.kind == DrawKind::Rect && (SameColor(call.color, kLightSquareColor) || SameColor(call.color, kDarkSquareColor))) {
                lastSquare = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kHintFromColor)) {
                hintFrom = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kHintToColor)) {
                hintTo = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kSelectedColor)) {
                selection = at;
            } else if (call.kind == DrawKind::Rect && SameColor(call.color, kLegalMoveColor)) {
                legal = at;
            } else if (call.kind == DrawKind::Texture && call.texture == pawn.Get()) {
                piece = at;
            }
        }
        ASSERT_GE(hintFrom, 0);
        ASSERT_GE(hintTo, 0);
        ASSERT_GE(selection, 0);
        ASSERT_GE(legal, 0);
        ASSERT_GE(piece, 0);
        EXPECT_LT(lastSquare, hintFrom) << "over the squares";
        EXPECT_LT(hintFrom, hintTo);
        EXPECT_LT(hintTo, selection) << "under the selection";
        EXPECT_LT(hintTo, legal) << "and under the legal moves";
        EXPECT_LT(hintTo, piece) << "and under the pieces";
        EXPECT_LT(selection, piece);
    }

    TEST_F(ChessUiTest, AMoveByClicksAResetASetPositionAndTheControlApiTakeTheHintOffAtTheNextFrame) {
        CountingAnalysisFactory factory({MakeShared<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        const auto expectHint = [&](bool shown) {
            Draw();
            EXPECT_EQ(HintFills().first.size(), shown ? 1u : 0u);
            EXPECT_EQ(HintFills().second.size(), shown ? 1u : 0u);
        };

        ASSERT_TRUE(ShowTheHint());
        expectHint(true);
        ClickMove("e2", "e3");  // a person's move
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        expectHint(false);

        ASSERT_TRUE(ShowTheHint());
        expectHint(true);
        Click("a7");  // the first click of a move only selects: the hint stays
        expectHint(true);
        Model().Reset();
        expectHint(false);

        ASSERT_TRUE(ShowTheHint());
        expectHint(true);
        Model().SetPosition(PositionFromFen(kBlackToMoveAfterE4));
        expectHint(false);

        ASSERT_TRUE(ShowTheHint());
        expectHint(true);
        Json request = Json::object();  // set_state and reset of the control API go through the same version
        request["fen"] = "k7/8/8/8/8/8/4K3/7R w - - 0 1";
        ASSERT_TRUE(ChessGame::GetActive()->AsControllable()->SetState(request).has_value());
        expectHint(false);

        ASSERT_TRUE(ShowTheHint());
        expectHint(true);
        ASSERT_TRUE(ChessGame::GetActive()->AsControllable()->Reset(Json::object()).has_value());
        expectHint(false);
    }

    TEST_F(ChessUiTest, AFinishedGameShowsNoHint) {
        CountingAnalysisFactory factory({MakeShared<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Mate, 1}, Mv("a8a1"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        StartFrom(kBlackMatesOnA1);
        ASSERT_TRUE(ShowTheHint());
        Draw();
        ASSERT_EQ(HintFills().first.size(), 1u) << "a8";
        EXPECT_TRUE(SameRect(HintFills().first[0]->rect, SquareRect("a8")));
        EXPECT_TRUE(SameRect(HintFills().second[0]->rect, SquareRect("a1")));

        ASSERT_TRUE(Play(Model(), "a8a1"));
        ASSERT_TRUE(Model().IsGameOver());
        _manager->UpdateAll();
        Draw();
        EXPECT_TRUE(HintFills().first.empty());
        EXPECT_TRUE(HintFills().second.empty());
        const auto refused = Analyst().RequestHint(Model());
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error(), HintRefusal::GameOver);
        IdleFrames(10);
        Draw();
        EXPECT_TRUE(HintFills().first.empty());
        EXPECT_TRUE(HintFills().second.empty());
    }

    TEST_F(ChessUiTest, WhileTheEngineStillThinksNothingIsDrawnAndTheAnswerBringsTheHint) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeShared<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"));
        engine->Gate(true);
        CountingAnalysisFactory factory({engine});
        Analyst().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Analyst().RequestHint(Model()).has_value());
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() > 0; })) << "the engine is asked and holds its answer";
        IdleFrames(20);
        Draw();
        EXPECT_TRUE(Analyst().IsHintRequested(Model()));
        EXPECT_TRUE(Analyst().IsThinking());
        EXPECT_TRUE(HintFills().first.empty());
        EXPECT_TRUE(HintFills().second.empty());

        engine->Open();
        ASSERT_TRUE(FramesUntil([&] { return Analyst().GetHint(Model()).has_value(); }));
        Draw();
        EXPECT_EQ(HintFills().first.size(), 1u);
        EXPECT_EQ(HintFills().second.size(), 1u);
    }

    TEST_F(ChessUiTest, TheHintPlaysNothingAndLeavesThePiecesAndTheStateOfTheGameAsTheyAre) {
        CountingAnalysisFactory factory({MakeShared<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        const auto boardOf = [&] {
            const auto state = ChessGame::GetActive()->AsControllable()->GetState();
            EXPECT_TRUE(state.has_value());
            const Json* board = state ? FindJsonMember(*state, "board") : nullptr;
            const Json* fen = state ? FindJsonMember(*state, "fen") : nullptr;
            const Json* moves = state ? FindJsonMember(*state, "moves") : nullptr;
            EXPECT_TRUE(board && fen && moves);
            return board && fen && moves ? DumpJson(*board) + DumpJson(*fen) + DumpJson(*moves) : String();
        };
        const String before = boardOf();
        const uint64_t modelVersion = Model().GetVersion();
        ASSERT_TRUE(ShowTheHint());
        IdleFrames(20);
        Draw();
        ASSERT_EQ(HintFills().first.size(), 1u);
        EXPECT_EQ(boardOf(), before);
        EXPECT_EQ(Model().GetVersion(), modelVersion) << "the model is not touched";
        EXPECT_TRUE(Model().GetMoves().empty());
        ExpectPiecesAtTheCellsOf(false);
        EXPECT_EQ(PieceOrigin("WhitePawn_e2"), (Pair<float, float>{420.0f, 560.0f}));
    }

    TEST_F(ChessUiTest, WithNoGameActiveTheBoardDrawsNoHint) {
        CountingAnalysisFactory factory({MakeShared<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e2e4"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        ASSERT_TRUE(ShowTheHint());
        Draw();
        ASSERT_EQ(HintFills().first.size(), 1u);

        GameManager::GetInstance().Shutdown();
        ASSERT_EQ(ChessGame::GetActive(), nullptr);
        Draw();
        EXPECT_TRUE(HintFills().first.empty());
        EXPECT_TRUE(HintFills().second.empty());
        EXPECT_EQ(RectCalls(kLightSquareColor).size() + RectCalls(kDarkSquareColor).size(), 64u) << "the squares are still drawn";
    }

    // ---- the last move on the board -----------------------------------------------------------------------------------------------
    // The board fills the from and to squares of GetMoves().back() in one color of its own, under the check, the hint and the selection; it keeps nothing, so a new game has no mark

    TEST_F(ChessUiTest, ANewGameMarksNoLastMove) {
        Draw();
        EXPECT_TRUE(LastMoveFills().empty());
        EXPECT_EQ(RectCalls(kLightSquareColor).size() + RectCalls(kDarkSquareColor).size(), 64u);
    }

    TEST_F(ChessUiTest, AMoveByClicksMarksItsTwoSquaresAndTheNextMoveReplacesThem) {
        for (const auto& size : {Pair<float, float>{1280.0f, 720.0f}, Pair<float, float>{720.0f, 1280.0f}}) {
            SCOPED_TRACE(Format("output {}x{}", size.first, size.second).c_str());
            Model().Reset();
            ClickMove("e2", "e4");
            ASSERT_EQ(Model().GetMoves().size(), 1u);
            DrawAt(size.first, size.second);
            auto fills = LastMoveFills();
            ASSERT_EQ(fills.size(), 2u);
            EXPECT_TRUE(SameRect(fills[0]->rect, SquareRect("e2"))) << "the square the piece left";
            EXPECT_TRUE(SameRect(fills[1]->rect, SquareRect("e4"))) << "the square it went to";

            ClickMove("e7", "e5");  // two people: the other side moves, and the mark follows
            ASSERT_EQ(Model().GetMoves().size(), 2u);
            DrawAt(size.first, size.second);
            fills = LastMoveFills();
            ASSERT_EQ(fills.size(), 2u);
            EXPECT_TRUE(SameRect(fills[0]->rect, SquareRect("e7")));
            EXPECT_TRUE(SameRect(fills[1]->rect, SquareRect("e5")));
        }
    }

    TEST_F(ChessUiTest, AMoveByTheControlApiAndAMoveByDragMarkTheirSquares) {
        Json action = Json::object();
        action["type"] = "move";
        action["uci"] = "g1f3";
        ASSERT_TRUE(ChessGame::GetActive()->AsControllable()->PerformAction(action).has_value());
        Draw();
        auto fills = LastMoveFills();
        ASSERT_EQ(fills.size(), 2u);
        EXPECT_TRUE(SameRect(fills[0]->rect, SquareRect("g1")));
        EXPECT_TRUE(SameRect(fills[1]->rect, SquareRect("f3")));

        Drag("e7", "e5");
        ASSERT_EQ(Model().GetMoves().size(), 2u);
        Draw();
        fills = LastMoveFills();
        ASSERT_EQ(fills.size(), 2u);
        EXPECT_TRUE(SameRect(fills[0]->rect, SquareRect("e7")));
        EXPECT_TRUE(SameRect(fills[1]->rect, SquareRect("e5")));
    }

    TEST_F(ChessUiTest, TheProgramsAnswerMarksItsSquares) {
        CountingFactory factory({MakeShared<InstantEngine>()});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        ClickMove("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 2; }));
        Draw();
        const auto [from, to] = LastMoveRects();
        const auto fills = LastMoveFills();
        ASSERT_EQ(fills.size(), 2u);
        EXPECT_TRUE(SameRect(fills[0]->rect, from));
        EXPECT_TRUE(SameRect(fills[1]->rect, to));
        EXPECT_EQ(Model().GetPieceAt(Model().GetMoves().back().To()).color, PieceColor::Black) << "it is the black move that is marked, not the person's e2e4";
        EXPECT_FALSE(SameRect(from, SquareRect("e2")));
    }

    TEST_F(ChessUiTest, AResetASetPositionAndSetStateWithAFenTakeTheMarkOff) {
        const auto expectMarked = [&](bool marked) {
            Draw();
            EXPECT_EQ(LastMoveFills().size(), marked ? 2u : 0u);
        };
        ClickMove("e2", "e4");
        expectMarked(true);
        Model().Reset();
        expectMarked(false);

        ClickMove("d2", "d4");
        expectMarked(true);
        Model().SetPosition(PositionFromFen(kBlackToMoveAfterE4));
        expectMarked(false);

        ClickMove("e7", "e5");
        expectMarked(true);
        Json request = Json::object();
        request["fen"] = "k7/8/8/8/8/8/4K3/7R w - - 0 1";
        ASSERT_TRUE(ChessGame::GetActive()->AsControllable()->SetState(request).has_value());
        expectMarked(false);

        ASSERT_TRUE(Play(Model(), "h1h2"));
        expectMarked(true);
        ASSERT_TRUE(ChessGame::GetActive()->AsControllable()->Reset(Json::object()).has_value());
        expectMarked(false);
    }

    TEST_F(ChessUiTest, CastlingMarksTheKingsMoveEnPassantAndPromotionThePawnsSquares) {
        const auto expectMarks = [&](const char* from, const char* to) {
            SCOPED_TRACE(Format("{}{}", from, to).c_str());
            Draw();
            const auto fills = LastMoveFills();
            ASSERT_EQ(fills.size(), 2u);
            EXPECT_TRUE(SameRect(fills[0]->rect, SquareRect(from)));
            EXPECT_TRUE(SameRect(fills[1]->rect, SquareRect(to)));
        };

        StartFrom("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        ASSERT_TRUE(Play(Model(), "e1g1"));  // the rook goes to f1 and is not marked
        expectMarks("e1", "g1");
        ASSERT_TRUE(Play(Model(), "e8c8"));
        expectMarks("e8", "c8");

        StartFrom("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
        ASSERT_TRUE(Play(Model(), "e5d6"));  // the pawn taken on d5 is not marked
        expectMarks("e5", "d6");

        StartFrom("8/P6k/8/8/8/8/8/K7 w - - 0 1");
        ASSERT_TRUE(Play(Model(), "a7a8q"));
        expectMarks("a7", "a8");
        StartFrom("8/P6k/8/8/8/8/8/K7 w - - 0 1");
        Click("a7");
        Click("a8");
        ASSERT_TRUE(Model().HasPendingPromotion());
        Draw();
        EXPECT_TRUE(LastMoveFills().empty()) << "a promotion still waiting for its piece is no move yet";
        PressWith(kPromoteAction, "knight");
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        expectMarks("a7", "a8");
    }

    TEST_F(ChessUiTest, TheTurnedBoardMarksTheTurnedCells) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory opponentFactory({MakeShared<GatedEngine>(true)});  // the program opens and holds its answer: the board stays as it is
        Opponent().SetEngineFactory(opponentFactory.AsFactory());
        _manager->UpdateAll();
        ControlPlays("white");
        _manager->UpdateAll();
        ASSERT_TRUE(BlackIsAtTheBottom());
        ASSERT_TRUE(Play(Model(), "e2e4"));
        Draw();
        const auto fills = LastMoveFills();
        ASSERT_EQ(fills.size(), 2u);
        EXPECT_TRUE(SameRect(fills[0]->rect, SquareRect("e2")));
        EXPECT_TRUE(SameRect(fills[1]->rect, SquareRect("e4")));
        // written out: e2 is the cell column 3, row 1 of the turned grid, e4 column 3, row 3
        EXPECT_TRUE(SameRect(fills[0]->rect, CellRect(3, 1)));
        EXPECT_TRUE(SameRect(fills[1]->rect, CellRect(3, 3)));
    }

    TEST_F(ChessUiTest, TheLastMoveIsDrawnOverTheSquaresAndUnderTheCheckTheHintTheSelectionAndTheLegalMoves) {
        CountingAnalysisFactory factory({MakeShared<AnalysingEngine>(AnalysisScore{AnalysisScoreKind::Centipawns, 35}, Mv("e8d8"))});
        Analyst().SetEngineFactory(factory.AsFactory());
        StartFrom("4k3/8/8/8/8/8/8/3R1K2 w - - 0 1");
        ASSERT_TRUE(Play(Model(), "d1e1"));  // check: the rook's squares are marked and the king's square is red
        ASSERT_TRUE(Model().IsInCheck());
        ASSERT_TRUE(ShowTheHint());
        Click("e8");  // a selection of the king: its own square and the squares it can go to
        ASSERT_TRUE(Model().HasSelection());
        Draw();

        long lastSquare = -1;
        long firstMark = -1;
        long lastMark = -1;
        long check = -1;
        long hintFrom = -1;
        long selection = -1;
        long legal = -1;
        const auto& calls = _renderer->calls;
        for (size_t i = 0; i < calls.size(); ++i) {
            const DrawCall& call = calls[i];
            if (call.kind != DrawKind::Rect) {
                continue;
            }
            const long at = static_cast<long>(i);
            if (SameColor(call.color, kLightSquareColor) || SameColor(call.color, kDarkSquareColor)) {
                lastSquare = at;
            } else if (SameColor(call.color, kLastMoveColor)) {
                firstMark = firstMark < 0 ? at : firstMark;
                lastMark = at;
            } else if (SameColor(call.color, kCheckColor)) {
                check = at;
            } else if (SameColor(call.color, kHintFromColor)) {
                hintFrom = at;
            } else if (SameColor(call.color, kSelectedColor)) {
                selection = at;
            } else if (SameColor(call.color, kLegalMoveColor)) {
                legal = at;
            }
        }
        ASSERT_GE(firstMark, 0);
        ASSERT_GE(check, 0);
        ASSERT_GE(hintFrom, 0);
        ASSERT_GE(selection, 0);
        ASSERT_GE(legal, 0);
        EXPECT_EQ(LastMoveFills().size(), 2u);
        EXPECT_LT(lastSquare, firstMark) << "over the squares";
        EXPECT_LT(lastMark, check) << "under the check";
        EXPECT_LT(lastMark, hintFrom) << "under the hint";
        EXPECT_LT(lastMark, selection) << "under the selection";
        EXPECT_LT(lastMark, legal) << "and under the legal moves";
    }

    TEST_F(ChessUiTest, TheMarkOfTheLastMoveStaysAfterTheGameEnds) {
        StartFrom(kBlackMatesOnA1);
        ASSERT_TRUE(Play(Model(), "a8a1"));
        ASSERT_TRUE(Model().IsGameOver());
        Draw();
        const auto fills = LastMoveFills();
        ASSERT_EQ(fills.size(), 2u);
        EXPECT_TRUE(SameRect(fills[0]->rect, SquareRect("a8")));
        EXPECT_TRUE(SameRect(fills[1]->rect, SquareRect("a1")));
    }

    TEST_F(ChessUiTest, WithNoGameActiveTheBoardMarksNoLastMove) {
        ClickMove("e2", "e4");
        Draw();
        ASSERT_EQ(LastMoveFills().size(), 2u);
        GameManager::GetInstance().Shutdown();
        ASSERT_EQ(ChessGame::GetActive(), nullptr);
        Draw();
        EXPECT_TRUE(LastMoveFills().empty());
        EXPECT_EQ(RectCalls(kLightSquareColor).size() + RectCalls(kDarkSquareColor).size(), 64u);
    }

    // A watch: both colors are programs (OpponentController::ChooseSeats). Gated engines that never answer keep the turn where it is, so the guard of the board is what is looked at.
    namespace {
        constexpr ProgramSides kWatchBoth{true, true};
        constexpr const char* kWatchBlackToMove = "rnbqkbnr/ppp1pppp/8/3P4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3";      // White has taken a pawn, Black to move
        constexpr const char* kWatchWhiteIsMated = "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3";  // the fool's mate: the game is over
    }  // namespace

    TEST_F(ChessUiTest, WatchTheBoardTakesNoClickOnWhitesTurnAndKeepsTheSearchAlone) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = MakeShared<GatedEngine>(true);
        const auto black = MakeShared<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return white->Calls() == 1; })) << "White's program has the search and waits at its gate";
        ASSERT_EQ(Model().GetTurn(), PieceColor::White);
        const uint64_t version = Model().GetVersion();

        for (const char* square : {"e2", "e4", "d2", "d4", "a4", "e7", "g1"}) {
            Click(square);
            EXPECT_FALSE(Model().HasSelection()) << square;
        }
        IdleFrames();

        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_FALSE(Model().HasPendingPromotion());
        EXPECT_EQ(Model().GetVersion(), version) << "the clicks did not move the game, so the search is not stale";
        EXPECT_TRUE(Opponent().IsThinking());
        EXPECT_FALSE(white->WasCancelled());
        EXPECT_EQ(white->Calls(), 1);
        EXPECT_EQ(black->Calls(), 0);
    }

    TEST_F(ChessUiTest, WatchTheBoardTakesNoClickOnBlacksTurnEither) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = MakeShared<GatedEngine>(true);
        const auto black = MakeShared<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kWatchBlackToMove);
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return black->Calls() == 1; })) << "Black's program has the search and waits at its gate";
        ASSERT_EQ(Model().GetTurn(), PieceColor::Black);
        const uint64_t version = Model().GetVersion();

        for (const char* square : {"e7", "e5", "d7", "d6", "a2", "d5", "g8"}) {
            Click(square);
            EXPECT_FALSE(Model().HasSelection()) << square;
        }
        IdleFrames();

        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_TRUE(Opponent().IsThinking());
        EXPECT_FALSE(black->WasCancelled());
        EXPECT_EQ(black->Calls(), 1);
    }

    TEST_F(ChessUiTest, WatchNoClickQueuesAPremove) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = MakeShared<GatedEngine>(true);
        const auto black = MakeShared<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kWatchBlackToMove);
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return black->Calls() == 1; }));

        ClickMove("d2", "d3");
        ClickMove("d2", "d4");
        IdleFrames(5);

        EXPECT_TRUE(Model().GetPremoves().empty());
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_TRUE(Model().GetMoves().empty());
    }

    TEST_F(ChessUiTest, WatchNoClickStartsAPromotion) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = MakeShared<GatedEngine>(true);
        const auto black = MakeShared<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kPromotionFen);  // the pawn on a7 would promote on a8 for a person
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return white->Calls() == 1; }));
        const uint64_t version = Model().GetVersion();

        ClickMove("a7", "a8");
        IdleFrames(5);

        EXPECT_FALSE(Model().HasSelection());
        EXPECT_FALSE(Model().HasPendingPromotion());
        EXPECT_FALSE(PromotionShown());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_TRUE(Model().GetMoves().empty());
    }

    TEST_F(ChessUiTest, WatchStartingDropsTheSelectionOfThePersonWhoseTurnItIs) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = MakeShared<GatedEngine>(true);
        const auto black = MakeShared<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        Click("e2");
        ASSERT_TRUE(Model().HasSelection()) << "two people: the click selects";

        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());

        EXPECT_FALSE(Model().HasSelection()) << "the program took the turn: the half-made move of the person goes";
        Draw();
        EXPECT_TRUE(RectCalls(kSelectedColor).empty()) << "and no selection is drawn";
        ASSERT_TRUE(FramesUntil([&] { return white->Calls() == 1; }));
    }

    TEST_F(ChessUiTest, WatchWhiteStaysAtTheBottomAndTheMaterialGroupsAreNotSwapped) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true), MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kWhiteTookAPawn);
        Settle();
        ASSERT_EQ(ShownMaterialRows(), "BottomPawn");

        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        _manager->UpdateAll();
        Settle();

        EXPECT_TRUE(Opponent().IsWatching());
        EXPECT_FALSE(Opponent().GetPlays().has_value());
        EXPECT_FALSE(BlackIsAtTheBottom());
        ExpectPiecesAtTheCellsOf(false);
        EXPECT_EQ(PieceOrigin("WhiteRook_a1"), (Pair<float, float>{100.0f, 640.0f}));
        EXPECT_EQ(Coordinates(true), "abcdefgh");
        EXPECT_EQ(Coordinates(false), "87654321");
        EXPECT_EQ(ShownMaterialRows(), "BottomPawn") << "White's captures stay in the lower group";
        EXPECT_EQ(MaterialSprite("Bottom", "Pawn"), "BlackPawn");
        EXPECT_EQ(Var("materialBottomBalance"), "+1");
        EXPECT_EQ(Var("materialTopBalance"), "");
    }

    TEST_F(ChessUiTest, WatchAClickAfterTheEndStartsANewGameAndTheProgramsOpenIt) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = MakeShared<GatedEngine>(true);
        const auto black = MakeShared<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kWatchWhiteIsMated);
        ASSERT_TRUE(Model().IsGameOver());
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        IdleFrames(10);
        EXPECT_EQ(white->Calls(), 0) << "nothing is asked for a finished game";
        EXPECT_EQ(black->Calls(), 0);
        const uint64_t version = Model().GetVersion();

        Click("e4");  // the game is over, so this is the "next game" click, also while two programs play

        EXPECT_FALSE(Model().IsGameOver());
        EXPECT_GT(Model().GetVersion(), version);
        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
        EXPECT_FALSE(Model().HasSelection());
        ASSERT_TRUE(FramesUntil([&] { return white->Calls() == 1; })) << "the new game opens with White's program";
        EXPECT_TRUE(Opponent().IsWatching()) << "the click starts the next game, not a person's game";

        Click("e2");
        EXPECT_FALSE(Model().HasSelection()) << "and the board is locked again";
    }

    TEST_F(ChessUiTest, WatchTheResetButtonStartsAgainAndTheBoardStaysLocked) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = MakeShared<GatedEngine>(true);
        const auto black = MakeShared<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        StartFrom(kWatchBlackToMove);
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return black->Calls() == 1; }));

        Press("ChessReset");
        _manager->UpdateAll();

        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
        EXPECT_TRUE(Opponent().IsWatching()) << "Reset keeps the opponent";
        ASSERT_TRUE(FramesUntil([&] { return white->Calls() == 1; })) << "and the program opens the new game";
        Click("e2");
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_EQ(factory.Calls(), 2) << "no new engine for a reset";
    }

    TEST_F(ChessUiTest, WatchTwoPlayersInThePanelGivesTheBoardBackToClicks) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = MakeShared<GatedEngine>(true);
        const auto black = MakeShared<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return white->Calls() == 1; }));
        Click("e2");
        ASSERT_FALSE(Model().HasSelection());

        PressWith(kSetModeAction, "two");
        _manager->UpdateAll();

        EXPECT_FALSE(Opponent().IsWatching());
        EXPECT_FALSE(Opponent().PlaysFor(PieceColor::White));
        EXPECT_FALSE(Opponent().PlaysFor(PieceColor::Black));
        EXPECT_FALSE(Opponent().IsThinking());
        EXPECT_TRUE(WaitUntil([&] { return white->WasCancelled(); })) << "the search of the program that left is cancelled";
        ClickMove("e2", "e4");
        EXPECT_EQ(Model().GetMoves().size(), 1u) << "both colors move by clicks again";
        ClickMove("e7", "e5");
        EXPECT_EQ(Model().GetMoves().size(), 2u);
    }

    TEST_F(ChessUiTest, WatchNobodyThroughTheControlApiGivesTheBoardBackToClicks) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true), MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ControlPlays("both");
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().IsWatching());
        Click("e2");
        ASSERT_FALSE(Model().HasSelection());

        ControlPlays(nullptr);
        _manager->UpdateAll();

        EXPECT_FALSE(Opponent().IsWatching());
        Click("e2");
        EXPECT_TRUE(Model().HasSelection());
    }

    TEST_F(ChessUiTest, WatchATakeoverLeavesTheClicksToThePersonsColorOnly) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = MakeShared<GatedEngine>(true);
        const auto black = MakeShared<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return white->Calls() == 1; }));

        ASSERT_TRUE(Opponent().ChooseSeats(ProgramSides{true, false}, {}, {}).has_value());  // the person takes Black over
        _manager->UpdateAll();
        EXPECT_FALSE(Opponent().IsWatching());
        EXPECT_EQ(Opponent().GetPlays(), Optional<PieceColor>(PieceColor::White));
        EXPECT_TRUE(BlackIsAtTheBottom()) << "one program: the board is turned for the person";
        EXPECT_FALSE(white->WasCancelled()) << "White's seat is unchanged: its search stays";

        Click("e2");
        EXPECT_FALSE(Model().HasSelection()) << "White's turn is the program's";
        white->Open();
        ASSERT_TRUE(FramesUntil([&] { return Model().GetMoves().size() == 1; }));
        EXPECT_EQ(Model().GetTurn(), PieceColor::Black);

        Click("a2");
        EXPECT_FALSE(Model().HasSelection()) << "a white piece is not the person's";
        Click("e7");
        EXPECT_TRUE(Model().HasSelection()) << "Black's turn is the person's";
        EXPECT_EQ(black->Calls(), 0) << "Black's program left, and nothing asked it";
    }

    TEST_F(ChessUiTest, WatchAnErrorOfOneSeatDoesNotUnlockTheBoard) {
        const auto white = MakeShared<FailingEngine>(EngineError{EngineErrorKind::LaunchFailed, "no engine in the test"});
        const auto black = MakeShared<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return Opponent().GetError().has_value(); })) << "White's program failed";
        const uint64_t version = Model().GetVersion();

        Click("e2");
        Click("e4");
        IdleFrames(20);

        EXPECT_TRUE(Opponent().GetError().has_value()) << "the error stands until the game moves";
        EXPECT_TRUE(Opponent().PlaysFor(PieceColor::White));
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetVersion(), version);
    }

    // ---- the drag --------------------------------------------------------------------------------------------------------------
    // A press acts as a click and arms a drag; the release over a legal target plays the move through the path of a second click, a release anywhere else calls nothing on the
    // model (the piece goes back and stays selected). Pointer events are dispatched through UIInput, like the window does, so the canvases get them first

    TEST_F(ChessUiTest, ADragPlaysWhatTwoClicksPlay) {
        const uint64_t start = Model().GetVersion();
        ClickMove("e2", "e4");
        const String byClicks = Model().GetPosition().ToFen();
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        const String clickedMove = Model().GetMoves().back().ToUci();
        EXPECT_EQ(Model().GetVersion(), start + 1);

        Model().Reset();
        const uint64_t again = Model().GetVersion();
        Drag("e2", "e4");

        EXPECT_EQ(Model().GetPosition().ToFen(), byClicks);
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), clickedMove);
        EXPECT_EQ(Model().GetVersion(), again + 1);
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_EQ(Model().GetTurn(), PieceColor::Black);
        EXPECT_FALSE(Lifted().has_value());
    }

    TEST_F(ChessUiTest, ADragToTheStartSquareAndAReleaseWithoutMotionPlayNothingAndKeepTheSelection) {
        const uint64_t version = Model().GetVersion();

        Drag("e2", "e2");
        EXPECT_TRUE(Model().GetMoves().empty());
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("e2"));
        EXPECT_EQ(Model().GetLegalMoves().size(), 2u);

        Model().ClearSelection();
        Click("e2");
        ReleaseOn("e2");
        EXPECT_TRUE(Model().GetMoves().empty());
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("e2"));
        EXPECT_EQ(Model().GetLegalMoves().size(), 2u);
        EXPECT_EQ(Model().GetVersion(), version);
    }

    TEST_F(ChessUiTest, ADragOffTheBoardPlaysNothingKeepsTheSelectionAndAClickFinishesTheMove) {
        Draw();
        const uint64_t version = Model().GetVersion();
        const Pair<float, float> places[] = {
            PixelOf(50.0f, 400.0f),   // the left margin, where the material chips are
            PixelOf(420.0f, 770.0f),  // under the board
            PixelOf(420.0f, 40.0f),   // above it
            PixelOf(800.0f, 400.0f),  // the right margin
            {5.0f, 5.0f},             // outside the view altogether
        };
        for (const auto& place : places) {
            SCOPED_TRACE(place.first);
            DragBetween(SquareCenter("e2"), place);
            EXPECT_TRUE(Model().GetMoves().empty());
            EXPECT_EQ(Model().GetVersion(), version);
            ASSERT_TRUE(Model().HasSelection());
            EXPECT_EQ(Model().GetSelected(), Sq("e2"));
            EXPECT_EQ(Model().GetLegalMoves().size(), 2u);
            EXPECT_FALSE(Lifted().has_value());
        }

        Click("e4");
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "e2e4");
    }

    TEST_F(ChessUiTest, ADragToASquareThatIsNotATargetOrOntoAnotherOwnPieceKeepsTheSelectionOnTheStartSquare) {
        const uint64_t version = Model().GetVersion();
        for (const char* to : {"e5", "d2", "b1", "e7", "h8"}) {
            SCOPED_TRACE(to);
            Drag("e2", to);
            EXPECT_TRUE(Model().GetMoves().empty());
            EXPECT_EQ(Model().GetVersion(), version);
            ASSERT_TRUE(Model().HasSelection());
            EXPECT_EQ(Model().GetSelected(), Sq("e2")) << "the release did not select the piece it landed on";
            EXPECT_EQ(Model().GetLegalMoves().size(), 2u);
        }
    }

    TEST_F(ChessUiTest, ADragCapturesLikeTwoClicks) {
        Drag("e2", "e4");
        Drag("d7", "d5");
        Drag("e4", "d5");

        ASSERT_EQ(Model().GetMoves().size(), 3u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "e4d5");
        EXPECT_EQ(Model().GetPieceAt(Sq("d5")).type, PieceType::Pawn);
        EXPECT_EQ(Model().GetPieceAt(Sq("d5")).color, PieceColor::White);
        EXPECT_TRUE(Model().GetPieceAt(Sq("e4")).IsEmpty());
    }

    TEST_F(ChessUiTest, APressOnAnotherPieceOfTheSideToMoveArmsTheDragOfThatPieceAndOtherPressesArmNothing) {
        Click("d2");
        ASSERT_EQ(Model().GetSelected(), Sq("d2"));
        Draw();
        const auto e2 = SquareCenter("e2");
        const auto e4 = SquareCenter("e4");
        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(e2.first, e2.second, true));
        ASSERT_EQ(Model().GetSelected(), Sq("e2"));
        MouseMove(e2.first + kDragStepPixels, e2.second, true);
        const auto lifted = Lifted();
        ASSERT_TRUE(lifted.has_value());
        EXPECT_EQ(lifted->from, Sq("e2"));
        Release(e4.first, e4.second);
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "e2e4");

        Model().Reset();
        // A piece of the side that is not to move, an empty square and (with a piece selected) an enemy piece arm nothing
        Drag("e7", "e5");
        Drag("e4", "e5");
        Click("e2");
        Drag("e7", "e4");
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_FALSE(Model().HasSelection()) << "the press on the enemy piece dropped the selection and the release did not make a move";
    }

    TEST_F(ChessUiTest, TheProgramsTurnTakesNoDragAndDoesNotCancelItsSearch) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto engine = MakeShared<GatedEngine>(true);
        CountingFactory factory({engine});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().SetSide(PieceColor::Black).has_value());
        Drag("e2", "e4");
        ASSERT_TRUE(FramesUntil([&] { return engine->Calls() == 1; })) << "Black's program has the search and waits at its gate";
        const uint64_t version = Model().GetVersion();

        Drag("e7", "e5");
        Drag("d7", "d5");
        Drag("g8", "f6");
        IdleFrames(5);

        EXPECT_FALSE(Model().HasSelection());
        EXPECT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_TRUE(Opponent().IsThinking());
        EXPECT_FALSE(engine->WasCancelled());
        EXPECT_EQ(engine->Calls(), 1);
        EXPECT_FALSE(Lifted().has_value());
    }

    TEST_F(ChessUiTest, WatchTheBoardTakesNoDragOnEitherTurn) {
        SKIP_WITHOUT_WORKER_THREADS();
        const auto white = MakeShared<GatedEngine>(true);
        const auto black = MakeShared<GatedEngine>(true);
        CountingFactory factory({white, black});
        Opponent().SetEngineFactory(factory.AsFactory());
        _manager->UpdateAll();
        ASSERT_TRUE(Opponent().ChooseSeats(kWatchBoth, {}, {}).has_value());
        ASSERT_TRUE(FramesUntil([&] { return white->Calls() == 1; }));
        const uint64_t version = Model().GetVersion();

        Drag("e2", "e4");
        Drag("d2", "d4");
        IdleFrames(5);
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_FALSE(white->WasCancelled());

        StartFrom(kWatchBlackToMove);
        ASSERT_TRUE(FramesUntil([&] { return black->Calls() == 1; }));
        const uint64_t blackVersion = Model().GetVersion();
        Drag("e7", "e5");
        Drag("d7", "d6");
        IdleFrames(5);
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetVersion(), blackVersion);
        EXPECT_FALSE(black->WasCancelled());
    }

    TEST_F(ChessUiTest, ThePressAfterTheGameEndedStartsANewGameAndItsReleasePlaysNothing) {
        StartFrom(kWatchWhiteIsMated);
        ASSERT_TRUE(Model().IsGameOver());

        Drag("e2", "e4");

        EXPECT_FALSE(Model().IsGameOver());
        EXPECT_EQ(Model().GetPosition().ToFen(), String(Position::kStartFen));
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_FALSE(Model().HasSelection());
        EXPECT_FALSE(Lifted().has_value());
    }

    TEST_F(ChessUiTest, WhileAPanelIsShownADragOnTheBoardArmsNothing) {
        ASSERT_TRUE(OpenThePanel());
        Drag("e2", "e4");
        EXPECT_FALSE(Model().HasSelection()) << "the opponent panel took the press";
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_FALSE(Lifted().has_value());
        Press("ChessCloseMenu");
        ASSERT_FALSE(Panel()->_visible);

        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());
        const uint64_t version = Model().GetVersion();
        Drag("a7", "a8");
        Drag("a7", "a6");
        EXPECT_TRUE(PromotionShown());
        EXPECT_TRUE(Model().HasPendingPromotion());
        EXPECT_EQ(Model().GetSelected(), Sq("a7"));
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_FALSE(Lifted().has_value());
    }

    TEST_F(ChessUiTest, ADragToTheLastRankOpensThePromotionPanelAndMovesNothingUntilAPieceIsPicked) {
        StartFrom(kPromotionFen);
        const uint64_t version = Model().GetVersion();
        const String fen = Model().GetPosition().ToFen();

        Drag("a7", "a8");

        EXPECT_TRUE(Model().HasPendingPromotion());
        EXPECT_TRUE(PromotionShown());
        EXPECT_EQ(Model().GetPosition().ToFen(), fen) << "nothing has moved";
        EXPECT_EQ(Model().GetVersion(), version);
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("a7"));

        ASSERT_TRUE(ClickPromotion("PromotionCancel").Both());
        EXPECT_FALSE(PromotionShown());
        EXPECT_TRUE(Model().HasSelection()) << "Cancel keeps the pawn selected";
        EXPECT_EQ(Model().GetSelected(), Sq("a7"));

        Drag("a7", "a8");
        EXPECT_TRUE(PromotionShown()) << "a second drag asks again";
        ASSERT_TRUE(ClickPromotion("PromoteQueen").Both());
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "a7a8q");
    }

    TEST_F(ChessUiTest, ADragInTheTurnedBoardGoesByTheDrawnCells) {
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        ControlPlays("white");
        StartFrom(kBlackToMoveAfterE4);
        ASSERT_TRUE(BlackIsAtTheBottom());
        const uint64_t version = Model().GetVersion();

        // Where e5 would be with White at the bottom (fifth column, fourth row from the top) is d4 on this board, not a target of the pawn on e7
        DragCells(3, 6, 4, 3);
        EXPECT_TRUE(Model().GetMoves().empty());
        EXPECT_EQ(Model().GetVersion(), version);
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("e7"));

        DragCells(3, 6, 3, 4);  // e7 to e5 as it is drawn
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "e7e5");
    }

    TEST_F(ChessUiTest, ADragThatWentStaleBetweenPressAndReleaseIsDropped) {
        Draw();
        const auto e2 = SquareCenter("e2");
        const auto e4 = SquareCenter("e4");
        const auto press = [&] {
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(e2.first, e2.second, true));
            MouseMove(e2.first + kDragStepPixels, e2.second, true);
            EXPECT_TRUE(Lifted().has_value());
        };

        // After each change the piece is selected again by hand, so only the check of the drag (not the selection) can keep the release from playing e2e4
        press();
        Model().SetPosition(PositionFromFen(Position::kStartFen));
        Model().OnSquareClicked(Sq("e2"));
        EXPECT_FALSE(Lifted().has_value()) << "SetPosition";
        Release(e4.first, e4.second);
        EXPECT_TRUE(Model().GetMoves().empty());

        press();
        Model().Reset();
        Model().OnSquareClicked(Sq("e2"));
        EXPECT_FALSE(Lifted().has_value()) << "Reset";
        Release(e4.first, e4.second);
        EXPECT_TRUE(Model().GetMoves().empty());

        Model().Reset();
        press();
        ASSERT_TRUE(Play(Model(), "g1f3"));  // the control API plays a move of its own
        EXPECT_FALSE(Lifted().has_value()) << "a move of someone else";
        Release(e4.first, e4.second);
        EXPECT_EQ(Model().GetMoves().size(), 1u);
    }

    TEST_F(ChessUiTest, ALostReleaseIsCleanedUpByTheNextMotionWithoutTheButton) {
        Draw();
        const auto e2 = SquareCenter("e2");
        const auto reset = ScreenCenter(*HudElement("Reset"));
        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(e2.first, e2.second, true));
        MouseMove(e2.first + kDragStepPixels, e2.second, true);
        MouseMove(reset.first, reset.second, true);
        EXPECT_TRUE(Lifted().has_value()) << "the button is still held";
        // The release over the Reset button is taken by the HUD: the board gets nothing. The next motion has no button held
        Release(reset.first, reset.second);
        EXPECT_TRUE(Lifted().has_value()) << "the release never reached the board";
        const auto board = SquareCenter("e3");
        MouseMove(board.first, board.second, false);
        EXPECT_FALSE(Lifted().has_value());

        ReleaseOn("e4");
        EXPECT_TRUE(Model().GetMoves().empty());
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("e2"));
    }

    TEST_F(ChessUiTest, TheLiftedPieceIsTheSquareItCameFromAndTheTopLeftIsThePointerMinusHalfASquare) {
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        for (const bool turned : {false, true}) {
            SCOPED_TRACE(turned ? "turned" : "White at the bottom");
            ControlPlays(turned ? "white" : nullptr);
            StartFrom(turned ? kBlackToMoveAfterE4 : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
            ASSERT_EQ(BlackIsAtTheBottom(), turned);
            const char* piece = turned ? "e7" : "e2";
            Draw();
            const auto at = SquareCenter(piece);

            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.first, at.second, true));
            EXPECT_FALSE(Lifted().has_value()) << "right after the press";
            MouseMove(at.first + kUnderThresholdPixels, at.second, true);
            EXPECT_FALSE(Lifted().has_value()) << "under the threshold";

            const float pointerX = at.first + kPointerOffsetX;
            const float pointerY = at.second + kPointerOffsetY;
            MouseMove(pointerX, pointerY, true);
            const auto lifted = Lifted();
            ASSERT_TRUE(lifted.has_value());
            EXPECT_EQ(lifted->from, Sq(piece));
            EXPECT_NEAR(lifted->x, GetScene().ScreenToSceneX(pointerX) - kHalfSquare, kPixelTolerance);
            EXPECT_NEAR(lifted->y, GetScene().ScreenToSceneY(pointerY) - kHalfSquare, kPixelTolerance);

            MouseMove(at.first + 1.0f, at.second, true);
            EXPECT_TRUE(Lifted().has_value()) << "once lifted the piece stays lifted";
            Release(pointerX, pointerY);
            EXPECT_FALSE(Lifted().has_value()) << "after the release";
        }
    }

    TEST_F(ChessUiTest, TwoClicksStillPlayAndAClickWithItsReleaseLeavesThePieceSelected) {
        ClickMove("e2", "e4");
        ASSERT_EQ(Model().GetMoves().size(), 1u);
        EXPECT_FALSE(Model().HasSelection());

        // A click as a mouse makes it: the press, then the release at the same place
        Click("e7");
        ReleaseOn("e7");
        ASSERT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("e7"));
        Click("e5");
        ReleaseOn("e5");
        ASSERT_EQ(Model().GetMoves().size(), 2u);
        EXPECT_EQ(Model().GetMoves().back().ToUci(), "e7e5");
        EXPECT_FALSE(Model().HasSelection());
    }

    TEST_F(ChessUiTest, ADraggedPieceIsCenteredUnderThePointerOnTheDragLayerAndGoesBackOrLandsOnTheTargetOnLetGo) {
        SKIP_WITHOUT_WORKER_THREADS();
        CountingFactory factory({MakeShared<GatedEngine>(true)});
        Opponent().SetEngineFactory(factory.AsFactory());
        for (const bool turned : {false, true}) {
            SCOPED_TRACE(turned ? "turned" : "White at the bottom");
            ControlPlays(turned ? "white" : nullptr);
            StartFrom(turned ? kBlackToMoveAfterE4 : "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
            ASSERT_EQ(BlackIsAtTheBottom(), turned);
            const char* piece = turned ? "e7" : "e2";
            const char* target = turned ? "e5" : "e4";
            const char* node = turned ? "BlackPawn_e7" : "WhitePawn_e2";
            const auto [homeColumn, homeRow] = CellOfSquare(Sq(piece), turned);
            const auto [targetColumn, targetRow] = CellOfSquare(Sq(target), turned);
            const auto home = CellOrigin(homeColumn, homeRow);
            const auto landing = CellOrigin(targetColumn, targetRow);
            ExpectPiecesAtTheCellsOf(turned);

            // In the hand: the transform is the pointer minus half a square, the node is on the drag layer and no other piece is
            Draw();
            const auto at = SquareCenter(piece);
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.first, at.second, true));
            _manager->UpdateAll();
            EXPECT_EQ(PieceOrigin(node), home) << "just pressed: not lifted yet";
            EXPECT_EQ(NodeLayer(node), kPieceLayer);
            const float pointerX = at.first + kPointerOffsetX;
            const float pointerY = at.second + kPointerOffsetY;
            MouseMove(pointerX, pointerY, true);
            _manager->UpdateAll();
            EXPECT_NEAR(PieceOrigin(node).first, GetScene().ScreenToSceneX(pointerX) - kHalfSquare, kPixelTolerance);
            EXPECT_NEAR(PieceOrigin(node).second, GetScene().ScreenToSceneY(pointerY) - kHalfSquare, kPixelTolerance);
            EXPECT_EQ(NodeLayer(node), kDragLayer);
            size_t others = 0;
            for (const auto& child : GetScene().GetRootNode().GetChildren()) {
                if (child->GetComponent<ChessPieceComponent>() && child->GetName() != PoolString::Intern(node)) {
                    EXPECT_EQ(child->GetLayer(), kPieceLayer) << child->GetName().CStr();
                    ++others;
                }
            }
            EXPECT_EQ(others, kPieceCount - 1);
            for (const auto& child : GetScene().GetRootNode().GetChildren()) {
                if (child->GetName() == PoolString::Intern(node)) {
                    EXPECT_FLOAT_EQ(child->GetComponent<TransformComponent>()->_width, 2.0f * kHalfSquare) << "the size of a square";
                }
            }

            // Let go off the board, in the margin: the piece is back on its cell and its layer, still selected
            Release(PixelOf(50.0f, 400.0f).first, PixelOf(50.0f, 400.0f).second);
            _manager->UpdateAll();
            EXPECT_EQ(PieceOrigin(node), home) << "off the board";
            EXPECT_EQ(NodeLayer(node), kPieceLayer);
            EXPECT_TRUE(Model().HasSelection());

            // Dragged onto its own square and let go there: back as well
            Drag(piece, piece);
            _manager->UpdateAll();
            EXPECT_EQ(PieceOrigin(node), home) << "the start square";
            EXPECT_EQ(NodeLayer(node), kPieceLayer);

            // Let go on the target: on the target cell at once, on the piece layer
            Click(piece);
            MouseMove(at.first + kDragStepPixels, at.second, true);
            _manager->UpdateAll();
            EXPECT_EQ(NodeLayer(node), kDragLayer);
            const auto over = SquareCenter(target);
            MouseMove(over.first, over.second, true);
            Release(over.first, over.second);
            _manager->UpdateAll();
            ASSERT_EQ(Model().GetMoves().size(), 1u);
            EXPECT_EQ(PieceOrigin(node), landing) << "dropped on the target";
            EXPECT_EQ(NodeLayer(node), kPieceLayer);
            ExpectPiecesAtTheCellsOf(turned);
        }
    }

    TEST_F(ChessUiTest, ADraggedPieceIsDrawnOverThePiecesAndTheChipsAndUnderTheHud) {
        const auto pawn = GiveAtlasCell("WhitePawn_b2");
        const auto king = GiveAtlasCell("WhiteKing_e1");
        _manager->UpdateAll();                                      // the pieces register, so that the handles follow the moves
        for (const char* uci : {"e2e4", "d7d5", "e4d5", "d8d5"}) {  // a pawn each is gone: there are chips to draw over, and White is to move
            ASSERT_TRUE(Play(Model(), uci)) << uci;
        }
        _manager->UpdateAll();
        Settle();
        ASSERT_NE(ShownMaterialRows(), "");

        const auto indexes = [&] {
            struct Found {
                long pawn = -1;
                long king = -1;
                long firstChip = -1;
                long lastChip = -1;
                long resetButton = -1;
            } found;
            for (size_t i = 0; i < _renderer->calls.size(); ++i) {
                const DrawCall& call = _renderer->calls[i];
                const long at = static_cast<long>(i);
                if (call.kind == DrawKind::Texture && call.texture == pawn.Get()) {
                    found.pawn = at;
                } else if (call.kind == DrawKind::Texture && call.texture == king.Get()) {
                    found.king = at;
                } else if (call.kind == DrawKind::Rect && SameColor(call.color, kMaterialChipColor)) {
                    found.firstChip = found.firstChip < 0 ? at : found.firstChip;
                    found.lastChip = at;
                } else if (call.kind == DrawKind::Rect && SameColor(call.color, kResetColor)) {
                    found.resetButton = at;
                }
            }
            return found;
        };

        Draw();
        auto resting = indexes();
        ASSERT_GE(resting.pawn, 0);
        ASSERT_GE(resting.king, 0);
        ASSERT_GE(resting.firstChip, 0);
        EXPECT_LT(resting.pawn, resting.firstChip) << "a piece on the board is under the chips";

        Click("b2");
        const auto at = SquareCenter("b2");
        MouseMove(at.first + kDragStepPixels, at.second, true);
        ASSERT_TRUE(Lifted().has_value());
        _manager->UpdateAll();
        Draw();
        const auto lifted = indexes();
        ASSERT_GE(lifted.pawn, 0);
        ASSERT_GE(lifted.king, 0);
        ASSERT_GE(lifted.lastChip, 0);
        ASSERT_GE(lifted.resetButton, 0);
        EXPECT_GT(lifted.pawn, lifted.king) << "over the other pieces";
        EXPECT_GT(lifted.pawn, lifted.lastChip) << "over the captured material";
        EXPECT_LT(lifted.pawn, lifted.resetButton) << "under the HUD";

        // The input order of the canvases follows the draw order, which the piece on its higher layer must not shuffle among them: the topmost is asked first
        const auto rankOf = [&](const char* name) {
            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(name)) {
                    return GetScene().GetDrawRank(*node);
                }
            }
            ADD_FAILURE() << name;
            return -1;
        };
        EXPECT_LT(rankOf("BoardCoordinates"), rankOf("Material"));
        EXPECT_LT(rankOf("Material"), rankOf("Hud"));
        EXPECT_LT(rankOf("Hud"), rankOf("MenuPanel"));
        EXPECT_LT(rankOf("MenuPanel"), rankOf("OpponentPanel"));
        EXPECT_LT(rankOf("OpponentPanel"), rankOf("PromotionPanel"));

        Release(at.first, at.second);
        _manager->UpdateAll();
        Draw();
        resting = indexes();
        EXPECT_LT(resting.pawn, resting.firstChip) << "let go: under the chips again";
    }

    TEST_F(ChessUiTest, AGameThatMovesUnderTheHandPutsThePieceBackAtOnce) {
        _manager->UpdateAll();
        Draw();
        const auto at = SquareCenter("e2");
        const auto home = PieceOrigin("WhitePawn_e2");
        const auto lift = [&] {
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.first, at.second, true));
            MouseMove(at.first + kDragStepPixels, at.second, true);
            _manager->UpdateAll();
            ASSERT_EQ(NodeLayer("WhitePawn_e2"), kDragLayer);
            ASSERT_NE(PieceOrigin("WhitePawn_e2"), home);
        };

        lift();
        Model().Reset();
        _manager->UpdateAll();
        EXPECT_FALSE(Lifted().has_value());
        EXPECT_EQ(PieceOrigin("WhitePawn_e2"), home) << "a reset";
        EXPECT_EQ(NodeLayer("WhitePawn_e2"), kPieceLayer);

        lift();
        ASSERT_TRUE(Play(Model(), "g1f3"));  // a move of the control API
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhitePawn_e2"), home) << "a move of someone else";
        EXPECT_EQ(NodeLayer("WhitePawn_e2"), kPieceLayer);

        Model().Reset();
        lift();
        Model().SetPosition(PositionFromFen(Position::kStartFen));
        _manager->UpdateAll();
        EXPECT_EQ(PieceOrigin("WhitePawn_e2"), home) << "a new position";
        EXPECT_EQ(NodeLayer("WhitePawn_e2"), kPieceLayer);
    }

}  // namespace BECore::Chess::Tests
