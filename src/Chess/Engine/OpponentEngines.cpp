#include "OpponentEngines.h"

namespace BECore::Chess {

    eastl::string_view OpponentEngineId(OpponentEngine engine) {
        switch (engine) {
            case OpponentEngine::Stockfish:
                return "stockfish";
            case OpponentEngine::Own:
                return "own";
        }
        return "";
    }

    eastl::string_view OpponentEngineName(OpponentEngine engine) {
        switch (engine) {
            case OpponentEngine::Stockfish:
                return "Stockfish";
            case OpponentEngine::Own:
                return "Own engine";
        }
        return "";
    }

    std::optional<OpponentEngine> FindOpponentEngine(eastl::string_view id) {
        for (const OpponentEngine engine : EnumUtils<OpponentEngine>::Values()) {
            if (OpponentEngineId(engine) == id) {
                return engine;
            }
        }
        return std::nullopt;
    }

    OpponentEngine NextOpponentEngine(OpponentEngine engine) {
        switch (engine) {
            case OpponentEngine::Stockfish:
                return OpponentEngine::Own;
            case OpponentEngine::Own:
                return OpponentEngine::Stockfish;
        }
        return kDefaultOpponentEngine;
    }

}  // namespace BECore::Chess
