#pragma once

#include <Chess/Engine/ChessEngine.h>

namespace BECore::Chess {

    /// One strength of the computer opponent: its words and what the engine is given. Pure data, see GetOpponentLevels()
    struct OpponentLevel {
        eastl::string_view id;    ///< lower case; the name of the level on the wire (the control API)
        eastl::string_view name;  ///< the word of the HUD ("Medium")
        int skillLevel = 0;       ///< the UCI option `Skill Level`, 0 (weakest) to 20 (full strength)
        int moveTimeMs = 0;       ///< how long the engine thinks about a move
    };

    /// The levels, weakest first. The skill and the time are first guesses, to be tuned by hand against a real Stockfish
    [[nodiscard]] eastl::span<const OpponentLevel> GetOpponentLevels();

    /// The index in GetOpponentLevels() of the level a game starts with ("medium")
    inline constexpr size_t kDefaultOpponentLevel = 1;

    /// The index of the level with this id (exactly as written: ids are lower case), or nothing
    [[nodiscard]] std::optional<size_t> FindOpponentLevel(eastl::string_view id);

    /// The time limit the engine gets for a move at this level (no depth limit)
    [[nodiscard]] SearchLimits LimitsOf(const OpponentLevel& level);

    /// The UCI options of this level for UciEngineSettings::options: name and value of `setoption name <first> value <second>`
    [[nodiscard]] eastl::vector<eastl::pair<eastl::string, eastl::string>> EngineOptionsOf(const OpponentLevel& level);

}  // namespace BECore::Chess
