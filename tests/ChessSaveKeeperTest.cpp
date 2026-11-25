#include <BECore/Audio/AudioManager.h>
#include <BECore/Audio/AudioPreferences.h>
#include <BECore/Config/ConfigManager.h>
#include <BECore/Control/ControlSettings.h>
#include <BECore/File/Path.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Save/SaveStorage.h>
#include <BECore/Time/Time.h>
#include <Chess/Engine/OpponentSettings.h>
#include <Chess/Save/ChessGraphicsState.h>
#include <Chess/Save/ChessSaveKeeper.h>
#include <Chess/Save/ChessSaveState.h>
#include <Chess/Save/SaveSettings.h>
#include <Common/EngineEnvironment.h>
#include <Common/LogRecorder.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <tests/ChessTestUtil.h>
#include <tests/TestEngines.h>

/// The settings of the memory of the game, the policy that decides whether a run saves (the gate) and the keeper that restores at the start and writes when the game changes. The clock is
/// the engine's manual one (Time::Advance), never a sleep; what reached the file is read back through a SaveGameService of its own
namespace BECore::Chess::Tests {

    namespace {

        Path ShippedSettingsConfig() {
            return Path(__FILE__).Parent().Parent() / "config" / "ChessSettings.xml";
        }

        SaveSettings Read(const char* shared, const char* local) {
            const XmlConfig sharedConfig = XmlConfig::Create();
            const XmlConfig localConfig = XmlConfig::Create();
            if (shared != nullptr) {
                EXPECT_TRUE(sharedConfig.LoadFromString(shared)) << shared;
            }
            if (local != nullptr) {
                EXPECT_TRUE(localConfig.LoadFromString(local)) << local;
            }
            return SaveSettings::FromConfig(sharedConfig.GetRoot(), localConfig.GetRoot());
        }

        CommandLine LineOf(const Vector<String>& arguments) {
            CommandLine line;
            line.Set(arguments);
            return line;
        }

        Path StubDefaultDirectory() {
            return Path("stub-default-folder");
        }

        Vector<uint8_t> BytesOfFile(const Path& path) {
            return ::BECore::Tests::ReadFileBytes(path);
        }

        void WriteBytes(const Path& path, const String& bytes) {
            ::BECore::Tests::WriteFile(path, StringView(bytes));
        }

        void PlayLine(ChessGameModel& model, StringView line) {
            size_t begin = 0;
            while (begin < line.size()) {
                size_t end = line.find(' ', begin);
                if (end == StringView::npos) {
                    end = line.size();
                }
                const Optional<Move> move = MoveGenerator::ParseUci(model.GetPosition(), line.substr(begin, end - begin));
                ASSERT_TRUE(move.has_value());
                ASSERT_TRUE(model.TryMove(*move).has_value());
                begin = end + 1;
            }
        }

        constexpr StringView kOpening = "e2e4 e7e5";

    }  // namespace

    // ---- SaveSettings ----

    TEST(SaveSettingsTest, NoFilesMeanOff) {
        EXPECT_FALSE(SaveSettings::FromConfig(XmlNode{}, XmlNode{}).rememberGame);
        EXPECT_FALSE(Read(nullptr, nullptr).rememberGame);
    }

    TEST(SaveSettingsTest, TheSharedFileAloneDecides) {
        EXPECT_TRUE(Read(R"(<ChessSettings rememberGame="true"/>)", nullptr).rememberGame);
        EXPECT_FALSE(Read(R"(<ChessSettings rememberGame="false"/>)", nullptr).rememberGame);
        EXPECT_FALSE(Read(R"(<ChessSettings stockfishPath=""/>)", nullptr).rememberGame);
    }

    TEST(SaveSettingsTest, TheLocalFileWinsWhenItHasTheAttribute) {
        EXPECT_FALSE(Read(R"(<ChessSettings rememberGame="true"/>)", R"(<ChessSettings rememberGame="false"/>)").rememberGame);
        EXPECT_TRUE(Read(R"(<ChessSettings rememberGame="false"/>)", R"(<ChessSettings rememberGame="true"/>)").rememberGame);
        EXPECT_TRUE(Read(nullptr, R"(<ChessSettings rememberGame="true"/>)").rememberGame);
    }

    TEST(SaveSettingsTest, ALocalFileWithoutTheAttributeLeavesTheSharedValue) {
        EXPECT_TRUE(Read(R"(<ChessSettings rememberGame="true"/>)", R"(<ChessSettings stockfishPath="/x"/>)").rememberGame);
    }

    TEST(SaveSettingsTest, AValueThatIsNotTrueOrFalseIsFalseAndIsNotAnError) {
        for (const char* value : {"yes", "1", "TRUE", "True", "", " true"}) {
            SCOPED_TRACE(value);
            const String shared = String("<ChessSettings rememberGame=\"") + value + "\"/>";
            EXPECT_FALSE(Read(shared.c_str(), nullptr).rememberGame);
        }
        // A bad local value is the local value: it does not fall through to the shared one
        EXPECT_FALSE(Read(R"(<ChessSettings rememberGame="true"/>)", R"(<ChessSettings rememberGame="maybe"/>)").rememberGame);
    }

