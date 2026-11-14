#include "TestEngines.h"

#include <BECore/Save/SaveGame.h>
#include <Chess/Save/ChessSaveState.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>

/// The save of a game and the opponent choice (Chess/Save/ChessSaveState.h): the state a game captures, written and read back through a real SaveGameService on a temp folder, and
/// checked and applied in two steps. A damaged file is refused as a whole: the live game, the controller and the choice stay exactly as they were, and the file on disk is not touched
namespace BECore::Chess::Tests {

    namespace {

        /// Plays a line of UCI moves, separated by spaces, through the model's own TryMove
        void PlayLine(ChessGameModel& model, StringView line) {
            size_t begin = 0;
            while (begin < line.size()) {
                size_t end = line.find(' ', begin);
                if (end == StringView::npos) {
                    end = line.size();
                }
                const Optional<Move> move = MoveGenerator::ParseUci(model.GetPosition(), line.substr(begin, end - begin));
                ASSERT_TRUE(move.has_value()) << "not a legal move: " << std::string(line.substr(begin, end - begin).data(), end - begin);
                ASSERT_TRUE(model.TryMove(*move).has_value());
                begin = end + 1;
            }
        }

        std::string MovesText(const ChessGameModel& model) {
            std::string text;
            for (const Move move : model.GetMoves()) {
                text += text.empty() ? "" : " ";
                text += move.ToUci().c_str();
            }
            return text;
        }

        SeatChoice Seat(OpponentEngine engine, StringView level) {
            const Optional<size_t> index = FindOpponentLevel(level);
            EXPECT_TRUE(index.has_value());
            return SeatChoice{engine, index.value_or(kDefaultOpponentLevel)};
        }

        /// The fields of a state as one comparable text
        std::string Describe(const ChessSaveState& state) {
            std::string text;
            for (const String* field : {&state.startFen, &state.moves, &state.plays, &state.whiteEngine, &state.whiteLevel, &state.blackEngine, &state.blackLevel, &state.personColor}) {
                text += std::string(field->c_str(), field->size()) + "|";
            }
            return text;
        }

        Vector<uint8_t> BytesOf(const Vector<uint8_t>& bytes) {
            return Vector<uint8_t>(bytes.begin(), bytes.end());
        }

        constexpr StringView kFoolsMate = "f2f3 e7e5 g2g4 d8h4";
        constexpr StringView kGameInProgress = "e2e4 e7e5 g1f3 b8c6";

    }  // namespace

    /// A game, its opponent controller (every engine a double: no process) and the choice of the person, as ChessGame holds them
    struct SavedGame {
        ChessGameModel model;
        OpponentController controller;
        OpponentChoice choice;
        CountingFactory factory;

        SavedGame() : controller(model), choice(controller), factory({eastl::make_shared<InstantEngine>()}) {
            controller.SetEngineFactory(factory.AsFactory());
        }

        [[nodiscard]] ChessSaveState Captured() const {
            return Capture(model, controller, choice);
        }
    };

