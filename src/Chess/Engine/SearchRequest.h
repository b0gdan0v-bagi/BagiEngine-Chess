#pragma once

#include <Chess/Engine/ChessEngine.h>
#include <Chess/Game/ChessGameModel.h>

namespace BECore::Chess {

    /**
     * @brief What an engine is asked: the game so far and how long to think.
     *
     * The game is its start position and the moves played from it, the way UCI wants it (`position fen <startFen> moves ...`), so that
     * the engine sees the repetitions; a bare Position has no moves behind it. The request owns copies of everything, so it can move to
     * a worker thread while the model goes on changing on the main one.
     */
    struct SearchRequest {
        eastl::string startFen;
        eastl::vector<Move> moves;
        SearchLimits limits;
        /// How many lines IChessEngine::Analyse() returns (1..kMaxAnalysisLines, best first); FindMove() always gives one move, but still validates it. The factories give 1
        int lines = 1;

        /// The game of @p model: its start FEN and its moves, repetitions included.
        [[nodiscard]] static SearchRequest FromModel(const ChessGameModel& model, SearchLimits limits);

        /// The position as it stands, with no moves. The repetition history of a Position is lost (the FEN does not carry it), a model keeps it: use FromModel() for a game.
        [[nodiscard]] static SearchRequest FromPosition(const Position& position, SearchLimits limits);

        /// BadRequest when the limits are negative or ask for nothing (both 0): an engine would search without end; and when `lines` is outside 1..kMaxAnalysisLines.
        [[nodiscard]] std::expected<void, EngineError> Validate() const;

        /// The position the moves lead to, with the history of the game (so that Position::RepetitionCount() counts). BadRequest for a start FEN that
        /// Position::FromFen() refuses and for a move that is not legal in its turn.
        [[nodiscard]] std::expected<Position, EngineError> FinalPosition() const;
    };

}  // namespace BECore::Chess
