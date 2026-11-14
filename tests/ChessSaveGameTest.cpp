#include "TestEngines.h"

#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Save/SaveGame.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ChessBoardComponent.h>
#include <Chess/Components/ChessPieceComponent.h>
#include <Chess/Save/ChessSaveState.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Common/TempPath.h>
#include <Games/GameManager.h>
#include <iterator>

/**
 * ChessGame with its keeper, on the real GameManager and the shipped scene: what a start restores and what a stop and the frames write. The run's command line decides whether the game
 * remembers itself (Chess/Save/SaveSettings.h): a test that wants the save passes `--save-dir=<temp>`, and one that does not leaves the command line as ChessTests has it, where
 * config/ is not mounted, so nothing is saved without a switch.
 */
namespace BECore::Chess::Tests {

    using namespace BECore::Tests;

    namespace {

        constexpr double kFrame = 1.0 / 32.0;
        constexpr float kPixelTolerance = 0.01f;
        constexpr StringView kGameInProgress = "e2e4 e7e5 g1f3 b8c6";
        constexpr const char* kFoolsMate = "f2f3 e7e5 g2g4 d8h4";
        constexpr const char* kOddStartFen = "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1";

        std::filesystem::path ShippedSceneConfig() {
            return std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "SceneConfig.xml";
        }

        bool Play(ChessGameModel& model, StringView line) {
            size_t begin = 0;
            while (begin < line.size()) {
                size_t end = line.find(' ', begin);
                if (end == StringView::npos) {
                    end = line.size();
                }
                const Optional<Move> move = MoveGenerator::ParseUci(model.GetPosition(), line.substr(begin, end - begin));
                if (!move || !model.TryMove(*move).has_value()) {
                    return false;
                }
                begin = end + 1;
            }
            return true;
        }

        Vector<uint8_t> BytesOfFile(const std::filesystem::path& path) {
            std::ifstream file(path, std::ios::binary);
            Vector<uint8_t> bytes;
            for (std::istreambuf_iterator<char> it(file), end; it != end; ++it) {
                bytes.push_back(static_cast<uint8_t>(*it));
            }
            return bytes;
        }

        String Member(const Json& json, StringView key) {
            const auto value = GetJsonString(json, key);
            return value ? String(value->c_str(), value->size()) : String("<missing>");
        }

    }  // namespace

