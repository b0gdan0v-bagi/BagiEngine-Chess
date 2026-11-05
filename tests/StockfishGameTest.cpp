#include "FakeEngineUtil.h"
#include "GamePlay.h"
#include "StockfishUtil.h"

#include <Chess/Engine/OpponentChoice.h>
#include <Chess/Engine/OpponentController.h>
#include <Chess/Engine/OpponentLevels.h>
#include <Chess/Engine/OwnEngine.h>
#include <Chess/Engine/UciEngine.h>
#include <Chess/Game/ChessGameModel.h>
#include <Common/EngineEnvironment.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#if !defined(_WIN32)
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif

/// Whole games with a real Stockfish, through the classes the window uses: Stockfish against a seeded random mover and against the own engine through ChessGameModel,
/// and a person's move answered by the real Stockfish through OpponentController with the DEFAULT factory (no SetEngineFactory). Every case skips itself without a
/// Stockfish (StockfishUtil.h). A time-limited Stockfish is never asserted to play a particular move or to win; the games assert that every move was accepted and nothing
/// failed. DISABLED_OwnEngineAgainstStockfishYardstick is the owner's measure of the own engine; a normal run does not pay for it
namespace BECore::Chess::Tests {

    namespace {

        constexpr int kStockfishSkill = 0;
        constexpr int kGameMoveTimeMs = 30;  ///< the time Stockfish has for a move in the games, short on purpose
        constexpr int kOwnDepth = 2;
        constexpr size_t kRandomGamePlies = 100;
        constexpr size_t kOwnGamePlies = 80;
        constexpr uint64_t kRandomSeed = 0x9E3779B97F4A7C15ULL;
        constexpr size_t kShortestMate = 4;  ///< the plies of the fool's mate: a quick win of Stockfish over a random mover is possible, a game of fewer plies is not
        constexpr int kQuitGraceMs = OpponentController::kQuitGraceMs;
        constexpr std::chrono::milliseconds kShutdownBudget{1000};  ///< Shutdown() cancels and returns, it does not wait for the search
        constexpr std::chrono::milliseconds kChildEndBudget{3000};  ///< the child leaves after the cancel: the search answers `stop`, then the engine is closed
        constexpr eastl::string_view kMissingPath = "/no/such/file";
        constexpr eastl::string_view kEasyLevelId = "easy";
        constexpr eastl::string_view kHardLevelId = "hard";
        constexpr eastl::string_view kMaxLevelId = "max";
        constexpr const char* kLaunchLogName = "launches.log";
        constexpr const char* kPidLogName = "pids.log";
        constexpr const char* kStdinLogName = "stdin.log";
        constexpr eastl::string_view kMaxGoCommand = "go movetime 2000";  ///< what the Max level sends (kMaxMoveTimeMs of OpponentLevels.cpp)
        constexpr eastl::string_view kStopCommand = "stop";
        constexpr const char* kWrapperName = "stockfish-wrapper.sh";
        constexpr std::filesystem::perms kScriptPermissions = std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
                                                              std::filesystem::perms::others_read | std::filesystem::perms::others_exec;  // 755

        // The yardstick: N games per level of the own engine, the colors alternating. CHESS_YARDSTICK_GAMES in the environment replaces N for a quick look
        constexpr int kYardstickGames = 4;
        constexpr int kYardstickMoveTimeMs = 50;
        constexpr size_t kYardstickPlies = 200;
        constexpr const char* kYardstickGamesVariable = "CHESS_YARDSTICK_GAMES";

        std::optional<int> ParseInt(const char* text, size_t length) {
            int value = 0;
            const auto [end, error] = std::from_chars(text, text + length, value);
            return error == std::errc{} && end == text + length ? std::optional<int>(value) : std::nullopt;
        }

