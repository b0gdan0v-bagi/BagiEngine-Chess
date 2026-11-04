#pragma once

#include <Chess/Engine/ChessEngine.h>
#include <Chess/Engine/OpponentEngines.h>

namespace BECore::Chess {

    /// One strength of the computer opponent: its words and what the engine is given. Pure data, see GetOpponentLevels()
    struct OpponentLevel {
        eastl::string_view id;    ///< lower case; the name of the level on the wire (the control API)
        eastl::string_view name;  ///< the word of the HUD ("Medium")
        int skillLevel = 0;       ///< the UCI option `Skill Level`, 0 (weakest) to 20 (full strength)
        int moveTimeMs = 0;       ///< how long Stockfish thinks about a move
        int ownDepth = 0;         ///< how many plies the own engine searches (a fixed depth is deterministic); 0 means none, which a search refuses
    };

    /// The levels, weakest first. The skill and the time are first guesses, to be tuned by hand against a real Stockfish
    [[nodiscard]] eastl::span<const OpponentLevel> GetOpponentLevels();

    /// The index in GetOpponentLevels() of the level a game starts with ("medium")
    inline constexpr size_t kDefaultOpponentLevel = 1;

    /// The index of the level with this id (exactly as written: ids are lower case), or nothing
    [[nodiscard]] std::optional<size_t> FindOpponentLevel(eastl::string_view id);

    /// The longest the own engine may think about a move at any level. It is not a strength knob (the depth of the level ends the search): it only keeps the window from
    /// a freak position, so the own engine is deterministic unless this cap is hit
    inline constexpr int kOwnMoveTimeCapMs = 10000;

    /// What the engine gets for a move at this level. Stockfish: the move time of the level and no depth. The own engine: the depth of the level, with kOwnMoveTimeCapMs as
    /// the protection of the window. A level without an ownDepth gives the own engine a depth of 0 (a request the search refuses)
    [[nodiscard]] SearchLimits LimitsOf(OpponentEngine engine, const OpponentLevel& level);

    /// The UCI options of this level for UciEngineSettings::options: name and value of `setoption name <first> value <second>`
    [[nodiscard]] eastl::vector<eastl::pair<eastl::string, eastl::string>> EngineOptionsOf(const OpponentLevel& level);

}  // namespace BECore::Chess
