#include "ChessTestUtil.h"

#include <Chess/Engine/EngineOpponent.h>
#include <Chess/Engine/OwnEngine.h>
#include <Chess/Engine/Search.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>
#include <Common/EngineEnvironment.h>
#include <TaskSystem/Awaitables.h>
#include <TaskSystem/TaskManager.h>
#include <iomanip>
#include <iostream>
#include <thread>
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
        /// The 1 and 2 plies of the easy and the medium level, and the depths of the mate and perpetual check positions
        constexpr int kGoldenEasyDepth = 1;
        constexpr int kGoldenMediumDepth = 2;
        constexpr int kGoldenChecksDepth = 6;
        /// The word that ends the name of a default row, which the rows with no table replace
        constexpr eastl::string_view kDefaultWord = "default";
        /// The nodes after which the node-limited rows stop: a node limit is a clock that does not tick, so the result is deterministic
        constexpr uint64_t kGoldenNodeLimits[] = {1, 3000, 4000, 20000};

        /// The six perft positions, named for the rows of the table
        struct NamedFen {
            const char* name;
            eastl::string_view fen;
        };

        /// Both queens can give check again and again (the fixture of SearchTest.ChecksThatCanGoOnForeverEndTheSearch)
        constexpr eastl::string_view kChecksFen = "7k/6q1/8/8/8/8/1Q6/K7 w - - 0 1";

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
                for (const int depth : {kGoldenEasyDepth, kGoldenMediumDepth}) {
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
                noTable.name.replace(noTable.name.size() - kDefaultWord.size(), kDefaultWord.size(), "no table");
                noTable.table = false;
                cases.push_back(noTable);
            }
            // Mates, a game that is over, and the stops before the first node
            GoldenCase mateInOne = Case("mate in one d64", "6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1", kGoldenForeverDepth);
            cases.push_back(mateInOne);
            cases.push_back(Case("mate in two d4", "kbK5/pp6/1P6/8/8/8/8/R7 w - - 0 1", kGoldenDepth));
            cases.push_back(Case("mate in three d8", "k7/8/8/3K4/8/8/8/7R w - - 0 1", kGoldenDeepestDepth));
            cases.push_back(Case("mated root d3", "R5k1/5ppp/8/8/8/8/8/4K3 b - - 0 1", kGoldenShallowDepth));
            cases.push_back(Case("stalemate root d3", "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", kGoldenShallowDepth));
            cases.push_back(Case("checks forever d6", kChecksFen, kGoldenChecksDepth));
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

        /// What a case hands to a search
        struct SearchInputs {
            Position position;
            SearchSettings settings;
            SearchControl control;
        };

        SearchInputs InputsOf(const GoldenCase& c) {
            SearchInputs inputs{PositionFromFen(c.fen), SettingsOf(c), {}};
            for (const char* uci : c.moves) {
                inputs.position.DoMove(Mv(uci));
            }
            if (c.excludeDefaultBest) {
                SearchSettings first;
                first.maxDepth = kGoldenShallowDepth;
                inputs.settings.excludedRootMoves.push_back(Search(inputs.position, first).best);
            }
            inputs.control.nodeLimit = c.nodeLimit;
            if (c.cancelled) {
                inputs.control.token = CancellationToken::Create();
                inputs.control.token.Cancel();
            }
            return inputs;
        }

        /// The search of a case through the one-call Search()
        SearchResult RunGoldenCase(const GoldenCase& c) {
            SearchInputs inputs = InputsOf(c);
            return Search(inputs.position, inputs.settings, inputs.control);
        }

        /// The nodes to give the call number @p call of a job (0, 1, ...)
        using SlicePolicy = std::function<uint64_t(size_t call)>;

        /// The same slice every time
        SlicePolicy Fixed(uint64_t nodes) {
            return [nodes](size_t) { return nodes; };
        }

        /// A slice that changes with every call: 1 to 300 nodes in a fixed, uneven order
        constexpr uint64_t kVaryingSliceModulus = 300;
        constexpr size_t kVaryingSliceStride = 37;
        uint64_t Varying(size_t call) {
            return 1 + (call * kVaryingSliceStride) % kVaryingSliceModulus;
        }

        /// Drives @p job to the end with the slices of @p policy; the number of calls it took
        size_t Drive(SearchJob& job, const SlicePolicy& policy) {
            size_t calls = 0;
            while (!job.IsDone()) {
                (void)job.Advance(policy(calls));
                ++calls;
            }
            return calls;
        }

        /// The search of a case through a SearchJob built from copies of its inputs
        SearchResult RunSlicedGoldenCase(const GoldenCase& c, const SlicePolicy& policy) {
            SearchInputs inputs = InputsOf(c);
            SearchJob job(inputs.position, inputs.settings, inputs.control);
            (void)Drive(job, policy);
            return job.GetResult();
        }

        bool IsLegalIn(const Position& position, Move move) {
            const MoveList moves = MoveGenerator::GenerateLegal(position);
            return eastl::find(moves.begin(), moves.end(), move) != moves.end();
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

    // -- the search in slices ------------------------------------------------------------------------------------------------

    namespace {

        /// The rows up to this many nodes are driven with every slice size (1 node a call too); the larger ones with the varying one, to keep the run short
        constexpr uint64_t kSmallRowNodes = 12000;
        constexpr uint64_t kSliceOfOneNode = 1;
        constexpr uint64_t kSliceOfFiveNodes = 5;
        constexpr uint64_t kSliceOfSevenNodes = 7;
        constexpr uint64_t kSliceOfAHundredNodes = 100;
        constexpr uint64_t kSliceOfManyNodes = 1000;

        struct NamedPolicy {
            const char* name;
            SlicePolicy policy;
        };

        size_t IndexOfCase(const std::vector<GoldenCase>& cases, const char* name) {
            for (size_t i = 0; i < cases.size(); ++i) {
                if (cases[i].name == name) {
                    return i;
                }
            }
            return cases.size();
        }

        /// A deadline far enough that a test never reaches it by accident, and short enough that a hang ends
        constexpr std::chrono::seconds kSafetyDeadline{60};
        /// The perpetual checks of kChecksFen at depth 64 go on for ever (17 plies in 45 million nodes), so the deep search is stopped by its nodes
        constexpr uint64_t kDeepSearchNodes = 300000;
        /// The iteration the deep search has reached by then (measured: more than this), which is past the 6 of the golden row
        constexpr int kDeepSearchIterations = 8;
        /// A node limit that stops a search of kGoldenDeepestDepth inside an iteration
        constexpr uint64_t kMidIterationNodeLimit = 3000;
        /// The deadline test: the first wait, the factor between attempts and the last wait worth trying (150, 600, 2400, 9600 ms)
        constexpr std::chrono::milliseconds kFirstDeadlineWait{150};
        constexpr int kDeadlineWaitFactor = 4;
        constexpr std::chrono::milliseconds kLastDeadlineWait{9600};
        /// How far past the deadline the paused job sleeps, so that the deadline has certainly passed
        constexpr std::chrono::milliseconds kPastTheDeadline{2};

    }  // namespace

    TEST(SearchSliceTest, TheSlicedSearchGivesTheOneCallResult) {
        // Every row of the golden table (the six perft positions with and without the table, the old quiet order and an excluded root move, the
        // positions with a history, the mates, the node limits) driven in slices gives the numbers of the one call
        const std::vector<GoldenCase> cases = BuildGoldenCases();
        ASSERT_EQ(std::size(kGoldenRows), cases.size());

        const NamedPolicy everySize[] = {
            {"1 node", Fixed(kSliceOfOneNode)}, {"7 nodes", Fixed(kSliceOfSevenNodes)}, {"256 nodes", Fixed(kSearchSliceNodes)}, {"1000 nodes", Fixed(kSliceOfManyNodes)}, {"varying", Varying},
        };
        const NamedPolicy fewSizes[] = {{"varying", Varying}};

        for (size_t i = 0; i < cases.size(); ++i) {
            const bool small = kGoldenRows[i].nodes <= kSmallRowNodes;
            const auto check = [&](const NamedPolicy& slices) {
                const SearchResult result = RunSlicedGoldenCase(cases[i], slices.policy);
                EXPECT_TRUE(Matches(kGoldenRows[i], result)) << "row \"" << cases[i].name.c_str() << "\" in slices of " << slices.name << ": " << Describe(kGoldenRows[i], result);
            };
            if (small) {
                for (const NamedPolicy& slices : everySize) {
                    check(slices);
                }
            } else {
                for (const NamedPolicy& slices : fewSizes) {
                    check(slices);
                }
            }
        }
    }

    TEST(SearchSliceTest, ASliceRunsAtMostItsNodes) {
        // A pause is before a move is tried, so a slice is exact but for the node at the horizon, which is counted twice: at most one more
        constexpr uint64_t kOverrun = 1;
        for (const NamedFen& position : {kPerftPositions[0], kPerftPositions[2]}) {
            SearchSettings settings;
            settings.maxDepth = kGoldenDepth;
            Position reference = PositionFromFen(position.fen);
            const SearchResult whole = Search(reference, settings);

            for (const uint64_t slice : {kSliceOfOneNode, kSliceOfFiveNodes, kSliceOfAHundredNodes}) {
                SearchJob job(PositionFromFen(position.fen), settings);
                size_t calls = 0;
                while (!job.IsDone()) {
                    const uint64_t before = job.GetResult().nodes;
                    (void)job.Advance(slice);
                    const uint64_t grew = job.GetResult().nodes - before;
                    ASSERT_LE(grew, slice + kOverrun) << position.name << " slice " << slice << " call " << calls;
                    ++calls;
                    ASSERT_LE(calls, whole.nodes + 1) << position.name << ": a slice that makes no progress";
                }

                EXPECT_EQ(job.GetResult().nodes, whole.nodes) << position.name << " slice " << slice;
                EXPECT_EQ(job.GetResult().best, whole.best) << position.name << " slice " << slice;
                // The slices are not far below their size: a call that ran a few nodes is the last of an iteration or of the search
                EXPECT_GE(calls, whole.nodes / (slice + kOverrun)) << position.name << " slice " << slice;
            }
        }
    }

    TEST(SearchSliceTest, ASliceOfNothingIsASliceOfOneNode) {
        SearchSettings settings;
        settings.maxDepth = kGoldenShallowDepth;
        SearchJob job(PositionFromFen(Position::kStartFen), settings);

        EXPECT_EQ(job.Advance(0), SearchProgress::Paused);

        EXPECT_GE(job.GetResult().nodes, 1u);
        EXPECT_LE(job.GetResult().nodes, 2u);
    }

    TEST(SearchSliceTest, AJobThatIsDoneStaysDone) {
        SearchSettings settings;
        settings.maxDepth = kGoldenEasyDepth;
        SearchJob job(PositionFromFen(Position::kStartFen), settings);

        EXPECT_EQ(job.Advance(UINT64_MAX), SearchProgress::Done);
        const SearchResult done = job.GetResult();

        EXPECT_TRUE(job.IsDone());
        EXPECT_EQ(job.Advance(), SearchProgress::Done);
        EXPECT_EQ(job.GetResult().nodes, done.nodes);
        EXPECT_EQ(job.GetResult().best, done.best);
        EXPECT_EQ(job.FinishedDepth(), kGoldenEasyDepth);
    }

    TEST(SearchSliceTest, TheNodeLimitIsTheSameInSlices) {
        const std::vector<GoldenCase> cases = BuildGoldenCases();
        const NamedPolicy sizes[] = {
            {"1 node", Fixed(kSliceOfOneNode)}, {"7 nodes", Fixed(kSliceOfSevenNodes)}, {"256 nodes", Fixed(kSearchSliceNodes)}, {"1000 nodes", Fixed(kSliceOfManyNodes)}, {"varying", Varying},
        };

        for (const char* name : {"start d8 nodes 3000", "start d8 nodes 20000", "kiwipete d64 nodes 4000"}) {
            const size_t row = IndexOfCase(cases, name);
            ASSERT_LT(row, cases.size()) << name;
            ASSERT_TRUE(kGoldenRows[row].stopped) << name;

            for (const NamedPolicy& slices : sizes) {
                const SearchResult result = RunSlicedGoldenCase(cases[row], slices.policy);

                EXPECT_TRUE(Matches(kGoldenRows[row], result)) << name << " in slices of " << slices.name << ": " << Describe(kGoldenRows[row], result);
            }
        }
    }

    TEST(SearchSliceTest, ACancelledTokenBetweenSlicesStopsTheJob) {
        SearchSettings settings;
        settings.maxDepth = kGoldenForeverDepth;
        SearchControl control;
        control.token = CancellationToken::Create();
        Position position = PositionFromFen(kKiwipeteFen);
        SearchJob job(position, settings, control);

        // The first iteration is never stopped; two finished, the job is in the third, where a stop is looked at
        constexpr int kIterations = 2;
        constexpr size_t kCallLimit = 100000;
        size_t calls = 0;
        while (job.FinishedDepth() < kIterations && calls < kCallLimit) {
            ASSERT_EQ(job.Advance(), SearchProgress::Paused);
            ++calls;
        }
        ASSERT_GE(job.FinishedDepth(), kIterations);
        const uint64_t nodesBefore = job.GetResult().nodes;
        control.token.Cancel();

        EXPECT_EQ(job.Advance(), SearchProgress::Done);

        const SearchResult result = job.GetResult();
        EXPECT_TRUE(job.IsDone());
        EXPECT_TRUE(result.stopped);
        EXPECT_TRUE(result.complete);
        EXPECT_EQ(result.nodes, nodesBefore) << "the cancel is seen at the start of the call, before a node is searched";
        EXPECT_FALSE(result.best.IsNull());
        EXPECT_TRUE(IsLegalIn(position, result.best));
        EXPECT_GE(result.depth, kIterations);
        EXPECT_EQ(job.FinishedDepth(), result.depth);
    }

    TEST(SearchSliceTest, ACancelledTokenBeforeTheFirstSliceGivesNoResult) {
        SearchSettings settings;
        settings.maxDepth = kGoldenDepth;
        SearchControl control;
        control.token = CancellationToken::Create();
        control.token.Cancel();
        SearchJob job(PositionFromFen(kKiwipeteFen), settings, control);

        EXPECT_EQ(job.Advance(), SearchProgress::Done);

        const SearchResult result = job.GetResult();
        EXPECT_TRUE(result.stopped);
        EXPECT_FALSE(result.complete);
        EXPECT_TRUE(result.best.IsNull());
        EXPECT_EQ(result.nodes, 0u);
    }

    TEST(SearchSliceTest, TheFirstIterationIsNotStoppedBetweenSlices) {
        // A token cancelled in the middle of the first iteration is not looked at: the answer has to exist
        SearchSettings settings;
        settings.maxDepth = kGoldenForeverDepth;
        SearchControl control;
        control.token = CancellationToken::Create();
        SearchJob job(PositionFromFen(kKiwipeteFen), settings, control);

        EXPECT_EQ(job.Advance(kSliceOfOneNode), SearchProgress::Paused);
        control.token.Cancel();

        // The first iteration of Kiwipete is 5180 nodes; nothing stops it, and the job stops at the end of it
        while (!job.IsDone() && job.FinishedDepth() < 1) {
            (void)job.Advance(kSearchSliceNodes);
        }
        EXPECT_GE(job.FinishedDepth(), 1);
        EXPECT_FALSE(job.GetResult().best.IsNull());
        (void)job.Advance(kSearchSliceNodes);
        EXPECT_TRUE(job.IsDone());
        EXPECT_TRUE(job.GetResult().stopped);
        EXPECT_EQ(job.GetResult().depth, 1);
    }

    TEST(SearchSliceTest, ADeadlineBetweenSlicesStopsTheJob) {
        SearchSettings settings;
        settings.maxDepth = kGoldenForeverDepth;
        constexpr int kIterations = 2;

        // The deadline has to pass while the job is paused after its second iteration (204 nodes of the start position, a millisecond), and not before;
        // on a machine so busy that it passes earlier the attempt is made again with a deadline kDeadlineWaitFactor times as far
        bool tried = false;
        for (std::chrono::milliseconds wait = kFirstDeadlineWait; wait <= kLastDeadlineWait && !tried; wait *= kDeadlineWaitFactor) {
            SearchControl control;
            const Clock::time_point deadline = Clock::now() + wait;
            control.deadline = deadline;
            SearchJob job(PositionFromFen(Position::kStartFen), settings, control);
            while (!job.IsDone() && job.FinishedDepth() < kIterations) {
                (void)job.Advance();
            }
            if (job.IsDone() || Clock::now() >= deadline) {
                continue;
            }
            tried = true;
            const uint64_t nodesBefore = job.GetResult().nodes;
            std::this_thread::sleep_until(deadline + kPastTheDeadline);

            EXPECT_EQ(job.Advance(), SearchProgress::Done);

            const SearchResult result = job.GetResult();
            EXPECT_TRUE(result.stopped);
            EXPECT_EQ(result.nodes, nodesBefore);
            EXPECT_GE(result.depth, kIterations);
            EXPECT_FALSE(result.best.IsNull());
        }
        EXPECT_TRUE(tried) << "the machine was too slow for every deadline";
    }

    TEST(SearchSliceTest, TheJobOwnsItsPosition) {
        // The job works on a copy: the caller's position is the same after a job that ran to the end, and a stopped Search() hands it back equal
        Position position = PositionFromFen(kKiwipeteFen);
        position.DoMove(Mv("e2a6"));
        const eastl::string fenBefore = position.ToFen();
        const ZobristKey keyBefore = position.Key();
        SearchSettings settings;
        settings.maxDepth = kGoldenShallowDepth;

        SearchJob job(position, settings);
        (void)Drive(job, Fixed(kSearchSliceNodes));

        EXPECT_TRUE(job.IsDone());
        EXPECT_EQ(position.ToFen(), fenBefore);
        EXPECT_EQ(position.Key(), keyBefore);

        SearchControl limited;
        limited.nodeLimit = kMidIterationNodeLimit;
        settings.maxDepth = kGoldenDeepestDepth;
        const SearchResult stopped = Search(position, settings, limited);

        EXPECT_TRUE(stopped.stopped);
        EXPECT_EQ(position.ToFen(), fenBefore);
        EXPECT_EQ(position.Key(), keyBefore);
    }

    TEST(SearchSliceTest, TheJobOutlivesItsInputs) {
        SearchSettings reference;
        reference.maxDepth = kGoldenDepth;
        Position referencePosition = PositionFromFen(kKiwipeteFen);
        const Move excluded = Search(referencePosition, reference).best;
        reference.excludedRootMoves.push_back(excluded);
        const SearchResult expected = Search(referencePosition, reference);
        ASSERT_NE(expected.best, excluded);

        std::optional<SearchJob> job;
        {
            // Everything the job was made from is gone at the end of the block
            Position position = PositionFromFen(kKiwipeteFen);
            SearchSettings settings;
            settings.maxDepth = kGoldenDepth;
            settings.excludedRootMoves.push_back(excluded);
            SearchControl control;
            control.nodeLimit = kDeepSearchNodes;
            job.emplace(eastl::move(position), eastl::move(settings), eastl::move(control));
            (void)job->Advance(kSearchSliceNodes);
        }
        // A moved job works on as well: the machine behind it does not move
        SearchJob moved = eastl::move(*job);
        job.reset();
        (void)Drive(moved, Fixed(kSearchSliceNodes));

        const SearchResult result = moved.GetResult();
        EXPECT_EQ(result.best, expected.best);
        EXPECT_EQ(result.score, expected.score);
        EXPECT_EQ(result.nodes, expected.nodes);
    }

    TEST(SearchSliceTest, ADeepSearchNeedsNoDeepStack) {
        // The plies of the line are frames on the heap, not calls: a search of the perpetual checks at depth 64 (the extension, then the iterations
        // that deepen the line one ply each) is driven on a thread of the default stack and ends when its nodes do. It does not reach the 96 plies
        // of the limit in a test run (it would need the iteration 64), so this guards the shape, not the limit
        SearchResult result;
        std::thread worker([&result]() {
            Position position = PositionFromFen(kChecksFen);
            SearchSettings settings;
            settings.maxDepth = kGoldenForeverDepth;
            settings.tableEntries = 0;
            SearchControl control;
            control.deadline = Clock::now() + kSafetyDeadline;
            control.nodeLimit = kDeepSearchNodes;
            SearchJob job(position, settings, control);
            (void)Drive(job, Fixed(kSearchSliceNodes));
            result = job.GetResult();
        });
        worker.join();

        EXPECT_TRUE(result.complete);
        EXPECT_TRUE(result.stopped);
        EXPECT_FALSE(result.best.IsNull());
        EXPECT_GE(result.depth, kDeepSearchIterations);
        EXPECT_GE(result.nodes, kDeepSearchNodes);
    }

    // -- the own engine in slices --------------------------------------------------------------------------------------------

    namespace {

        constexpr int kEngineShallowDepth = 3;
        constexpr int kEngineDepths[] = {3, 4};
        constexpr int kAnalysisLineCounts[] = {1, 2, 3, 8};
        /// A position where Black has the king move a7 and nothing else, a mate and a stalemate
        constexpr eastl::string_view kOneLegalMoveFen = "k7/8/2K5/8/8/8/8/1R6 b - - 0 1";
        constexpr eastl::string_view kMatedFen = "R5k1/5ppp/8/8/8/8/8/4K3 b - - 0 1";
        constexpr eastl::string_view kStalematedFen = "7k/5Q2/6K1/8/8/8/8/8 b - - 0 1";
        /// The game behind a request that has a history (the Italian opening)
        constexpr const char* kHistoryMoves[] = {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4"};
        /// The move time of the deadline test, and the pause that lets it run out before the first slice
        constexpr int kTinyMoveTimeMs = 1;
        constexpr std::chrono::milliseconds kPastTinyMoveTime{20};
        /// The slices a cancel test drives before it cancels (of kSearchSliceNodes each: a depth-64 search is far from its end by then)
        constexpr int kSlicesBeforeCancel = 3;
        /// The most a coroutine test waits for the pool: a passing test never waits it out
        constexpr std::chrono::seconds kPoolWaitLimit{30};
        /// How often a test polls for the pool's answer
        constexpr std::chrono::milliseconds kPoolPollInterval{1};
        /// How long a coroutine searches before its test cancels the token
        constexpr std::chrono::milliseconds kRunBeforeCancel{20};
        /// The most rounds of the interleaving test: a search that makes no progress ends the test
        constexpr size_t kInterleaveRoundLimit = 100000;

        SearchRequest RequestFor(eastl::string_view fen, int depth, int lines = 1) {
            SearchRequest request = SearchRequest::FromPosition(PositionFromFen(fen), SearchLimits{0, depth});
            request.lines = lines;
            return request;
        }

        /// A request with a game behind it: the start position and the moves of kHistoryMoves
        SearchRequest RequestWithHistory(int depth, int lines = 1) {
            ChessGameModel model;
            for (const char* uci : kHistoryMoves) {
                const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
                EXPECT_TRUE(move.has_value() && model.TryMove(*move).has_value()) << uci;
            }
            SearchRequest request = SearchRequest::FromModel(model, SearchLimits{0, depth});
            request.lines = lines;
            return request;
        }

        /// Drives @p search to its end with slices of the policy; the call count is bounded, so a search that makes no progress ends the test
        void DriveToTheEnd(OwnSearch& search, const SlicePolicy& slices) {
            constexpr size_t kCallLimit = 5000000;
            size_t calls = 0;
            while (!search.IsDone() && calls < kCallLimit) {
                (void)search.Advance(slices(calls));
                ++calls;
            }
        }

        /// The kind of an error, or nothing for a value: a form that EXPECT_EQ can compare and print
        std::optional<EngineErrorKind> KindOf(const std::expected<OwnSearch, EngineError>& begun) {
            return begun ? std::nullopt : std::optional<EngineErrorKind>(begun.error().kind);
        }

        std::optional<EngineErrorKind> KindOf(const std::expected<Analysis, EngineError>& answer) {
            return answer ? std::nullopt : std::optional<EngineErrorKind>(answer.error().kind);
        }

        std::string Describe(const AnalysisLine& line) {
            return std::string(line.move.ToUci().c_str()) + " " + (line.score.kind == AnalysisScoreKind::Mate ? "mate " : "cp ") + std::to_string(line.score.value) + " depth " +
                   std::to_string(line.depth);
        }

        /// Whether two analyses have the same lines: move, score (kind and value) and depth
        bool SameLines(const Analysis& a, const Analysis& b) {
            if (a.lines.size() != b.lines.size()) {
                return false;
            }
            for (size_t i = 0; i < a.lines.size(); ++i) {
                if (a.lines[i].move != b.lines[i].move || !(a.lines[i].score == b.lines[i].score) || a.lines[i].depth != b.lines[i].depth) {
                    return false;
                }
            }
            return true;
        }

        std::string Describe(const Analysis& analysis) {
            std::string text;
            for (const AnalysisLine& line : analysis.lines) {
                text += "[" + Describe(line) + "] ";
            }
            return text;
        }

        /// The slice 2 pattern: a coroutine on the pool that begins a search and drives it a slice at a time, giving the frame back first
        struct CoroutineAnswer {
            std::atomic<bool> done{false};
            std::expected<Analysis, EngineError> answer = std::unexpected(EngineError{EngineErrorKind::Unsupported, "not answered"});
            uint64_t slices = 0;
        };

        Task<void> DriveOnThePool(OwnEngine engine, SearchRequest request, CancellationToken token, eastl::shared_ptr<CoroutineAnswer> out) {
            auto search = engine.BeginFindMove(request, token);
            if (!search) {
                out->answer = std::unexpected(std::move(search.error()));
                out->done.store(true, std::memory_order_release);
                co_return;
            }
            while (!search->IsDone()) {
                co_await YieldIfNeeded();
                (void)search->Advance();
                ++out->slices;
            }
            out->answer = search->Result();
            out->done.store(true, std::memory_order_release);
            co_return;
        }

        bool WaitFor(const CoroutineAnswer& out) {
            const Clock::time_point deadline = Clock::now() + kPoolWaitLimit;
            while (!out.done.load(std::memory_order_acquire)) {
                if (Clock::now() >= deadline) {
                    return false;
                }
                std::this_thread::sleep_for(kPoolPollInterval);
            }
            return true;
        }

    }  // namespace

    TEST(SearchSliceTest, FindMoveInSlicesGivesFindMovesAnswer) {
        OwnEngine engine;
        const NamedPolicy sizes[] = {{"1 node", Fixed(kSliceOfOneNode)}, {"7 nodes", Fixed(kSliceOfSevenNodes)}, {"256 nodes", Fixed(kSearchSliceNodes)}, {"varying", Varying}};

        for (const int depth : kEngineDepths) {
            std::vector<std::pair<std::string, SearchRequest>> requests;
            for (const NamedFen& position : kPerftPositions) {
                requests.emplace_back(position.name, RequestFor(position.fen, depth));
            }
            requests.emplace_back("history", RequestWithHistory(depth));

            for (const auto& [name, request] : requests) {
                const auto expected = engine.FindMove(request, CancellationToken::Create());
                ASSERT_TRUE(expected.has_value()) << name << " d" << depth;

                // The same search driven to its end in one call is the reference for the node count
                auto whole = engine.BeginFindMove(request, CancellationToken::Create());
                ASSERT_TRUE(whole.has_value());
                EXPECT_EQ(whole->Advance(UINT64_MAX), SearchProgress::Done);
                ASSERT_TRUE(whole->Result().has_value());
                EXPECT_EQ(whole->Result()->Best().move, *expected) << name << " d" << depth;

                for (size_t sizeIndex = 0; sizeIndex < std::size(sizes); ++sizeIndex) {
                    const NamedPolicy& slices = sizes[sizeIndex];
                    // The 1-node slice (the first size) of a depth-4 search is hundreds of thousands of calls: only the shallow depth gets it
                    if (depth > kEngineShallowDepth && sizeIndex == 0) {
                        continue;
                    }
                    auto search = engine.BeginFindMove(request, CancellationToken::Create());
                    ASSERT_TRUE(search.has_value());
                    DriveToTheEnd(*search, slices.policy);

                    ASSERT_TRUE(search->IsDone()) << name << " d" << depth << " in slices of " << slices.name;
                    const auto answer = search->Result();
                    ASSERT_TRUE(answer.has_value()) << name << " d" << depth << " in slices of " << slices.name;
                    EXPECT_EQ(answer->LineCount(), 1u);
                    EXPECT_EQ(answer->Best().move, *expected) << name << " d" << depth << " in slices of " << slices.name;
                    EXPECT_TRUE(SameLines(*answer, *whole->Result()))
                        << name << " d" << depth << " in slices of " << slices.name << ": " << Describe(*answer) << " against " << Describe(*whole->Result());
                    EXPECT_EQ(search->GetNodes(), whole->GetNodes()) << name << " d" << depth << " in slices of " << slices.name;
                }
            }
        }
    }

    TEST(SearchSliceTest, AnalyseInSlicesGivesAnalyses) {
        OwnEngine engine;
        const NamedPolicy sizes[] = {{"100 nodes", Fixed(kSliceOfAHundredNodes)}, {"256 nodes", Fixed(kSearchSliceNodes)}, {"varying", Varying}};

        for (const NamedFen& position : {kPerftPositions[0], kPerftPositions[1], kPerftPositions[3]}) {
            for (const int lines : kAnalysisLineCounts) {
                const SearchRequest request = RequestFor(position.fen, kEngineShallowDepth, lines);
                const auto expected = engine.Analyse(request, CancellationToken::Create());
                ASSERT_TRUE(expected.has_value()) << position.name << " lines " << lines;
                EXPECT_EQ(expected->LineCount(), eastl::min(static_cast<size_t>(lines), MoveGenerator::GenerateLegal(PositionFromFen(position.fen)).size()));

                for (const NamedPolicy& slices : sizes) {
                    auto search = engine.BeginAnalyse(request, CancellationToken::Create());
                    ASSERT_TRUE(search.has_value());
                    DriveToTheEnd(*search, slices.policy);

                    ASSERT_TRUE(search->IsDone());
                    const auto answer = search->Result();
                    ASSERT_TRUE(answer.has_value()) << position.name << " lines " << lines << " in slices of " << slices.name;
                    EXPECT_TRUE(SameLines(*answer, *expected)) << position.name << " lines " << lines << " in slices of " << slices.name << ": " << Describe(*answer) << " against "
                                                               << Describe(*expected);
                }
                // Asked again: the same lines
                const auto again = engine.Analyse(request, CancellationToken::Create());
                ASSERT_TRUE(again.has_value());
                EXPECT_TRUE(SameLines(*again, *expected));
            }
        }
    }

    TEST(SearchSliceTest, AnalyseInSlicesWithAHistoryGivesAnalyses) {
        OwnEngine engine;
        const SearchRequest request = RequestWithHistory(kEngineShallowDepth, 3);
        const auto expected = engine.Analyse(request, CancellationToken::Create());
        ASSERT_TRUE(expected.has_value());

        auto search = engine.BeginAnalyse(request, CancellationToken::Create());
        ASSERT_TRUE(search.has_value());
        DriveToTheEnd(*search, Fixed(kSliceOfSevenNodes));

        ASSERT_TRUE(search->Result().has_value());
        EXPECT_TRUE(SameLines(*search->Result(), *expected));
    }

    TEST(SearchSliceTest, APositionWithOneLegalMoveGivesOneLineInSlices) {
        OwnEngine engine;
        ASSERT_EQ(MoveGenerator::GenerateLegal(PositionFromFen(kOneLegalMoveFen)).size(), 1u);

        for (const int lines : kAnalysisLineCounts) {
            auto search = engine.BeginAnalyse(RequestFor(kOneLegalMoveFen, kEngineShallowDepth, lines), CancellationToken::Create());
            ASSERT_TRUE(search.has_value()) << lines;
            DriveToTheEnd(*search, Fixed(kSliceOfFiveNodes));

            ASSERT_TRUE(search->Result().has_value()) << lines;
            EXPECT_EQ(search->Result()->LineCount(), 1u) << lines;
            EXPECT_EQ(search->Result()->Best().move.ToUci(), "a8a7") << lines;
        }
    }

    TEST(SearchSliceTest, BeginReportsTheErrorsOfTheBlockingCalls) {
        OwnEngine engine;
        const CancellationToken token = CancellationToken::Create();

        SearchRequest noLimits = RequestFor(Position::kStartFen, 1);
        noLimits.limits = SearchLimits{0, 0};
        SearchRequest brokenFen = RequestFor(Position::kStartFen, 1);
        brokenFen.startFen = "this is not a fen";
        SearchRequest illegalMove = RequestFor(Position::kStartFen, 1);
        illegalMove.moves.push_back(Mv("e2e5"));
        SearchRequest noLines = RequestFor(Position::kStartFen, 1, 0);
        SearchRequest tooManyLines = RequestFor(Position::kStartFen, 1, kMaxAnalysisLines + 1);

        for (const SearchRequest* request : {&noLimits, &brokenFen, &illegalMove, &noLines, &tooManyLines}) {
            const auto find = engine.BeginFindMove(*request, token);
            const auto analyse = engine.BeginAnalyse(*request, token);

            EXPECT_EQ(KindOf(find), EngineErrorKind::BadRequest);
            EXPECT_EQ(KindOf(analyse), EngineErrorKind::BadRequest);
            // The same kind as the blocking calls
            EXPECT_EQ(find.error().kind, engine.FindMove(*request, token).error().kind);
            EXPECT_EQ(KindOf(analyse), KindOf(engine.Analyse(*request, token)));
        }
        for (const eastl::string_view fen : {kMatedFen, kStalematedFen}) {
            EXPECT_EQ(KindOf(engine.BeginFindMove(RequestFor(fen, 1), token)), EngineErrorKind::NoMove) << fen.data();
            EXPECT_EQ(KindOf(engine.BeginAnalyse(RequestFor(fen, 1), token)), EngineErrorKind::NoMove) << fen.data();
        }
        CancellationToken cancelled = CancellationToken::Create();
        cancelled.Cancel();
        EXPECT_EQ(KindOf(engine.BeginFindMove(RequestFor(Position::kStartFen, 1), cancelled)), EngineErrorKind::Cancelled);
        EXPECT_EQ(KindOf(engine.BeginAnalyse(RequestFor(Position::kStartFen, 1), cancelled)), EngineErrorKind::Cancelled);
        // The lines of a request for FindMove are only validated
        EXPECT_TRUE(engine.BeginFindMove(RequestFor(Position::kStartFen, 1, kMaxAnalysisLines), token).has_value());
    }

    TEST(SearchSliceTest, ACancelBetweenSlicesIsCancelled) {
        OwnEngine engine;

        for (const int lines : {1, 3}) {
            CancellationToken token = CancellationToken::Create();
            const SearchRequest request = RequestFor(kKiwipeteFen, kGoldenForeverDepth, lines);
            auto search = lines == 1 ? engine.BeginFindMove(request, token) : engine.BeginAnalyse(request, token);
            ASSERT_TRUE(search.has_value()) << lines;

            for (int slice = 0; slice < kSlicesBeforeCancel; ++slice) {
                ASSERT_EQ(search->Advance(), SearchProgress::Paused) << lines;
            }
            const uint64_t nodes = search->GetNodes();
            EXPECT_GT(nodes, 0u);
            token.Cancel();

            EXPECT_EQ(search->Advance(), SearchProgress::Done) << lines;

            EXPECT_TRUE(search->IsDone());
            ASSERT_FALSE(search->Result().has_value());
            EXPECT_EQ(search->Result().error().kind, EngineErrorKind::Cancelled);
            EXPECT_EQ(search->GetNodes(), nodes) << "the nodes do not grow after the cancel";
            EXPECT_EQ(search->Advance(), SearchProgress::Done) << "a search that is over stays over";
            EXPECT_EQ(search->GetNodes(), nodes);
        }
    }

    TEST(SearchSliceTest, ADeadlineThatRanOutGivesTheFirstIteration) {
        OwnEngine engine;
        SearchRequest request = SearchRequest::FromPosition(PositionFromFen(kKiwipeteFen), SearchLimits{kTinyMoveTimeMs, 0});
        auto search = engine.BeginFindMove(request, CancellationToken::Create());
        ASSERT_TRUE(search.has_value());
        std::this_thread::sleep_for(kPastTinyMoveTime);

        // The deadline has passed before the first node: the retry at depth 1 answers, as the blocking call does
        DriveToTheEnd(*search, Fixed(kSearchSliceNodes));

        ASSERT_TRUE(search->IsDone());
        ASSERT_TRUE(search->Result().has_value());
        EXPECT_EQ(search->Result()->LineCount(), 1u);
        EXPECT_EQ(search->Result()->Best().depth, kGoldenEasyDepth);
        Position position = PositionFromFen(kKiwipeteFen);
        EXPECT_TRUE(IsLegalIn(position, search->Result()->Best().move));

        // The same with a few lines: each line ran out of time and has its first iteration
        request.lines = 3;
        auto lines = engine.BeginAnalyse(request, CancellationToken::Create());
        ASSERT_TRUE(lines.has_value());
        std::this_thread::sleep_for(kPastTinyMoveTime);
        DriveToTheEnd(*lines, Fixed(kSearchSliceNodes));

        ASSERT_TRUE(lines->Result().has_value());
        ASSERT_EQ(lines->Result()->LineCount(), 3u);
        for (const AnalysisLine& line : lines->Result()->lines) {
            EXPECT_EQ(line.depth, kGoldenEasyDepth);
            EXPECT_TRUE(IsLegalIn(position, line.move));
        }
    }

    TEST(SearchSliceTest, ASearchOutlivesItsRequest) {
        OwnEngine expectedEngine;
        const auto expected = expectedEngine.Analyse(RequestFor(kKiwipeteFen, kEngineShallowDepth, 2), CancellationToken::Create());
        ASSERT_TRUE(expected.has_value());

        std::optional<OwnSearch> search;
        {
            // The engine, the request and the token are gone at the end of the block
            OwnEngine engine;
            const SearchRequest request = RequestFor(kKiwipeteFen, kEngineShallowDepth, 2);
            auto begun = engine.BeginAnalyse(request, CancellationToken::Create());
            ASSERT_TRUE(begun.has_value());
            search.emplace(std::move(*begun));
            (void)search->Advance(kSearchSliceNodes);
        }
        OwnSearch moved = std::move(*search);
        search.reset();
        DriveToTheEnd(moved, Fixed(kSearchSliceNodes));

        ASSERT_TRUE(moved.Result().has_value());
        EXPECT_TRUE(SameLines(*moved.Result(), *expected));
    }

    TEST(SearchSliceTest, TwoSearchesOfOneEngineInterleave) {
        OwnEngine engine;
        const SearchRequest first = RequestFor(kKiwipeteFen, kGoldenDepth, 2);
        const SearchRequest second = RequestFor(Position::kStartFen, kGoldenDeepDepth);
        const auto expectedFirst = engine.Analyse(first, CancellationToken::Create());
        const auto expectedSecond = engine.Analyse(second, CancellationToken::Create());
        ASSERT_TRUE(expectedFirst.has_value());
        ASSERT_TRUE(expectedSecond.has_value());

        auto a = engine.BeginAnalyse(first, CancellationToken::Create());
        auto b = engine.BeginAnalyse(second, CancellationToken::Create());
        ASSERT_TRUE(a.has_value());
        ASSERT_TRUE(b.has_value());
        size_t rounds = 0;
        while ((!a->IsDone() || !b->IsDone()) && ++rounds < kInterleaveRoundLimit) {
            (void)a->Advance(kSliceOfAHundredNodes);
            (void)b->Advance(kSliceOfSevenNodes);
        }

        ASSERT_TRUE(a->Result().has_value());
        ASSERT_TRUE(b->Result().has_value());
        EXPECT_TRUE(SameLines(*a->Result(), *expectedFirst)) << Describe(*a->Result()) << " against " << Describe(*expectedFirst);
        EXPECT_TRUE(SameLines(*b->Result(), *expectedSecond)) << Describe(*b->Result()) << " against " << Describe(*expectedSecond);
    }

    /// Slice 2's pattern in a test: a coroutine on the pool begins the search and drives it with the frame budget in mind. On a threaded build `YieldIfNeeded()` never suspends
    class OwnSearchOnThePoolTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            ::BECore::Tests::EngineEnvironment::EnsureCoreServices();  // TaskManager
        }
    };

    TEST_F(OwnSearchOnThePoolTest, ACoroutineCanDriveTheSearchOnThePool) {
#if BE_SINGLE_THREAD
        // A single-thread build resumes a task that yielded only in TaskManager::Update, which only CoreManager calls, and this test waits with a poll and never pumps (OpponentOverFramesTest does,
        // with PumpTaskFrame)
        GTEST_SKIP() << "needs worker threads (BE_SINGLE_THREAD)";
#else
        OwnEngine engine;
        const SearchRequest request = RequestFor(kKiwipeteFen, kGoldenDepth);
        const auto expected = engine.FindMove(request, CancellationToken::Create());
        ASSERT_TRUE(expected.has_value());

        auto out = eastl::make_shared<CoroutineAnswer>();
        TaskManager::GetInstance().Run(DriveOnThePool(engine, request, CancellationToken::Create(), out));

        ASSERT_TRUE(WaitFor(*out)) << "the coroutine did not finish";
        ASSERT_TRUE(out->answer.has_value());
        EXPECT_EQ(out->answer->Best().move, *expected);
        EXPECT_GT(out->slices, 1u) << "the search ran in more than one slice";
#endif
    }

    TEST_F(OwnSearchOnThePoolTest, ACoroutineThatIsCancelledEndsWithCancelled) {
#if BE_SINGLE_THREAD
        GTEST_SKIP() << "needs worker threads (BE_SINGLE_THREAD)";
#else
        OwnEngine engine;
        CancellationToken token = CancellationToken::Create();
        auto out = eastl::make_shared<CoroutineAnswer>();
        TaskManager::GetInstance().Run(DriveOnThePool(engine, RequestFor(kKiwipeteFen, kGoldenForeverDepth), token, out));

        std::this_thread::sleep_for(kRunBeforeCancel);
        token.Cancel();

        ASSERT_TRUE(WaitFor(*out)) << "the coroutine did not finish";
        ASSERT_FALSE(out->answer.has_value());
        EXPECT_EQ(out->answer.error().kind, EngineErrorKind::Cancelled);
#endif
    }

    // -- the async calls of IChessEngine -------------------------------------------------------------------------------------

    namespace {

        /// The answer of an async call that a task on the pool writes: the test polls `done`
        template <typename T>
        struct AsyncAnswer {
            std::atomic<bool> done{false};
            std::expected<T, EngineError> answer = std::unexpected(EngineError{EngineErrorKind::Unsupported, "not answered"});
        };

        /// Awaits @p call on the pool (the way EngineOpponent's task body does) and stores what it gave
        template <typename T>
        Task<void> AwaitOnThePool(Task<std::expected<T, EngineError>> call, eastl::shared_ptr<AsyncAnswer<T>> out) {
            out->answer = AnswerOfTask(co_await std::move(call));
            out->done.store(true, std::memory_order_release);
            co_return;
        }

        template <typename T>
        bool WaitForAnswer(const AsyncAnswer<T>& out) {
            const Clock::time_point deadline = Clock::now() + kPoolWaitLimit;
            while (!out.done.load(std::memory_order_acquire)) {
                if (Clock::now() >= deadline) {
                    return false;
                }
                std::this_thread::sleep_for(kPoolPollInterval);
            }
            return true;
        }

        /// Runs @p call as a task on the pool and waits for it by a poll with a time limit; the answer is in @p out
        template <typename T>
        eastl::shared_ptr<AsyncAnswer<T>> RunOnThePool(Task<std::expected<T, EngineError>> call) {
            auto out = eastl::make_shared<AsyncAnswer<T>>();
            TaskManager::GetInstance().Run(AwaitOnThePool<T>(std::move(call), out));
            return out;
        }

        /// An engine with the blocking calls only: the async ones are the defaults of IChessEngine
        class BlockingOnlyEngine final : public IChessEngine {
        public:
            std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken&) override {
                ++_findCalls;
                const auto found = request.FinalPosition();
                if (!found) {
                    return std::unexpected(found.error());
                }
                return MoveGenerator::GenerateLegal(*found)[0];
            }

            std::expected<Analysis, EngineError> Analyse(const SearchRequest& request, const CancellationToken& token) override {
                ++_analyseCalls;
                auto move = FindMove(request, token);
                if (!move) {
                    return std::unexpected(std::move(move.error()));
                }
                Analysis analysis;
                analysis.lines.push_back(AnalysisLine{*move, AnalysisScore{AnalysisScoreKind::Centipawns, kScriptedCentipawns}, 1});
                return analysis;
            }

            [[nodiscard]] int FindCalls() const {
                return _findCalls;
            }

            [[nodiscard]] int AnalyseCalls() const {
                return _analyseCalls;
            }

            static constexpr int kScriptedCentipawns = 17;

        private:
            int _findCalls = 0;
            int _analyseCalls = 0;
        };

    }  // namespace

    TEST_F(OwnSearchOnThePoolTest, OwnEngineAsyncGivesTheBlockingAnswer) {
        SKIP_WITHOUT_WORKER_THREADS();
        OwnEngine engine;

        for (const NamedFen& position : kPerftPositions) {
            SCOPED_TRACE(position.name);
            const SearchRequest request = RequestFor(position.fen, kEngineShallowDepth);
            const auto expectedMove = engine.FindMove(request, CancellationToken::Create());
            ASSERT_TRUE(expectedMove.has_value());

            const auto moveOut = RunOnThePool<Move>(engine.FindMoveAsync(request, CancellationToken::Create()));
            ASSERT_TRUE(WaitForAnswer(*moveOut)) << "FindMoveAsync did not finish";
            ASSERT_TRUE(moveOut->answer.has_value());
            EXPECT_EQ(*moveOut->answer, *expectedMove);

            for (const int lines : kAnalysisLineCounts) {
                if (lines > 3) {
                    continue;
                }
                const SearchRequest linesRequest = RequestFor(position.fen, kEngineShallowDepth, lines);
                const auto expected = engine.Analyse(linesRequest, CancellationToken::Create());
                ASSERT_TRUE(expected.has_value());

                const auto out = RunOnThePool<Analysis>(engine.AnalyseAsync(linesRequest, CancellationToken::Create()));
                ASSERT_TRUE(WaitForAnswer(*out)) << "AnalyseAsync did not finish";
                ASSERT_TRUE(out->answer.has_value()) << lines << " lines";
                EXPECT_TRUE(SameLines(*out->answer, *expected)) << lines << " lines: " << Describe(*out->answer) << " against " << Describe(*expected);
            }
        }
    }

    TEST_F(OwnSearchOnThePoolTest, OwnEngineAsyncReportsTheErrorsOfTheBlockingCalls) {
        SKIP_WITHOUT_WORKER_THREADS();
        OwnEngine engine;
        SearchRequest noLimits = RequestFor(Position::kStartFen, 1);
        noLimits.limits = SearchLimits{0, 0};
        CancellationToken cancelled = CancellationToken::Create();
        cancelled.Cancel();

        struct Case {
            const char* name;
            SearchRequest request;
            CancellationToken token;
            EngineErrorKind kind;
        };
        const Case cases[] = {
            {"bad request", noLimits, CancellationToken::Create(), EngineErrorKind::BadRequest},
            {"no move", RequestFor(kMatedFen, 1), CancellationToken::Create(), EngineErrorKind::NoMove},
            {"cancelled", RequestFor(Position::kStartFen, 1), cancelled, EngineErrorKind::Cancelled},
        };

        for (const Case& c : cases) {
            SCOPED_TRACE(c.name);
            const auto blocking = engine.FindMove(c.request, c.token);
            ASSERT_FALSE(blocking.has_value());
            EXPECT_EQ(blocking.error().kind, c.kind);

            const auto moveOut = RunOnThePool<Move>(engine.FindMoveAsync(c.request, c.token));
            ASSERT_TRUE(WaitForAnswer(*moveOut));
            ASSERT_FALSE(moveOut->answer.has_value());
            EXPECT_EQ(moveOut->answer.error().kind, c.kind);

            const auto analyseOut = RunOnThePool<Analysis>(engine.AnalyseAsync(c.request, c.token));
            ASSERT_TRUE(WaitForAnswer(*analyseOut));
            ASSERT_FALSE(analyseOut->answer.has_value());
            EXPECT_EQ(analyseOut->answer.error().kind, c.kind);
        }
    }

    TEST_F(OwnSearchOnThePoolTest, OwnEngineAsyncCancelledWhileRunning) {
        SKIP_WITHOUT_WORKER_THREADS();
        OwnEngine engine;
        CancellationToken moveToken = CancellationToken::Create();
        CancellationToken analyseToken = CancellationToken::Create();
        const auto moveOut = RunOnThePool<Move>(engine.FindMoveAsync(RequestFor(kKiwipeteFen, kGoldenForeverDepth), moveToken));
        const auto analyseOut = RunOnThePool<Analysis>(engine.AnalyseAsync(RequestFor(kKiwipeteFen, kGoldenForeverDepth, 2), analyseToken));

        std::this_thread::sleep_for(kRunBeforeCancel);
        EXPECT_FALSE(moveOut->done.load()) << "a depth-64 search is not over after a few milliseconds";
        moveToken.Cancel();
        analyseToken.Cancel();

        ASSERT_TRUE(WaitForAnswer(*moveOut)) << "the task did not end";
        ASSERT_TRUE(WaitForAnswer(*analyseOut)) << "the task did not end";
        ASSERT_FALSE(moveOut->answer.has_value());
        ASSERT_FALSE(analyseOut->answer.has_value());
        EXPECT_EQ(moveOut->answer.error().kind, EngineErrorKind::Cancelled);
        EXPECT_EQ(analyseOut->answer.error().kind, EngineErrorKind::Cancelled);
    }

    TEST_F(OwnSearchOnThePoolTest, TheDefaultAsyncCallsTheBlockingOne) {
        SKIP_WITHOUT_WORKER_THREADS();
        BlockingOnlyEngine engine;
        const SearchRequest request = RequestFor(Position::kStartFen, 1);

        const auto moveOut = RunOnThePool<Move>(engine.FindMoveAsync(request, CancellationToken::Create()));
        ASSERT_TRUE(WaitForAnswer(*moveOut));
        ASSERT_TRUE(moveOut->answer.has_value());
        EXPECT_EQ(engine.FindCalls(), 1) << "the default async call went through the blocking one";
        EXPECT_EQ(*moveOut->answer, *engine.FindMove(request, CancellationToken::Create()));

        const auto analyseOut = RunOnThePool<Analysis>(engine.AnalyseAsync(request, CancellationToken::Create()));
        ASSERT_TRUE(WaitForAnswer(*analyseOut));
        ASSERT_TRUE(analyseOut->answer.has_value());
        EXPECT_EQ(analyseOut->answer->Best().score.value, BlockingOnlyEngine::kScriptedCentipawns);
        EXPECT_EQ(engine.AnalyseCalls(), 1);
    }

    TEST_F(OwnSearchOnThePoolTest, TheDefaultAsyncOfAnEngineThatDoesNotAnalyseIsUnsupported) {
        SKIP_WITHOUT_WORKER_THREADS();
        struct PlayingOnly final : IChessEngine {
            std::expected<Move, EngineError> FindMove(const SearchRequest&, const CancellationToken&) override {
                return Move();
            }
        } engine;

        const auto out = RunOnThePool<Analysis>(engine.AnalyseAsync(RequestFor(Position::kStartFen, 1), CancellationToken::Create()));
        ASSERT_TRUE(WaitForAnswer(*out));
        ASSERT_FALSE(out->answer.has_value());
        EXPECT_EQ(out->answer.error().kind, EngineErrorKind::Unsupported);
    }

    // -- the opponent over many frames ---------------------------------------------------------------------------------------

    namespace {

        /// A frame budget so small that one slice of the search spends it: every frame resumes about one slice
        constexpr std::chrono::microseconds kTinyFrameBudget{5};
        /// The depth of the opponent's search: far more than one slice of nodes, and quick in a Debug build
        constexpr int kFramesDepth = 4;
        /// A frame is one slice plus a poll: a generous multiple of the budget, since a Debug slice costs far more than 5 us. Only DISABLED_FrameLengthYardstick uses it
        constexpr std::chrono::milliseconds kFrameLimit{250};
        /// The frames of the interleaving tests run before they cancel
        constexpr int kFramesBeforeCancel = 5;

        /// Runs the opponent over frames (a task frame, then Update) until the model has moved on or the time is up. Returns the frames it took; @p longestFrame (may be null) gets the
        /// longest of them
        [[nodiscard]] size_t PlayFrames(EngineOpponent& opponent, ChessGameModel& model, std::chrono::milliseconds* longestFrame = nullptr) {
            const uint64_t version = model.GetVersion();
            const Clock::time_point deadline = Clock::now() + kPoolWaitLimit;
            size_t frames = 0;
            while (model.GetVersion() == version && Clock::now() < deadline) {
                const Clock::time_point start = Clock::now();
                PumpTaskFrame();
                opponent.Update(model);
                if (longestFrame != nullptr) {
                    *longestFrame = std::max(*longestFrame, std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start));
                }
                ++frames;
#if !BE_SINGLE_THREAD
                std::this_thread::sleep_for(kPoolPollInterval);
#endif
            }
            return frames;
        }

        class OpponentOverFramesTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                ::BECore::Tests::EngineEnvironment::EnsureCoreServices();  // TaskManager
            }

            void SetUp() override {
                _savedBudget = TaskManager::GetInstance().GetFrameBudget();
                TaskManager::GetInstance().SetFrameBudget(kTinyFrameBudget);
                PumpTaskFrame();  // the first frame: from here a task started between frames yields at its first check
            }

            void TearDown() override {
                // Single-thread builds: let a search a failed test left parked run to its end (it is cancelled: the opponent is gone). On a threaded build nothing is parked
                const Clock::time_point deadline = Clock::now() + kPoolWaitLimit;
                while (TaskManager::GetInstance().GetPendingTaskCount() > 0 && Clock::now() < deadline) {
                    PumpTaskFrame();
                }
                TaskManager::GetInstance().SetFrameBudget(_savedBudget);
            }

            TaskManager::Duration _savedBudget{};
        };

    }  // namespace

    /// The single-thread build interleaves a search with frames (runs in the web-like build and is not skipped there); a threaded build only checks the answer
    TEST_F(OpponentOverFramesTest, TheOwnEnginesSearchSpansFramesAndGivesTheBlockingMove) {
        ChessGameModel model;
        const SearchLimits limits{0, kFramesDepth};
        const auto expected = OwnEngine().FindMove(SearchRequest::FromModel(model, limits), CancellationToken::Create());
        ASSERT_TRUE(expected.has_value());

        EngineOpponent opponent(eastl::make_shared<OwnEngine>(), PieceColor::White, limits);
        [[maybe_unused]] const size_t frames = PlayFrames(opponent, model);

        ASSERT_EQ(model.GetMoves().size(), 1u) << "the opponent did not play within the time limit";
        EXPECT_EQ(model.GetMoves()[0], *expected);
#if BE_SINGLE_THREAD
        EXPECT_GT(frames, 2u) << "the search ran in one frame";
#endif
    }

    /// By hand, on a quiet machine (the claim the test above dropped: no frame of the sliced search is long; most telling in a single-thread build, where the search runs in the frames):
    /// ChessTests --gtest_also_run_disabled_tests --gtest_filter=OpponentOverFramesTest.DISABLED_FrameLengthYardstick
    TEST_F(OpponentOverFramesTest, DISABLED_FrameLengthYardstick) {
        ChessGameModel model;
        EngineOpponent opponent(eastl::make_shared<OwnEngine>(), PieceColor::White, SearchLimits{0, kFramesDepth});
        std::chrono::milliseconds longestFrame{0};
        [[maybe_unused]] const size_t frames = PlayFrames(opponent, model, &longestFrame);

        ASSERT_EQ(model.GetMoves().size(), 1u) << "the opponent did not play within the time limit";
        EXPECT_LT(longestFrame, kFrameLimit);
    }

    TEST_F(OpponentOverFramesTest, ACancelInTheMiddleOfAMultiFrameSearchPlaysNothing) {
        ChessGameModel model;
        EngineOpponent opponent(eastl::make_shared<OwnEngine>(), PieceColor::White, SearchLimits{0, kGoldenForeverDepth});

        for (int frame = 0; frame < kFramesBeforeCancel; ++frame) {
            PumpTaskFrame();
            opponent.Update(model);
        }
        EXPECT_TRUE(opponent.IsThinking());
        EXPECT_TRUE(model.GetMoves().empty());
        opponent.Cancel();
        EXPECT_FALSE(opponent.IsThinking());

        // The search sees the token at its next slice and ends (single-thread: pumped here until no task is parked; threaded: the pending count does not include a running task,
        // so the sleep below is the wait). The Update after it drops the answer, and being the White opponent's turn it starts a new search that the destructor cancels
        const Clock::time_point deadline = Clock::now() + kPoolWaitLimit;
        while (TaskManager::GetInstance().GetPendingTaskCount() > 0 && Clock::now() < deadline) {
            PumpTaskFrame();
        }
        std::this_thread::sleep_for(kRunBeforeCancel);
        opponent.Update(model);
        EXPECT_TRUE(model.GetMoves().empty());
    }

    TEST_F(OpponentOverFramesTest, AMovedVersionInTheMiddleOfAMultiFrameSearchDropsTheAnswer) {
        ChessGameModel model;
        EngineOpponent opponent(eastl::make_shared<OwnEngine>(), PieceColor::White, SearchLimits{0, kGoldenForeverDepth});
        for (int frame = 0; frame < kFramesBeforeCancel; ++frame) {
            PumpTaskFrame();
            opponent.Update(model);
        }
        ASSERT_TRUE(opponent.IsThinking());

        model.Reset();  // the version moves: the next Update cancels the search
        opponent.Update(model);
        EXPECT_FALSE(opponent.IsThinking());

        const Clock::time_point deadline = Clock::now() + kPoolWaitLimit;
        while (TaskManager::GetInstance().GetPendingTaskCount() > 0 && Clock::now() < deadline) {
            PumpTaskFrame();
        }
        std::this_thread::sleep_for(kRunBeforeCancel);
        opponent.Update(model);
        EXPECT_TRUE(model.GetMoves().empty());
        EXPECT_FALSE(opponent.GetLastError().has_value());
    }

    // -- the benchmark -------------------------------------------------------------------------------------------------------

    namespace {

        constexpr int kBenchmarkRepeats = 5;

        struct BenchmarkPosition {
            const char* name;
            eastl::string_view fen;
            int depth;
        };

        /// Depth 4 on the six positions, depth 5 on the start position and Kiwipete: about 70 ms of work in Release (one pass over the rows)
        std::vector<BenchmarkPosition> BenchmarkPositions() {
            std::vector<BenchmarkPosition> positions;
            for (const NamedFen& position : kPerftPositions) {
                positions.push_back({position.name, position.fen, kGoldenDepth});
            }
            positions.push_back({kPerftPositions[0].name, kPerftPositions[0].fen, kGoldenDeepDepth});
            positions.push_back({kPerftPositions[1].name, kPerftPositions[1].fen, kGoldenDeepDepth});
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

        /// A SearchJob driven to the end in slices of kSearchSliceNodes, as a coroutine caller does it
        uint64_t JobInSlices(eastl::string_view fen, int depth) {
            SearchSettings settings;
            settings.maxDepth = depth;
            SearchJob job(PositionFromFen(fen), settings);
            while (job.Advance(kSearchSliceNodes) == SearchProgress::Paused) {
            }
            return job.GetResult().nodes;
        }

        /// The same, with the cheap look that a coroutine caller takes between slices: whether the frame budget is spent (it never is here,
        /// no task runs, so this is the cost of the call and of the pointer test). The result is the nodes plus the polls that said yes (none),
        /// so it is comparable with the other two and the poll cannot be optimized away
        uint64_t JobInSlicesWithAPoll(eastl::string_view fen, int depth) {
            SearchSettings settings;
            settings.maxDepth = depth;
            SearchJob job(PositionFromFen(fen), settings);
            uint64_t polls = 0;
            while (!job.IsDone()) {
                polls += TaskManager::GetInstance().ShouldYield() ? 1 : 0;
                (void)job.Advance(kSearchSliceNodes);
            }
            return job.GetResult().nodes + polls;
        }

        /// What OwnEngine::FindMove() does: begin the search of a request for the depth and run it to the end in one call (the nodes are read from the OwnSearch)
        uint64_t OwnSearchToTheEnd(eastl::string_view fen, int depth) {
            OwnEngine engine;
            auto search = engine.BeginFindMove(RequestFor(fen, depth), CancellationToken::Create());
            (void)search->Advance(UINT64_MAX);
            return search->GetNodes();
        }

        /// The same driven in slices of kSearchSliceNodes, as a coroutine caller does it
        uint64_t OwnSearchInSlices(eastl::string_view fen, int depth) {
            OwnEngine engine;
            auto search = engine.BeginFindMove(RequestFor(fen, depth), CancellationToken::Create());
            while (search->Advance(kSearchSliceNodes) == SearchProgress::Paused) {
            }
            return search->GetNodes();
        }

    }  // namespace

    // Run by hand, in a Release build, on a quiet machine: ChessTests --gtest_also_run_disabled_tests --gtest_filter='SearchSliceBenchmark.*'
    // ctest does not run it. The first column uses the public Search() only, so the same file measures the code before and after a change
    TEST(SearchSliceBenchmark, DISABLED_NodesPerSecond) {
        RunBenchmark({{"Search() one call", SearchToTheEnd},
                      {"SearchJob in slices of kSearchSliceNodes", JobInSlices},
                      {"SearchJob in slices with a ShouldYield() poll", JobInSlicesWithAPoll},
                      {"OwnEngine search in one call (what FindMove() does)", OwnSearchToTheEnd},
                      {"OwnEngine search in slices of kSearchSliceNodes", OwnSearchInSlices}});
    }

}  // namespace BECore::Chess::Tests
