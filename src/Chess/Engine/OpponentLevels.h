#pragma once

#include <Chess/Engine/ChessEngine.h>
#include <Chess/Engine/OpponentEngines.h>

namespace BECore::Chess {

    /// One strength of the computer opponent: its words, what the engine is given and how often the handicap replaces its move. Pure data
    struct OpponentLevel {
        StringView id;           ///< lower case; the name of the level on the wire (the control API)
        StringView name;         ///< the word of the HUD ("Medium")
        int skillLevel = 0;      ///< the UCI option `Skill Level`, 0 (weakest) to 20 (full strength)
        int moveTimeMs = 0;      ///< how long Stockfish thinks about a move
        int ownDepth = 0;        ///< plies the own engine searches (a fixed depth is deterministic); 0 means no depth limit, only kOwnMoveTimeCapMs
        int blunderPercent = 0;  ///< 0..100: the share of moves replaced by a random legal move (BlunderEngine), for both engines; the one knob below the floor of the engines
        int varietyMargin = 0;   ///< centipawns: the own engine may play any move this close to its best (OwnEngine, with a seed); 0 = always the best
    };

    /// The levels, weakest first, from a beginner (Easy) to the full strength of each engine (Max). The numbers are first guesses
    [[nodiscard]] Span<const OpponentLevel> GetOpponentLevels();

    /// The index in GetOpponentLevels() of the level a game starts with
    inline constexpr size_t kDefaultOpponentLevel = 1;

    /// The index of the level with this id (exactly as written), or nothing
    [[nodiscard]] Optional<size_t> FindOpponentLevel(StringView id);

    /// The longest the own engine may think about a move at any level; not a strength knob (the depth ends the search) but a guard against a freak position.
    /// The own engine is deterministic unless this cap is hit
    inline constexpr int kOwnMoveTimeCapMs = 10000;

    /// The engine as the game plays this level: the same pointer when the level has no blunderPercent, else a BlunderEngine around it (a blunder starts no search)
    [[nodiscard]] SharedPtr<IChessEngine> WithHandicap(SharedPtr<IChessEngine> engine, const OpponentLevel& level);

    /// What the engine gets for a move at this level (the handicap is not a limit: see WithHandicap). Stockfish: the move time of the level and no depth. The own engine: the depth
    /// of the level and kOwnMoveTimeCapMs as the time
    [[nodiscard]] SearchLimits LimitsOf(OpponentEngine engine, const OpponentLevel& level);

    /// The UCI options of this level for UciEngineSettings::options (the Skill Level only: UCI_Elo has the same floor): name and value of `setoption name <first> value <second>`
    [[nodiscard]] Vector<Pair<String, String>> EngineOptionsOf(const OpponentLevel& level);

}  // namespace BECore::Chess
