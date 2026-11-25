#include <BECore/File/File.h>
#include <Common/Printers.h>
#include <Common/TestFiles.h>
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

        Path SourceRoot() {
            return Path(__FILE__).Parent().Parent();
        }

        Path GameConfigFolder() {
            return SourceRoot() / "config";
        }

        Path OverlayConfigFolder() {
            return SourceRoot() / "engine" / "web" / "config";
        }

        Vector<Path> XmlFilesOf(const Path& folder) {
            Vector<Path> files;
            const auto walked = WalkDirectory(folder.View(), WalkMode::Folder, [&](StringView name, FileKind kind) {
                if (kind == FileKind::File && Path(name).Extension().View() == ".xml") {
                    files.push_back(folder / name);
                }
                return true;
            });
            EXPECT_TRUE(walked.has_value()) << folder.CStr();
            std::sort(files.begin(), files.end());  //-NoStd: no eastl sort of Path here
            return files;
        }

        using Attributes = Map<String, String>;

        /// The attributes of the root element, name to value (empty when the file does not load)
        Attributes RootAttributes(const Path& file) {
            Attributes attributes;
            pugi::xml_document document;
            if (!document.load_file(file.CStr())) {
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
        ASSERT_TRUE(::BECore::Tests::IsFolder(OverlayConfigFolder())) << OverlayConfigFolder().CStr();
        EXPECT_TRUE(::BECore::Tests::PathExists(OverlayConfigFolder() / kApplicationConfigName));
        EXPECT_FALSE(XmlFilesOf(OverlayConfigFolder()).empty());
        ASSERT_TRUE(::BECore::Tests::IsFolder(GameConfigFolder())) << GameConfigFolder().CStr();
        EXPECT_TRUE(::BECore::Tests::PathExists(GameConfigFolder() / kApplicationConfigName));
    }

    TEST(ChessWebConfigTest, EveryOverlayFileIsAbsentFromChessOrIsTheApplicationConfigThatAgrees) {
        for (const Path& overlayFile : XmlFilesOf(OverlayConfigFolder())) {
            const String name(overlayFile.Filename().View());
            const Path gameFile = GameConfigFolder() / name;
            if (!::BECore::Tests::PathExists(gameFile)) {
                continue;
            }
            // Chess has a file the overlay replaces: only ApplicationConfig.xml is known to say the same thing, apart from the renderer
            ASSERT_EQ(name, String(kApplicationConfigName)) << "config/" << name.c_str() << " has the name of a file of engine/web/config/: " << kWhy
                                                            << ". Remove it, or teach the engine a game's web overlay (needs engine change)";
            const Attributes game = RootAttributes(gameFile);
            const Attributes overlay = RootAttributes(overlayFile);
            ASSERT_FALSE(game.empty()) << gameFile.CStr();
            ASSERT_FALSE(overlay.empty()) << overlayFile.CStr();
            const String difference = DifferenceExceptRenderer(game, overlay);
            EXPECT_TRUE(difference.empty()) << "config/" << name.c_str() << " differs from engine/web/config/" << name.c_str() << " in the root attributes:\n" << difference.c_str() << kWhy;
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
