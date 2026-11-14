#include "EngineOpponent.h"

#include <TaskSystem/TaskManager.h>

namespace BECore::Chess {

    EngineOpponent::EngineOpponent(SharedPtr<IChessEngine> engine, PieceColor side, SearchLimits limits) : _engine(std::move(engine)), _side(side), _limits(limits) {}

    EngineOpponent::~EngineOpponent() {
        Cancel();
    }

    void EngineOpponent::Update(ChessGameModel& model) {
        if (_search) {
            if (!_search->done.load(std::memory_order_acquire)) {
                CancelWhenStale(model);
                return;
            }
            TakeAnswer(model);
        }
        if (_lastError.has_value() || model.IsGameOver() || model.GetTurn() != _side) {
            return;
        }
        StartSearch(model);
    }

    bool EngineOpponent::IsThinking() const {
        return _search && !_search->token.IsCancelled();
    }

    void EngineOpponent::Cancel() {
        if (_search) {
            _search->token.Cancel();
        }
    }

    Task<void> EngineOpponent::RunSearch(SharedPtr<Search> search, SearchRequest request) {
        std::expected<Move, EngineError> answer = AnswerOfTask(co_await search->engine->FindMoveAsync(request, search->token));
        {
            const LockGuard lock(search->mutex);
            search->answer = std::move(answer);
        }
        search->done.store(true, std::memory_order_release);
        co_return;
    }

    void EngineOpponent::StartSearch(const ChessGameModel& model) {
        _search = MakeShared<Search>(_engine);
        _searchVersion = model.GetVersion();
        // Fire and forget: the task keeps itself alive until it ends, and the answer comes through _search, not through the handle
        TaskManager::GetInstance().Run(RunSearch(_search, SearchRequest::FromModel(model, _limits)));
    }

    void EngineOpponent::CancelWhenStale(const ChessGameModel& model) {
        if (_searchVersion != model.GetVersion() && !_search->token.IsCancelled()) {
            LOG_INFO("Engine opponent ({}): the game moved on while the engine was thinking, the search is cancelled"_format(EnumUtils<PieceColor>::ToString(_side)));
            _search->token.Cancel();
        }
    }

    void EngineOpponent::TakeAnswer(ChessGameModel& model) {
        std::expected<Move, EngineError> answer = std::unexpected(EngineError{});
        {
            const LockGuard lock(_search->mutex);
            answer = std::move(_search->answer);
        }
        const bool wanted = !_search->token.IsCancelled() && _searchVersion == model.GetVersion();
        _search.reset();

        const StringView side = EnumUtils<PieceColor>::ToString(_side);
        if (!wanted) {
            LOG_INFO("Engine opponent ({}): the answer of a cancelled or out-of-date search is dropped"_format(side));
            return;
        }
        if (!answer) {
            Fail(std::move(answer.error()));
            return;
        }
        if (const auto played = model.TryMove(*answer); !played) {
            Fail(EngineError{EngineErrorKind::IllegalMove, Format("the game refused the move {} ({})", answer->ToUci(), EnumUtils<MoveError>::ToString(played.error()))});
        }
    }

    void EngineOpponent::Fail(EngineError error) {
        LOG_ERROR("Engine opponent ({}): no move, {}: {}"_format(EnumUtils<PieceColor>::ToString(_side), EnumUtils<EngineErrorKind>::ToString(error.kind), error.detail));
        _lastError = std::move(error);
    }

}  // namespace BECore::Chess
