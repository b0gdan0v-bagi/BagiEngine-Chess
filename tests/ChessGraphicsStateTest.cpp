#include <BECore/Save/SaveGame.h>
#include <Chess/Save/ChessGraphicsState.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>

/// The look as a save (Chess/Save/ChessGraphicsState.h): captured, written and read back through a real SaveGameService on a temp folder, checked and applied in two steps. A file that is
/// damaged, of another game or with an id of no preset is refused with a reason that names the field, and the live choice stays as it was
namespace BECore::Chess::Tests {

    class ChessGraphicsStateTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ::BECore::Tests::EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _directory = ::BECore::Tests::TestTempDirectory("ChessGraphicsState");
            ::BECore::Tests::MakeEmptyDirectory(_directory);
            ASSERT_TRUE(_service.Initialize(_directory / "saves"));
        }

        void TearDown() override {
            ::BECore::Tests::RemoveDirectory(_directory);
        }

        /// What the game does at the start: read the slot, check it, apply it. The error of the first step that refused
        std::expected<GraphicsPreset, SaveError> LoadAndRestore(GraphicsChoice& choice) {
            ChessGraphicsState state;
            const std::expected<SaveInfo, SaveError> info = _service.Load(kChessGraphicsSlot, kChessGraphicsSchema, state);
            if (!info) {
                return std::unexpected(info.error());
            }
            const std::expected<GraphicsPreset, SaveError> preset = Validate(state);
            if (preset) {
                Apply(*preset, choice);
            }
            return preset;
        }

        void SaveId(StringView id) {
            ChessGraphicsState state;
            state.preset = String(id.data(), id.size());
            ASSERT_TRUE(_service.Save(kChessGraphicsSlot, kChessGraphicsSchema, state).has_value());
        }

        /// The file is refused with @p kind, the choice (a Beer one, so a restore to Classic would show) keeps its preset and revision, and the file is byte for byte the same
        void ExpectRefused(SaveErrorKind kind) {
            GraphicsChoice choice;
            choice.Set(GraphicsPreset::Beer);
            const uint64_t revision = choice.GetRevision();
            const std::expected<Vector<uint8_t>, SaveError> before = _service.ReadSlot(kChessGraphicsSlot);
            ASSERT_TRUE(before.has_value());

            const std::expected<GraphicsPreset, SaveError> restored = LoadAndRestore(choice);

            ASSERT_FALSE(restored.has_value()) << "the file was accepted";
            EXPECT_EQ(restored.error().kind, kind) << restored.error().detail.c_str();
            EXPECT_FALSE(restored.error().detail.empty());
            EXPECT_EQ(choice.Get(), GraphicsPreset::Beer);
            EXPECT_EQ(choice.GetRevision(), revision);
            const std::expected<Vector<uint8_t>, SaveError> after = _service.ReadSlot(kChessGraphicsSlot);
            ASSERT_TRUE(after.has_value());
            EXPECT_EQ(*after, *before);
        }

        Path _directory;
        FileSystem _fileSystem;
        SaveGameService _service{_fileSystem};
    };

    TEST_F(ChessGraphicsStateTest, EachPresetComesBackAsItWasSaved) {
        for (const GraphicsPreset preset : {GraphicsPreset::Classic, GraphicsPreset::Beer}) {
            SCOPED_TRACE(String(PresetId(preset).data(), PresetId(preset).size()).c_str());
            GraphicsChoice source;
            source.Set(preset);
            ASSERT_TRUE(_service.Save(kChessGraphicsSlot, kChessGraphicsSchema, Capture(source)).has_value());
            GraphicsChoice target;
            target.Set(preset == GraphicsPreset::Beer ? GraphicsPreset::Classic : GraphicsPreset::Beer);

            const std::expected<GraphicsPreset, SaveError> restored = LoadAndRestore(target);

            ASSERT_TRUE(restored.has_value()) << restored.error().detail.c_str();
            EXPECT_EQ(target.Get(), preset);
        }
    }

    TEST_F(ChessGraphicsStateTest, CaptureWritesTheIdOfTheControlApi) {
        GraphicsChoice choice;
        EXPECT_EQ(Capture(choice).preset, "classic");

        choice.Set(GraphicsPreset::Beer);
        EXPECT_EQ(Capture(choice).preset, "beer");
    }

    TEST_F(ChessGraphicsStateTest, ApplyingThePresetThatIsThereKeepsTheRevision) {
        GraphicsChoice choice;
        const uint64_t revision = choice.GetRevision();

        Apply(GraphicsPreset::Classic, choice);

        EXPECT_EQ(choice.GetRevision(), revision);
    }

    TEST_F(ChessGraphicsStateTest, NoFileIsNotFound) {
        GraphicsChoice choice;

        const std::expected<GraphicsPreset, SaveError> restored = LoadAndRestore(choice);

        ASSERT_FALSE(restored.has_value());
        EXPECT_EQ(restored.error().kind, SaveErrorKind::NotFound);
        EXPECT_EQ(choice.Get(), GraphicsPreset::Classic);
    }

    TEST_F(ChessGraphicsStateTest, AnIdOfNoPresetIsRefusedWithAReasonThatNamesNoContents) {
        SaveId("sepia");
        ExpectRefused(SaveErrorKind::Corrupt);

        ChessGraphicsState state;
        state.preset = "sepia";
        const std::expected<GraphicsPreset, SaveError> checked = Validate(state);
        ASSERT_FALSE(checked.has_value());
        EXPECT_EQ(checked.error().detail.find("sepia"), String::npos);
        EXPECT_NE(checked.error().detail.find("preset"), String::npos);
    }

    TEST_F(ChessGraphicsStateTest, AnIdThatIsNotExactlyOneIsRefused) {
        for (const StringView id : {"", "Beer", "BEER", " beer", "beer ", "1"}) {
            SCOPED_TRACE(String(id.data(), id.size()).c_str());
            SaveId(id);
            ExpectRefused(SaveErrorKind::Corrupt);
        }
    }

    TEST_F(ChessGraphicsStateTest, AnEmptyFileIsRefused) {
        ASSERT_TRUE(_service.WriteSlot(kChessGraphicsSlot, {}).has_value());
        ExpectRefused(SaveErrorKind::Corrupt);
    }

    TEST_F(ChessGraphicsStateTest, ATruncatedFileIsRefused) {
        SaveId("beer");
        Vector<uint8_t> bytes = *_service.ReadSlot(kChessGraphicsSlot);
        bytes.resize(bytes.size() / 2);
        ASSERT_TRUE(_service.WriteSlot(kChessGraphicsSlot, bytes).has_value());
        ExpectRefused(SaveErrorKind::Corrupt);
    }

    TEST_F(ChessGraphicsStateTest, AFileOfAnotherGameIsRefused) {
        ChessGraphicsState state;
        state.preset = "classic";
        ASSERT_TRUE(_service.Save(kChessGraphicsSlot, SaveSchema{"Checkers", 1, 1}, state).has_value());
        ExpectRefused(SaveErrorKind::WrongGame);
    }

    TEST_F(ChessGraphicsStateTest, AFileOfANewerVersionIsRefused) {
        ChessGraphicsState state;
        state.preset = "classic";
        ASSERT_TRUE(_service.Save(kChessGraphicsSlot, SaveSchema{kChessGraphicsSchema.game, kChessGraphicsSchema.version + 1, kChessGraphicsSchema.version}, state).has_value());
        ExpectRefused(SaveErrorKind::VersionTooNew);
    }

}  // namespace BECore::Chess::Tests
