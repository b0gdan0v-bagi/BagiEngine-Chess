#pragma once

#include <Chess/Engine/ChessEngine.h>

namespace BECore::Chess {

    /// The program the computer opponent can be: Stockfish (a separate program the person installs, talked to over UCI) or the engine of our own (OwnEngine, in the process of the game).
    /// The person picks one in the window; nothing picks one for them, and one never stands in for the other when it fails
    CORE_ENUM(OpponentEngine, uint8_t, Stockfish, Own)

    /// The engine a game starts with. Stockfish, as before the own engine could be picked, so a missing Stockfish stays "Engine not found" until the person picks the other
    inline constexpr OpponentEngine kDefaultOpponentEngine = OpponentEngine::Stockfish;

    /// The name on the wire (the control API): "stockfish", "own"
    [[nodiscard]] eastl::string_view OpponentEngineId(OpponentEngine engine);

    /// The word of the HUD: "Stockfish", "Own engine"
    [[nodiscard]] eastl::string_view OpponentEngineName(OpponentEngine engine);

    /// The engine with this id (exactly as written: ids are lower case), or nothing
    [[nodiscard]] std::optional<OpponentEngine> FindOpponentEngine(eastl::string_view id);

    /// The engine after this one in the cycle of the Engine button: Stockfish, Own, and back to Stockfish
    [[nodiscard]] OpponentEngine NextOpponentEngine(OpponentEngine engine);

}  // namespace BECore::Chess
