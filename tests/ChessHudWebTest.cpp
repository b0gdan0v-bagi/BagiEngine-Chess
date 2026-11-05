#include "HudWords.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIScreenContent.h>
#include <BECore/UI/UISegmented.h>
#include <BECore/UI/UIWebCompiler.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/UILayoutMatrix.h>
#include <Common/UITreeCompare.h>
#include <gtest/gtest.h>
#include <memory>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 0.01f;
        constexpr float kViewWidth = 840.0f;   ///< viewWidth of ChessScene
        constexpr float kViewHeight = 800.0f;  ///< viewHeight of ChessScene

        // The HUD of Chess written in HTML + CSS, the authoring format of the engine's web front-end (UIWebCompiler). The test below checks
        // that it lays out like the HUD in config/SceneConfig.xml, the one the game ships.
        constexpr const char* kChessHudHtml = R"(
<span id="Turn">{turn}</span>
<button id="Opponent" data-action="ChessOpenOpponent">{side}</button>
<button id="Reset" data-action="ChessReset">Reset</button>
<span id="Status">{status}</span>)";

        constexpr const char* kChessHudCss = R"(
#Turn   { left: 12px; top: 12px; width: 320px; height: 56px; font-size: 28px; vertical-align: middle; }
#Opponent { anchor: top-right; right: 134px; top: 12px; width: 190px; height: 56px; font-size: 24px; background: rgb(45, 75, 120); }
#Reset  { anchor: top-right; right: 12px; top: 12px; width: 110px; height: 56px; font-size: 26px; background: rgb(40, 90, 60); }
#Status { anchor: bottom-right; right: 12px; bottom: 12px; width: 48%; height: 56px; font-size: 28px; text-align: right; vertical-align: middle; }
)";

        // The plate of the opponent panel of Chess written in HTML + CSS with the engine's choice-group form (a div with data-widget="segmented" is a UISegmented, data-bind names the
        // variable, data-value the id of a segment, :checked the look of the selected one). The test below checks that it lays out like the plate in config/SceneConfig.xml. The dim
        // behind the plate is not written here: it is a full-screen element of the XML that the web form has no use for.
        constexpr const char* kChessPanelHtml = R"(
<div id="Plate">
  <span id="Title">Opponent</span>
  <div id="ModeRow" class="row">
    <span id="ModeCaption" class="caption">Opponent</span>
    <div id="ModeGroup" class="group" data-widget="segmented" data-bind="opponentMode">
      <button id="ModeTwo" data-action="ChessSetMode" data-value="two">Two players</button>
      <button id="ModeComputer" data-action="ChessSetMode" data-value="computer">Computer</button>
    </div>
  </div>
  <div id="ColorRow" class="row">
    <span id="ColorCaption" class="caption">Play as</span>
    <div id="ColorGroup" class="group" data-widget="segmented" data-bind="opponentColor">
      <button id="ColorWhite" data-action="ChessSetColor" data-value="white">White</button>
      <button id="ColorBlack" data-action="ChessSetColor" data-value="black">Black</button>
    </div>
  </div>
  <div id="EngineRow" class="row">
    <span id="EngineCaption" class="caption">Engine</span>
    <div id="EngineGroup" class="group" data-widget="segmented" data-bind="opponentEngine">
      <button id="EngineStockfish" data-action="ChessSetEngine" data-value="stockfish">Stockfish</button>
      <button id="EngineOwn" data-action="ChessSetEngine" data-value="own">Own engine</button>
    </div>
  </div>
  <div id="LevelRow" class="row">
    <span id="LevelCaption" class="caption">Level</span>
    <div id="LevelGroup" class="group" data-widget="segmented" data-bind="opponentLevel">
      <button id="LevelEasy" data-action="ChessSetLevel" data-value="easy">Easy</button>
      <button id="LevelMedium" data-action="ChessSetLevel" data-value="medium">Medium</button>
      <button id="LevelHard" data-action="ChessSetLevel" data-value="hard">Hard</button>
      <button id="LevelMax" data-action="ChessSetLevel" data-value="max">Max</button>
    </div>
  </div>
  <span id="OpponentError">{opponentError}</span>
  <button id="Ok" data-action="ChessCloseOpponent">OK</button>