        bool Play(ChessGameModel& model, eastl::string_view uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        /// What a game loop does: Update every frame until @p done
        template <typename Condition>
        bool UpdateUntil(OpponentController& controller, Condition done) {
            return WaitUntil([&] {
                controller.Update();
                return done();
            });
        }

        size_t LevelIndex(eastl::string_view id) {
            const std::optional<size_t> index = FindOpponentLevel(id);
            EXPECT_TRUE(index.has_value()) << id.data();
            return index.value_or(kDefaultOpponentLevel);
        }

        OpponentSettings SettingsFor(eastl::string_view path) {
            return OpponentSettings{eastl::string(path.data(), path.size())};
        }

        OpponentSettings SettingsFor(const std::filesystem::path& program) {
            const std::string path = program.string();
            return SettingsFor(eastl::string_view(path.c_str(), path.size()));
        }

        /// A Stockfish at @p path at the weakest `Skill Level`, the way the game starts one for the weakest level (but not through the factory: the games ask it directly)
        UciEngineSettings WeakStockfish(const eastl::string& path) {
            UciEngineSettings settings = StockfishSettings(path);
            settings.options.emplace_back("Skill Level", Format("{}", kStockfishSkill));
            settings.quitGraceMs = kQuitGraceMs;
            return settings;
        }

        eastl::string ResultOf(const ChessGameModel& model) {
            if (!model.IsGameOver()) {
                return "unfinished";
            }
            const std::optional<PieceColor> winner = model.GetWinner();
            return winner ? Format("{} wins ({})", PieceColorId(*winner), EnumUtils<GameStatus>::ToString(model.GetStatus())) : Format("draw ({})", EnumUtils<GameStatus>::ToString(model.GetStatus()));
        }

#if !defined(_WIN32)
        /// A shell script that notes its start in @p launches, its process id in @p pidFile and, when @p stdinLog is given, a copy of every line the game sends in that file
        /// (tee), then becomes the Stockfish at @p stockfish. Without @p stdinLog the script `exec`s the program, so its id is the engine's. With it the script stays the
        /// parent of a pipe `tee | program`: the second line of @p pidFile is the id of the program itself (a shell that `exec`s it), the first is the script's
        std::filesystem::path WriteWrapper(const TempDirectory& directory, const eastl::string& stockfish, const char* pidFile, const char* stdinLog = nullptr) {
            const std::filesystem::path script = directory.File(kWrapperName);
            {
                std::ofstream out(script);
                out << "#!/bin/sh\n";
                out << "echo started >> \"" << directory.File(kLaunchLogName).string() << "\"\n";
                out << "echo $$ >> \"" << directory.File(pidFile).string() << "\"\n";
                if (stdinLog == nullptr) {
                    out << "exec \"" << stockfish.c_str() << "\" \"$@\"\n";
                } else {
                    out << "tee -a \"" << directory.File(stdinLog).string() << "\" | sh -c 'echo $$ >> \"$1\"; shift; exec \"$@\"' sh \"" << directory.File(pidFile).string() << "\" \""
                        << stockfish.c_str() << "\" \"$@\"\n";
                }
            }
            std::filesystem::permissions(script, kScriptPermissions, std::filesystem::perm_options::replace);
            return script;
        }

        /// The process id on line @p index (from 0) of @p pidFile, once the script has written it
        std::optional<int> ProcessIdAt(const std::filesystem::path& pidFile, size_t index) {
            const eastl::vector<eastl::string> lines = ReadFileLines(pidFile);
            return index < lines.size() ? ParseInt(lines[index].c_str(), lines[index].size()) : std::nullopt;
        }

        /// The process is gone and collected: a zombie would still answer kill(pid, 0)
        bool IsGone(int processId) {
            return kill(static_cast<pid_t>(processId), 0) == -1 && errno == ESRCH;
        }
#endif

        int YardstickGames() {
            const char* value = std::getenv(kYardstickGamesVariable);
            const std::optional<int> games = value != nullptr ? ParseInt(value, std::strlen(value)) : std::nullopt;
            return games.has_value() && *games > 0 ? *games : kYardstickGames;
        }

    }  // namespace

