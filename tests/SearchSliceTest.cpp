#include "ChessTestUtil.h"

#include <Chess/Engine/Search.h>
#include <Chess/Game/MoveGenerator.h>
#include <iomanip>
#include <iostream>
#include <vector>

/// The search as a machine that can be driven in slices: the golden results of the recursive search it replaces and the speed benchmark.
/// Pure functions of a position (no engine services). The golden table was written by running the recursive `Search()` before it was
/// changed; every later form of the search (one call, many slices) has to give the same numbers
namespace BECore::Chess::Tests {

    namespace {

        using Clock = std::chrono::steady_clock;

        /// The depths of the rows: 3 and 4 for the six positions, 5 where the table matters, 8 and 64 for the stops by the node limit
        constexpr int kGoldenShallowDepth = 3;
        constexpr int kGoldenDepth = 4;
        constexpr int kGoldenDeepDepth = 5;
        constexpr int kGoldenDeepestDepth = 8;
        constexpr int kGoldenForeverDepth = 64;
        /// The nodes after which the node-limited rows stop: a node limit is a clock that does not tick, so the result is deterministic
        constexpr uint64_t kGoldenNodeLimits[] = {1, 3000, 4000, 20000};

        /// The six perft positions, named for the rows of the table
        struct NamedFen {
            const char* name;
            eastl::string_view fen;
        };

        const NamedFen kPerftPositions[] = {
            {"start", Position::kStartFen}, {"kiwipete", kKiwipeteFen}, {"pos3", kPosition3Fen}, {"pos4", kPosition4Fen}, {"pos5", kPosition5Fen}, {"pos6", kPosition6Fen},
        };

        /// One search to run and to compare: a position (with the moves behind it), the depth and the settings that differ from the defaults
        struct GoldenCase {
            eastl::string name;
            eastl::string_view fen;
            eastl::vector<const char*> moves;
            int depth = 1;
            bool table = true;
            bool quietOrdering = true;
            bool quiescence = true;
            bool checkExtension = true;
            /// The root leaves out the best move of the default search at depth 3 of the same position
            bool excludeDefaultBest = false;
            uint64_t nodeLimit = 0;
            /// The token is cancelled before the call
            bool cancelled = false;
        };

        /// What a row of the table says
        struct GoldenRow {
            const char* name;
            const char* best;
            int score;
            int depth;
            uint64_t nodes;
            bool stopped;
            bool complete;
        };

        GoldenCase Case(eastl::string name, eastl::string_view fen, int depth) {
            GoldenCase c;
            c.name = eastl::move(name);
            c.fen = fen;
            c.depth = depth;
            return c;
        }

