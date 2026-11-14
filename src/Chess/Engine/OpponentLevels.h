#pragma once

#include <Chess/Engine/ChessEngine.h>
#include <Chess/Engine/OpponentEngines.h>

namespace BECore::Chess {

    /// One strength of the computer opponent: its words, what the engine is given and how often the handicap replaces its move. Pure data, see GetOpponentLevels()
    struct OpponentLevel {
        StringView id;           ///< lower case; the name of the level on the wire (the control API)
        StringView name;         ///< the word of the HUD ("Medium")
        int skillLevel = 0;      ///< the UCI option `Skill Level`, 0 (weakest) to 20 (full strength)
        int moveTimeMs = 0;      ///< how long Stockfish thinks about a move
        int ownDepth = 0;        ///< how many plies the own engine searches (a fixed depth is deterministic); 0 means none, which a search refuses
        int blunderPercent = 0;  ///< 0..100: the share of moves replaced by a random legal move (BlunderEngine), for both engines; the one knob that goes below the floor of the engines
    };

    /// The levels, weakest first: a ladder for a casual person, from a beginner (Easy) to the full strength of each engine (Max). Stockfish at `Skill Level` 0 is about a
    /// 1300-1400 player, which is the floor of the engine, so the two lowest levels add the handicap of blunderPercent. Every number is a first guess, to be tuned by playing
    [[nodiscard]] Span<const OpponentLevel> GetOpponentLevels();

    /// The index in GetOpponentLevels() of the level a game starts with ("medium")
    inline constexpr size_t kDefaultOpponentLevel = 1;

    /// The index of the level with this id (exactly as written: ids are lower case), or nothing
    [[nodiscard]] Optional<size_t> FindOpponentLevel(StringView id);

    /// The longest the own engine may think about a move at any level. It is not a strength knob (the depth of the level ends the search): it only keeps the window from
    /// a freak position, so the own engine is deterministic unless this cap is hit
    inline constexpr int kOwnMoveTimeCapMs = 10000;

    /// The engine as the game plays this level: the same pointer when the level has no blunderPercent, else a BlunderEngine around it. The handicap is the same for
    /// Stockfish and for the own engine, and a blunder starts neither of them
    [[nodiscard]] eastl::shared_ptr<IChessEngine> WithHandicap(eastl::shared_ptr<IChessEngine> engine, const OpponentLevel& level);

    /// What the engine gets for a move at this level (the handicap of the level is not a limit: see WithHandicap). Stockfish: the move time of the level and no depth. The own engine: the depth of the
    /// level, with kOwnMoveTimeCapMs as the protection of the window. A level without an ownDepth gives the own engine a depth of 0 (a request the search refuses)
    [[nodiscard]] SearchLimits LimitsOf(OpponentEngine engine, const OpponentLevel& level);

    /// The UCI options of this level for UciEngineSettings::options (the Skill Level only; no UCI_Elo, it has the same floor as Skill Level 0): name and value of `setoption name <first> value
    /// <second>`
    [[nodiscard]] Vector<eastl::pair<String, String>> EngineOptionsOf(const OpponentLevel& level);

}  // namespace BECore::Chess