    class StockfishGameTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ::BECore::Tests::EngineEnvironment::EnsureCoreServices();  // TaskManager, for the tests of the running opponent
        }
    };

    TEST_F(StockfishGameTest, ARandomMoverDoesNotBeatStockfishWithEitherColor) {
        SKIP_WITHOUT_STOCKFISH(path);
        UciEngine stockfish(WeakStockfish(*path));
        SeededRandom random(kRandomSeed);

        for (int game = 0; game < 2; ++game) {
            const bool stockfishIsWhite = game == 0;
            ChessGameModel model;
            const Player engine = EnginePlayer(stockfish, SearchLimits{kGameMoveTimeMs, 0});
            const Player mover = RandomPlayer(random);

            const eastl::vector<eastl::string> moves = PlayGame(model, stockfishIsWhite ? engine : mover, stockfishIsWhite ? mover : engine, kRandomGamePlies);

            // Every move was accepted (PlayGame); the one claim about the result is that the random side did not win
            EXPECT_TRUE(model.IsGameOver() || model.GetMoves().size() == kRandomGamePlies) << "game " << game;
            EXPECT_GE(moves.size(), kShortestMate) << "a game, not a stop at the first move";
            const std::optional<PieceColor> winner = model.GetWinner();
            const PieceColor randomColor = stockfishIsWhite ? PieceColor::Black : PieceColor::White;
            EXPECT_FALSE(winner.has_value() && *winner == randomColor) << "game " << game << " was won by the random mover: " << ResultOf(model).c_str();
        }
    }

    TEST_F(StockfishGameTest, TheOwnEngineAndStockfishPlayALegalGameWithEitherColor) {
        SKIP_WITHOUT_STOCKFISH(path);
        UciEngine stockfish(WeakStockfish(*path));
        OwnEngine own;

        for (int game = 0; game < 2; ++game) {
            const bool ownIsWhite = game == 0;
            ChessGameModel model;
            const Player ownPlayer = EnginePlayer(own, SearchLimits{0, kOwnDepth});
            const Player stockfishPlayer = EnginePlayer(stockfish, SearchLimits{kGameMoveTimeMs, 0});

            const eastl::vector<eastl::string> moves = PlayGame(model, ownIsWhite ? ownPlayer : stockfishPlayer, ownIsWhite ? stockfishPlayer : ownPlayer, kOwnGamePlies);

            // No refused move and no EngineError (the players report both); the result is recorded, not asserted: a time-limited Stockfish is not deterministic
            EXPECT_TRUE(model.IsGameOver() || model.GetMoves().size() == kOwnGamePlies) << "game " << game;
            EXPECT_GE(moves.size(), kShortestMate) << "a game, not a stop at the first move";
            const eastl::string result = Format("own engine plays {}: {} after {} plies", ownIsWhite ? "White" : "Black", ResultOf(model), model.GetMoves().size());
            RecordProperty(Format("game{}", game).c_str(), result.c_str());
            std::printf("[   RESULT ] %s\n", result.c_str());
        }
    }

    namespace {

        /// A person's e2e4 answered through the default factory (no SetEngineFactory: the controller starts the Stockfish at the path itself, under the handicap of the level)
        void ExpectALegalAnswerAtLevel(eastl::string_view level) {
            SKIP_WITHOUT_STOCKFISH(path);
            ChessGameModel model;
            OpponentController controller(model, SettingsFor(*path));
            ASSERT_TRUE(controller.SetLevel(LevelIndex(level)).has_value());

            const auto picked = controller.SetSide(PieceColor::Black);
            ASSERT_TRUE(picked.has_value()) << Describe(picked.error()).c_str();
            ASSERT_TRUE(Play(model, "e2e4"));
            const bool answered = UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; });

            ASSERT_TRUE(answered) << DescribeError(controller.GetError()).c_str();
            EXPECT_EQ(model.GetTurn(), PieceColor::White);  // the model accepted the reply, so it is legal; which move it is, is not asserted (Stockfish, or the handicap)
            EXPECT_FALSE(controller.GetError().has_value());
            EXPECT_FALSE(controller.IsThinking());
            EXPECT_FALSE(model.IsGameOver());
        }

    }  // namespace

    TEST_F(StockfishGameTest, ThePersonsMoveIsAnsweredByTheRealStockfishThroughTheDefaultFactory) {
        ExpectALegalAnswerAtLevel(kHardLevelId);  // hard has no handicap: the move is Stockfish's
    }

    TEST_F(StockfishGameTest, ThePersonsMoveIsAnsweredAtEasyThroughTheDefaultFactoryWithTheHandicap) {
        ExpectALegalAnswerAtLevel(kEasyLevelId);
    }

