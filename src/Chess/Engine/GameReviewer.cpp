#include "GameReviewer.h"

#include <Chess/Engine/OpponentSettings.h>
#include <Chess/Engine/PositionAnalyst.h>
#include <TaskSystem/TaskManager.h>

namespace BECore::Chess {

    namespace {

        /// Who is to move in position @p ply of a game whose position 0 has @p startSide to move
        PieceColor SideToMoveAt(PieceColor startSide, size_t ply) noexcept {
            return ply % 2 == 0 ? startSide : Opposite(startSide);
        }

    }  // namespace

    Optional<ReviewRefusal> ReviewRefusalFor(const ChessGameModel& model) {
        if (!model.IsGameOver()) {
            return ReviewRefusal::NotFinished;
        }
        if (model.GetMoves().empty()) {
            return ReviewRefusal::NoMoves;
        }
        return std::nullopt;
    }

    GameReviewer::~GameReviewer() {
        Cancel();
    }

    void GameReviewer::SetEngineFactory(AnalysisEngineFactory factory) {
        _factory = std::move(factory);
    }

    std::expected<void, ReviewRefusal> GameReviewer::Start(const ChessGameModel& model) {
        if (const Optional<ReviewRefusal> refusal = ReviewRefusalFor(model)) {
            return std::unexpected(*refusal);
        }
        if (IsHeld(model) && _state != ReviewState::Failed) {
            return {};
        }
        Drop();

        std::expected<Position, FenError> position = Position::FromFen(model.GetStartFen());
        ASSERT(position.has_value(), "GameReviewer::Start: the start FEN of a model is always valid");
        _startFen = model.GetStartFen();
        _moves = model.GetMoves();
        _startSide = position->SideToMove();
        _positions.assign(_moves.size() + 1, std::nullopt);
        for (const Move move : _moves) {
            position->DoMove(move);
        }
        _total = _positions.size();
        if (const Optional<int> terminal = TerminalCentipawns(*position)) {
            const AnalysisScore score = *terminal == 0 ? AnalysisScore{AnalysisScoreKind::Centipawns, 0} : AnalysisScore{AnalysisScoreKind::Mate, 0};
            _positions.back() = ReviewedPosition{score, *terminal, Move{}, SideToMoveAt(_startSide, _moves.size())};
            --_total;
        }
        _done = 0;
        _state = ReviewState::Running;
        ++_revision;
        return {};
    }

    void GameReviewer::Update(const ChessGameModel& model) {
        BE_PROFILE_SCOPE("Chess reviewer update");
        if (_state == ReviewState::Idle) {
            return;
        }
        if (!IsHeld(model)) {
            Drop();
            return;
        }
        if (_search && _search->done.load(std::memory_order_acquire)) {
            TakeAnswer();
        }
        if (_state == ReviewState::Running && !_search) {
            StartSearch();
        }
    }

    void GameReviewer::Cancel() {
        if (_search) {
            _search->token.Cancel();
        }
    }

    void GameReviewer::Abandon() {
        Drop();
    }

    void GameReviewer::Shutdown() {
        Drop();
        _engine.Reset();
    }

    ReviewState GameReviewer::GetState(const ChessGameModel& model) const {
        return IsHeld(model) ? _state : ReviewState::Idle;
    }

    ReviewProgress GameReviewer::GetProgress(const ChessGameModel& model) const {
        return IsHeld(model) ? ReviewProgress{_done, _total} : ReviewProgress{};
    }

    ReviewedPositions GameReviewer::GetPositions(const ChessGameModel& model) const {
        return IsHeld(model) ? _positions : ReviewedPositions{};
    }

    Optional<ReviewedPosition> GameReviewer::GetPosition(const ChessGameModel& model, size_t index) const {
        if (!IsHeld(model) || index >= _positions.size()) {
            return std::nullopt;
        }
        return _positions[index];
    }