        /// The cases of the golden table, in the order of its rows. The names are the key: a row of the table is found by name
        std::vector<GoldenCase> BuildGoldenCases() {
            std::vector<GoldenCase> cases;
            // The six positions at depth 3 and 4 with the defaults, with no table, with the old quiet order, and (depth 3) with neither
            // quiescence nor extension: plain depth-limited alpha-beta
            for (const NamedFen& position : kPerftPositions) {
                for (const int depth : {kGoldenShallowDepth, kGoldenDepth}) {
                    cases.push_back(Case(Format("{} d{} default", position.name, depth), position.fen, depth));
                    GoldenCase noTable = Case(Format("{} d{} no table", position.name, depth), position.fen, depth);
                    noTable.table = false;
                    cases.push_back(noTable);
                    GoldenCase plainOrder = Case(Format("{} d{} plain order", position.name, depth), position.fen, depth);
                    plainOrder.quietOrdering = false;
                    cases.push_back(plainOrder);
                }
                GoldenCase plain = Case(Format("{} d{} no quiescence", position.name, kGoldenShallowDepth), position.fen, kGoldenShallowDepth);
                plain.quiescence = false;
                plain.checkExtension = false;
                cases.push_back(plain);
            }
            // The shallow levels of the game (easy and medium search 1 and 2 plies)
            for (const NamedFen& position : kPerftPositions) {
                for (const int depth : {1, 2}) {
                    cases.push_back(Case(Format("{} d{} default", position.name, depth), position.fen, depth));
                }
            }
            // Depth 5, the depth of the strongest level, with the defaults
            for (const NamedFen& position : {kPerftPositions[0], kPerftPositions[1]}) {
                cases.push_back(Case(Format("{} d{} default", position.name, kGoldenDeepDepth), position.fen, kGoldenDeepDepth));
            }
            // The root without its best move, as the lines after the first of an analysis search
            for (const NamedFen& position : kPerftPositions) {
                GoldenCase excluded = Case(Format("{} d{} excluded", position.name, kGoldenShallowDepth), position.fen, kGoldenShallowDepth);
                excluded.excludeDefaultBest = true;
                cases.push_back(excluded);
            }
            // Node limits: deterministic, they stop the search in the middle of an iteration
            for (const uint64_t limit : kGoldenNodeLimits) {
                GoldenCase limited = Case(Format("start d{} nodes {}", kGoldenDeepestDepth, limit), Position::kStartFen, kGoldenDeepestDepth);
                limited.nodeLimit = limit;
                cases.push_back(limited);
            }
            for (const uint64_t limit : {kGoldenNodeLimits[0], kGoldenNodeLimits[2]}) {
                GoldenCase limited = Case(Format("kiwipete d{} nodes {}", kGoldenForeverDepth, limit), kKiwipeteFen, kGoldenForeverDepth);
                limited.nodeLimit = limit;
                cases.push_back(limited);
            }
            // Positions with a game behind them (the fixtures of SearchTest): the repetition in the game's history, the 50-move clock
            // near its limit, a shuffle behind the root; at depth 5 where the table is built, on and off
            GoldenCase repetition = Case("repetition d5 default", "k7/8/8/8/8/8/8/3QK3 b - - 0 1", kGoldenDeepDepth);
            repetition.moves = {"a8b8", "d1d2", "b8a8", "d2d1"};
            GoldenCase clock = Case("clock96 d5 default", "4k3/8/8/8/8/8/8/R3K3 w - - 96 80", kGoldenDeepDepth);
            GoldenCase shuffle = Case("shuffle d5 default", "k7/8/8/8/8/8/8/3QK3 w - - 0 1", kGoldenDeepDepth);
            shuffle.moves = {"d1d2", "a8b8", "d2d1"};
            for (const GoldenCase& history : {repetition, clock, shuffle}) {
                cases.push_back(history);
                GoldenCase noTable = history;
                noTable.name = history.name;
                noTable.name.replace(noTable.name.size() - 7, 7, "no table");
                noTable.table = false;
                cases.push_back(noTable);
            }
            // Mates, a game that is over, and the stops before the first node
            GoldenCase mateInOne = Case("mate in one d64", "6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1", kGoldenForeverDepth);
            cases.push_back(mateInOne);
            cases.push_back(Case("mate in two d4", "kbK5/pp6/1P6/8/8/8/8/R7 w - - 0 1", 4));
            cases.push_back(Case("mate in three d8", "k7/8/8/3K4/8/8/8/7R w - - 0 1", 8));
            cases.push_back(Case("mated root d3", "R5k1/5ppp/8/8/8/8/8/4K3 b - - 0 1", kGoldenShallowDepth));
            cases.push_back(Case("stalemate root d3", "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", kGoldenShallowDepth));
            cases.push_back(Case("checks forever d6", "7k/6q1/8/8/8/8/1Q6/K7 w - - 0 1", 6));
            GoldenCase cancelled = Case("cancelled before d64", kKiwipeteFen, kGoldenForeverDepth);
            cancelled.cancelled = true;
            cases.push_back(cancelled);
            return cases;
        }

