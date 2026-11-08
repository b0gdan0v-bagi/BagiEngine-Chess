#pragma once

#include <Chess/Engine/SearchRequest.h>
#include <Chess/Uci/UciCommand.h>
#include <TaskSystem/CancellationToken.h>
#include <thread>

namespace BECore::Chess::Uci {

    /// The name the engine gives in `id name`, and the author in `id author`
    inline constexpr eastl::string_view kEngineName = "BagiChess Own";
    inline constexpr eastl::string_view kEngineAuthor = "BagiEngine-Chess";

    /// The options of the engine (the names UCI shows to the GUI; matching is case-insensitive)
    inline constexpr eastl::string_view kDepthOption = "Depth";
    inline constexpr eastl::string_view kBlunderPercentOption = "BlunderPercent";
    inline constexpr eastl::string_view kLevelOption = "Level";

    /// The value of the Level option that changes nothing: the other options stay as they were set
    inline constexpr eastl::string_view kCustomLevel = "custom";

    /// The range of the Depth option; 0 is no limit from the option. The top is the deepest the engine searches
    inline constexpr int kMaxDepthOption = 64;
    inline constexpr int kMaxBlunderPercentOption = 100;

    /**
     * @brief The server side of UCI over an IChessEngine: what a GUI or a tournament tool talks to.
     *
     * HandleLine() takes one line of the GUI and answers through the sink, one line each, without the end of line. Everything but a search is
     * answered inside HandleLine(); a `go` starts the search on a worker thread and returns, so the caller (the reader of stdin) goes on reading
     * `isready`, `stop` and `quit` while the engine thinks. One search runs at a time: `position`, `ucinewgame`, `setoption` and a second `go`
     * are refused with an `info string` line until the `bestmove` is out. A line that is no command is skipped, a bad `position` or `setoption`
     * is answered with an `info string` line and changes nothing.
     *
     * The engine that answers is a BlunderEngine over the engine given, with the share of the option BlunderPercent, so a level plays as the
     * game plays it. A bounded `go` (a depth, a move time or a clock) is one FindMove() call. `go infinite` and `go ponder` deepen: depth 1, 2,
     * 3, ... with Analyse() until `stop`, a mate score or kMaxSearchDepth. `stop` cancels the search; the answer is then the move of the last
     * depth that finished, or, for a bounded `go` (or an infinite one stopped before depth 1 finished), the move of a search of depth 1.
     * Every `go` is answered by exactly one `bestmove`; `quit` is a `stop` that also waits for that answer, so it never waits for a long search. HandleLine()
     * is for one thread; the sink may be called from the worker.
     */
    class UciServer {
    public:
        using Sink = eastl::function<void(eastl::string_view)>;

        /// @p engine answers the searches (not null); @p sink gets every line of the server
        UciServer(eastl::shared_ptr<IChessEngine> engine, Sink sink);

        /// Cancels a running search and waits for the worker (its `bestmove` still goes to the sink)
        ~UciServer();

        UciServer(const UciServer&) = delete;
        UciServer& operator=(const UciServer&) = delete;

        /// Handles one line of the GUI; a `go` returns at once
        void HandleLine(eastl::string_view line);

        /// True after `quit`
        [[nodiscard]] bool WantsQuit() const {
            return _quit.load();
        }

        /// Waits until the worker has finished (its `bestmove` is out). Does not cancel: a search that never ends needs a `stop` first
        void WaitIdle();

    private:
        /// What a worker needs for one `go`: copies of everything, so the reader may go on
        struct SearchJob {
            SearchRequest request;
            eastl::shared_ptr<IChessEngine> engine;
            CancellationToken token;
            bool deepen = false;
        };

        void HandleSetOption(const SetOptionParams& params);
        void HandlePosition(const PositionParams& params);
        void HandleGo(const GoParams& params);
        void StopSearch();

        /// The limits of a `go`: the depth (the smaller of the `go` and the Depth option) and the move time (`movetime`, else the clock of the side to move)
        [[nodiscard]] SearchLimits LimitsOf(const GoParams& params, bool whiteToMove) const;

        void RunSearch(const SearchJob& job);
        void Say(eastl::string_view line);
        void SayInfo(eastl::string_view text);
        void SayBestMove(const Move* move);

        eastl::shared_ptr<IChessEngine> _engine;
        Sink _sink;
        std::mutex _sinkMutex;

        SearchRequest _game;
        int _depth = 0;
        int _blunderPercent = 0;

        std::thread _worker;
        CancellationToken _token;
        std::atomic<bool> _searching = false;
        std::atomic<bool> _quit = false;
    };

}  // namespace BECore::Chess::Uci