</div>)";

        constexpr const char* kChessPanelCss = R"(
#Plate         { anchor: center; display: flex; flex-direction: column; align-items: stretch; gap: 12px; padding: 16px; width: 94%; max-width: 640px; height: 460px; background: rgb(36, 40, 52); }
#Title         { height: 44px; font-size: 30px; text-align: center; }
.row           { display: flex; flex-direction: row; align-items: stretch; gap: 8px; height: 56px; }
.caption       { width: 150px; height: 56px; font-size: 24px; vertical-align: middle; }
.group         { display: flex; flex-direction: row; gap: 4px; flex: 1; height: 56px; background: rgb(30, 30, 30); }
.group button  { flex: 1; height: 56px; font-size: 24px; background: rgb(70, 70, 100); }
#OpponentError { height: 32px; font-size: 22px; text-align: center; vertical-align: middle; color: rgb(240, 120, 120); }
#Ok            { height: 56px; font-size: 26px; background: rgb(50, 115, 85); }
)";

        constexpr const char* kPanelNames[] = {"Plate",        "Title",      "ModeRow",    "ModeCaption", "ModeGroup",     "ModeTwo",     "ModeComputer",    "ColorRow",  "ColorCaption",
                                               "ColorGroup",   "ColorWhite", "ColorBlack", "EngineRow",   "EngineCaption", "EngineGroup", "EngineStockfish", "EngineOwn", "LevelRow",
                                               "LevelCaption", "LevelGroup", "LevelEasy",  "LevelMedium", "LevelHard",     "LevelMax",    "OpponentError",   "Ok"};

        std::filesystem::path ShippedSceneConfig() {
            return std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "SceneConfig.xml";
        }

        /// The `<elements>` of the canvas on the Hud node of the shipped scene, as the content of a screen
        std::string ShippedHudXml() {
            pugi::xml_document document;
            if (!document.load_file(ShippedSceneConfig().string().c_str())) {
                return {};
            }
            pugi::xml_node hud = document.select_node("//node[@name='Hud']").node();
            pugi::xml_node elements = hud.select_node(".//component[@type='UICanvasComponent']/elements").node();
            if (!elements) {
                return {};
            }
            std::ostringstream out;
            out << "<ui>";
            elements.print(out, "", pugi::format_raw);
            out << "</ui>";
            return out.str();
        }

        /// The plate of the canvas on the OpponentPanel node of the shipped scene (without the dim), as the content of a screen, with ONE difference from the file: every
        /// UISegmented gets `blocksInput="true"`. The HTML front-end writes it on every element with a background (a filled box takes clicks in a web page), so the compiled
        /// group has it; the shipped file does without, because the plate behind the group blocks input already and the only place the group would take a click is the 4-unit
        /// gap between two segments, where the result is the same (the modal canvas takes it). The engine's own UIWebTest makes the same step for its panel (kPanelXml).
        std::string ShippedPanelXml() {
            pugi::xml_document document;
            if (!document.load_file(ShippedSceneConfig().string().c_str())) {
                return {};
            }
            pugi::xml_node panel = document.select_node("//node[@name='OpponentPanel']").node();
            pugi::xml_node plate = panel.select_node(".//component[@type='UICanvasComponent']/elements/element[@name='Plate']").node();
            if (!plate) {
                return {};
            }
            for (pugi::xpath_node group : plate.select_nodes(".//element[@type='UISegmented']")) {
                group.node().append_attribute("blocksInput") = "true";
            }
            std::ostringstream out;
            out << "<ui><elements>";
            plate.print(out, "", pugi::format_raw);
            out << "</elements></ui>";
            return out.str();
        }

        struct Compiled {
            eastl::string xml;
            UIWebReport report;
            UIScreenContent content;
            pugi::xml_document document;  ///< the compiled XML, for the comparison of attributes
            bool deserializeErrors = false;

            [[nodiscard]] UIElement* Element(const char* name) const {
                for (const auto& element : content.elements) {
                    if (auto* found = element->FindByName(PoolString::Intern(name))) {
                        return found;
                    }
                }
                return nullptr;
            }
        };

        std::unique_ptr<Compiled> Compile(const std::string& html, const std::string& css) {
            auto compiled = std::make_unique<Compiled>();
            const std::string source = "<style>" + css + "</style>" + html;
            compiled->xml = UIWebCompiler::Compile(eastl::string_view(source.data(), source.size()), {}, compiled->report);
            compiled->document.load_string(compiled->xml.c_str());
            XmlDeserializer deserializer;
            if (deserializer.LoadFromString(compiled->xml)) {
                compiled->content.Deserialize(deserializer);
                compiled->deserializeErrors = deserializer.HasErrors();
            }
            return compiled;
        }

        UIScreenContent ReadXml(const std::string& xml) {
            UIScreenContent content;
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
            content.Deserialize(deserializer);
            EXPECT_FALSE(deserializer.HasErrors());
            return content;
        }

        /// A canvas in the margins of the scene's view (the HUD), laid out for an output
        struct LaidOut {
            UICanvasLayout layout;
            explicit LaidOut(const UIScreenContent& content) {
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

            void At(float width, float height) {
                UILayoutContext context;
                context.outputWidth = width;
                context.outputHeight = height;
                context.sceneViewWidth = kViewWidth;
                context.sceneViewHeight = kViewHeight;
                layout.Update(context);
            }

            [[nodiscard]] const UIElement* Find(const char* name) const {
                return layout.root->FindByName(PoolString::Intern(name));
            }
        };

        void ExpectSameRect(const UIElement* expected, const UIElement* actual, const char* name) {
            ASSERT_NE(expected, nullptr) << name;
            ASSERT_NE(actual, nullptr) << name;
            EXPECT_NEAR(expected->rect.x, actual->rect.x, kTolerance) << name;
            EXPECT_NEAR(expected->rect.y, actual->rect.y, kTolerance) << name;
            EXPECT_NEAR(expected->rect.w, actual->rect.w, kTolerance) << name;
            EXPECT_NEAR(expected->rect.h, actual->rect.h, kTolerance) << name;
        }

        /// Every attribute of every `element` of @p expected is on the same element of @p actual with the same text, and the other way round: the two XML texts say the same.
        /// A `UIStack` is a `UIElement` with `layout="Stack"` (the compile writes the second) and a `Vertical` direction is the default and is not written; neither is compared.
        /// Same as the helper of the engine's UIWebTest, which is in an anonymous namespace of that file.
        void ExpectSameAttributes(pugi::xml_node expected, pugi::xml_node actual) {
            ASSERT_EQ(std::string(expected.attribute("name").as_string()), std::string(actual.attribute("name").as_string()));
            const std::string name = expected.attribute("name").as_string();
            const auto skipped = [](const pugi::xml_attribute& attribute, const pugi::xml_node& owner) {
                const std::string key = attribute.name();
                return key == "layout" || (key == "type" && std::string(attribute.as_string()) == "UIStack" && !owner.attribute("bind")) ||
                       (key == "direction" && std::string(attribute.as_string()) == "Vertical");
            };
            for (pugi::xml_attribute attribute : expected.attributes()) {
                if (!skipped(attribute, expected)) {
                    EXPECT_EQ(std::string(attribute.as_string()), std::string(actual.attribute(attribute.name()).as_string())) << name << "." << attribute.name();
                }
            }
            for (pugi::xml_attribute attribute : actual.attributes()) {
                if (!skipped(attribute, actual) && std::string(attribute.name()) != "type") {
                    EXPECT_FALSE(expected.attribute(attribute.name()).empty()) << name << " has the extra attribute " << attribute.name();
                }
            }
            pugi::xml_node a = actual.child("children").first_child();
            for (pugi::xml_node e : expected.child("children").children()) {
                ASSERT_TRUE(a) << name << " lacks a child " << e.attribute("name").as_string();
                ExpectSameAttributes(e, a);
                a = a.next_sibling();
            }
            EXPECT_FALSE(a) << name << " has more children than expected";
        }

    }  // namespace

    class ChessHudWebTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void TearDown() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetResourceManager().ClearCache();
        }
    };

    TEST_F(ChessHudWebTest, TheHudInHtmlLaysOutLikeTheShippedXml) {
        const auto compiled = Compile(kChessHudHtml, kChessHudCss);
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_FALSE(compiled->report.HasIssues()) << compiled->report.issues[0].message.c_str() << " / " << compiled->xml.c_str();

        const std::string shippedXml = ShippedHudXml();
        ASSERT_FALSE(shippedXml.empty()) << ShippedSceneConfig().string();
        const UIScreenContent shipped = ReadXml(shippedXml);
        LaidOut web(compiled->content);
        LaidOut xml(shipped);
        for (const auto& [width, height] : {std::pair{1280.0f, 720.0f}, {480.0f, 480.0f}, {720.0f, 1280.0f}, {2560.0f, 1080.0f}}) {
            web.At(width, height);
            xml.At(width, height);
            for (const char* name : {"Turn", "Opponent", "Reset", "Status"}) {
                SCOPED_TRACE(Format("{} at {}x{}", name, width, height).c_str());
                ExpectSameRect(xml.Find(name), web.Find(name), name);
            }
        }
    }

    TEST_F(ChessHudWebTest, TheHudInHtmlPassesTheSizeMatrixInTheMarginsOfTheView) {
        const auto compiled = Compile(kChessHudHtml, kChessHudCss);
        LaidOut web(compiled->content);
        // Every word the game can show (the tables of the game, not a copy of them): round i sets the word i of each variable
        const Chess::Tests::HudWords words = Chess::Tests::AllHudWords();
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        UILayoutContext base;
        base.sceneViewWidth = kViewWidth;
        base.sceneViewHeight = kViewHeight;
        for (size_t round = 0; round < words.Rounds(); ++round) {
            words.Publish(round);
            const auto failures = ValidateOnMatrix(web.layout, options, base);
            EXPECT_TRUE(failures.empty()) << "round " << round << ": " << DescribeMatrixFailures(failures).c_str();
        }
    }

    // ---- the opponent panel -----------------------------------------------------------------------------------------------

    TEST_F(ChessHudWebTest, ThePanelInHtmlBuildsTheElementsOfTheShippedXml) {
        const auto compiled = Compile(kChessPanelHtml, kChessPanelCss);
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        EXPECT_FALSE(compiled->report.HasIssues()) << compiled->report.issues[0].message.c_str() << " / " << compiled->xml.c_str();

        const std::string shippedXml = ShippedPanelXml();
        ASSERT_FALSE(shippedXml.empty()) << ShippedSceneConfig().string();
        pugi::xml_document reference;
        ASSERT_TRUE(reference.load_string(shippedXml.c_str()));
        ExpectSameAttributes(reference.child("ui").child("elements").child("element"), compiled->document.child("ui").child("elements").child("element"));

        const UIScreenContent shipped = ReadXml(shippedXml);
        ExpectSameElements(shipped.elements, compiled->content.elements);

        // The fields the tree comparison does not look at: a segmented div is a UISegmented bound to the variable, a segment keeps its id and its action
        const struct {
            const char* group;
            const char* bind;
            const char* action;
            std::vector<std::pair<const char*, const char*>> segments;
        } groups[] = {{"ModeGroup", "opponentMode", "ChessSetMode", {{"ModeTwo", "two"}, {"ModeComputer", "computer"}}},
                      {"ColorGroup", "opponentColor", "ChessSetColor", {{"ColorWhite", "white"}, {"ColorBlack", "black"}}},
                      {"EngineGroup", "opponentEngine", "ChessSetEngine", {{"EngineStockfish", "stockfish"}, {"EngineOwn", "own"}}},
                      {"LevelGroup", "opponentLevel", "ChessSetLevel", {{"LevelEasy", "easy"}, {"LevelMedium", "medium"}, {"LevelHard", "hard"}, {"LevelMax", "max"}}}};
        for (const auto& group : groups) {
            SCOPED_TRACE(group.group);
            const auto* segmented = dynamic_cast<UISegmented*>(compiled->Element(group.group));
            ASSERT_NE(segmented, nullptr) << "a data-widget=\"segmented\" div is a UISegmented";
            EXPECT_EQ(segmented->bind.ToStringView(), group.bind);
            for (const auto& [name, value] : group.segments) {
                const auto* button = dynamic_cast<UIButton*>(compiled->Element(name));
                ASSERT_NE(button, nullptr) << name;
                EXPECT_EQ(button->value.ToStringView(), value) << name;
                EXPECT_EQ(button->action.ToStringView(), group.action) << name;
            }
        }
    }

    TEST_F(ChessHudWebTest, ThePanelInHtmlLaysOutLikeTheShippedXml) {
        const auto compiled = Compile(kChessPanelHtml, kChessPanelCss);
        ASSERT_FALSE(compiled->deserializeErrors) << compiled->xml.c_str();
        const std::string shippedXml = ShippedPanelXml();
        ASSERT_FALSE(shippedXml.empty()) << ShippedSceneConfig().string();
        const UIScreenContent shipped = ReadXml(shippedXml);
        LaidOut web(compiled->content);
        LaidOut xml(shipped);
        for (const auto& [width, height] : {std::pair{1280.0f, 720.0f}, {640.0f, 360.0f}, {480.0f, 480.0f}, {720.0f, 1280.0f}, {2560.0f, 1080.0f}}) {
            web.At(width, height);
            xml.At(width, height);
            for (const char* name : kPanelNames) {
                SCOPED_TRACE(Format("{} at {}x{}", name, width, height).c_str());
                ExpectSameRect(xml.Find(name), web.Find(name), name);
            }
        }
        // The panel has the height the XML says, so the comparison above is not between two empty rects
        web.At(1280.0f, 720.0f);
        ASSERT_NE(web.Find("LevelGroup"), nullptr);
        EXPECT_GT(web.Find("LevelGroup")->rect.w, 100.0f);
        EXPECT_FLOAT_EQ(web.Find("LevelEasy")->rect.h, 56.0f);
    }

    TEST_F(ChessHudWebTest, ThePanelInHtmlFollowsTheVariablesOfTheGame) {
        const auto compiled = Compile(kChessPanelHtml, kChessPanelCss);
        auto* color = dynamic_cast<UISegmented*>(compiled->Element("ColorGroup"));
        auto* white = dynamic_cast<UIButton*>(compiled->Element("ColorWhite"));
        auto* black = dynamic_cast<UIButton*>(compiled->Element("ColorBlack"));
        ASSERT_TRUE(color != nullptr && white != nullptr && black != nullptr);
        CoreManager::GetUIVars().Set("opponentColor"_intern, "black");
        color->SyncChildren();
        EXPECT_FALSE(white->selected);
        EXPECT_TRUE(black->selected);
        EXPECT_EQ(color->GetSelectedValue(), "black");
    }

    TEST_F(ChessHudWebTest, ThePanelInHtmlPassesTheSizeMatrixWithEveryErrorSentence) {
        const auto compiled = Compile(kChessPanelHtml, kChessPanelCss);
        LaidOut web(compiled->content);
        const Chess::Tests::HudWords words = Chess::Tests::AllHudWords();
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        UILayoutContext base;
        base.sceneViewWidth = kViewWidth;
        base.sceneViewHeight = kViewHeight;
        for (size_t round = 0; round < words.Rounds(); ++round) {
            words.Publish(round);
            const auto failures = ValidateOnMatrix(web.layout, options, base);
            EXPECT_TRUE(failures.empty()) << "round " << round << ": " << DescribeMatrixFailures(failures).c_str();
        }
    }

}  // namespace BECore::Tests
