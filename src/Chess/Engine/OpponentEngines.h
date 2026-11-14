#pragma once

#include <Chess/Engine/ChessEngine.h>

namespace BECore::Chess {

    /// The program the computer opponent can be: Stockfish (a separate program the person installs, talked to over UCI) or the engine of our own (OwnEngine, in the process of the game).
    /// The person picks one in the window; nothing picks one for them, and one never stands in for the other when it fails
    CORE_ENUM(OpponentEngine, uint8_t, Stockfish, Own)

    /// Whether this build can run Stockfish at all: false with CHESS_NO_STOCKFISH (the web build, which has no child process, and a desktop build that switches it on to check the web one)
    inline constexpr bool kStockfishAvailable = !CHESS_NO_STOCKFISH;

    /// The engine can be used in this build: Own always, Stockfish when kStockfishAvailable. (Whether a Stockfish FILE is found is another question, asked at the pick)
    [[nodiscard]] constexpr bool IsOpponentEngineAvailable(OpponentEngine engine) noexcept {
        return engine == OpponentEngine::Own || kStockfishAvailable;
    }

    /// The engine a game starts with. Stockfish, as before the own engine could be picked, so a missing Stockfish stays "Engine not found" until the person picks the other. Own where Stockfish is
    /// not available: a default that cannot start would be an error at the first pick, and nothing falls back from one engine to the other
    inline constexpr OpponentEngine kDefaultOpponentEngine = kStockfishAvailable ? OpponentEngine::Stockfish : OpponentEngine::Own;

    /// The name on the wire (the control API): "stockfish", "own"
    [[nodiscard]] eastl::string_view OpponentEngineId(OpponentEngine engine);

    /// The word of the HUD: "Stockfish", "Own engine"
    [[nodiscard]] eastl::string_view OpponentEngineName(OpponentEngine engine);

    /// The engine with this id (exactly as written: ids are lower case), or nothing
    [[nodiscard]] Optional<OpponentEngine> FindOpponentEngine(eastl::string_view id);

    /// The engine after this one in the cycle of OpponentController::CycleEngine: Stockfish, Own, and back to Stockfish; an engine that is not available (IsOpponentEngineAvailable) is skipped,
    /// so without Stockfish the cycle stays on Own
    [[nodiscard]] OpponentEngine NextOpponentEngine(OpponentEngine engine);

}  // namespace BECore::Chess
