#include "OpponentEngines.h"

namespace BECore::Chess {

    StringView OpponentEngineId(OpponentEngine engine) {
        switch (engine) {
            case OpponentEngine::Stockfish:
                return "stockfish";
            case OpponentEngine::Own:
                return "own";
        }
        return "";
    }

    StringView OpponentEngineName(OpponentEngine engine) {
        switch (engine) {
            case OpponentEngine::Stockfish:
                return "Stockfish";
            case OpponentEngine::Own:
                return "Own engine";
        }
        return "";
    }

    Optional<OpponentEngine> FindOpponentEngine(StringView id) {
        for (const OpponentEngine engine : EnumUtils<OpponentEngine>::Values()) {
            if (OpponentEngineId(engine) == id) {
                return engine;
            }
        }
        return std::nullopt;
    }

    OpponentEngine NextOpponentEngine(OpponentEngine engine) {
        OpponentEngine next = kDefaultOpponentEngine;
        switch (engine) {
            case OpponentEngine::Stockfish:
                next = OpponentEngine::Own;
                break;
            case OpponentEngine::Own:
                next = OpponentEngine::Stockfish;
                break;
        }
        return IsOpponentEngineAvailable(next) ? next : engine;
    }

}  // namespace BECore::Chess
