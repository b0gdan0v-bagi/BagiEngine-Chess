#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIScreenContent.h>
#include <BECore/UI/UIWebCompiler.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/UILayoutMatrix.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <string>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 0.01f;
        constexpr float kViewWidth = 840.0f;   ///< viewWidth of ChessScene
        constexpr float kViewHeight = 800.0f;  ///< viewHeight of ChessScene

        // The HUD of Chess written in HTML + CSS, the authoring format of the engine's web front-end (UIWebCompiler). The test below checks
        // that it lays out like the HUD in config/SceneConfig.xml, the one the game ships.
        constexpr const char* kChessHudHtml = R"(
<span id="Turn">{turn}</span>
<button id="Reset" data-action="ChessReset">Reset</button>
<span id="Status">{status}</span>)";

        constexpr const char* kChessHudCss = R"(
#Turn   { left: 100px; top: 12px; width: 420px; height: 56px; font-size: 32px; vertical-align: middle; }
#Reset  { anchor: top-right; right: 100px; top: 12px; width: 150px; height: 56px; font-size: 26px; background: rgb(40, 90, 60); }
#Status { anchor: bottom; bottom: 12px; width: 76%; height: 56px; font-size: 28px; text-align: center; vertical-align: middle; }
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
            for (const char* name : {"Turn", "Reset", "Status"}) {
                SCOPED_TRACE(Format("{} at {}x{}", name, width, height).c_str());
                ExpectSameRect(xml.Find(name), web.Find(name), name);
            }
        }
    }

    TEST_F(ChessHudWebTest, TheHudInHtmlPassesTheSizeMatrixInTheMarginsOfTheView) {
        const auto compiled = Compile(kChessHudHtml, kChessHudCss);
        LaidOut web(compiled->content);
        // The longest words the game can show
        CoreManager::GetUIVars().Set("turn"_intern, "White to move");
        CoreManager::GetUIVars().Set("status"_intern, "Draw: threefold repetition");
        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        UILayoutContext base;
        base.sceneViewWidth = kViewWidth;
        base.sceneViewHeight = kViewHeight;
        const auto failures = ValidateOnMatrix(web.layout, options, base);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

}  // namespace BECore::Tests
