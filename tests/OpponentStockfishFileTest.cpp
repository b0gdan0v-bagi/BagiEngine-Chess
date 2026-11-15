#include <Chess/Engine/OpponentController.h>
#include <Chess/Game/ChessGameModel.h>
#include <tests/ChessTestUtil.h>
#include <tests/FakeEngineUtil.h>
#include <tests/TestEngines.h>

/// Whether OpponentController offers Stockfish: a machine-independent test of the "no Stockfish file found" rule. Every case names its own stockfishPath (a file that is not there, or one
/// made in a temporary directory), so neither $STOCKFISH nor PATH of the machine is asked. Nothing plays, so no search and no process start. Where Stockfish is not in the build
/// (CHESS_NO_STOCKFISH) the same rule is held by the cases of OpponentControllerTest
#if !CHESS_NO_STOCKFISH

namespace BECore::Chess::Tests {

    namespace {

        constexpr const char* kFileName = "stockfish-file";
        constexpr std::filesystem::perms kExecutable =
            std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec | std::filesystem::perms::others_read | std::filesystem::perms::others_exec;

        /// The file FindStockfish() accepts: a regular file with the execute bit (what it holds is never read before a search)
        void MakeStockfishFile(const std::filesystem::path& path) {
            {
                //-NoStd: file layer (engine-file-layer)
                std::ofstream out(path);
                out << "not an engine\n";
            }
            std::filesystem::permissions(path, kExecutable, std::filesystem::perm_options::replace);
        }

        OpponentSettings SettingsAt(const std::filesystem::path& path) {
            return OpponentSettings{FromStdString(path.string())};
        }

        size_t EasyLevel() {
            const Optional<size_t> index = FindOpponentLevel("easy");
            EXPECT_TRUE(index.has_value());
            return index.value_or(kDefaultOpponentLevel);
        }

    }  // namespace

    TEST(OpponentStockfishFileTest, WithNoFileFoundStockfishIsNotOfferedAndTheGameWouldStartOnTheOwnEngine) {
        const TempDirectory directory;
        ChessGameModel model;
        OpponentController controller(model, SettingsAt(directory.File(kFileName)));

        EXPECT_FALSE(controller.OffersStockfish());
        EXPECT_EQ(controller.DefaultEngine(), OpponentEngine::Own);
        EXPECT_EQ(controller.GetEngine(), kDefaultOpponentEngine) << "a fresh controller still starts with what the build prefers: DefaultEngine() is what a game asks for";
        EXPECT_EQ(controller.GetRevision(), 0u);
    }

