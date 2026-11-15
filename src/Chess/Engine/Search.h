#pragma once

#include <Chess/Engine/Evaluation.h>
#include <Chess/Game/Move.h>
#include <Chess/Game/Position.h>
#include <TaskSystem/CancellationToken.h>

namespace BECore::Chess {

    /// The deepest iteration of a search, in plies. A larger SearchSettings::maxDepth is brought down to it
    inline constexpr int kMaxSearchDepth = 64;

    /// Nodes between two looks at the clock and at the token (the node limit is looked at in every node)
    inline constexpr uint64_t kSearchPollInterval = 1024;

    /// The default size of the transposition table of a search, in entries (an entry is 16 bytes)
    inline constexpr size_t kDefaultTableEntries = size_t{1} << 18;

    /// What to search for.
    struct SearchSettings {
        /// The last iteration, in plies from the root; 1 or more
        int maxDepth = 1;
        /// Whether a node at depth 0 goes on with captures and promotions (and check evasions) instead of returning the
        /// static evaluation. Switched off, the search is plain depth-limited alpha-beta
        bool quiescence = true;
        /// Whether a node in check is searched one ply deeper
        bool checkExtension = true;
        /// Whether the quiet moves of a node are tried in the order of the killer moves and the history table instead of the
        /// generator's (captures and promotions come first either way)
        bool quietOrdering = true;
        /// The size of the transposition table, in entries (rounded up to a power of two); 0 means no table. The table belongs to
        /// one call of Search(): it is built when the search starts and freed when it ends, nothing is kept between calls. It
        /// is built only for maxDepth of 3 or more, so the shallow levels pay no allocation
        size_t tableEntries = kDefaultTableEntries;
        /// Moves the root does not try (the earlier lines of a multi-line analysis, see OwnEngine::Analyse()). Only the list of root moves is filtered:
        /// the table, the killers and the history are not touched. The caller leaves at least one legal move (the list may name moves
        /// that are not legal, they are ignored); a position whose every legal move is excluded has nothing to search and the result
        /// is not complete
        Vector<Move> excludedRootMoves;
    };

    /// When to stop. A search with the default control runs to SearchSettings::maxDepth.
    struct SearchControl {
        /// A cancelled token stops the search (an invalid, default token never does). It may be cancelled from another thread
        CancellationToken token;
        /// The time after which the search stops; nothing means no time limit
        Optional<std::chrono::steady_clock::time_point> deadline;
        /// Stop once this many nodes are searched; 0 means no limit. Meant for tests, where nodes are a clock that does not tick
        uint64_t nodeLimit = 0;
    };

    /// What a search found.
    struct SearchResult {
        /// The best move of the last iteration that finished; the null move when none did
        Move best;
        /// The score of @ref best from the side to move, in centipawns, or a mate score (MateIn())
        int score = 0;
        /// The depth of the last iteration that finished; 0 when none did
        int depth = 0;
        /// Nodes searched, those of an aborted iteration included
        uint64_t nodes = 0;
        /// Whether an iteration finished, so that @ref best, @ref score and @ref depth are valid. False when the search was
        /// stopped before it began, and when every legal root move was excluded
        bool complete = false;
        /// Whether the control ended the search before it was done (the deeper iterations were left out or cut short)
        bool stopped = false;
    };

