#pragma once

#include <Chess/Engine/ChessEngine.h>

namespace BECore::Chess {

    /// The program the computer opponent can be: Stockfish (a separate program the person installs, over UCI) or the own engine (OwnEngine). The person picks one;
    /// nothing falls back from one to the other
    CORE_ENUM(OpponentEngine, uint8_t, Stockfish, Own)

    /// False with CHESS_NO_STOCKFISH (the web build has no child process)
    inline constexpr bool kStockfishAvailable = !CHESS_NO_STOCKFISH;

    /// Own always, Stockfish when kStockfishAvailable (whether a Stockfish FILE is found is asked at the pick)
    [[nodiscard]] constexpr bool IsOpponentEngineAvailable(OpponentEngine engine) noexcept {
        return engine == OpponentEngine::Own || kStockfishAvailable;
    }

    /// The engine a game starts with: Stockfish, so a missing one stays "Engine not found" until the person picks the other; Own where Stockfish is not available
    inline constexpr OpponentEngine kDefaultOpponentEngine = kStockfishAvailable ? OpponentEngine::Stockfish : OpponentEngine::Own;

    /// The id on the wire (the control API): "stockfish", "own"
    [[nodiscard]] StringView OpponentEngineId(OpponentEngine engine);

    /// The word of the HUD: "Stockfish", "Own engine"
    [[nodiscard]] StringView OpponentEngineName(OpponentEngine engine);

    /// The engine with this id (exactly as written), or nothing
    [[nodiscard]] Optional<OpponentEngine> FindOpponentEngine(StringView id);

    /// The next engine of the cycle Stockfish, Own, Stockfish; one that is not available is skipped, so without Stockfish it stays on Own
    [[nodiscard]] OpponentEngine NextOpponentEngine(OpponentEngine engine);

}  // namespace BECore::Chess