        SearchSettings SettingsOf(const GoldenCase& c) {
            SearchSettings settings;
            settings.maxDepth = c.depth;
            settings.quiescence = c.quiescence;
            settings.checkExtension = c.checkExtension;
            settings.quietOrdering = c.quietOrdering;
            settings.tableEntries = c.table ? kDefaultTableEntries : 0;
            return settings;
        }

        /// The search of a case through the one-call Search()
        SearchResult RunGoldenCase(const GoldenCase& c) {
            Position position = PositionFromFen(c.fen);
            for (const char* uci : c.moves) {
                position.DoMove(Mv(uci));
            }
            SearchSettings settings = SettingsOf(c);
            if (c.excludeDefaultBest) {
                SearchSettings first;
                first.maxDepth = kGoldenShallowDepth;
                settings.excludedRootMoves.push_back(Search(position, first).best);
            }
            SearchControl control;
            control.nodeLimit = c.nodeLimit;
            if (c.cancelled) {
                control.token = CancellationToken::Create();
                control.token.Cancel();
            }
            return Search(position, settings, control);
        }

        // clang-format off
        // The numbers of the recursive Search(), taken before it was rewritten: best move, score, depth, nodes, stopped, complete.
        // Regenerated only by DISABLED_PrintTheGoldenTable and only when the search is meant to change its answers
        const GoldenRow kGoldenRows[] = {
            {"start d3 default", "b1c3", 50, 3, 1263, false, true},
            {"start d3 no table", "b1c3", 50, 3, 1246, false, true},
            {"start d3 plain order", "b1c3", 50, 3, 1336, false, true},
            {"start d4 default", "b1c3", 0, 4, 4833, false, true},
            {"start d4 no table", "b1c3", 0, 4, 4228, false, true},
            {"start d4 plain order", "b1c3", 0, 4, 9083, false, true},
            {"start d3 no quiescence", "b1c3", 50, 3, 722, false, true},
            {"kiwipete d3 default", "e2a6", 45, 3, 32760, false, true},
            {"kiwipete d3 no table", "e2a6", 45, 3, 32789, false, true},
            {"kiwipete d3 plain order", "e2a6", 45, 3, 32760, false, true},
            {"kiwipete d4 default", "e2a6", 45, 4, 72179, false, true},
            {"kiwipete d4 no table", "e2a6", 45, 4, 90552, false, true},
            {"kiwipete d4 plain order", "e2a6", 45, 4, 72179, false, true},
            {"kiwipete d3 no quiescence", "e2a6", 400, 3, 2622, false, true},
            {"pos3 d3 default", "b4f4", 70, 3, 1241, false, true},
            {"pos3 d3 no table", "b4f4", 70, 3, 1800, false, true},
            {"pos3 d3 plain order", "b4f4", 70, 3, 1308, false, true},
            {"pos3 d4 default", "b4f4", 35, 4, 5125, false, true},
            {"pos3 d4 no table", "b4f4", 35, 4, 9204, false, true},
            {"pos3 d4 plain order", "b4f4", 35, 4, 7361, false, true},
            {"pos3 d3 no quiescence", "b4f4", 110, 3, 387, false, true},
            {"pos4 d3 default", "c4c5", -475, 3, 5796, false, true},
            {"pos4 d3 no table", "c4c5", -475, 3, 5712, false, true},
            {"pos4 d3 plain order", "c4c5", -475, 3, 5796, false, true},
            {"pos4 d4 default", "c4c5", -475, 4, 14214, false, true},
            {"pos4 d4 no table", "c4c5", -475, 4, 13854, false, true},
            {"pos4 d4 plain order", "c4c5", -475, 4, 14214, false, true},
            {"pos4 d3 no quiescence", "d2d4", -205, 3, 369, false, true},
            {"pos5 d3 default", "d7c8q", 560, 3, 4642, false, true},
            {"pos5 d3 no table", "d7c8q", 560, 3, 5032, false, true},
            {"pos5 d3 plain order", "d7c8q", 560, 3, 4643, false, true},
            {"pos5 d4 default", "d7c8q", 515, 4, 18652, false, true},
            {"pos5 d4 no table", "d7c8q", 515, 4, 26448, false, true},
            {"pos5 d4 plain order", "d7c8q", 515, 4, 18773, false, true},
            {"pos5 d3 no quiescence", "d7c8q", 560, 3, 2127, false, true},
            {"pos6 d3 default", "c3d5", 30, 3, 7292, false, true},
            {"pos6 d3 no table", "c3d5", 30, 3, 7899, false, true},
            {"pos6 d3 plain order", "c3d5", 30, 3, 7292, false, true},
            {"pos6 d4 default", "c3d5", 30, 4, 67226, false, true},
            {"pos6 d4 no table", "c3d5", 30, 4, 83208, false, true},
            {"pos6 d4 plain order", "c3d5", 30, 4, 67855, false, true},
            {"pos6 d3 no quiescence", "c3d5", 240, 3, 2374, false, true},
            {"start d1 default", "b1c3", 50, 1, 40, false, true},
            {"start d2 default", "b1c3", 0, 2, 204, false, true},
            {"kiwipete d1 default", "e2a6", 70, 1, 5180, false, true},
            {"kiwipete d2 default", "e2a6", 70, 2, 10478, false, true},
            {"pos3 d1 default", "b4f4", 70, 1, 33, false, true},
            {"pos3 d2 default", "b4f4", 80, 2, 378, false, true},
            {"pos4 d1 default", "c4c5", -460, 1, 1118, false, true},
            {"pos4 d2 default", "c4c5", -460, 2, 2322, false, true},
            {"pos5 d1 default", "d7c8q", 560, 1, 115, false, true},
            {"pos5 d2 default", "d7c8q", 560, 2, 758, false, true},
            {"pos6 d1 default", "c3d5", 10, 1, 354, false, true},
            {"pos6 d2 default", "c3d5", 30, 2, 2080, false, true},
            {"start d5 default", "b1c3", 40, 5, 31253, false, true},
            {"kiwipete d5 default", "d5e6", 40, 5, 329010, false, true},
            {"start d3 excluded", "g1f3", 50, 3, 1173, false, true},
            {"kiwipete d3 excluded", "d5e6", 45, 3, 31424, false, true},
            {"pos3 d3 excluded", "e2e4", 0, 3, 1062, false, true},
            {"pos4 d3 excluded", "d2d4", -520, 3, 4649, false, true},
            {"pos5 d3 excluded", "d7c8r", 560, 3, 4638, false, true},
            {"pos6 d3 excluded", "g5f6", 0, 3, 10297, false, true},
            {"start d8 nodes 1", "b1c3", 50, 1, 40, true, true},
            {"start d8 nodes 3000", "b1c3", 50, 3, 3000, true, true},
            {"start d8 nodes 4000", "b1c3", 50, 3, 4000, true, true},
            {"start d8 nodes 20000", "b1c3", 0, 4, 20000, true, true},
            {"kiwipete d64 nodes 1", "e2a6", 70, 1, 5180, true, true},
            {"kiwipete d64 nodes 4000", "e2a6", 70, 1, 5180, true, true},
            {"repetition d5 default", "a8b8", 0, 5, 418, false, true},
            {"repetition d5 no table", "a8b8", 0, 5, 513, false, true},
            {"clock96 d5 default", "a1a7", 0, 5, 2161, false, true},
            {"clock96 d5 no table", "a1a7", 0, 5, 2161, false, true},
            {"shuffle d5 default", "b8a8", 0, 5, 548, false, true},
            {"shuffle d5 no table", "b8a8", 0, 5, 884, false, true},
            {"mate in one d64", "a1a8", 29999, 1, 29, false, true},
            {"mate in two d4", "a1a6", 29997, 3, 695, false, true},
            {"mate in three d8", "d5c6", 29995, 5, 18871, false, true},
            {"mated root d3", "(none)", -30000, 0, 0, false, true},
            {"stalemate root d3", "(none)", 0, 0, 0, false, true},
            {"checks forever d6", "a1b1", 0, 6, 1149, false, true},
            {"cancelled before d64", "(none)", 0, 0, 0, true, false},
        };
        // clang-format on