    /**
     * @brief The best move of @p position by iterative deepening negamax with alpha-beta pruning, quiescence, killer and history
     * ordering and a transposition table.
     *
     * The best move of the last finished iteration is the answer. Moves are tried best-first and the order is stable, so ties never
     * depend on the platform. Killers and history are learned in the one call and used in the full-width nodes only (not at the
     * root, not in quiescence).
     *
     * Scores are from the side to move. Being mated at a ply p from the root scores -MateIn(p), so a shorter mate is
     * preferred and a longer defense. Inside the tree, a stalemate, insufficient material, 50 moves without progress and a
     * position that has occurred before (the game's history counts, not only the search's) all score kDrawScore. Quiescence
     * applies no draw rule. The root is never judged this way: it always has a move to return (unless the game is over: then
     * @ref SearchResult::best is null, @ref SearchResult::complete is true and the score tells mate or draw).
     *
     * The table (SearchSettings::tableEntries) is private to the call: tableEntries x 16 bytes, allocated once at the start, so equal
     * requests give equal answers and a caller may run Search() on several threads with nothing to share. A mate score is kept as
     * "mate in m plies from this node", so an entry read at another ply gives the right distance from the root. The score at a fixed
     * depth may differ with the table on (a deeper entry can answer a shallower node), but it is the same on every run.
     *
     * **Draws that depend on the history are not stored:** a node whose subtree returned a draw because a position had occurred
     * before (or the 50-move clock) stores nothing, and a table result is taken (not just its move) only where the 50-move clock
     * cannot reach the limit inside the subtree. The limit that remains is the usual one of tables, the graph history
     * interaction: a result stored on a path where a later position could not repeat is read on a path where it could. It is
     * not cured.
     *
     * Deterministic: the same position with the same history, SearchSettings and no stop gives the same best move, score
     * and node count on every run and platform (no randomness, no clock). A search stopped by @p control is not:
     * the clock and the thread that cancels decide where it ends. The first iteration always finishes, a stop is looked at
     * only after it, so a search that was not stopped before it began has a move to give. A forced mate within the depth
     * searched ends the iterations early (a deeper one cannot find a shorter mate).
     *
     * @p position is the game; the search plays moves on it and takes them back, and hands it back equal (same key, same FEN),
     * also when it was stopped. It must not be used by another thread meanwhile. The line being searched is not on the native stack
     * (the frames of the plies live on the heap, SearchJob below is the same machine driven in slices), so the search needs a few
     * tens of kilobytes of stack at any depth.
     */
    [[nodiscard]] SearchResult Search(Position& position, const SearchSettings& settings, const SearchControl& control = {});

    /// Whether SearchJob::Advance() stopped to give the caller the thread, or the search is over
    CORE_ENUM(SearchProgress, uint8_t, Paused, Done)

    /// The nodes of a slice that a caller which must stay responsive hands to SearchJob::Advance(): short against a frame
    /// even in Debug and in the browser, long against the cost of the call itself
    inline constexpr uint64_t kSearchSliceNodes = 256;

    /**
     * @brief The search of Search(), as a state machine that a caller drives a few nodes at a time.
     *
     * The same code as Search(), which is this machine run to the end on the caller's position, so a job gives, slice by slice,
     * the best move, score, depth, node count and stop flag that Search() gives in one call, for any sizes of the slices. The job
     * owns copies of the position (with its history), the settings and the control, so a coroutine that drives it may outlive the
     * objects it was made from, and it owns its table (SearchSettings::tableEntries x 16 bytes), killers, history and stack of plies.
     * It is moved, never copied.
     *
     * Not thread safe: one thread drives a job at a time, though not always the same one. SearchControl::token may be cancelled from any thread.
     */
    class SearchJob {
    public:
        SearchJob(Position position, SearchSettings settings, SearchControl control = {});
        ~SearchJob();
        SearchJob(SearchJob&&) noexcept;
        SearchJob& operator=(SearchJob&&) noexcept;
        SearchJob(const SearchJob&) = delete;
        SearchJob& operator=(const SearchJob&) = delete;

        /**
         * @brief Runs the search for about @p maxNodes more nodes and answers whether it is over.
         *
         * `Paused` means the search goes on at the next call, `Done` that it ended (the depth was reached, a forced mate was found, or the
         * control stopped it); after `Done` a call does nothing and answers `Done`. UINT64_MAX runs to the end, and 0 is read as 1, so every call
         * makes progress.
         *
         * A slice ends only between two nodes, just before a move is tried, so the position is whole at the pause and nothing about the search
         * (a count, an order, a limit) depends on where the pauses are. A slice runs at most @p maxNodes nodes plus one: a node at the
         * horizon is counted twice (as a full-width node and as the quiescence node it turns into), and the pause is looked for before
         * the next move, so the last node of a slice may take that one extra. The node limit is looked at in every node; the clock and the
         * token every kSearchPollInterval nodes counted from the start of the search, not of the slice, **and at the start of every call** once the
         * first iteration is done (the first iteration is never stopped): a token cancelled or a deadline passed while the job was
         * paused ends it at once, with the nodes of the call at 0.
         */
        [[nodiscard]] SearchProgress Advance(uint64_t maxNodes = kSearchSliceNodes);

        [[nodiscard]] bool IsDone() const noexcept;

        /// What the search has found so far: the last finished iteration (none until one does), and the nodes searched so far,
        /// those of an iteration still running included. Final once IsDone()
        [[nodiscard]] SearchResult GetResult() const noexcept;

        /// The depth of the last iteration that finished, 0 while none has
        [[nodiscard]] int FinishedDepth() const noexcept;

    private:
        struct Impl;
        UniquePtr<Impl> _impl;
    };

}  // namespace BECore::Chess
