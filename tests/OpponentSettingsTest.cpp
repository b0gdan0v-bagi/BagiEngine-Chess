#include <Chess/Engine/OpponentSettings.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>

/// The path of Stockfish from the two config files: the tests feed the settings strings or the shipped file read from the source tree. Only Load() meets the ConfigManager
namespace BECore::Chess::Tests {

    namespace {

        std::filesystem::path ShippedSettingsConfig() {
            return std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "ChessSettings.xml";
        }

        /// The settings of two files given as text; null means no such file
        OpponentSettings Read(const char* shared, const char* local) {
            const XmlConfig sharedConfig = XmlConfig::Create();
            const XmlConfig localConfig = XmlConfig::Create();
            if (shared != nullptr) {
                EXPECT_TRUE(sharedConfig.LoadFromString(shared)) << shared;
            }
            if (local != nullptr) {
                EXPECT_TRUE(localConfig.LoadFromString(local)) << local;
            }
            // An XmlConfig that loaded nothing has an invalid root, which is how a file that is not there looks
            return OpponentSettings::FromConfig(sharedConfig.GetRoot(), localConfig.GetRoot());
        }

    }  // namespace

    class OpponentSettingsLoadTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            BECore::Tests::EngineEnvironment::EnsureCoreServices();
        }
    };

    TEST(OpponentSettingsTest, NoFilesMeanNoPath) {
        EXPECT_EQ(OpponentSettings::FromConfig(XmlNode{}, XmlNode{}).stockfishPath, "");
        EXPECT_EQ(Read(nullptr, nullptr).stockfishPath, "");
    }

    TEST(OpponentSettingsTest, TheSharedFileAloneGivesItsPath) {
        EXPECT_EQ(Read(R"(<ChessSettings stockfishPath="/opt/stockfish"/>)", nullptr).stockfishPath, "/opt/stockfish");
    }

    TEST(OpponentSettingsTest, TheLocalFileAloneGivesItsPath) {
        EXPECT_EQ(Read(nullptr, R"(<ChessSettings stockfishPath="/home/me/stockfish"/>)").stockfishPath, "/home/me/stockfish");
    }

    TEST(OpponentSettingsTest, TheLocalPathWinsOverTheSharedOne) {
        const OpponentSettings settings = Read(R"(<ChessSettings stockfishPath="/shared/stockfish"/>)", R"(<ChessSettings stockfishPath="/local/stockfish"/>)");

        EXPECT_EQ(settings.stockfishPath, "/local/stockfish");
    }

    TEST(OpponentSettingsTest, AnEmptyLocalValueDoesNotHideTheSharedOne) {
        const OpponentSettings settings = Read(R"(<ChessSettings stockfishPath="/shared/stockfish"/>)", R"(<ChessSettings stockfishPath=""/>)");

        EXPECT_EQ(settings.stockfishPath, "/shared/stockfish");
    }

    TEST(OpponentSettingsTest, ABlankLocalValueDoesNotHideTheSharedOne) {
        const OpponentSettings settings = Read(R"(<ChessSettings stockfishPath="/shared/stockfish"/>)", R"(<ChessSettings stockfishPath="  "/>)");

        EXPECT_EQ(settings.stockfishPath, "/shared/stockfish");
    }

    TEST(OpponentSettingsTest, ALocalFileWithoutTheAttributeDoesNotHideTheSharedPath) {
        const OpponentSettings settings = Read(R"(<ChessSettings stockfishPath="/shared/stockfish"/>)", R"(<ChessSettings/>)");

        EXPECT_EQ(settings.stockfishPath, "/shared/stockfish");
    }

    TEST(OpponentSettingsTest, WhiteSpaceAroundThePathIsTrimmedAndInsideItStays) {
        EXPECT_EQ(Read(R"(<ChessSettings stockfishPath="  /opt/stock fish  "/>)", nullptr).stockfishPath, "/opt/stock fish");
        EXPECT_EQ(Read(nullptr, R"(<ChessSettings stockfishPath=" /local/stockfish "/>)").stockfishPath, "/local/stockfish");
    }

    TEST(OpponentSettingsTest, ABlankSharedValueIsNoPath) {
        EXPECT_EQ(Read(R"(<ChessSettings stockfishPath="   "/>)", nullptr).stockfishPath, "");
    }

    TEST(OpponentSettingsTest, AnUnknownAttributeIsIgnored) {
        const OpponentSettings settings = Read(R"(<ChessSettings level="hard" stockfishPath="/opt/stockfish" side="black"/>)", nullptr);

        EXPECT_EQ(settings.stockfishPath, "/opt/stockfish");
        EXPECT_EQ(Read(R"(<ChessSettings level="hard"/>)", nullptr).stockfishPath, "");
    }

    TEST(OpponentSettingsTest, TheNameOfTheRootElementDoesNotMatter) {
        // The file stem is the key; a root element named otherwise is read the same way
        EXPECT_EQ(Read(R"(<Settings stockfishPath="/opt/stockfish"/>)", nullptr).stockfishPath, "/opt/stockfish");
        EXPECT_EQ(Read(R"(<Settings stockfishPath="/opt/stockfish"/>)", R"(<root stockfishPath="/local/stockfish"/>)").stockfishPath, "/local/stockfish");
    }

    TEST(OpponentSettingsTest, TheAttributeBelongsToTheRootNotToAChild) {
        EXPECT_EQ(Read(R"(<ChessSettings><opponent stockfishPath="/opt/stockfish"/></ChessSettings>)", nullptr).stockfishPath, "");
    }

    TEST(OpponentSettingsTest, ARelativePathIsKeptAsItIs) {
        EXPECT_EQ(Read(R"(<ChessSettings stockfishPath="bin/stockfish"/>)", nullptr).stockfishPath, "bin/stockfish");
    }

    TEST(OpponentSettingsTest, TheTwoConfigsHaveTheNamesTheirFilesGive) {
        // ConfigManager keys a file by its stem: config/ChessSettings.xml and config/ChessSettings.local.xml
        EXPECT_EQ(std::filesystem::path("ChessSettings.xml").stem().string(), std::string(OpponentSettings::kSharedConfigName.data(), OpponentSettings::kSharedConfigName.size()));
        EXPECT_EQ(std::filesystem::path("ChessSettings.local.xml").stem().string(), std::string(OpponentSettings::kLocalConfigName.data(), OpponentSettings::kLocalConfigName.size()));
    }

    TEST(OpponentSettingsTest, TheShippedFileCarriesNoPath) {
        const XmlConfig config = XmlConfig::Create();
        ASSERT_TRUE(config.LoadFromFile(ShippedSettingsConfig())) << ShippedSettingsConfig().string();
        const XmlNode root = config.GetRoot();
        ASSERT_TRUE(root);

        EXPECT_EQ(root.Name(), OpponentSettings::kSharedConfigName);
        // The key is there, with an empty value: a person's path goes into the local file, never into the committed one
        const auto value = root.GetAttribute(OpponentSettings::kStockfishPathAttribute);
        ASSERT_TRUE(value.has_value());
        EXPECT_TRUE(value->empty()) << "config/ChessSettings.xml is committed: put your own path in config/ChessSettings.local.xml";
        EXPECT_EQ(OpponentSettings::FromConfig(root, XmlNode{}).stockfishPath, "");
    }

    TEST_F(OpponentSettingsLoadTest, ConfigsThatAreNotLoadedMeanNoPath) {
        // The game's own config/ is not mounted in the test environment, so neither of the two files is there
        EXPECT_EQ(OpponentSettings::Load().stockfishPath, "");
    }

}  // namespace BECore::Chess::Tests