        std::string Describe(const GoldenRow& row) {
            return std::string(row.best) + " score " + std::to_string(row.score) + " depth " + std::to_string(row.depth) + " nodes " + std::to_string(row.nodes) + (row.stopped ? " stopped" : "") +
                   (row.complete ? " complete" : " incomplete");
        }

        std::string Describe(const SearchResult& result) {
            return std::string(result.best.IsNull() ? "(none)" : result.best.ToUci().c_str()) + " score " + std::to_string(result.score) + " depth " + std::to_string(result.depth) + " nodes " +
                   std::to_string(result.nodes) + (result.stopped ? " stopped" : "") + (result.complete ? " complete" : " incomplete");
        }

        std::string Describe(const GoldenRow& row, const SearchResult& result) {
            return "expected " + Describe(row) + ", got " + Describe(result);
        }

        bool Matches(const GoldenRow& row, const SearchResult& result) {
            const std::string best = result.best.IsNull() ? "(none)" : std::string(result.best.ToUci().c_str());
            return best == row.best && result.score == row.score && result.depth == row.depth && result.nodes == row.nodes && result.stopped == row.stopped && result.complete == row.complete;
        }

    }  // namespace

    // -- the golden results --------------------------------------------------------------------------------------------------

    TEST(SearchSliceTest, TheGoldenTableHasARowPerCase) {
        const std::vector<GoldenCase> cases = BuildGoldenCases();

        ASSERT_EQ(std::size(kGoldenRows), cases.size());
        for (size_t i = 0; i < cases.size(); ++i) {
            EXPECT_STREQ(kGoldenRows[i].name, cases[i].name.c_str()) << "row " << i;
        }
    }

