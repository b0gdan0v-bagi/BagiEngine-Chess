#include <Common/Printers.h>
#include <gtest/gtest.h>

/// The web build's overlay (engine/web/config/) replaces a config file of the game whole, so a game file with the name of an overlay file
/// must say what the overlay says, or the browser plays another game. Plain files read from the source tree: no engine services, no scene
namespace BECore::Chess::Tests {

    namespace {

        /// The only attribute that Chess may differ in from the overlay's ApplicationConfig.xml
        constexpr const char* kRendererAttribute = "renderer";
        constexpr const char* kApplicationConfigName = "ApplicationConfig.xml";
        constexpr const char* kDesktopRenderer = "VulkanRendererBackend";
        constexpr const char* kWebRenderer = "SDLRendererBackend";

        /// Said in every failure: what the test guards and where it is written down
        constexpr const char* kWhy = "the web overlay replaces this file whole in the browser; see CLAUDE.md, 'The web build'";

        std::filesystem::path SourceRoot() {
            return std::filesystem::path(__FILE__).parent_path().parent_path();
        }

        std::filesystem::path GameConfigFolder() {
            return SourceRoot() / "config";
        }

        std::filesystem::path OverlayConfigFolder() {
            return SourceRoot() / "engine" / "web" / "config";
        }

        std::vector<std::filesystem::path> XmlFilesOf(const std::filesystem::path& folder) {
            std::vector<std::filesystem::path> files;
            for (const auto& entry : std::filesystem::directory_iterator(folder)) {
                if (entry.is_regular_file() && entry.path().extension() == ".xml") {
                    files.push_back(entry.path());
                }
            }
            std::sort(files.begin(), files.end());
            return files;
        }

        using Attributes = Map<String, String>;

        /// The attributes of the root element, name to value (empty when the file does not load)
        Attributes RootAttributes(const std::filesystem::path& file) {
            Attributes attributes;
            pugi::xml_document document;
            if (!document.load_file(file.string().c_str())) {
                return attributes;
            }
            for (const pugi::xml_attribute attribute : document.document_element().attributes()) {
                attributes[String(attribute.name())] = String(attribute.value());
            }
            return attributes;
        }

        /// One line per attribute that is missing on a side or has another value; the renderer is left out
        String DifferenceExceptRenderer(const Attributes& game, const Attributes& overlay) {
            Set<String> names;
            for (const auto& [name, value] : game) {
                names.insert(name);
            }
            for (const auto& [name, value] : overlay) {
                names.insert(name);
            }
            String difference;
            for (const String& name : names) {
                if (name == kRendererAttribute) {
                    continue;
                }
                const auto inGame = game.find(name);
                const auto inOverlay = overlay.find(name);
                const String gameValue = inGame != game.end() ? "\"" + inGame->second + "\"" : "(missing)";
                const String overlayValue = inOverlay != overlay.end() ? "\"" + inOverlay->second + "\"" : "(missing)";
                if (gameValue != overlayValue) {
                    difference += "  " + name + ": Chess " + gameValue + ", overlay " + overlayValue + "\n";
                }
            }
            return difference;
        }

    }  // namespace

    TEST(ChessWebConfigTest, TheOverlayIsFoundAndHasItsFiles) {
        // A moved overlay would make the other cases pass over an empty list
        ASSERT_TRUE(std::filesystem::is_directory(OverlayConfigFolder())) << OverlayConfigFolder().string();
        EXPECT_TRUE(std::filesystem::exists(OverlayConfigFolder() / kApplicationConfigName));
        EXPECT_FALSE(XmlFilesOf(OverlayConfigFolder()).empty());
        ASSERT_TRUE(std::filesystem::is_directory(GameConfigFolder())) << GameConfigFolder().string();
        EXPECT_TRUE(std::filesystem::exists(GameConfigFolder() / kApplicationConfigName));
    }

    TEST(ChessWebConfigTest, EveryOverlayFileIsAbsentFromChessOrIsTheApplicationConfigThatAgrees) {
        for (const std::filesystem::path& overlayFile : XmlFilesOf(OverlayConfigFolder())) {
            const std::string name = overlayFile.filename().string();
            const std::filesystem::path gameFile = GameConfigFolder() / name;
            if (!std::filesystem::exists(gameFile)) {
                continue;
            }
            // Chess has a file the overlay replaces: only ApplicationConfig.xml is known to say the same thing, apart from the renderer
            ASSERT_EQ(name, kApplicationConfigName) << "config/" << name << " has the name of a file of engine/web/config/: " << kWhy
                                                    << ". Remove it, or teach the engine a game's web overlay (needs engine change)";
            const Attributes game = RootAttributes(gameFile);
            const Attributes overlay = RootAttributes(overlayFile);
            ASSERT_FALSE(game.empty()) << gameFile.string();
            ASSERT_FALSE(overlay.empty()) << overlayFile.string();
            const String difference = DifferenceExceptRenderer(game, overlay);
            EXPECT_TRUE(difference.empty()) << "config/" << name << " differs from engine/web/config/" << name << " in the root attributes:\n" << difference.c_str() << kWhy;
        }
    }

    TEST(ChessWebConfigTest, TheRendererIsTheOneDifferenceAndItIsTheExpectedOne) {
        // Asserted too, so that a Chess config that drifts to the SDL renderer (or an overlay that changes it) is noticed
        const Attributes game = RootAttributes(GameConfigFolder() / kApplicationConfigName);
        const Attributes overlay = RootAttributes(OverlayConfigFolder() / kApplicationConfigName);
        ASSERT_TRUE(game.contains(kRendererAttribute));
        ASSERT_TRUE(overlay.contains(kRendererAttribute));
        EXPECT_EQ(game.at(kRendererAttribute), String(kDesktopRenderer)) << kWhy;
        EXPECT_EQ(overlay.at(kRendererAttribute), String(kWebRenderer)) << kWhy;
    }

    TEST(ChessWebConfigTest, TheComparisonSeesADifferentAttribute) {
        // The guard on the guard: the helper reports an added, a missing and a changed attribute, and nothing for the renderer alone
        const Attributes overlay{{"type", "A"}, {"mode", "Game"}, {"renderer", "SDLRendererBackend"}};
        EXPECT_TRUE(DifferenceExceptRenderer({{"type", "A"}, {"mode", "Game"}, {"renderer", "VulkanRendererBackend"}}, overlay).empty());
        EXPECT_FALSE(DifferenceExceptRenderer({{"type", "A"}, {"mode", "Game"}, {"renderer", "VulkanRendererBackend"}, {"title", "Chess"}}, overlay).empty());
        EXPECT_FALSE(DifferenceExceptRenderer({{"type", "A"}, {"renderer", "VulkanRendererBackend"}}, overlay).empty());
        EXPECT_FALSE(DifferenceExceptRenderer({{"type", "A"}, {"mode", "Editor"}, {"renderer", "VulkanRendererBackend"}}, overlay).empty());
    }

}  // namespace BECore::Chess::Tests
