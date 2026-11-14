#include "MoveRater.h"

#include <Chess/Engine/OpponentSettings.h>
#include <Chess/Engine/PositionAnalyst.h>
#include <TaskSystem/TaskManager.h>

namespace BECore::Chess {

    MoveRater::~MoveRater() {
        Cancel();
    }

    void MoveRater::SetEngineFactory(AnalysisEngineFactory factory) {
        _factory = std::move(factory);
    }

    void MoveRater::SetOn(bool on) {
        if (_on == on) {
            return;
        }
        _on = on;
        ++_revision;
        ClearError();
    }

    void MoveRater::Update(const ChessGameModel& model) {
        SyncHistory(model);
        if (_search && _search->done.load(std::memory_order_acquire)) {
            TakeAnswer();
        }
        if (_search) {
            if (!_on) {
                _search->token.Cancel();  // nobody wants the answer any more: not worth a log line
            }
            return;
        }
        if (!_on || _blocked) {
            return;
        }
        if (const Optional<size_t> ply = OldestUnrated()) {
            StartSearch(*ply);
        }
    }

    void MoveRater::Cancel() {
        if (_search) {
            _search->token.Cancel();
        }
    }

    void MoveRater::Shutdown() {
        SetOn(false);
        DropHistory();
        _startFen.clear();
        _engine.reset();
    }

    bool MoveRater::IsThinking() const {
        return _search && !_search->token.IsCancelled();
    }

    MoveRatings MoveRater::GetRatings(const ChessGameModel& model) const {
        const eastl::vector<Move>& moves = model.GetMoves();
        MoveRatings ratings(moves.size());
        if (Extends(model)) {
            eastl::copy(_ratings.begin(), _ratings.end(), ratings.begin());
        }
        return ratings;
    }

    Task<void> MoveRater::RunSearch(eastl::shared_ptr<Search> search, SearchRequest request) {
        std::expected<Analysis, EngineError> answer = AnswerOfTask(co_await search->engine->AnalyseAsync(request, search->token));
        {
            const std::lock_guard lock(search->mutex);
            search->answer = std::move(answer);
        }
        search->done.store(true, std::memory_order_release);
        co_return;
    }

    void MoveRater::SyncHistory(const ChessGameModel& model) {
        const eastl::vector<Move>& moves = model.GetMoves();
        if (!Extends(model)) {
            DropHistory();
            _startFen = model.GetStartFen();
        }
        for (size_t ply = _moves.size(); ply < moves.size(); ++ply) {
            _moves.push_back(moves[ply]);
        }
        _ratings.resize(_moves.size());
        _failed.resize(_moves.size(), false);
    }

    bool MoveRater::Extends(const ChessGameModel& model) const {
        const eastl::vector<Move>& moves = model.GetMoves();
        return _startFen == model.GetStartFen() && moves.size() >= _moves.size() && eastl::equal(_moves.begin(), _moves.end(), moves.begin());
    }

    void MoveRater::DropHistory() {
        ++_generation;
        if (_search) {
            _search->token.Cancel();
        }
        const bool hadRatings = eastl::any_of(_ratings.begin(), _ratings.end(), [](const Optional<MoveRating>& rating) { return rating.has_value(); });
        _moves.clear();
        _ratings.clear();
        _failed.clear();
        if (hadRatings) {
            ++_revision;
        }
        ClearError();
    }

    void MoveRater::TakeAnswer() {
        std::expected<Analysis, EngineError> answer = std::unexpected(EngineError{});
        {
            const std::lock_guard lock(_search->mutex);
            answer = std::move(_search->answer);
        }
        const bool wanted = !_search->token.IsCancelled() && _searchGeneration == _generation;
        const size_t ply = _searchPly;
        _search.reset();

        if (!wanted) {
            LOG_INFO("Move rater: the answer of a cancelled or out-of-date search is dropped");
            return;
        }
        if (!answer) {
            Fail(ply, std::move(answer.error()));
            return;
        }
        if (answer->lines.empty()) {
            Fail(ply, EngineError{EngineErrorKind::NoMove, "the engine gave no line"});
            return;
        }
        _ratings[ply] = RateMove(*answer, _moves[ply]);
        ++_revision;
    }

    void MoveRater::StartSearch(size_t ply) {
        if (!_engine) {
            std::expected<eastl::shared_ptr<IChessEngine>, EngineError> made = _factory ? _factory(OpponentEngine::Own) : MakeAnalysisEngine(OpponentEngine::Own, OpponentSettings{});
            if (!made) {
                Fail(std::nullopt, std::move(made.error()));
                return;
            }
            if (!*made) {
                Fail(std::nullopt, EngineError{EngineErrorKind::LaunchFailed, "the factory made no engine"});
                return;
            }
            _engine = std::move(*made);
        }
        SearchRequest request;
        request.startFen = _startFen;
        request.moves.assign(_moves.begin(), _moves.begin() + ply);
        request.limits = SearchLimits{0, kRatingDepth};
        request.lines = kRatingLines;
        _search = eastl::make_shared<Search>(_engine);
        _searchPly = ply;
        _searchGeneration = _generation;
        // Fire and forget: the task keeps itself alive until it ends, and the answer comes through _search, not through the handle
        TaskManager::GetInstance().Run(RunSearch(_search, std::move(request)));
    }

    Optional<size_t> MoveRater::OldestUnrated() const {
        for (size_t ply = 0; ply < _ratings.size(); ++ply) {
            if (!_ratings[ply] && !_failed[ply]) {
                return ply;
            }
        }
        return std::nullopt;
    }

    void MoveRater::Fail(Optional<size_t> ply, EngineError error) {
        const String which = ply ? Format(" of ply {}", *ply + 1) : String();
        LOG_ERROR("Move rater: no rating{}, {}: {}"_format(which, EnumUtils<EngineErrorKind>::ToString(error.kind), error.detail));
        _error = std::move(error);
        if (ply) {
            _failed[*ply] = true;
        } else {
            _blocked = true;
        }
        ++_revision;
    }

    void MoveRater::ClearError() {
        const bool hadError = _error.has_value();
        _error.reset();
        _blocked = false;
        eastl::fill(_failed.begin(), _failed.end(), false);
        if (hadError) {
            ++_revision;
        }
    }

}  // namespace BECore::Chess