    TEST(OpponentStockfishFileTest, ThePickOfStockfishWithNoFileIsRefusedWhileNobodyPlaysAndNothingChanges) {
        const TempDirectory directory;
        ChessGameModel model;
        OpponentController controller(model, SettingsAt(directory.File(kFileName)));
        ASSERT_TRUE(controller.SetEngine(OpponentEngine::Own).has_value());
        const uint64_t revision = controller.GetRevision();

        const auto refused = controller.SetEngine(OpponentEngine::Stockfish);

        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().kind, EngineErrorKind::LaunchFailed);
        EXPECT_NE(refused.error().detail.find(FromStdString(directory.File(kFileName).string()).c_str()), String::npos) << "the sentence names the path that was tried";
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own);
        EXPECT_FALSE(controller.GetPlays().has_value());
        ASSERT_TRUE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetError()->kind, EngineErrorKind::LaunchFailed);
        EXPECT_EQ(controller.GetRevision(), revision + 1) << "the error that appeared is shown once";

        // The person picks the engine they have: the error goes, as with any pick
        ASSERT_TRUE(controller.SetEngine(OpponentEngine::Own).has_value());
        EXPECT_FALSE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own);
    }

    TEST(OpponentStockfishFileTest, APickWithAStockfishSeatIsRefusedAsAWholeAlsoForTheSeatNobodyPlays) {
        const TempDirectory directory;
        ChessGameModel model;
        OpponentController controller(model, SettingsAt(directory.File(kFileName)));
        ASSERT_TRUE(controller.SetEngine(OpponentEngine::Own).has_value());
        const SeatChoice own{OpponentEngine::Own, EasyLevel()};
        const SeatChoice stockfish{OpponentEngine::Stockfish, EasyLevel()};

        const auto refused = controller.ChooseSeats(ProgramSides::Of(PieceColor::White), own, stockfish);  // Black's seat is not played, and still refused

        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().kind, EngineErrorKind::LaunchFailed);
        EXPECT_FALSE(controller.PlaysFor(PieceColor::White)) << "the whole pick is refused: White's good seat is not applied either";
        EXPECT_EQ(controller.GetSeatChoice(PieceColor::Black).engine, OpponentEngine::Own);

        EXPECT_FALSE(controller.ChooseSeats(ProgramSides{}, stockfish, own).has_value());
        EXPECT_FALSE(controller.Choose(PieceColor::Black, EasyLevel(), OpponentEngine::Stockfish).has_value());
        EXPECT_FALSE(controller.GetPlays().has_value());
    }

    TEST(OpponentStockfishFileTest, ASeatThatAlreadyHoldsStockfishKeepsItUntilItPlaysAndTheFactoryFindsNoFile) {
        const TempDirectory directory;
        ChessGameModel model;
        OpponentController controller(model, SettingsAt(directory.File(kFileName)));  // a fresh controller holds what the build prefers, Stockfish, with no file behind it
        ASSERT_EQ(controller.GetEngine(), OpponentEngine::Stockfish);

        EXPECT_TRUE(controller.SetLevel(EasyLevel()).has_value()) << "a level is no pick of Stockfish: it is only remembered while nobody plays";
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Stockfish);

        const auto played = controller.SetSide(PieceColor::Black);
        ASSERT_FALSE(played.has_value());
        EXPECT_EQ(played.error().kind, EngineErrorKind::LaunchFailed);
        EXPECT_FALSE(controller.GetPlays().has_value());
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Stockfish) << "nothing falls back";
    }

    TEST(OpponentStockfishFileTest, WithAFileFoundStockfishIsOfferedTheDefaultAndPickable) {
        const TempDirectory directory;
        MakeStockfishFile(directory.File(kFileName));
        ChessGameModel model;
        OpponentController controller(model, SettingsAt(directory.File(kFileName)));

        EXPECT_TRUE(controller.OffersStockfish());
        EXPECT_EQ(controller.DefaultEngine(), OpponentEngine::Stockfish);

        ASSERT_TRUE(controller.SetEngine(OpponentEngine::Own).has_value());
        ASSERT_TRUE(controller.SetEngine(OpponentEngine::Stockfish).has_value());
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Stockfish);
        EXPECT_FALSE(controller.GetError().has_value());

        ASSERT_TRUE(controller.Choose(PieceColor::Black, EasyLevel(), OpponentEngine::Stockfish).has_value()) << "the program is not started before its first search";
        EXPECT_TRUE(controller.PlaysFor(PieceColor::Black));
    }

    TEST(OpponentStockfishFileTest, AFileThatAppearsLaterIsSeenByRefreshAndByThePickButNotBeforeTheyLook) {
        const TempDirectory directory;
        ChessGameModel model;
        OpponentController controller(model, SettingsAt(directory.File(kFileName)));
        ASSERT_FALSE(controller.OffersStockfish());
        const uint64_t revision = controller.GetRevision();

        MakeStockfishFile(directory.File(kFileName));
        EXPECT_FALSE(controller.OffersStockfish()) << "the answer is cached: PATH is not walked per frame";

        controller.RefreshStockfish();
        EXPECT_TRUE(controller.OffersStockfish());
        EXPECT_EQ(controller.GetRevision(), revision) << "what is installed is not the state of the game";

        // A pick looks afresh on its own, and the cache learns from it
        std::error_code ignored;
        std::filesystem::remove(directory.File(kFileName), ignored);
        controller.RefreshStockfish();
        ASSERT_FALSE(controller.OffersStockfish());
        ASSERT_TRUE(controller.SetEngine(OpponentEngine::Own).has_value());
        MakeStockfishFile(directory.File(kFileName));
        EXPECT_TRUE(controller.SetEngine(OpponentEngine::Stockfish).has_value());
        EXPECT_TRUE(controller.OffersStockfish());
    }

    TEST(OpponentStockfishFileTest, NewSettingsLookAgain) {
        const TempDirectory directory;
        MakeStockfishFile(directory.File(kFileName));
        ChessGameModel model;
        OpponentController controller(model, SettingsAt(directory.File("absent")));
        ASSERT_FALSE(controller.OffersStockfish());

        controller.SetSettings(SettingsAt(directory.File(kFileName)));

        EXPECT_TRUE(controller.OffersStockfish());
        EXPECT_EQ(controller.GetRevision(), 0u);
    }

    TEST(OpponentStockfishFileTest, TheEngineCycleSkipsStockfishWhileItIsNotOffered) {
        const TempDirectory directory;
        ChessGameModel model;
        OpponentController controller(model, SettingsAt(directory.File(kFileName)));

        EXPECT_TRUE(controller.CycleEngine().has_value());
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own) << "from the build's preferred Stockfish to the one that can run";
        const uint64_t revision = controller.GetRevision();

        EXPECT_TRUE(controller.CycleEngine().has_value());
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own);
        EXPECT_EQ(controller.GetRevision(), revision) << "the pick changes nothing";
        EXPECT_FALSE(controller.GetError().has_value());

        MakeStockfishFile(directory.File(kFileName));
        controller.RefreshStockfish();
        EXPECT_TRUE(controller.CycleEngine().has_value());
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Stockfish);
        EXPECT_TRUE(controller.CycleEngine().has_value());
        EXPECT_EQ(controller.GetEngine(), OpponentEngine::Own);
    }

    TEST(OpponentStockfishFileTest, AFactoryOfTheTestOffersStockfishAndIsAskedWithoutAnyLookup) {
        const TempDirectory directory;
        ChessGameModel model;
        CountingFactory factory({MakeShared<InstantEngine>()});
        OpponentController controller(model, SettingsAt(directory.File(kFileName)));
        controller.SetEngineFactory(factory.AsFactory());  // the test world: its doubles are called Stockfish and Own, and no file is behind them

        EXPECT_TRUE(controller.OffersStockfish());
        EXPECT_EQ(controller.DefaultEngine(), OpponentEngine::Stockfish);
        ASSERT_TRUE(controller.Choose(PieceColor::Black, EasyLevel(), OpponentEngine::Stockfish).has_value());

        ASSERT_EQ(factory.Calls(), 1);
        EXPECT_EQ(factory.Engines()[0], OpponentEngineId(OpponentEngine::Stockfish));
    }

}  // namespace BECore::Chess::Tests

#endif  // !CHESS_NO_STOCKFISH
