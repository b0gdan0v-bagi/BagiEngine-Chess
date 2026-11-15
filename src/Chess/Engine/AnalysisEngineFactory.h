#pragma once

#include <BECore/Utils/MoveOnlyFunction.h>
#include <Chess/Engine/ChessEngine.h>
#include <Chess/Engine/OpponentEngines.h>

namespace BECore::Chess {

    /// Makes the engine that analyses for the picked kind, or says why there is none. A test gives a double; the default is MakeAnalysisEngine() (PositionAnalyst.h). Shared by PositionAnalyst and
    /// MoveRater
    //-NoStd: no callable alias yet (callables-move-only)
    using AnalysisEngineFactory = MoveOnlyFunction<std::expected<ChessEnginePtr, EngineError>(OpponentEngine)>;

}  // namespace BECore::Chess
