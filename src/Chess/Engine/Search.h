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

    /// The default size of the transposition table of a search, in entries: 4 MB (an entry is 16 bytes)
    inline constexpr size_t kDefaultTableEntries = size_t{1} << 18;

    /// What to search for.
    struct SearchSettings {
        /// The last iteration, in plies from the root; 1 or more
        int maxDepth = 1;
        /// Whether a node at depth 0 goes on with captures and promotions (and check evasions) instead of returning the
        /// static evaluation. Switched off, the search is plain depth-limited alpha-beta, which the tests compare with minimax
        bool quiescence = true;
        /// Whether a node in check is searched one ply deeper. Switched off for the same comparison
        bool checkExtension = true;
        /// Whether the quiet moves of a node are tried in the order of the killer moves and the history table (below) instead of the
        /// generator's. Switched off, the order is the old one (captures, promotions, generator order); the tests compare the two
        bool quietOrdering = true;
        /// The size of the transposition table, in entries (rounded up to a power of two); 0 means no table. The table belongs to
        /// one call of Search(): it is built when the search starts and freed when it ends, nothing is kept between calls. It
        /// is built only for maxDepth of 3 or more, so the shallow levels pay no allocation. The tests switch it off to compare
        size_t tableEntries = kDefaultTableEntries;
        /// Moves the root does not try (the earlier lines of a multi-line analysis, see OwnEngine::Analyse()). Only the list of root moves is filtered:
        /// the table, the killers and the history are not touched, so an empty list is the search of before, bit for bit. The caller leaves at least one
        /// legal move (the list may name moves that are not legal, they are ignored); a position whose every legal move is excluded has nothing to search
        /// and the result is not complete
        eastl::vector<Move> excludedRootMoves;
    };

    /// When to stop. A search with the default control runs to SearchSettings::maxDepth.
    struct SearchControl {
        /// A cancelled token stops the search (an invalid, default token never does). It may be cancelled from another thread
        CancellationToken token;
        /// The time after which the search stops; nothing means no time limit
        std::optional<std::chrono::steady_clock::time_point> deadline;
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
        /// Whether an iteration finished, so that @ref best, @ref score and @ref depth are valid. False only when the search was
        /// stopped before it began
        bool complete = false;
        /// Whether the control ended the search before it was done (the deeper iterations were left out or cut short)
        bool stopped = false;
    };

    /**
     * @brief The best move of @p position found by iterative deepening negamax with alpha-beta pruning.
     *
     * Iteration d searches every line d plies deep (a check does not use up a ply, up to a ply limit), then captures and
     * promotions until the position is quiet (quiescence, with the static score as a stand-pat), and the best move of the
     * last finished iteration is the answer. Moves are tried best-first: the move that was best in the previous iteration at the
     * root, then captures by "most valuable victim, least valuable attacker", then promotions, then the two killer moves of the
     * ply (quiet moves that cut off a sibling node at the same ply), then the other quiet moves by their history score (how
     * often and how deep a quiet move from square to square has cut off, per color), then the rest in the order of
     * MoveGenerator; the order is stable, so ties never depend on the platform. Killers and history are learned in the one
     * call and used in the full-width nodes only (not at the root, not in quiescence).
     *
     * Scores are from the side to move. Being mated at a ply p from the root scores -MateIn(p), so a shorter mate is
     * preferred and a longer defense. Inside the tree, a stalemate, insufficient material, 50 moves without progress and a
     * position that has occurred before (the game's history counts, not only the search's) all score kDrawScore. Quiescence
     * applies no draw rule (captures and promotions only: the static score, and mate when in check). The root is never judged
     * this way: it always has a move to return (unless the game is over: then @ref SearchResult::best is null,
     * @ref SearchResult::complete is true and the score tells mate or draw).
     *
     * Transposition table (SearchSettings::tableEntries, on by default, built when maxDepth is 3 or more): the full-width nodes (not the root, not
     * quiescence) remember their result by the position's Zobrist key and use it when the same position comes again at least as
     * deep (an exact score, or a bound that cuts or narrows the window) or else as the first move to try. A mate score is kept as
     * "mate in m plies from this node", so an entry read at another ply gives the right distance from the root. The table is
     * private to the call: tableEntries x 16 bytes, allocated once at the start, so equal requests give equal answers and an
     * object that calls Search() from several threads has nothing to share. The score at a fixed depth may differ with the table on
     * (a deeper entry can answer a shallower node), but it is the same on every run; and the table is not always fewer nodes (it is on
     * deep middlegames, not on every position and depth).
     *
     * Draws that depend on the history are not stored: a node whose subtree returned a draw because a position had occurred
     * before (or the 50-move clock) stores nothing, and a table result is taken (not just its move) only where the 50-move clock
     * cannot reach the limit inside the subtree. The limit that remains is the usual one of tables, the graph history
     * interaction: a result stored on a path where a later position could not repeat is read on a path where it could. It is
     * not cured; the tests compare the answer with the table on and off on the repetition positions.
     *
     * Deterministic: the same position with the same history, SearchSettings and no stop gives the same best move, score
     * and node count on every run and platform (a table owned by the call, no randomness, no clock). A search stopped by @p control is not:
     * the clock and the thread that cancels decide where it ends. The first iteration always finishes, a stop is looked at
     * only after it, so a search that was not stopped before it began has a move to give. A forced mate within the depth
     * searched ends the iterations early (a deeper one cannot find a shorter mate).
     *
     * @p position is the game; the search plays moves on it and takes them back, and hands it back equal (same key, same FEN),
     * also when it was stopped. It must not be used by another thread meanwhile. No allocation beyond the position's own history and the table.
     */
    [[nodiscard]] SearchResult Search(Position& position, const SearchSettings& settings, const SearchControl& control = {});

}  // namespace BECore::Chess