    class ChessSaveStateTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ::BECore::Tests::EngineEnvironment::EnsureCoreServices();  // TaskManager, which the controller's opponents need
        }

        void SetUp() override {
            _directory = ::BECore::Tests::TestTempDirectory("ChessSaveState");
            std::filesystem::remove_all(_directory);
            ASSERT_TRUE(_service.Initialize(_directory / "saves"));
        }

        void TearDown() override {
            std::error_code ignored;
            std::filesystem::remove_all(_directory, ignored);
        }

        /// What the game does at the start: read the slot, check it, apply it. The error of the first step that refused
        std::expected<SaveInfo, SaveError> LoadAndRestore(SavedGame& game) {
            ChessSaveState state;
            const std::expected<SaveInfo, SaveError> info = _service.Load(kChessSaveSlot, kChessSaveSchema, state);
            if (!info) {
                return std::unexpected(info.error());
            }
            const std::expected<RestorePlan, SaveError> plan = Validate(state);
            if (!plan) {
                return std::unexpected(plan.error());
            }
            Apply(*plan, game.model, game.controller, game.choice);
            return *info;
        }

        void SaveGameOf(const SavedGame& game) {
            const auto saved = _service.Save(kChessSaveSlot, kChessSaveSchema, game.Captured());
            ASSERT_TRUE(saved.has_value()) << saved.error().detail.c_str();
        }

        Vector<uint8_t> FileBytes() const {
            const std::expected<Vector<uint8_t>, SaveError> bytes = _service.ReadSlot(kChessSaveSlot);
            return bytes ? BytesOf(*bytes) : Vector<uint8_t>();
        }

        /// Everything that must not move when a file is refused
        struct Snapshot {
            std::string state;
            uint64_t modelVersion = 0;
            uint64_t revision = 0;
            bool hasError = false;
        };

        static Snapshot Snap(const SavedGame& game) {
            return Snapshot{Describe(game.Captured()), game.model.GetVersion(), game.controller.GetRevision(), game.controller.GetError().has_value()};
        }

        static void ExpectSame(const Snapshot& before, const Snapshot& after) {
            EXPECT_EQ(after.state, before.state);
            EXPECT_EQ(after.modelVersion, before.modelVersion);
            EXPECT_EQ(after.revision, before.revision);
            EXPECT_EQ(after.hasError, before.hasError);
        }

        /// The file is refused with @p kind: the game, the controller and the choice stay as they were and the file is byte for byte the same
        void ExpectRefused(SaveErrorKind kind, const char* what) {
            SCOPED_TRACE(what);
            SavedGame game;
            PlayLine(game.model, kGameInProgress);
            ASSERT_TRUE(game.controller.ChooseSeats(ProgramSides::Of(PieceColor::Black), Seat(OpponentEngine::Own, "hard"), Seat(OpponentEngine::Own, "hard")).has_value());
            const Snapshot before = Snap(game);
            const Vector<uint8_t> fileBefore = FileBytes();

            const std::expected<SaveInfo, SaveError> restored = LoadAndRestore(game);

            ASSERT_FALSE(restored.has_value()) << "the file was accepted";
            EXPECT_EQ(restored.error().kind, kind) << restored.error().detail.c_str();
            EXPECT_FALSE(restored.error().detail.empty());
            ExpectSame(before, Snap(game));
            EXPECT_EQ(FileBytes(), fileBefore);
        }

        /// A good state of a game with something in every field
        static ChessSaveState GoodState() {
            SavedGame game;
            PlayLine(game.model, kGameInProgress);
            [[maybe_unused]] const auto picked = game.controller.ChooseSeats(ProgramSides::Of(PieceColor::White), Seat(OpponentEngine::Own, "max"), Seat(OpponentEngine::Own, "easy"));
            return game.Captured();
        }

        void SaveAltered(const eastl::function<void(ChessSaveState&)>& alter) {
            ChessSaveState state = GoodState();
            alter(state);
            ASSERT_TRUE(_service.Save(kChessSaveSlot, kChessSaveSchema, state).has_value());
        }

        std::filesystem::path _directory;
        FileSystem _fileSystem;
        SaveGameService _service{_fileSystem};
    };

    TEST_F(ChessSaveStateTest, CaptureOfANewGameSaysNobodyPlays) {
        SavedGame game;

        const ChessSaveState state = game.Captured();

        EXPECT_EQ(state.startFen, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        EXPECT_TRUE(state.moves.empty());
        EXPECT_EQ(state.plays, "none");
        EXPECT_EQ(state.personColor, "white");
    }

    TEST_F(ChessSaveStateTest, CastlingEnPassantAndTheMovesComeBack) {
        SavedGame source;
        PlayLine(source.model, "e2e4 a7a6 e4e5 d7d5 e5d6 g8f6 g1f3 b8c6 f1c4 e7e6 e1g1 f8e7");
        SaveGameOf(source);

        SavedGame target;
        const auto restored = LoadAndRestore(target);

        ASSERT_TRUE(restored.has_value()) << restored.error().detail.c_str();
        EXPECT_EQ(target.model.GetPosition().ToFen(), source.model.GetPosition().ToFen());
        EXPECT_EQ(MovesText(target.model), MovesText(source.model));
        EXPECT_EQ(target.model.GetStartFen(), source.model.GetStartFen());
        EXPECT_EQ(target.model.GetStatus(), source.model.GetStatus());
        EXPECT_EQ(target.model.GetMoves().size(), 12u);
    }

    TEST_F(ChessSaveStateTest, AGameFromASetPositionKeepsItsStartAndItsPromotion) {
        SavedGame source;
        source.model.SetPosition(*Position::FromFen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1"));
        PlayLine(source.model, "a7a8r e8d7 e1e2");
        SaveGameOf(source);

        SavedGame target;
        const auto restored = LoadAndRestore(target);

        ASSERT_TRUE(restored.has_value()) << restored.error().detail.c_str();
        EXPECT_EQ(target.model.GetStartFen(), "4k3/P7/8/8/8/8/8/4K3 w - - 0 1");
        EXPECT_EQ(MovesText(target.model), "a7a8r e8d7 e1e2");
        EXPECT_EQ(target.model.GetPosition().ToFen(), source.model.GetPosition().ToFen());
        EXPECT_EQ(target.model.GetPieceAt(Sq("a8")).type, PieceType::Rook);
    }

    TEST_F(ChessSaveStateTest, ARepetitionIsKeptSoTheThirdOneEndsTheGameAsBefore) {
        SavedGame source;
        PlayLine(source.model, "g1f3 g8f6 f3g1 f6g8");  // the opening position for the second time
        ASSERT_EQ(source.model.GetStatus(), GameStatus::Ongoing);
        SaveGameOf(source);

        SavedGame target;
        ASSERT_TRUE(LoadAndRestore(target).has_value());

        EXPECT_EQ(target.model.GetStatus(), GameStatus::Ongoing);
        EXPECT_EQ(target.model.GetPosition().RepetitionCount(), source.model.GetPosition().RepetitionCount());
        PlayLine(source.model, "g1f3 g8f6 f3g1 f6g8");
        PlayLine(target.model, "g1f3 g8f6 f3g1 f6g8");
        EXPECT_EQ(source.model.GetStatus(), GameStatus::DrawThreefold);
        EXPECT_EQ(target.model.GetStatus(), source.model.GetStatus());
    }

    TEST_F(ChessSaveStateTest, TwoPeopleKeepTheSeatsAndTheColorOfThePerson) {
        SavedGame source;
        ASSERT_TRUE(source.controller.ChooseSeats({}, Seat(OpponentEngine::Own, "hard"), Seat(OpponentEngine::Stockfish, "easy")).has_value());
        ASSERT_TRUE(source.choice.SetColor("black").has_value());
        SaveGameOf(source);

        SavedGame target;
        ASSERT_TRUE(LoadAndRestore(target).has_value());

        EXPECT_EQ(Describe(target.Captured()), Describe(source.Captured()));
        EXPECT_EQ(target.choice.ColorId(), "black");
        EXPECT_EQ(target.choice.ModeId(), "two");
        EXPECT_EQ(target.controller.GetSeatChoice(PieceColor::Black), Seat(OpponentEngine::Stockfish, "easy"));
    }

    TEST_F(ChessSaveStateTest, OneProgramKeepsItsSideAndBothSeats) {
        SavedGame source;
        PlayLine(source.model, "e2e4");
        ASSERT_TRUE(source.controller.ChooseSeats(ProgramSides::Of(PieceColor::Black), Seat(OpponentEngine::Stockfish, "max"), Seat(OpponentEngine::Own, "easy")).has_value());
        SaveGameOf(source);

        SavedGame target;
        ASSERT_TRUE(LoadAndRestore(target).has_value());

        EXPECT_EQ(Describe(target.Captured()), Describe(source.Captured()));
        EXPECT_EQ(target.controller.GetPlays(), PieceColor::Black);
        EXPECT_EQ(target.choice.ColorId(), "white");
        EXPECT_EQ(target.controller.GetSeatChoice(PieceColor::White), Seat(OpponentEngine::Stockfish, "max"));
        EXPECT_EQ(target.controller.GetSeatChoice(PieceColor::Black), Seat(OpponentEngine::Own, "easy"));
    }

    TEST_F(ChessSaveStateTest, TwoProgramsKeepTheirSeatsAndTheRememberedColor) {
        SavedGame source;
        ASSERT_TRUE(source.choice.SetColor("black").has_value());
        ASSERT_TRUE(source.controller.ChooseSeats({true, true}, Seat(OpponentEngine::Own, "hard"), Seat(OpponentEngine::Stockfish, "max")).has_value());
        SaveGameOf(source);

        SavedGame target;
        ASSERT_TRUE(LoadAndRestore(target).has_value());

        EXPECT_EQ(Describe(target.Captured()), Describe(source.Captured()));
        EXPECT_TRUE(target.controller.IsWatching());
        EXPECT_EQ(target.choice.ColorId(), "black");
    }

    TEST_F(ChessSaveStateTest, AFinishedGameIsNotRestoredButTheChoiceIs) {
        SavedGame source;
        PlayLine(source.model, kFoolsMate);
        ASSERT_EQ(source.model.GetStatus(), GameStatus::Checkmate);
        ASSERT_TRUE(source.controller.ChooseSeats(ProgramSides::Of(PieceColor::Black), Seat(OpponentEngine::Own, "hard"), Seat(OpponentEngine::Own, "hard")).has_value());
        SaveGameOf(source);
        ChessSaveState state;
        ASSERT_TRUE(_service.Load(kChessSaveSlot, kChessSaveSchema, state).has_value());
        const std::expected<RestorePlan, SaveError> plan = Validate(state);
        ASSERT_TRUE(plan.has_value());
        EXPECT_TRUE(plan->finished);

        SavedGame target;
        const uint64_t versionBefore = target.model.GetVersion();
        ASSERT_TRUE(LoadAndRestore(target).has_value());

        EXPECT_EQ(target.model.GetVersion(), versionBefore);
        EXPECT_EQ(target.model.GetStatus(), GameStatus::Ongoing);
        EXPECT_TRUE(target.model.GetMoves().empty());
        EXPECT_EQ(target.model.GetPosition().ToFen(), eastl::string(Position::kStartFen.data(), Position::kStartFen.size()));
        EXPECT_EQ(target.controller.GetPlays(), PieceColor::Black);
        EXPECT_EQ(target.controller.GetSeatChoice(PieceColor::Black), Seat(OpponentEngine::Own, "hard"));
    }

    TEST_F(ChessSaveStateTest, APickTheControllerRefusesLeavesThePositionRestoredAndTheProgramOut) {
        SavedGame source;
        PlayLine(source.model, kGameInProgress);
        ASSERT_TRUE(source.controller.ChooseSeats(ProgramSides::Of(PieceColor::White), Seat(OpponentEngine::Stockfish, "max"), Seat(OpponentEngine::Stockfish, "max")).has_value());
        SaveGameOf(source);

        SavedGame target;
        CountingFactory noStockfish(EngineError{EngineErrorKind::LaunchFailed, "no Stockfish in the test"});
        target.controller.SetEngineFactory(noStockfish.AsFactory());
        const auto restored = LoadAndRestore(target);

        ASSERT_TRUE(restored.has_value()) << "a refused pick is not a failure of the restore";
        EXPECT_EQ(MovesText(target.model), MovesText(source.model));
        EXPECT_FALSE(target.controller.GetPlays().has_value());
        ASSERT_TRUE(target.controller.GetError().has_value());
        EXPECT_EQ(target.controller.GetError()->kind, EngineErrorKind::LaunchFailed);
        EXPECT_EQ(target.choice.ColorId(), "black") << "the person of the saved game stays remembered";
    }

    TEST_F(ChessSaveStateTest, NoFileIsNotFound) {
        SavedGame game;

        const auto restored = LoadAndRestore(game);

        ASSERT_FALSE(restored.has_value());
        EXPECT_EQ(restored.error().kind, SaveErrorKind::NotFound);
    }

    // The damage: every case writes a file the game must refuse, and ExpectRefused() checks that nothing moved

    TEST_F(ChessSaveStateTest, ABadStartFenIsRefused) {
        SaveAltered([](ChessSaveState& state) { state.startFen = "this is not a position"; });
        ExpectRefused(SaveErrorKind::Corrupt, "bad FEN");
    }

    TEST_F(ChessSaveStateTest, AnEmptyStartFenIsRefused) {
        SaveAltered([](ChessSaveState& state) { state.startFen = ""; });
        ExpectRefused(SaveErrorKind::Corrupt, "empty FEN");
    }

    TEST_F(ChessSaveStateTest, AnIllegalMoveInTheMiddleIsRefused) {
        SaveAltered([](ChessSaveState& state) { state.moves = "e2e4 e7e5 e2e4 b8c6"; });
        ExpectRefused(SaveErrorKind::Corrupt, "illegal move");
    }

    TEST_F(ChessSaveStateTest, AMoveAfterTheEndOfTheGameIsRefused) {
        SaveAltered([](ChessSaveState& state) { state.moves = String(kFoolsMate.data(), kFoolsMate.size()) + " a2a3"; });
        ExpectRefused(SaveErrorKind::Corrupt, "move after the mate");
    }

    TEST_F(ChessSaveStateTest, MovesWithAnEmptyPlaceAreRefused) {
        SaveAltered([](ChessSaveState& state) { state.moves = "e2e4  e7e5"; });
        ExpectRefused(SaveErrorKind::Corrupt, "double space");
    }

    TEST_F(ChessSaveStateTest, MovesWithATrailingSpaceAreRefused) {
        SaveAltered([](ChessSaveState& state) { state.moves = "e2e4 "; });
        ExpectRefused(SaveErrorKind::Corrupt, "trailing space");
    }

    TEST_F(ChessSaveStateTest, AnUnknownEngineIsRefused) {
        SaveAltered([](ChessSaveState& state) { state.whiteEngine = "deepblue"; });
        ExpectRefused(SaveErrorKind::Corrupt, "white engine");
    }

    TEST_F(ChessSaveStateTest, AnUnknownEngineOfTheBlackSeatIsRefused) {
        SaveAltered([](ChessSaveState& state) { state.blackEngine = "Own"; });
        ExpectRefused(SaveErrorKind::Corrupt, "black engine, wrong case");
    }

    TEST_F(ChessSaveStateTest, AnUnknownLevelIsRefused) {
        SaveAltered([](ChessSaveState& state) { state.whiteLevel = "legendary"; });
        ExpectRefused(SaveErrorKind::Corrupt, "level");
    }

    TEST_F(ChessSaveStateTest, ALevelAsAnIndexIsRefused) {
        SaveAltered([](ChessSaveState& state) { state.blackLevel = "9"; });
        ExpectRefused(SaveErrorKind::Corrupt, "level out of range");
    }

    TEST_F(ChessSaveStateTest, BadPlaysIsRefused) {
        SaveAltered([](ChessSaveState& state) { state.plays = "red"; });
        ExpectRefused(SaveErrorKind::Corrupt, "plays");
    }

    TEST_F(ChessSaveStateTest, ABadColorOfThePersonIsRefused) {
        SaveAltered([](ChessSaveState& state) { state.personColor = ""; });
        ExpectRefused(SaveErrorKind::Corrupt, "person color");
    }

    TEST_F(ChessSaveStateTest, APersonWhoPlaysTheProgramsColorIsRefused) {
        SaveAltered([](ChessSaveState& state) {
            state.plays = "white";
            state.personColor = "white";
        });
        ExpectRefused(SaveErrorKind::Corrupt, "both play white");
    }

    TEST_F(ChessSaveStateTest, AnEmptyFileIsRefused) {
        ASSERT_TRUE(_service.WriteSlot(kChessSaveSlot, {}).has_value());
        ExpectRefused(SaveErrorKind::Corrupt, "empty file");
    }

    TEST_F(ChessSaveStateTest, ATruncatedFileIsRefused) {
        SaveAltered([](ChessSaveState&) {});
        Vector<uint8_t> bytes = *_service.ReadSlot(kChessSaveSlot);
        bytes.resize(bytes.size() / 2);
        ASSERT_TRUE(_service.WriteSlot(kChessSaveSlot, bytes).has_value());
        ExpectRefused(SaveErrorKind::Corrupt, "truncated");
    }

    TEST_F(ChessSaveStateTest, AFileOfAnotherGameIsRefused) {
        ASSERT_TRUE(_service.Save(kChessSaveSlot, SaveSchema{"Checkers", 1, 1}, GoodState()).has_value());
        ExpectRefused(SaveErrorKind::WrongGame, "another game");
    }

    TEST_F(ChessSaveStateTest, AFileOfANewerVersionIsRefused) {
        ASSERT_TRUE(_service.Save(kChessSaveSlot, SaveSchema{kChessSaveSchema.game, kChessSaveSchema.version + 1, kChessSaveSchema.version}, GoodState()).has_value());
        ExpectRefused(SaveErrorKind::VersionTooNew, "newer version");
    }

    TEST_F(ChessSaveStateTest, AValidationThatFailsLeavesTheLiveObjectsAloneWithoutAFileSystem) {
        // Validate() has no file system and no live object: the same state, checked twice, gives the same answer and moves nothing
        ChessSaveState state = GoodState();
        state.moves = "e2e4 e2e4";
        SavedGame game;
        const Snapshot before = Snap(game);

        const auto first = Validate(state);
        const auto second = Validate(state);

        ASSERT_FALSE(first.has_value());
        ASSERT_FALSE(second.has_value());
        EXPECT_EQ(first.error().detail, second.error().detail);
        EXPECT_EQ(first.error().detail.find("e2e4"), String::npos) << "the reason names the move number, not the text of the file";
        ExpectSame(before, Snap(game));
    }

}  // namespace BECore::Chess::Tests
