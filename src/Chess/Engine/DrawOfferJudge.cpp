#include "DrawOfferJudge.h"

#include <Chess/Engine/OpponentSettings.h>
#include <Chess/Engine/PositionAnalyst.h>
#include <TaskSystem/TaskManager.h>

namespace BECore::Chess {

    DrawOfferJudge::~DrawOfferJudge() {
        Cancel();
    }

    void DrawOfferJudge::SetEngineFactory(AnalysisEngineFactory factory) {
        _factory = std::move(factory);
    }

    void DrawOfferJudge::Offer(const ChessGameModel& model, PieceColor programColor) {
        const uint64_t version = model.GetVersion();
        if (model.IsGameOver() || (_state != DrawOfferState::Idle && _offerVersion == version)) {
            return;
        }
        Drop();
        _state = DrawOfferState::Pending;
        _offerVersion = version;
        _sideToMove = model.GetTurn();
        _programColor = programColor;
        ++_revision;

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
        _search = New<Search>(_engine);
        // Fire and forget: the task keeps itself alive until it ends, and the answer comes through _search, not through the handle
        SearchRequest request = SearchRequest::FromModel(model, SearchLimits{0, kDrawOfferDepth});
        TaskManager::GetInstance().Run(RunSearch(_search, std::move(request)), TaskPriority::Normal, ThreadType::Background, BE_TASK_LABEL("Chess draw offer"));
    }

    Optional<DrawVerdict> DrawOfferJudge::Update(const ChessGameModel& model) {
        if (_state == DrawOfferState::Idle) {
            return std::nullopt;
        }
        if (_offerVersion != model.GetVersion()) {
            Drop();
            return std::nullopt;
        }
        if (_search && _search->done.load(std::memory_order_acquire)) {
            TakeAnswer();
        }
        const Optional<DrawVerdict> verdict = _verdict;
        _verdict.reset();
        return verdict;
    }

    void DrawOfferJudge::Cancel() {
        Drop();
    }

    void DrawOfferJudge::Shutdown() {
        Drop();
        _engine.Reset();
    }

    Task<void> DrawOfferJudge::RunSearch(IntrusivePtr<Search> search, SearchRequest request) {
        std::expected<Analysis, EngineError> answer = AnswerOfTask(co_await search->engine->AnalyseAsync(request, search->token));
        {
            const LockGuard lock(search->mutex);
            search->answer = std::move(answer);
        }
        search->done.store(true, std::memory_order_release);
        co_return;
    }

    void DrawOfferJudge::Drop() {
        if (_search) {
            _search->token.Cancel();
            _search.Reset();
        }
        if (_state != DrawOfferState::Idle) {
            ++_revision;
        }
        _state = DrawOfferState::Idle;
        _verdict.reset();
        _error.reset();
    }

    void DrawOfferJudge::TakeAnswer() {
        std::expected<Analysis, EngineError> answer = std::unexpected(EngineError{});
        {
            const LockGuard lock(_search->mutex);
            answer = std::move(_search->answer);
        }
        _search.Reset();

        if (!answer) {
            Fail(std::move(answer.error()));
            return;
        }
        if (answer->lines.empty()) {
            Fail(EngineError{EngineErrorKind::NoMove, "the engine gave no line"});
            return;
        }
        Resolve(ProgramAcceptsDraw(answer->Best().score, _sideToMove, _programColor) ? DrawVerdict::Accepted : DrawVerdict::Declined);
    }

    void DrawOfferJudge::Resolve(DrawVerdict verdict) {
        _verdict = verdict;
        // An accepted offer ends the game, so nothing is left to show; the version moves with it
        _state = verdict == DrawVerdict::Accepted ? DrawOfferState::Idle : DrawOfferState::Declined;
        ++_revision;
    }

    void DrawOfferJudge::Fail(EngineError error) {
        LOG_ERROR("Draw offer: the program could not judge the position and declines, {}: {}", EnumUtils<EngineErrorKind>::ToString(error.kind), error.detail);
        _error = std::move(error);
        Resolve(DrawVerdict::Declined);
    }

    std::expected<void, DrawOfferRefusal> PersonOffersDraw(ChessGameModel& model, ProgramSides programPlays, DrawOfferJudge& judge) {
        if (const Optional<DrawOfferRefusal> refusal = DrawOfferRefusalFor(model, programPlays)) {
            return std::unexpected(*refusal);
        }
        if (!programPlays.white && !programPlays.black) {
            static_cast<void>(model.AgreeDraw());  // the refusal above ruled out the only error, a finished game
            return {};
        }
        judge.Offer(model, programPlays.white ? PieceColor::White : PieceColor::Black);
        return {};
    }

}  // namespace BECore::Chess