#if !defined(_WIN32)

    TEST_F(StockfishGameTest, AChangeOfLevelStartsANewProcessAndItAnswersTheNextMoveToo) {
        SKIP_WITHOUT_STOCKFISH(path);
        const TempDirectory directory;
        const std::filesystem::path wrapper = WriteWrapper(directory, *path, kPidLogName);  // the real Stockfish behind a script that counts its starts
        ChessGameModel model;
        OpponentController controller(model, SettingsFor(wrapper));
        // Hard and max never blunder, so the real program is asked at both (the handicap of easy and medium would answer some moves without starting it)
        ASSERT_TRUE(controller.SetLevel(LevelIndex(kHardLevelId)).has_value());
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(Play(model, "e2e4"));
        ASSERT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();
        EXPECT_EQ(ReadFileLines(directory.File(kLaunchLogName)).size(), 1u) << "one process for the first level";

        ASSERT_TRUE(controller.SetLevel(LevelIndex(kMaxLevelId)).has_value());
        ASSERT_TRUE(Play(model, "d2d4")) << "Black's answer to e2e4 cannot take this away";
        const bool answered = UpdateUntil(controller, [&] { return model.GetMoves().size() == 4; });

        ASSERT_TRUE(answered) << DescribeError(controller.GetError()).c_str();
        EXPECT_EQ(ReadFileLines(directory.File(kLaunchLogName)).size(), 2u) << "the new level made a new process";
        EXPECT_FALSE(controller.GetError().has_value());
        EXPECT_FALSE(controller.IsThinking());
    }

    TEST_F(StockfishGameTest, ShutdownDuringAMaxLevelSearchReturnsAtOnceAndTheChildEnds) {
        SKIP_WITHOUT_STOCKFISH(path);
        const TempDirectory directory;
        const std::filesystem::path wrapper = WriteWrapper(directory, *path, kPidLogName, kStdinLogName);  // a copy of what the game writes to the program lands in stdin.log
        ChessGameModel model;
        OpponentController controller(model, SettingsFor(wrapper));
        ASSERT_TRUE(controller.SetLevel(LevelIndex(kMaxLevelId)).has_value());  // two seconds a move: the search is still running when Shutdown() comes
        ASSERT_TRUE(controller.SetSide(PieceColor::Black).has_value());
        ASSERT_TRUE(Play(model, "e2e4"));
        controller.Update();
        ASSERT_TRUE(controller.IsThinking());
        // The proof that a search runs: the program was told to go. Shutdown() before this line would be a cancel during the handshake, which no test covers with a process yet
        ASSERT_TRUE(WaitForLogLine(directory.File(kStdinLogName), kMaxGoCommand)) << "the program never got `" << kMaxGoCommand.data() << "`";
        ASSERT_TRUE(WaitUntil([&] { return ProcessIdAt(directory.File(kPidLogName), 1).has_value(); })) << "the program's process id";
        const int scriptId = *ProcessIdAt(directory.File(kPidLogName), 0);
        const int programId = *ProcessIdAt(directory.File(kPidLogName), 1);

        const auto started = std::chrono::steady_clock::now();
        controller.Shutdown();
        const auto took = std::chrono::steady_clock::now() - started;

        EXPECT_LT(took, kShutdownBudget);
        EXPECT_TRUE(WaitUntil([&] { return IsGone(scriptId); }, kChildEndBudget)) << "the script " << scriptId << " is still there";
        EXPECT_TRUE(WaitUntil([&] { return IsGone(programId); }, kChildEndBudget)) << "the program " << programId << " is still there";
        EXPECT_TRUE(WaitForLogLine(directory.File(kStdinLogName), kStopCommand, kChildEndBudget)) << "a running search is told `stop` before the program is closed";
        EXPECT_EQ(model.GetMoves().size(), 1u) << "a late answer is never played";
        EXPECT_FALSE(controller.GetPlays().has_value()) << "nobody plays after Shutdown()";
    }