    TEST(SearchSliceTest, TheSearchGivesTheGoldenResults) {
        // Every row was taken from the recursive search before it was rewritten. A row that moves means the search gives another answer
        const std::vector<GoldenCase> cases = BuildGoldenCases();
        ASSERT_EQ(std::size(kGoldenRows), cases.size());

        for (size_t i = 0; i < cases.size(); ++i) {
            const SearchResult result = RunGoldenCase(cases[i]);

            EXPECT_TRUE(Matches(kGoldenRows[i], result)) << "row \"" << cases[i].name.c_str() << "\": " << Describe(kGoldenRows[i], result);
        }
    }

    // Run by hand on the unchanged search to write the table above: it prints the initialisers of kGoldenRows
    TEST(SearchSliceGolden, DISABLED_PrintTheGoldenTable) {
        for (const GoldenCase& c : BuildGoldenCases()) {
            const SearchResult result = RunGoldenCase(c);
            const std::string best = result.best.IsNull() ? "(none)" : std::string(result.best.ToUci().c_str());
            std::cout << "            {\"" << c.name.c_str() << "\", \"" << best << "\", " << result.score << ", " << result.depth << ", " << result.nodes << ", "
                      << (result.stopped ? "true" : "false") << ", " << (result.complete ? "true" : "false") << "},\n";
        }
    }

    // -- the benchmark -------------------------------------------------------------------------------------------------------

    namespace {

        constexpr int kBenchmarkRepeats = 5;

        struct BenchmarkPosition {
            const char* name;
            eastl::string_view fen;
            int depth;
        };

