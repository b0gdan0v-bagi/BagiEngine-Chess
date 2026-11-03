#include "SearchRequest.h"

#include <Chess/Game/MoveGenerator.h>

namespace BECore::Chess {

    namespace {

        std::unexpected<EngineError> BadRequest(eastl::string detail) {
            return std::unexpected(EngineError{EngineErrorKind::BadRequest, std::move(detail)});
        }

    }  // namespace

    SearchRequest SearchRequest::FromModel(const ChessGameModel& model, SearchLimits limits) {
        SearchRequest request;
        request.startFen = model.GetStartFen();
        request.moves = model.GetMoves();
        request.limits = limits;
        return request;
    }

    SearchRequest SearchRequest::FromPosition(const Position& position, SearchLimits limits) {
        SearchRequest request;
        request.startFen = position.ToFen();
        request.limits = limits;
        return request;
    }

    std::expected<void, EngineError> SearchRequest::Validate() const {
        if (limits.moveTimeMs < 0 || limits.depth < 0) {
            return BadRequest(Format("search limits cannot be negative (movetime {}, depth {})", limits.moveTimeMs, limits.depth));
        }
        if (limits.moveTimeMs == 0 && limits.depth == 0) {
            return BadRequest("search limits ask for nothing: give a movetime or a depth");
        }
        return {};
    }

    std::expected<Position, EngineError> SearchRequest::FinalPosition() const {
        const auto start = Position::FromFen(eastl::string_view(startFen.data(), startFen.size()));
        if (!start) {
            return BadRequest(Format("start position '{}' is not a position this game can play: {}", startFen, EnumUtils<FenError>::ToString(start.error())));
        }
        Position position = *start;
        for (size_t index = 0; index < moves.size(); ++index) {
            // The legal move of that spelling, not the stored one: a hand-made Move may lack its kind (castling, en passant)
            const eastl::string text = moves[index].ToUci();
            const auto legal = MoveGenerator::ParseUci(position, eastl::string_view(text.data(), text.size()));
            if (!legal) {
                return BadRequest(Format("move {} ({}) is not legal in its turn", index + 1, text));
            }
            position.DoMove(*legal);
        }
        return position;
    }

}  // namespace BECore::Chess