    TEST(SaveSettingsTest, TheShippedFileRemembersTheGameAndKeepsTheEmptyStockfishPath) {
        const XmlConfig config = XmlConfig::Create();
        ASSERT_TRUE(config.LoadFromFile(ShippedSettingsConfig())) << ShippedSettingsConfig().CStr();

        EXPECT_TRUE(SaveSettings::FromConfig(config.GetRoot(), XmlNode{}).rememberGame);
        EXPECT_EQ(OpponentSettings::FromConfig(config.GetRoot(), XmlNode{}).stockfishPath, "");
    }

    class SaveSettingsLoadTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ::BECore::Tests::EngineEnvironment::EnsureCoreServices();
        }
    };

    TEST_F(SaveSettingsLoadTest, ConfigsThatAreNotLoadedMeanOff) {
        // config/ is not mounted in the test environment: every gtest starts clean with no switch. A machine where it is says nothing about this case
        const ConfigManager& configs = CoreManager::GetConfigManager();
        if (configs.HasConfig(OpponentSettings::kSharedConfigName) || configs.HasConfig(OpponentSettings::kLocalConfigName)) {
            GTEST_SKIP() << "the test environment has a ChessSettings config mounted";
        }

        EXPECT_FALSE(SaveSettings::Load().rememberGame);
    }

    // ---- The policy ----

    TEST(SaveSettingsPolicyTest, ARunWithNothingAskedForSavesInTheDefaultFolderOnlyWhenTheGameIsRemembered) {
        if (!PersistentSaveDirectory().Empty()) {
            GTEST_SKIP() << "a browser build: the folder is the page's";
        }

        const Optional<Path> on = ResolveSaveDirectory(LineOf({}), true, &StubDefaultDirectory);
        ASSERT_TRUE(on.has_value());
        EXPECT_EQ(*on, StubDefaultDirectory());
        EXPECT_FALSE(ResolveSaveDirectory(LineOf({}), false, &StubDefaultDirectory).has_value());
    }

    TEST(SaveSettingsPolicyTest, ADefaultFolderThatIsNotThereMeansOff) {
        if (!PersistentSaveDirectory().Empty()) {
            GTEST_SKIP() << "a browser build: the folder is the page's";
        }

        EXPECT_FALSE(ResolveSaveDirectory(LineOf({}), true, [] { return Path(); }).has_value());
    }

    TEST(SaveSettingsPolicyTest, EveryControlFlagSwitchesItOff) {
        for (const char* flag : {"--control", "--control-port=0", "--control-port=9000", "--headless", "--manual-clock"}) {
            SCOPED_TRACE(flag);
            EXPECT_FALSE(ResolveSaveDirectory(LineOf({flag}), true, &StubDefaultDirectory).has_value());
        }
        EXPECT_FALSE(ResolveSaveDirectory(LineOf({"--control", "--headless", "--manual-clock"}), true, &StubDefaultDirectory).has_value());
    }

    TEST(SaveSettingsPolicyTest, ASaveDirTurnsItOnInAnyRun) {
        const Vector<String> control = {"--control", "--headless", "--manual-clock", "--control-port=0", "--save-dir=somewhere"};

        for (const bool remembered : {true, false}) {
            const Optional<Path> folder = ResolveSaveDirectory(LineOf(control), remembered, &StubDefaultDirectory);
            ASSERT_TRUE(folder.has_value());
            EXPECT_EQ(*folder, Path("somewhere"));
        }
        const Optional<Path> plain = ResolveSaveDirectory(LineOf({"--save-dir=elsewhere"}), false, &StubDefaultDirectory);
        ASSERT_TRUE(plain.has_value());
        EXPECT_EQ(*plain, Path("elsewhere"));
    }

    TEST(SaveSettingsPolicyTest, NoSaveWinsOverEverything) {
        EXPECT_FALSE(ResolveSaveDirectory(LineOf({"--no-save"}), true, &StubDefaultDirectory).has_value());
        EXPECT_FALSE(ResolveSaveDirectory(LineOf({"--no-save", "--save-dir=somewhere"}), true, &StubDefaultDirectory).has_value());
        EXPECT_FALSE(ResolveSaveDirectory(LineOf({"--save-dir=somewhere", "--no-save"}), true, &StubDefaultDirectory).has_value());
    }

    TEST(SaveSettingsPolicyTest, AnEmptySaveDirIsNotAFolder) {
        EXPECT_FALSE(ResolveSaveDirectory(LineOf({"--save-dir=", "--headless"}), true, &StubDefaultDirectory).has_value());
    }

    // ---- The keeper ----

    /// A game, its controller (every engine a double: no process) and the choice and the look, as ChessGame holds them, with the keeper over them
    struct KeptGame {
        ChessGameModel model;
        OpponentController controller;
        OpponentChoice choice;
        GraphicsChoice graphics;
        CountingFactory factory;
        ChessSaveKeeper keeper;

        KeptGame() : controller(model), choice(controller), factory({New<InstantEngine>()}) {
            controller.SetEngineFactory(factory.AsFactory());
        }

        void Start() {
            keeper.Start(model, controller, choice, graphics);
        }
        void Update() {
            keeper.Update(model, controller, choice, false);
        }
        void Flush() {
            keeper.Flush(model, controller, choice, false);
        }
        void UpdateDuringAPuzzle() {
            keeper.Update(model, controller, choice, true);
        }
        void FlushDuringAPuzzle() {
            keeper.Flush(model, controller, choice, true);
        }
    };

    class ChessSaveKeeperTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ::BECore::Tests::EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            for (const String& argument : CoreManager::GetCommandLine().GetArguments()) {
                _savedArguments.push_back(argument);
            }
            CoreManager::GetTime().Reset();
            _directory = ::BECore::Tests::TestTempDirectory("ChessSaveKeeper");
            ::BECore::Tests::MakeEmptyDirectory(_directory);
            _saves = _directory / "saves";
        }

        void TearDown() override {
            CoreManager::GetCommandLine().Set(_savedArguments);
            CoreManager::GetTime().Reset();
            ::BECore::Tests::RemoveDirectory(_directory);
        }

        /// The run's command line: the folder of the test and the flags given
        void SetCommandLine(const Vector<String>& flags, bool withFolder = true) {
            Vector<String> arguments = flags;
            if (withFolder) {
                const String path = String(_saves.View());
                arguments.push_back(String("--save-dir=") + path);
            }
            CoreManager::GetCommandLine().Set(arguments);
        }

        Path SaveFile() const {
            return _saves / "game.save";
        }

        /// What reached the file; nothing when there is none or it cannot be read
        Optional<ChessSaveState> Saved() const {
            FileSystem fileSystem;
            SaveGameService service(fileSystem);
            if (!service.Initialize(_saves)) {
                return std::nullopt;
            }
            ChessSaveState state;
            if (!service.Load(kChessSaveSlot, kChessSaveSchema, state)) {
                return std::nullopt;
            }
            return state;
        }

        Path AudioFile() const {
            return _saves / "audio.save";
        }

        /// The levels the audio file holds; nothing when there is none or it cannot be read
        Optional<AudioPreferences> SavedLevels() const {
            FileSystem fileSystem;
            SaveGameService service(fileSystem);
            if (!service.Initialize(_saves)) {
                return std::nullopt;
            }
            AudioPreferences levels;
            if (!service.Load(kAudioPreferencesSlot, kAudioPreferencesSchema, levels)) {
                return std::nullopt;
            }
            return levels;
        }

        Path GraphicsFile() const {
            return _saves / "graphics.save";
        }

        /// The look the graphics file holds; nothing when there is none or it cannot be read
        Optional<ChessGraphicsState> SavedLook() const {
            FileSystem fileSystem;
            SaveGameService service(fileSystem);
            if (!service.Initialize(_saves)) {
                return std::nullopt;
            }
            ChessGraphicsState look;
            if (!service.Load(kChessGraphicsSlot, kChessGraphicsSchema, look)) {
                return std::nullopt;
            }
            return look;
        }

        /// A graphics file as an earlier run left it
        void MakeGraphicsFile(StringView preset) {
            FileSystem fileSystem;
            SaveGameService service(fileSystem);
            ASSERT_TRUE(service.Initialize(_saves));
            ChessGraphicsState look;
            look.preset = String(preset.data(), preset.size());
            ASSERT_TRUE(service.Save(kChessGraphicsSlot, kChessGraphicsSchema, look).has_value());
        }

        /// An audio file as an earlier run left it; the manager is left at the same levels
        void MakeAudioFile(float volume, bool muted) {
            AudioManager& audio = CoreManager::GetAudio();
            audio.SetMasterVolume(volume);
            audio.SetMasterMuted(muted);
            FileSystem fileSystem;
            SaveGameService service(fileSystem);
            ASSERT_TRUE(service.Initialize(_saves));
            ASSERT_TRUE(SaveAudioPreferences(service, audio).has_value());
        }

        static void SetDefaultLevels() {
            CoreManager::GetAudio().SetMasterVolume(1.0f);
            CoreManager::GetAudio().SetMasterMuted(false);
        }

        /// A frame is clamped to Time::kMaxFrameTime, so the debounce takes several of them
        static void AdvancePastTheDebounce() {
            double advanced = 0.0;
            while (advanced <= ChessSaveKeeper::kSaveDebounceSeconds) {
                CoreManager::GetTime().Advance(Time::kMaxFrameTime);
                advanced += Time::kMaxFrameTime;
            }
        }

        ScopedAudioLevels _levels;
        Vector<String> _savedArguments;
        Path _directory;
        Path _saves;
    };

    TEST_F(ChessSaveKeeperTest, AKeeperThatIsOffTouchesNoFileAndReadsNone) {
        SetCommandLine({}, false);  // no flag, and config/ is not mounted: rememberGame is false
        ::BECore::Tests::MakeDirectory(_saves);
        WriteBytes(SaveFile(), "a game of somebody");
        const Vector<uint8_t> before = BytesOfFile(SaveFile());
        KeptGame game;

        game.Start();
        PlayLine(game.model, kOpening);
        game.Update();
        AdvancePastTheDebounce();
        game.Update();
        game.Flush();

        EXPECT_FALSE(game.keeper.IsOn());
        EXPECT_EQ(BytesOfFile(SaveFile()), before);
        EXPECT_EQ(game.model.GetMoves().size(), 2u);  // only what the test played: nothing was restored
    }

    TEST_F(ChessSaveKeeperTest, NoSaveSwitchesOffEvenWithASaveDir) {
        SetCommandLine({"--no-save"});
        KeptGame game;

        game.Start();
        PlayLine(game.model, kOpening);
        game.Flush();

        EXPECT_FALSE(game.keeper.IsOn());
        EXPECT_FALSE(::BECore::Tests::PathExists(SaveFile()));
        EXPECT_FALSE(::BECore::Tests::PathExists(_saves));
    }

    TEST_F(ChessSaveKeeperTest, ASaveDirTurnsItOnInAControlRun) {
        SetCommandLine({"--control", "--headless", "--manual-clock"});
        KeptGame game;

        game.Start();
        PlayLine(game.model, kOpening);
        game.Flush();

        EXPECT_TRUE(game.keeper.IsOn());
        ASSERT_TRUE(Saved().has_value());
        EXPECT_EQ(Saved()->moves, "e2e4 e7e5");
    }

    TEST_F(ChessSaveKeeperTest, AFirstStartWithNoFileWritesNothing) {
        SetCommandLine({});
        KeptGame game;

        game.Start();
        for (int frame = 0; frame < 3; ++frame) {
            AdvancePastTheDebounce();
            game.Update();
        }
        game.Flush();

        EXPECT_TRUE(game.keeper.IsOn());
        EXPECT_FALSE(::BECore::Tests::PathExists(SaveFile()));
    }

    TEST_F(ChessSaveKeeperTest, AChangeThenFlushWrites) {
        SetCommandLine({});
        KeptGame game;
        game.Start();
        PlayLine(game.model, kOpening);
        ASSERT_TRUE(game.controller.ChooseSeats(ProgramSides::Of(PieceColor::Black), SeatChoice{OpponentEngine::Own, 1}, SeatChoice{OpponentEngine::Own, 1}).has_value());

        game.Flush();

        const Optional<ChessSaveState> saved = Saved();
        ASSERT_TRUE(saved.has_value());
        EXPECT_EQ(saved->moves, "e2e4 e7e5");
        EXPECT_EQ(saved->plays, "black");
        EXPECT_EQ(saved->personColor, "white");
    }

    TEST_F(ChessSaveKeeperTest, TheColorOfThePersonIsPartOfTheKey) {
        SetCommandLine({});
        KeptGame game;
        game.Start();
        ASSERT_TRUE(game.choice.SetColor("black").has_value());

        game.Flush();

        ASSERT_TRUE(Saved().has_value());
        EXPECT_EQ(Saved()->personColor, "black");
    }

    TEST_F(ChessSaveKeeperTest, TwoChangesInsideTheDebounceMakeOneWrite) {
        SetCommandLine({});
        KeptGame game;
        game.Start();

        PlayLine(game.model, "e2e4");
        game.Update();  // the first write of a run waits for nothing
        ASSERT_TRUE(Saved().has_value());
        EXPECT_EQ(Saved()->moves, "e2e4");

        PlayLine(game.model, "e7e5");
        CoreManager::GetTime().Advance(1.0 / 32);
        game.Update();
        PlayLine(game.model, "g1f3");
        CoreManager::GetTime().Advance(1.0 / 32);
        game.Update();
        EXPECT_EQ(Saved()->moves, "e2e4") << "written inside the debounce";

        AdvancePastTheDebounce();
        game.Update();
        // The two changes reached the file together: the file never held "e2e4 e7e5"
        EXPECT_EQ(Saved()->moves, "e2e4 e7e5 g1f3");
    }

    TEST_F(ChessSaveKeeperTest, AClockThatDoesNotMoveWritesNothingMore) {
        SetCommandLine({});
        KeptGame game;
        game.Start();
        PlayLine(game.model, "e2e4");
        game.Update();

        PlayLine(game.model, "e7e5");
        for (int frame = 0; frame < 5; ++frame) {
            game.Update();
        }

        EXPECT_EQ(Saved()->moves, "e2e4");
        game.Flush();  // the end of the game writes whatever the clock says
        EXPECT_EQ(Saved()->moves, "e2e4 e7e5");
    }

    TEST_F(ChessSaveKeeperTest, NothingOfTheGameIsWrittenWhileAPuzzleIsOnAndTheFirstFrameAfterItWritesTheGame) {
        SetCommandLine({});
        KeptGame game;
        game.Start();
        PlayLine(game.model, "e2e4");
        game.Update();
        ASSERT_EQ(Saved()->moves, "e2e4");

        PlayLine(game.model, "e7e5 g1f3");  // what a puzzle's position is to the keeper: a changed game
        AdvancePastTheDebounce();
        game.UpdateDuringAPuzzle();
        game.FlushDuringAPuzzle();
        EXPECT_EQ(Saved()->moves, "e2e4") << "the game from before the puzzle is what a restart brings back";

        game.Update();
        EXPECT_EQ(Saved()->moves, "e2e4 e7e5 g1f3");
    }

    TEST_F(ChessSaveKeeperTest, TheLevelsAreWrittenWhileAPuzzleIsOn) {
        SetCommandLine({});
        KeptGame game;
        game.Start();
        CoreManager::GetAudio().SetMasterVolume(0.25f);

        game.FlushDuringAPuzzle();

        ASSERT_TRUE(SavedLevels().has_value());
        EXPECT_FLOAT_EQ(SavedLevels()->_master._volume, 0.25f);
    }

    TEST_F(ChessSaveKeeperTest, AFlushAfterAWriteWritesNothingMore) {
        SetCommandLine({});
        KeptGame game;
        game.Start();
        PlayLine(game.model, "e2e4");
        game.Update();
        ::BECore::Tests::RemoveFileIfPresent(SaveFile());

        game.Flush();  // the key did not move since the write: nothing to write

        EXPECT_FALSE(::BECore::Tests::PathExists(SaveFile()));
    }

    TEST_F(ChessSaveKeeperTest, TheRestoredGameIsNotRewrittenAtTheStart) {
        SetCommandLine({});
        {
            KeptGame first;
            first.Start();
            PlayLine(first.model, "e2e4 e7e5 g1f3");
            ASSERT_TRUE(first.controller.ChooseSeats(ProgramSides::Of(PieceColor::Black), SeatChoice{OpponentEngine::Own, 2}, SeatChoice{OpponentEngine::Own, 2}).has_value());
            first.Flush();
        }
        ASSERT_TRUE(::BECore::Tests::PathExists(SaveFile()));
        const Vector<uint8_t> before = BytesOfFile(SaveFile());

        KeptGame second;
        second.Start();

        EXPECT_EQ(second.model.GetMoves().size(), 3u);
        EXPECT_EQ(second.controller.GetPlays(), PieceColor::Black);
        EXPECT_EQ(second.controller.GetSeatChoice(PieceColor::Black).level, 2u);
        ::BECore::Tests::RemoveFileIfPresent(SaveFile());
        for (int frame = 0; frame < 3; ++frame) {
            AdvancePastTheDebounce();
            second.Update();
        }
        second.Flush();
        EXPECT_FALSE(::BECore::Tests::PathExists(SaveFile())) << "the restored game was written back";
        EXPECT_FALSE(before.empty());
    }

    TEST_F(ChessSaveKeeperTest, ADamagedFileLeavesTheGameFreshAndTheFileAloneUntilTheNextChange) {
        SetCommandLine({});
        ::BECore::Tests::MakeDirectory(_saves);
        WriteBytes(SaveFile(), "this is not a save file");
        const Vector<uint8_t> before = BytesOfFile(SaveFile());
        KeptGame game;

        game.Start();

        EXPECT_TRUE(game.keeper.IsOn());
        EXPECT_TRUE(game.model.GetMoves().empty());
        EXPECT_FALSE(game.controller.GetPlays().has_value());
        for (int frame = 0; frame < 3; ++frame) {
            AdvancePastTheDebounce();
            game.Update();
        }
        game.Flush();
        EXPECT_EQ(BytesOfFile(SaveFile()), before);

        PlayLine(game.model, "d2d4");
        game.Flush();

        const Optional<ChessSaveState> saved = Saved();
        ASSERT_TRUE(saved.has_value()) << "the game's own change did not replace the damaged file";
        EXPECT_EQ(saved->moves, "d2d4");
    }

    TEST_F(ChessSaveKeeperTest, AFileOfAnotherGameIsRefusedAndLeftAlone) {
        SetCommandLine({});
        {
            FileSystem fileSystem;
            SaveGameService service(fileSystem);
            ASSERT_TRUE(service.Initialize(_saves));
            ChessSaveState state;
            ASSERT_TRUE(service.Save(kChessSaveSlot, SaveSchema{"Checkers", 1, 1}, state).has_value());
        }
        const Vector<uint8_t> before = BytesOfFile(SaveFile());
        KeptGame game;

        game.Start();
        game.Flush();

        EXPECT_TRUE(game.model.GetMoves().empty());
        EXPECT_EQ(BytesOfFile(SaveFile()), before);
    }

    TEST_F(ChessSaveKeeperTest, AFolderThatCannotBeMadeTurnsItOffAndTheGameRuns) {
        WriteBytes(_directory / "a-file", "x");
        CoreManager::GetCommandLine().Set(Vector<String>{String("--save-dir=") + String((_directory / "a-file").View())});
        KeptGame game;

        game.Start();
        PlayLine(game.model, kOpening);
        game.Update();
        game.Flush();

        EXPECT_FALSE(game.keeper.IsOn());
        EXPECT_EQ(game.model.GetMoves().size(), 2u);
    }

    TEST_F(ChessSaveKeeperTest, AFailedWriteIsTriedAgainAtTheNextChange) {
        SetCommandLine({});
        KeptGame game;
        game.Start();
        // The save folder is replaced by a file: the write fails
        ::BECore::Tests::RemoveDirectory(_saves);
        WriteBytes(_saves, "in the way");

        PlayLine(game.model, "e2e4");
        game.Update();
        AdvancePastTheDebounce();
        game.Update();  // the same key: not tried again
        EXPECT_TRUE(::BECore::Tests::IsFile(_saves));

        ::BECore::Tests::RemoveFileIfPresent(_saves);
        ::BECore::Tests::MakeDirectory(_saves);
        PlayLine(game.model, "e7e5");
        AdvancePastTheDebounce();
        game.Update();

        ASSERT_TRUE(Saved().has_value());
        EXPECT_EQ(Saved()->moves, "e2e4 e7e5");
    }

    TEST_F(ChessSaveKeeperTest, AStartOverStartsFromTheFileAgain) {
        SetCommandLine({});
        KeptGame game;
        game.Start();
        PlayLine(game.model, "e2e4 e7e5");
        game.Flush();

        KeptGame again;
        again.Start();

        EXPECT_EQ(again.model.GetMoves().size(), 2u);
        again.model.Reset();
        again.Flush();  // a reset is a change: the new game is what the file holds now
        ASSERT_TRUE(Saved().has_value());
        EXPECT_TRUE(Saved()->moves.empty());
    }

    // ---- The player's levels ----

    TEST_F(ChessSaveKeeperTest, TheLevelsComeBackAfterAStopAndAStartAndAreNotWrittenBack) {
        SetCommandLine({});
        {
            KeptGame first;
            first.Start();
            CoreManager::GetAudio().SetMasterVolume(0.4f);
            CoreManager::GetAudio().SetMasterMuted(true);
            first.Update();
            first.Flush();
        }
        ASSERT_TRUE(SavedLevels().has_value());
        SetDefaultLevels();
        KeptGame second;

        second.Start();

        EXPECT_FLOAT_EQ(CoreManager::GetAudio().GetSettings()._master._volume, 0.4f);
        EXPECT_TRUE(CoreManager::GetAudio().GetSettings()._master._muted);
        ::BECore::Tests::RemoveFileIfPresent(AudioFile());
        for (int frame = 0; frame < 3; ++frame) {
            AdvancePastTheDebounce();
            second.Update();
        }
        second.Flush();
        EXPECT_FALSE(::BECore::Tests::PathExists(AudioFile())) << "the restored levels were written back";
    }

    TEST_F(ChessSaveKeeperTest, AStartWithNoAudioFileKeepsTheLevelsAndWritesNothing) {
        SetCommandLine({});
        CoreManager::GetAudio().SetMasterVolume(0.6f);
        KeptGame game;

        game.Start();
        for (int frame = 0; frame < 3; ++frame) {
            AdvancePastTheDebounce();
            game.Update();
        }
        game.Flush();

        EXPECT_FLOAT_EQ(CoreManager::GetAudio().GetSettings()._master._volume, 0.6f);
        EXPECT_FALSE(::BECore::Tests::PathExists(AudioFile()));
    }

    TEST_F(ChessSaveKeeperTest, ARefusedAudioFileLeavesTheLevelsOneWarningAndTheFileAloneWhileTheGameStillComesBack) {
        SetCommandLine({});
        {
            KeptGame first;
            first.Start();
            PlayLine(first.model, kOpening);
            first.Flush();
        }
        WriteBytes(AudioFile(), "these are not levels");
        const Vector<uint8_t> before = BytesOfFile(AudioFile());
        SetDefaultLevels();
        const ::BECore::Tests::LogRecorder recorder;
        KeptGame game;

        game.Start();
        for (int frame = 0; frame < 3; ++frame) {
            AdvancePastTheDebounce();
            game.Update();
        }
        game.Flush();
        LogEvent::Flush();

        EXPECT_EQ(game.model.GetMoves().size(), 2u);
        EXPECT_FLOAT_EQ(CoreManager::GetAudio().GetSettings()._master._volume, 1.0f);
        EXPECT_FALSE(CoreManager::GetAudio().GetSettings()._master._muted);
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {"slot 'audio'"}), 1u);
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {"slot 'game'"}), 0u);
        EXPECT_EQ(BytesOfFile(AudioFile()), before);
    }

    TEST_F(ChessSaveKeeperTest, ARunThatSavesNothingNeitherReadsNorWritesTheLevels) {
        SetCommandLine({}, false);  // no flag and no folder: config/ is not mounted, so rememberGame is false
        ::BECore::Tests::MakeDirectory(_saves);
        MakeAudioFile(0.9f, true);
        const Vector<uint8_t> before = BytesOfFile(AudioFile());
        SetDefaultLevels();
        KeptGame game;

        game.Start();
        EXPECT_FLOAT_EQ(CoreManager::GetAudio().GetSettings()._master._volume, 1.0f);
        CoreManager::GetAudio().SetMasterVolume(0.2f);
        AdvancePastTheDebounce();
        game.Update();
        game.Flush();

        EXPECT_FALSE(game.keeper.IsOn());
        EXPECT_FLOAT_EQ(CoreManager::GetAudio().GetSettings()._master._volume, 0.2f);
        EXPECT_EQ(BytesOfFile(AudioFile()), before);
    }

    TEST_F(ChessSaveKeeperTest, NoSaveReadsAndWritesNeitherSlotEvenWithASaveDir) {
        SetCommandLine({"--no-save"});
        ::BECore::Tests::MakeDirectory(_saves);
        MakeAudioFile(0.9f, true);
        const Vector<uint8_t> before = BytesOfFile(AudioFile());
        SetDefaultLevels();
        KeptGame game;

        game.Start();
        EXPECT_FLOAT_EQ(CoreManager::GetAudio().GetSettings()._master._volume, 1.0f);
        CoreManager::GetAudio().SetMasterVolume(0.2f);
        AdvancePastTheDebounce();
        game.Update();
        game.Flush();

        EXPECT_EQ(BytesOfFile(AudioFile()), before);
        EXPECT_FALSE(::BECore::Tests::PathExists(SaveFile()));
    }

    TEST_F(ChessSaveKeeperTest, TwoLevelChangesInsideTheDebounceMakeOneWrite) {
        SetCommandLine({});
        KeptGame game;
        game.Start();

        CoreManager::GetAudio().SetMasterVolume(0.8f);
        game.Update();  // the first write of a run waits for nothing
        ASSERT_TRUE(SavedLevels().has_value());
        EXPECT_FLOAT_EQ(SavedLevels()->_master._volume, 0.8f);

        CoreManager::GetAudio().SetMasterVolume(0.5f);
        CoreManager::GetTime().Advance(1.0 / 32);
        game.Update();
        CoreManager::GetAudio().SetMasterVolume(0.3f);
        CoreManager::GetTime().Advance(1.0 / 32);
        game.Update();
        EXPECT_FLOAT_EQ(SavedLevels()->_master._volume, 0.8f) << "written inside the debounce";

        AdvancePastTheDebounce();
        game.Update();
        EXPECT_FLOAT_EQ(SavedLevels()->_master._volume, 0.3f) << "the file never held 0.5";
    }

    TEST_F(ChessSaveKeeperTest, TheLevelsDebounceAndTheGamesDebounceAreNotOne) {
        SetCommandLine({});
        KeptGame game;
        game.Start();
        PlayLine(game.model, "e2e4");
        game.Update();
        ASSERT_TRUE(Saved().has_value());

        CoreManager::GetAudio().SetMasterVolume(0.7f);
        CoreManager::GetTime().Advance(1.0 / 32);
        game.Update();

        ASSERT_TRUE(SavedLevels().has_value()) << "the game's write delayed the levels";
        EXPECT_FLOAT_EQ(SavedLevels()->_master._volume, 0.7f);
    }

    TEST_F(ChessSaveKeeperTest, AFlushWritesALevelThatChangedInTheLastFrame) {
        SetCommandLine({});
        KeptGame game;
        game.Start();
        CoreManager::GetAudio().SetMasterVolume(0.8f);
        game.Update();
        CoreManager::GetAudio().SetMasterMuted(true);
        CoreManager::GetTime().Advance(1.0 / 32);
        game.Update();
        ASSERT_TRUE(SavedLevels().has_value());
        EXPECT_FALSE(SavedLevels()->_master._muted);

        game.Flush();

        EXPECT_TRUE(SavedLevels()->_master._muted);
    }

    // ---- The person's look ----

    TEST_F(ChessSaveKeeperTest, APickOfTheLookIsWrittenAtOnceAndComesBackAtTheNextStart) {
        SetCommandLine({});
        {
            KeptGame first;
            first.Start();
            first.graphics.Set(GraphicsPreset::Beer);
            first.Update();
            ASSERT_TRUE(SavedLook().has_value()) << "the first write of a run waits for nothing";
            EXPECT_EQ(SavedLook()->preset, "beer");
        }
        KeptGame second;

        second.Start();

        EXPECT_EQ(second.graphics.Get(), GraphicsPreset::Beer);
    }

    TEST_F(ChessSaveKeeperTest, ARestoredLookIsNotWrittenBack) {
        SetCommandLine({});
        ::BECore::Tests::MakeDirectory(_saves);
        MakeGraphicsFile("beer");
        KeptGame game;

        game.Start();
        ::BECore::Tests::RemoveFileIfPresent(GraphicsFile());
        for (int frame = 0; frame < 3; ++frame) {
            AdvancePastTheDebounce();
            game.Update();
        }
        game.Flush();

        EXPECT_EQ(game.graphics.Get(), GraphicsPreset::Beer);
        EXPECT_FALSE(::BECore::Tests::PathExists(GraphicsFile())) << "the restored look was written back";
    }

    TEST_F(ChessSaveKeeperTest, TwoPicksInsideTheDebounceMakeOneWriteAndAFlushWritesTheLastOne) {
        SetCommandLine({});
        KeptGame game;
        game.Start();
        game.graphics.Set(GraphicsPreset::Beer);
        game.Update();
        ASSERT_TRUE(SavedLook().has_value());
        ASSERT_EQ(SavedLook()->preset, "beer");

        game.graphics.Set(GraphicsPreset::Classic);
        CoreManager::GetTime().Advance(1.0 / 32);
        game.Update();
        EXPECT_EQ(SavedLook()->preset, "beer") << "written inside the debounce";

        game.Flush();
        EXPECT_EQ(SavedLook()->preset, "classic");
    }

    TEST_F(ChessSaveKeeperTest, TheLookIsWrittenWhileAPuzzleIsOn) {
        SetCommandLine({});
        KeptGame game;
        game.Start();

        game.graphics.Set(GraphicsPreset::Beer);
        game.UpdateDuringAPuzzle();

        ASSERT_TRUE(SavedLook().has_value());
        EXPECT_EQ(SavedLook()->preset, "beer");
    }

    TEST_F(ChessSaveKeeperTest, ARefusedLookFileKeepsClassicOneWarningAndStaysAloneWhileTheGameComesBack) {
        SetCommandLine({});
        {
            KeptGame first;
            first.Start();
            PlayLine(first.model, kOpening);
            first.Flush();
        }
        WriteBytes(GraphicsFile(), "this is not a look");
        const Vector<uint8_t> before = BytesOfFile(GraphicsFile());
        const ::BECore::Tests::LogRecorder recorder;
        KeptGame game;

        game.Start();
        for (int frame = 0; frame < 3; ++frame) {
            AdvancePastTheDebounce();
            game.Update();
        }
        game.Flush();
        LogEvent::Flush();

        EXPECT_EQ(game.graphics.Get(), GraphicsPreset::Classic);
        EXPECT_EQ(game.model.GetMoves().size(), 2u);
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {"slot 'graphics'"}), 1u);
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {"slot 'game'"}), 0u);
        EXPECT_EQ(BytesOfFile(GraphicsFile()), before);
    }

    TEST_F(ChessSaveKeeperTest, AnUnknownPresetIdIsRefusedLikeADamagedFile) {
        SetCommandLine({});
        ::BECore::Tests::MakeDirectory(_saves);
        MakeGraphicsFile("sepia");
        const Vector<uint8_t> before = BytesOfFile(GraphicsFile());
        const ::BECore::Tests::LogRecorder recorder;
        KeptGame game;

        game.Start();
        game.Flush();
        LogEvent::Flush();

        EXPECT_EQ(game.graphics.Get(), GraphicsPreset::Classic);
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {"slot 'graphics'"}), 1u);
        EXPECT_EQ(BytesOfFile(GraphicsFile()), before);
    }

    TEST_F(ChessSaveKeeperTest, ARunThatSavesNothingNeitherReadsNorWritesTheLook) {
        SetCommandLine({}, false);  // no flag and no folder: config/ is not mounted, so rememberGame is false
        KeptGame game;

        game.Start();
        game.graphics.Set(GraphicsPreset::Beer);
        AdvancePastTheDebounce();
        game.Update();
        game.Flush();

        EXPECT_FALSE(game.keeper.IsOn());
        EXPECT_FALSE(::BECore::Tests::PathExists(GraphicsFile()));
    }

    TEST_F(ChessSaveKeeperTest, NoSaveReadsAndWritesNotTheLookEvenWithASaveDir) {
        SetCommandLine({"--no-save"});
        ::BECore::Tests::MakeDirectory(_saves);
        MakeGraphicsFile("beer");
        const Vector<uint8_t> before = BytesOfFile(GraphicsFile());
        KeptGame game;

        game.Start();
        EXPECT_EQ(game.graphics.Get(), GraphicsPreset::Classic);
        game.graphics.Set(GraphicsPreset::Beer);
        game.graphics.Set(GraphicsPreset::Classic);
        game.Flush();

        EXPECT_EQ(BytesOfFile(GraphicsFile()), before);
    }

    TEST_F(ChessSaveKeeperTest, AFailedWriteOfTheLookIsLoggedOnceAndTriedAgainAtTheNextPick) {
        SetCommandLine({});
        KeptGame game;
        game.Start();
        ::BECore::Tests::RemoveDirectory(_saves);  // the folder is replaced by a file: the write fails
        WriteBytes(_saves, "in the way");
        const ::BECore::Tests::LogRecorder recorder;

        game.graphics.Set(GraphicsPreset::Beer);
        game.Update();
        AdvancePastTheDebounce();
        game.Update();
        LogEvent::Flush();
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {"slot 'graphics'"}), 1u) << "the same failed revision is not tried again";

        ::BECore::Tests::RemoveFileIfPresent(_saves);
        ::BECore::Tests::MakeDirectory(_saves);
        game.graphics.Set(GraphicsPreset::Classic);
        AdvancePastTheDebounce();
        game.Update();

        ASSERT_TRUE(SavedLook().has_value());
        EXPECT_EQ(SavedLook()->preset, "classic");
    }

}  // namespace BECore::Chess::Tests