#endif  // !defined(_WIN32)

    TEST_F(StockfishGameTest, AWrongPathIsLaunchFailedWithARealStockfishAroundAndTheRightOneIsTheWayOn) {
        SKIP_WITHOUT_STOCKFISH(path);  // the guard is the point: a Stockfish is on PATH or in STOCKFISH, and a typo in the config must not find it
        ChessGameModel model;
        OpponentController controller(model, SettingsFor(kMissingPath));

        const auto refused = controller.SetSide(PieceColor::Black);

        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().kind, EngineErrorKind::LaunchFailed);
        EXPECT_NE(refused.error().detail.find(kMissingPath.data(), 0, kMissingPath.size()), eastl::string::npos) << refused.error().detail.c_str();
        EXPECT_FALSE(controller.GetPlays().has_value()) << "a pick that finds no engine changes nothing";
        ASSERT_TRUE(controller.GetError().has_value());
        EXPECT_EQ(controller.GetError()->kind, EngineErrorKind::LaunchFailed);

        // The person fixes the line and picks again: the Stockfish at the right path plays
        controller.SetSettings(SettingsFor(*path));
        ASSERT_TRUE(controller.SetLevel(LevelIndex(kHardLevelId)).has_value());
        const auto picked = controller.SetSide(PieceColor::Black);
        ASSERT_TRUE(picked.has_value()) << Describe(picked.error()).c_str();
        EXPECT_FALSE(controller.GetError().has_value());
        ASSERT_TRUE(Play(model, "e2e4"));
        EXPECT_TRUE(UpdateUntil(controller, [&] { return model.GetMoves().size() == 2; })) << DescribeError(controller.GetError()).c_str();
    }

    TEST_F(StockfishGameTest, DISABLED_OwnEngineAgainstStockfishYardstick) {
        // The strength of the own engine at each of its levels against Stockfish at Skill Level 0 with a fixed move time. Run it by hand:
        //   ChessTests --gtest_also_run_disabled_tests --gtest_filter='StockfishGameTest.DISABLED_*'
        // CHESS_YARDSTICK_GAMES=2 shortens it. Only a refused move or an engine error fails it; the table is what it is for
        SKIP_WITHOUT_STOCKFISH(path);
        UciEngine stockfish(WeakStockfish(*path));
        OwnEngine own;
        const int games = YardstickGames();

        std::printf("\nown engine against Stockfish (Skill Level %d, %d ms a move), %d games per level, colors alternating, cap %zu plies\n", kStockfishSkill, kYardstickMoveTimeMs, games,
                    kYardstickPlies);
        std::printf("%-8s %5s %5s %6s %7s %s\n", "level", "depth", "wins", "draws", "losses", "(draws include games stopped at the cap)");
        for (const OpponentLevel& level : GetOpponentLevels()) {
            int wins = 0;
            int draws = 0;
            int losses = 0;
            int capped = 0;
            for (int game = 0; game < games; ++game) {
                const bool ownIsWhite = game % 2 == 0;
                ChessGameModel model;
                const Player ownPlayer = EnginePlayer(own, LimitsOf(OpponentEngine::Own, level));
                const Player stockfishPlayer = EnginePlayer(stockfish, SearchLimits{kYardstickMoveTimeMs, 0});

                PlayGame(model, ownIsWhite ? ownPlayer : stockfishPlayer, ownIsWhite ? stockfishPlayer : ownPlayer, kYardstickPlies);

                const std::optional<PieceColor> winner = model.GetWinner();
                const PieceColor ownColor = ownIsWhite ? PieceColor::White : PieceColor::Black;
                if (winner.has_value()) {
                    ++(*winner == ownColor ? wins : losses);
                } else {
                    ++draws;
                    capped += model.IsGameOver() ? 0 : 1;
                }
            }
            std::printf("%-8s %5d %5d %6d %7d (%d capped)\n", level.id.data(), level.ownDepth, wins, draws, losses, capped);
            RecordProperty(Format("{}", level.id).c_str(), Format("wins {} draws {} losses {}", wins, draws, losses).c_str());
            EXPECT_EQ(wins + draws + losses, games);
        }
    }

}  // namespace BECore::Chess::Tests