    class ChessSaveGameTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            for (const String& argument : CoreManager::GetCommandLine().GetArguments()) {
                _savedArguments.push_back(argument);
            }
            _directory = TestTempDirectory("ChessSaveGame");
            std::filesystem::remove_all(_directory);
            std::filesystem::create_directories(_directory);
            _saves = _directory / "saves";
            CoreManager::GetTweens().Clear();
            CoreManager::GetTime().Reset();
            CoreManager::GetFontAtlasLibrary().Clear();
            CoreManager::GetUIVars().Clear();
        }

        void TearDown() override {
            StopManager();
            CoreManager::GetCommandLine().Set(_savedArguments);
            CoreManager::GetTweens().Clear();
            CoreManager::GetTime().Reset();
            CoreManager::GetUIVars().Clear();
            CoreManager::GetFontAtlasLibrary().Clear();
            std::error_code ignored;
            std::filesystem::remove_all(_directory, ignored);
        }

        /// The command line of the run: the flags, and the folder of the test when @p withFolder
        void SetCommandLine(const Vector<String>& flags, bool withFolder) {
            Vector<String> arguments = flags;
            if (withFolder) {
                const String path = FromStdString(_saves.string());
                arguments.push_back(String("--save-dir=") + path);
            }
            CoreManager::GetCommandLine().Set(arguments);
        }

        /// The scene and the game, as the application starts them; Start() of ChessGame runs inside
        void StartManager() {
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
            EngineEnvironment::UseSceneManager(_manager);

            GameManager::GetInstance().Initialize();
            _started = true;
            ASSERT_NE(ChessGame::GetActive(), nullptr) << "Chess is the active game";
        }

        void StopManager() {
            if (!_started) {
                return;
            }
            GameManager::GetInstance().Shutdown();
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            _started = false;
        }

        ChessGame& Game() const {
            ChessGame* game = ChessGame::GetActive();
            EXPECT_NE(game, nullptr);
            return *game;
        }

        /// The doubles of this test's engines (no process, nothing plays by itself while it is the person's turn)
        void UseDoubles() {
            _factory = MakeUnique<CountingFactory>(std::initializer_list<SharedPtr<IChessEngine>>{MakeShared<InstantEngine>()});
            Game().GetOpponent().SetEngineFactory(_factory->AsFactory());
        }

        /// One frame of the game loop, in its order: the clock, the tweens, the scenes (which run the board component's update)
        void Frame(double seconds = kFrame) {
            CoreManager::GetTime().Advance(seconds);
            CoreManager::GetTweens().Advance(CoreManager::GetTime().GetDeltaTime());
            _manager->UpdateAll();
        }

        void Frames(int count) {
            for (int frame = 0; frame < count; ++frame) {
                Frame();
            }
        }

        /// What the game does when the window closes and the next run begins, in one process
        void Restart() {
            Game().Stop();
            Game().Start();
        }

        std::filesystem::path SaveFile() const {
            return _saves / "game.save";
        }

        /// A save file as an earlier run left it
        void MakeSave(const ChessSaveState& state) {
            FileSystem fileSystem;
            SaveGameService service(fileSystem);
            ASSERT_TRUE(service.Initialize(_saves));
            ASSERT_TRUE(service.Save(kChessSaveSlot, kChessSaveSchema, state).has_value());
        }

        Optional<ChessSaveState> Saved() const {
            FileSystem fileSystem;
            SaveGameService service(fileSystem);
            ChessSaveState state;
            if (!service.Initialize(_saves) || !service.Load(kChessSaveSlot, kChessSaveSchema, state)) {
                return std::nullopt;
            }
            return state;
        }

        Json State() const {
            auto state = Game().AsControllable()->GetState();
            EXPECT_TRUE(state.has_value());
            return state ? *state : Json();
        }

        Json OpponentOfState() const {
            const Json state = State();
            const Json* found = FindJsonMember(state, "opponent");
            EXPECT_NE(found, nullptr);
            return found ? *found : Json();
        }

        /// A state with moves and a computer opponent, as the first run of a test would have left it
        static ChessSaveState SavedGameOf(StringView moves, const char* plays) {
            ChessSaveState state;
            state.startFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
            state.moves = String(moves.data(), moves.size());
            state.plays = plays;
            state.whiteEngine = "own";
            state.whiteLevel = "hard";
            state.blackEngine = "own";
            state.blackLevel = "hard";
            state.personColor = "white";
            return state;
        }

        Vector<IntrusivePtr<SceneNode>> PieceNodes() const {
            Vector<IntrusivePtr<SceneNode>> nodes;
            for (const auto& node : _manager->GetSceneByName(PoolString::Intern("ChessScene"))->GetRootNode().GetChildren()) {
                if (node->GetComponent<ChessPieceComponent>()) {
                    nodes.push_back(node);
                }
            }
            return nodes;
        }

        /// Every piece that stands on a square is drawn on it, at once: no slide is under way
        void ExpectEveryPieceOnItsSquare(const char* what) const {
            size_t checked = 0;
            for (const auto& node : PieceNodes()) {
                const Square square = Game().GetModel().GetSquareOf(node->GetComponent<ChessPieceComponent>().Get());
                if (!square.IsInside()) {
                    continue;
                }
                float x = 0.0f;
                float y = 0.0f;
                ChessBoardComponent::GetActive()->SquareToPixel(square.file, square.rank, x, y);
                const auto transform = node->GetComponent<TransformComponent>();
                ASSERT_TRUE(transform);
                EXPECT_NEAR(transform->_x, x, kPixelTolerance) << what << " " << node->GetName().CStr();
                EXPECT_NEAR(transform->_y, y, kPixelTolerance) << what << " " << node->GetName().CStr();
                ++checked;
            }
            EXPECT_GT(checked, 0u);
        }

        Vector<String> _savedArguments;
        std::filesystem::path _directory;
        std::filesystem::path _saves;
        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        UniquePtr<CountingFactory> _factory;
        bool _started = false;
    };

    // ---- Clean starts: nothing is read or written without a switch ----

    TEST_F(ChessSaveGameTest, AStartWithNoFlagIsAFreshGameAndMakesNoFile) {
        CoreManager::GetCommandLine().Set(Vector<String>{});  // config/ is not mounted in ChessTests, so rememberGame is false: no switch, no memory
        StartManager();
        const ChessGame& game = Game();

        EXPECT_FALSE(game.GetKeeper().IsOn());
        EXPECT_EQ(game.GetModel().GetPosition().ToFen(), Position::kStartFen);
        EXPECT_TRUE(game.GetModel().GetMoves().empty());
        EXPECT_FALSE(game.GetOpponent().GetPlays().has_value());
        EXPECT_EQ(game.GetOpponent().GetEngine(), kDefaultOpponentEngine);
        EXPECT_EQ(game.GetOpponent().GetLevel(), kDefaultOpponentLevel);

        UseDoubles();
        ASSERT_TRUE(Play(Game().GetModel(), kGameInProgress));
        Frames(4);
        Game().Stop();
        EXPECT_TRUE(std::filesystem::is_empty(_directory)) << "a folder or a file was made";
    }

    TEST_F(ChessSaveGameTest, AControlRunIgnoresASaveThatNoFlagNames) {
        MakeSave(SavedGameOf(kGameInProgress, "none"));
        const Vector<uint8_t> before = BytesOfFile(SaveFile());
        SetCommandLine({"--control", "--headless", "--manual-clock"}, false);
        StartManager();

        EXPECT_FALSE(Game().GetKeeper().IsOn());
        EXPECT_TRUE(Game().GetModel().GetMoves().empty());
        ASSERT_TRUE(Play(Game().GetModel(), "d2d4"));
        Frames(4);
        Game().Stop();

        EXPECT_EQ(BytesOfFile(SaveFile()), before);
    }

    TEST_F(ChessSaveGameTest, NoSaveLeavesTheSaveAloneEvenWithASaveDir) {
        MakeSave(SavedGameOf(kGameInProgress, "none"));
        const Vector<uint8_t> before = BytesOfFile(SaveFile());
        SetCommandLine({"--no-save"}, true);
        StartManager();

        EXPECT_TRUE(Game().GetModel().GetMoves().empty());
        ASSERT_TRUE(Play(Game().GetModel(), "d2d4"));
        Game().Stop();

        EXPECT_EQ(BytesOfFile(SaveFile()), before);
    }

    // ---- Asked for: a save and a restore ----

    TEST_F(ChessSaveGameTest, TheGameAndTheChoiceComeBackAfterAStop) {
        SetCommandLine({"--control", "--headless"}, true);
        StartManager();
        UseDoubles();
        ASSERT_TRUE(Play(Game().GetModel(), kGameInProgress));
        ASSERT_TRUE(Game().GetChoice().SetEngine("own").has_value());
        ASSERT_TRUE(Game().GetChoice().SetLevel("hard").has_value());
        ASSERT_TRUE(Game().GetChoice().SetMode("computer").has_value());  // the person is White and it is White's turn: the program has nothing to answer
        Frames(3);
        const String fen = String(Game().GetModel().GetPosition().ToFen().c_str());
        const Json before = State();

        Restart();

        const Json after = State();
        EXPECT_EQ(Member(after, "fen"), fen);
        EXPECT_EQ(Member(after, "startFen"), Member(before, "startFen"));
        ASSERT_NE(FindJsonMember(after, "moves"), nullptr);
        EXPECT_EQ(*FindJsonMember(after, "moves"), *FindJsonMember(before, "moves"));
        const Json opponent = OpponentOfState();
        EXPECT_EQ(Member(opponent, "plays"), "black");
        EXPECT_EQ(Member(opponent, "level"), "hard");
        EXPECT_EQ(Member(opponent, "engine"), "own");
        EXPECT_EQ(Game().GetChoice().ColorId(), "white");
        EXPECT_EQ(Game().GetModel().GetMoves().size(), 4u);
    }

    TEST_F(ChessSaveGameTest, AFrameWritesAChangeWithoutAStop) {
        SetCommandLine({}, true);
        StartManager();
        ASSERT_TRUE(Play(Game().GetModel(), "e2e4 e7e5"));

        Frame();  // the first write of a run waits for nothing, and it is not the end of the game that writes it

        const Optional<ChessSaveState> saved = Saved();
        ASSERT_TRUE(saved.has_value());
        EXPECT_EQ(saved->moves, "e2e4 e7e5");
    }

    TEST_F(ChessSaveGameTest, FramesAfterAStopWriteNothing) {
        SetCommandLine({}, true);
        StartManager();
        UseDoubles();
        ASSERT_TRUE(Play(Game().GetModel(), "e2e4"));
        ASSERT_TRUE(Game().GetChoice().SetEngine("own").has_value());
        ASSERT_TRUE(Game().GetChoice().SetMode("computer").has_value());
        Game().Stop();

        // The window still draws a few frames after Stop(): the opponent is gone (two people), which must not become the saved choice
        ASSERT_TRUE(Play(Game().GetModel(), "e7e5"));
        Frames(static_cast<int>(1.0 / kFrame) + 8);

        const Optional<ChessSaveState> saved = Saved();
        ASSERT_TRUE(saved.has_value());
        EXPECT_EQ(saved->moves, "e2e4");
        EXPECT_EQ(saved->plays, "black");
    }

    TEST_F(ChessSaveGameTest, AResetThenARestartGivesTheFreshGameAndKeepsTheChoice) {
        SetCommandLine({}, true);
        StartManager();
        UseDoubles();
        ASSERT_TRUE(Play(Game().GetModel(), kGameInProgress));
        ASSERT_TRUE(Game().GetChoice().SetLevel("max").has_value());
        ASSERT_TRUE(Game().GetChoice().SetEngine("own").has_value());
        Frames(2);

        Game().Reset();
        Restart();

        EXPECT_TRUE(Game().GetModel().GetMoves().empty());
        EXPECT_EQ(Game().GetModel().GetPosition().ToFen(), Position::kStartFen);
        EXPECT_EQ(Game().GetChoice().LevelId(), "max");
        EXPECT_EQ(Game().GetChoice().EngineId(), "own");
    }

    TEST_F(ChessSaveGameTest, AGameBegunFromAFenReturnsWithItsStartFen) {
        SetCommandLine({}, true);
        StartManager();
        {
            Json request = Json::object();
            request["fen"] = MakeJsonString(kOddStartFen);
            ASSERT_TRUE(Game().AsControllable()->SetState(request).has_value());
        }
        ASSERT_TRUE(Play(Game().GetModel(), "e2e4"));
        const String fen = String(Game().GetModel().GetPosition().ToFen().c_str());
        Frames(2);

        Restart();

        const Json state = State();
        EXPECT_EQ(Member(state, "startFen"), kOddStartFen);
        EXPECT_EQ(Member(state, "fen"), fen);
        EXPECT_EQ(Game().GetModel().GetMoves().size(), 1u);
    }

    TEST_F(ChessSaveGameTest, AFinishedGameIsNotRestoredButTheChoiceIs) {
        SetCommandLine({}, true);
        StartManager();
        UseDoubles();
        ASSERT_TRUE(Game().GetChoice().SetLevel("hard").has_value());
        ASSERT_TRUE(Game().GetChoice().SetEngine("own").has_value());
        ASSERT_TRUE(Play(Game().GetModel(), kFoolsMate));
        ASSERT_TRUE(Game().GetModel().IsGameOver());
        Frames(2);

        Restart();

        EXPECT_FALSE(Game().GetModel().IsGameOver());
        EXPECT_TRUE(Game().GetModel().GetMoves().empty());
        EXPECT_EQ(Game().GetChoice().LevelId(), "hard");
        EXPECT_EQ(Game().GetChoice().EngineId(), "own");
    }

    TEST_F(ChessSaveGameTest, ASaveThatIsThereAtTheFirstStartIsRestoredBeforeThePiecesRegister) {
        MakeSave(SavedGameOf("e2e4 e7e5 g1f3 b8c6 f1b5", "none"));
        SetCommandLine({}, true);

        StartManager();
        Frames(2);

        EXPECT_EQ(Game().GetModel().GetMoves().size(), 5u);
        ExpectEveryPieceOnItsSquare("restored at the first start");
    }

    TEST_F(ChessSaveGameTest, ARestoredMoveIsNotSlid) {
        MakeSave(SavedGameOf("e2e4", "none"));
        SetCommandLine({}, true);

        StartManager();
        Frames(2);  // the first frame registers the pieces, the second places them
        ExpectEveryPieceOnItsSquare("the frame that placed the pieces");
        Frames(2);
        ExpectEveryPieceOnItsSquare("two frames later: no slide under way");
    }

    TEST_F(ChessSaveGameTest, ARestoredMoveOnPiecesThatAreAlreadyThereIsNotSlid) {
        SetCommandLine({}, true);
        StartManager();
        Frames(3);  // the pieces are registered and have been seen at the opening
        ExpectEveryPieceOnItsSquare("the fresh game");
        Game().Stop();
        MakeSave(SavedGameOf("e2e4", "none"));

        Game().Start();  // restores "e2e4" into a model whose pieces stand at the opening
        Frame();
        ExpectEveryPieceOnItsSquare("the frame after a restore");
        Frames(2);
        ExpectEveryPieceOnItsSquare("two frames after a restore");
        EXPECT_EQ(Game().GetModel().GetMoves().size(), 1u);
    }

}  // namespace BECore::Chess::Tests