        /// Depth 4 on the six positions, depth 5 on the start position and Kiwipete: about a second of work in Release
        std::vector<BenchmarkPosition> BenchmarkPositions() {
            std::vector<BenchmarkPosition> positions;
            for (const NamedFen& position : kPerftPositions) {
                positions.push_back({position.name, position.fen, 4});
            }
            positions.push_back({kPerftPositions[0].name, kPerftPositions[0].fen, 5});
            positions.push_back({kPerftPositions[1].name, kPerftPositions[1].fen, 5});
            return positions;
        }

        /// One way to run a search to the end: takes the position and the depth, answers the nodes it searched. Later forms of the search
        /// add a column by adding one of these
        struct BenchmarkColumn {
            const char* title;
            std::function<uint64_t(eastl::string_view fen, int depth)> run;
        };

        struct Measured {
            uint64_t nodes = 0;
            double milliseconds = 0;
        };

        /// The fastest of kBenchmarkRepeats runs: the least disturbed by the machine
        Measured Fastest(const BenchmarkColumn& column, const BenchmarkPosition& position) {
            Measured best;
            for (int repeat = 0; repeat < kBenchmarkRepeats; ++repeat) {
                const Clock::time_point start = Clock::now();
                const uint64_t nodes = column.run(position.fen, position.depth);
                const double milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                if (repeat == 0 || milliseconds < best.milliseconds) {
                    best.milliseconds = milliseconds;
                }
                best.nodes = nodes;
            }
            return best;
        }

        double NodesPerSecond(const Measured& measured) {
            return measured.milliseconds > 0 ? static_cast<double>(measured.nodes) / measured.milliseconds / 1000.0 : 0.0;
        }

        /// Runs every column over every position and prints `name depth nodes ms Mnodes/s` per row and a total per column
        /// (the sum of the nodes over the sum of the fastest times)
        void RunBenchmark(const std::vector<BenchmarkColumn>& columns) {
            const std::vector<BenchmarkPosition> positions = BenchmarkPositions();
            for (const BenchmarkColumn& column : columns) {
                std::cout << "\n" << column.title << "\n";
                std::cout << std::left << std::setw(10) << "name" << std::right << std::setw(6) << "depth" << std::setw(12) << "nodes" << std::setw(12) << "ms" << std::setw(12) << "Mnodes/s" << "\n";
                uint64_t totalNodes = 0;
                double totalMilliseconds = 0;
                for (const BenchmarkPosition& position : positions) {
                    const Measured measured = Fastest(column, position);
                    totalNodes += measured.nodes;
                    totalMilliseconds += measured.milliseconds;
                    std::cout << std::left << std::setw(10) << position.name << std::right << std::setw(6) << position.depth << std::setw(12) << measured.nodes << std::setw(12) << std::fixed
                              << std::setprecision(2) << measured.milliseconds << std::setw(12) << std::setprecision(3) << NodesPerSecond(measured) << "\n";
                }
                const Measured total{totalNodes, totalMilliseconds};
                std::cout << std::left << std::setw(10) << "total" << std::right << std::setw(6) << "" << std::setw(12) << total.nodes << std::setw(12) << std::fixed << std::setprecision(2)
                          << total.milliseconds << std::setw(12) << std::setprecision(3) << NodesPerSecond(total) << "\n";
            }
            std::cout << std::flush;
        }

        /// The one-call Search() on the caller's position
        uint64_t SearchToTheEnd(eastl::string_view fen, int depth) {
            Position position = PositionFromFen(fen);
            SearchSettings settings;
            settings.maxDepth = depth;
            return Search(position, settings).nodes;
        }

    }  // namespace

    // Run by hand, in a Release build, on a quiet machine: ChessTests --gtest_also_run_disabled_tests --gtest_filter='SearchSliceBenchmark.*'
    // ctest does not run it. It uses the public Search() only, so the same file measures the code before and after a change
    TEST(SearchSliceBenchmark, DISABLED_NodesPerSecond) {
        RunBenchmark({{"Search() one call", SearchToTheEnd}});
    }

}  // namespace BECore::Chess::Tests
