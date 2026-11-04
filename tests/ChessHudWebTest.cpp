#include "HudWords.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIScreenContent.h>
#include <BECore/UI/UIWebCompiler.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/UILayoutMatrix.h>
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
<button id="Level" data-action="ChessCycleLevel">{level}</button>
<button id="Reset" data-action="ChessReset">Reset</button>
<button id="Side" data-action="ChessCycleSide">{side}</button>
<span id="Status">{status}</span>)";

        constexpr const char* kChessHudCss = R"(
#Turn   { left: 12px; top: 12px; width: 320px; height: 56px; font-size: 28px; vertical-align: middle; }
#Level  { anchor: top-right; right: 134px; top: 12px; width: 190px; height: 56px; font-size: 24px; background: rgb(45, 75, 120); }
#Reset  { anchor: top-right; right: 12px; top: 12px; width: 110px; height: 56px; font-size: 26px; background: rgb(40, 90, 60); }
#Side   { anchor: bottom-left; left: 12px; bottom: 12px; width: 170px; height: 56px; font-size: 24px; background: rgb(110, 75, 40); }
#Status { anchor: bottom-right; right: 12px; bottom: 12px; width: 60%; height: 56px; font-size: 28px; text-align: right; vertical-align: middle; }
)";

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

        struct Compiled {
            eastl::string xml;
            UIWebReport report;
            UIScreenContent content;
            bool deserializeErrors = false;
        };

        std::unique_ptr<Compiled> Compile(const std::string& html, const std::string& css) {
            auto compiled = std::make_unique<Compiled>();
            const std::string source = "<style>" + css + "</style>" + html;
            compiled->xml = UIWebCompiler::Compile(eastl::string_view(source.data(), source.size()), {}, compiled->report);
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
            for (const char* name : {"Turn", "Level", "Reset", "Side", "Status"}) {
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

}  // namespace BECore::Tests
