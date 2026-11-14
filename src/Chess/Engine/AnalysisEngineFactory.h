#pragma once

#include <Chess/Engine/ChessEngine.h>
#include <Chess/Engine/OpponentEngines.h>

namespace BECore::Chess {

    /// Makes the engine that analyses for the picked kind, or says why there is none. A test gives a double; the default is MakeAnalysisEngine() (PositionAnalyst.h). Shared by PositionAnalyst and
    /// MoveRater
    using AnalysisEngineFactory = eastl::function<std::expected<SharedPtr<IChessEngine>, EngineError>(OpponentEngine)>;

}  // namespace BECore::Chess