    ReviewedMoves GameReviewer::GetMoves(const ChessGameModel& model) const {
        ReviewedMoves reviewed;
        if (!IsHeld(model)) {
            return reviewed;
        }
        reviewed.resize(_moves.size());
        for (size_t ply = 0; ply < _moves.size(); ++ply) {
            const Optional<ReviewedPosition>& before = _positions[ply];
            const Optional<ReviewedPosition>& after = _positions[ply + 1];
            if (before && after) {
                reviewed[ply] = ReviewedMove{ReviewMove(before->centipawns, before->best, _moves[ply], after->centipawns), before->best};
            }
        }
        return reviewed;
    }

    Task<void> GameReviewer::RunSearch(IntrusivePtrAtomic<Search> search, SearchRequest request) {
        std::expected<Analysis, EngineError> answer = AnswerOfTask(co_await search->engine->AnalyseAsync(request, search->token));
        {
            const LockGuard lock(search->mutex);
            search->answer = std::move(answer);
        }
        search->done.store(true, std::memory_order_release);
        co_return;
    }

    bool GameReviewer::IsHeld(const ChessGameModel& model) const {
        return _state != ReviewState::Idle && _startFen == model.GetStartFen() && _moves == model.GetMoves();
    }

    void GameReviewer::Drop() {
        if (_search) {
            _search->token.Cancel();
            _search.Reset();
        }
        if (_state != ReviewState::Idle) {
            ++_revision;
        }
        _state = ReviewState::Idle;
        _startFen.clear();
        _moves.clear();
        _positions.clear();
        _done = 0;
        _total = 0;
        _error.reset();
    }

    void GameReviewer::TakeAnswer() {
        std::expected<Analysis, EngineError> answer = std::unexpected(EngineError{});
        {
            const LockGuard lock(_search->mutex);
            answer = std::move(_search->answer);
        }
        const bool wanted = !_search->token.IsCancelled();
        _search.Reset();

        if (!wanted) {
            LOG_INFO("Game review: the answer of a cancelled search is dropped");
            return;
        }
        if (!answer) {
            Fail(std::move(answer.error()));
            return;
        }
        if (answer->lines.empty()) {
            Fail(EngineError{EngineErrorKind::NoMove, "the engine gave no line"});
            return;
        }
        const AnalysisLine& line = answer->Best();
        _positions[_done] = ReviewedPosition{line.score, ScoreToCentipawns(line.score), line.move, SideToMoveAt(_startSide, _done)};
        ++_done;
        if (_done == _total) {
            _state = ReviewState::Done;
        }
        ++_revision;
    }

    void GameReviewer::StartSearch() {
        if (!_engine) {
            std::expected<ChessEnginePtr, EngineError> made = _factory ? _factory(OpponentEngine::Own) : MakeAnalysisEngine(OpponentEngine::Own, OpponentSettings{});
            if (!made) {
                Fail(std::move(made.error()));
                return;
            }
            if (!*made) {
                Fail(EngineError{EngineErrorKind::LaunchFailed, "the factory made no engine"});
                return;
            }
            _engine = std::move(*made);
        }
        SearchRequest request;
        request.startFen = _startFen;
        request.moves.assign(_moves.begin(), _moves.begin() + _done);
        request.limits = SearchLimits{0, kReviewDepth};
        request.lines = kReviewLines;
        _search = New<Search>(_engine);
        // Fire and forget: the task keeps itself alive until it ends, and the answer comes through _search, not through the handle
        TaskManager::GetInstance().Run(RunSearch(_search, std::move(request)), TaskPriority::Normal, ThreadType::Background, BE_TASK_LABEL("Chess review"));
    }

    void GameReviewer::Fail(EngineError error) {
        LOG_ERROR("Game review: stopped at position {} of {}, {}: {}", _done + 1, _total, EnumUtils<EngineErrorKind>::ToString(error.kind), error.detail);
        _error = std::move(error);
        _state = ReviewState::Failed;
        ++_revision;
    }

}  // namespace BECore::Chess
