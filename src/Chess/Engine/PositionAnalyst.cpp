#include "PositionAnalyst.h"

#include <Chess/Engine/OpponentController.h>
#include <Chess/Engine/OpponentLevels.h>
#include <Chess/Engine/OwnEngine.h>
#include <Chess/Engine/UciEngine.h>
#include <TaskSystem/TaskManager.h>

namespace BECore::Chess {

    std::optional<HintRefusal> HintRefusalFor(const ChessGameModel& model, ProgramSides programPlays) {
        if (model.IsGameOver()) {
            return HintRefusal::GameOver;
        }
        if (programPlays.Plays(model.GetTurn())) {
            return HintRefusal::ProgramsTurn;
        }
        return std::nullopt;
    }

    std::optional<HintRefusal> HintRefusalFor(const ChessGameModel& model, std::optional<PieceColor> programPlays) {
        return HintRefusalFor(model, ProgramSides::Of(programPlays));
    }

    std::expected<eastl::shared_ptr<IChessEngine>, EngineError> MakeAnalysisEngine(OpponentEngine engine, const OpponentSettings& settings) {
        switch (engine) {
            case OpponentEngine::Stockfish: {
                const std::optional<eastl::string> program = FindStockfish(settings.stockfishPath);
                if (!program) {
                    return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, NoStockfishDetail(settings.stockfishPath)});
                }
                UciEngineSettings uci;
                uci.launch.path = *program;
                uci.quitGraceMs = OpponentController::kQuitGraceMs;
                return eastl::make_shared<UciEngine>(std::move(uci));
            }
            case OpponentEngine::Own:
                return eastl::make_shared<OwnEngine>();
        }
        return std::unexpected(EngineError{EngineErrorKind::BadRequest, "unknown engine"});
    }

    PositionAnalyst::~PositionAnalyst() {
        Cancel();
    }

    void PositionAnalyst::SetEngineFactory(AnalysisEngineFactory factory) {
        _rater.SetEngineFactory(factory);
        _factory = std::move(factory);
    }

    void PositionAnalyst::SetPicked(OpponentEngine picked) {
        if (_search && _searchEngine != picked && !_search->token.IsCancelled()) {
            LOG_INFO("Position analyst ({}): the engine pick changed while the engine was analysing, the search is cancelled"_format(OpponentEngineId(_searchEngine)));
            _search->token.Cancel();
        }
        if (_engine && _engineKind != picked) {
            _engine.reset();  // a running search keeps its own reference to the old one
        }
        if (_result && _resultEngine != picked) {
            _result.reset();
            _second.reset();
            ++_revision;
        }
        if (_error && _errorEngine != picked) {
            ClearError();
        }
    }

    void PositionAnalyst::Update(const ChessGameModel& model, OpponentEngine picked) {
        UpdateSearch(model, picked);
        _rater.Update(model);
    }

    void PositionAnalyst::UpdateSearch(const ChessGameModel& model, OpponentEngine picked) {
        SetPicked(picked);
        if (_search && _search->done.load(std::memory_order_acquire)) {
            TakeAnswer(model, picked);
        }
        DropWhatIsOld(model, picked);
        if (_search) {
            CancelWhenUnwanted(model, picked);
            return;
        }
        if (!IsWantedAt(model.GetVersion()) || _result.has_value() || _error.has_value() || model.IsGameOver()) {
            return;
        }
        StartSearch(model, picked);
    }

    void PositionAnalyst::SetEvaluation(bool on) {
        if (_evaluationOn != on) {
            _evaluationOn = on;
            ++_revision;
        }
    }

    std::expected<void, HintRefusal> PositionAnalyst::RequestHint(const ChessGameModel& model) {
        if (model.IsGameOver()) {
            return std::unexpected(HintRefusal::GameOver);
        }
        const uint64_t version = model.GetVersion();
        if (!_hintVersion || *_hintVersion != version) {
            _hintVersion = version;
            ++_revision;
        }
        if (_error && _errorVersion == version) {
            ClearError();  // the retry: the next Update() asks the engine again
        }
        return {};
    }

    void PositionAnalyst::CancelHint() {
        if (_hintVersion) {
            _hintVersion.reset();
            ++_revision;
        }
    }

    void PositionAnalyst::SetRatings(bool on) {
        _rater.SetOn(on);
    }

    void PositionAnalyst::Cancel() {
        _rater.Cancel();
        if (_search) {
            _search->token.Cancel();
        }
        if (_result) {
            _result.reset();
            _second.reset();
            ++_revision;
        }
    }

    void PositionAnalyst::Shutdown() {
        Cancel();
        SetEvaluation(false);
        CancelHint();
        ClearError();
        _engine.reset();
        _rater.Shutdown();
    }

    bool PositionAnalyst::IsThinking() const {
        return _search && !_search->token.IsCancelled();
    }

    std::optional<AnalysisLine> PositionAnalyst::GetEvaluation(const ChessGameModel& model) const {
        if (!_evaluationOn || !_result || _resultVersion != model.GetVersion()) {
            return std::nullopt;
        }
        return _result;
    }

    std::optional<AnalysisLine> PositionAnalyst::GetHint(const ChessGameModel& model) const {
        if (!IsHintRequested(model) || !_result || _resultVersion != model.GetVersion()) {
            return std::nullopt;
        }
        return _result;
    }

    eastl::vector<AnalysisLine> PositionAnalyst::GetLines(const ChessGameModel& model) const {
        eastl::vector<AnalysisLine> lines;
        if ((!_evaluationOn && !IsHintRequested(model)) || !_result || _resultVersion != model.GetVersion()) {
            return lines;
        }
        lines.push_back(*_result);
        if (_second) {
            lines.push_back(*_second);
        }
        return lines;
    }

    Task<void> PositionAnalyst::RunSearch(eastl::shared_ptr<Search> search, SearchRequest request) {
        std::expected<Analysis, EngineError> answer = search->engine->Analyse(request, search->token);
        {
            const std::lock_guard lock(search->mutex);
            search->answer = std::move(answer);
        }
        search->done.store(true, std::memory_order_release);
        co_return;
    }

    bool PositionAnalyst::IsWantedAt(uint64_t version) const noexcept {
        return _evaluationOn || (_hintVersion && *_hintVersion == version);
    }

    void PositionAnalyst::TakeAnswer(const ChessGameModel& model, OpponentEngine picked) {
        std::expected<Analysis, EngineError> answer = std::unexpected(EngineError{});
        {
            const std::lock_guard lock(_search->mutex);
            answer = std::move(_search->answer);
        }
        const bool wanted = !_search->token.IsCancelled() && _searchVersion == model.GetVersion() && _searchEngine == picked;
        const uint64_t version = _searchVersion;
        const OpponentEngine engine = _searchEngine;
        _search.reset();

        if (!wanted) {
            LOG_INFO("Position analyst ({}): the answer of a cancelled or out-of-date search is dropped"_format(OpponentEngineId(engine)));
            return;
        }
        if (!answer) {
            Fail(version, engine, std::move(answer.error()));
            return;
        }
        if (answer->lines.empty()) {
            Fail(version, engine, EngineError{EngineErrorKind::NoMove, "the engine gave no line"});
            return;
        }
        _result = answer->Best();
        _second = answer->Second() ? std::optional<AnalysisLine>(*answer->Second()) : std::nullopt;
        _resultVersion = version;
        _resultEngine = engine;
        ++_revision;
    }

    void PositionAnalyst::CancelWhenUnwanted(const ChessGameModel& model, OpponentEngine picked) {
        if (_search->token.IsCancelled()) {
            return;
        }
        if (_searchVersion != model.GetVersion() || _searchEngine != picked) {
            LOG_INFO("Position analyst ({}): the game or the engine moved on while the engine was analysing, the search is cancelled"_format(OpponentEngineId(_searchEngine)));
            _search->token.Cancel();
        } else if (!IsWantedAt(_searchVersion)) {
            _search->token.Cancel();  // nobody wants the answer any more (the evaluation went off, the hint was dropped): not worth a log line
        }
    }

    void PositionAnalyst::DropWhatIsOld(const ChessGameModel& model, OpponentEngine picked) {
        const uint64_t version = model.GetVersion();
        if (_result && _resultVersion != version) {
            _result.reset();
            _second.reset();
        }
        if (_hintVersion && *_hintVersion != version) {
            _hintVersion.reset();
        }
        if (_error && (_errorVersion != version || _errorEngine != picked)) {
            ClearError();
        }
    }

    void PositionAnalyst::StartSearch(const ChessGameModel& model, OpponentEngine picked) {
        const uint64_t version = model.GetVersion();
        if (!_engine) {
            std::expected<eastl::shared_ptr<IChessEngine>, EngineError> made = _factory ? _factory(picked) : MakeAnalysisEngine(picked, OpponentSettings{});
            if (!made) {
                Fail(version, picked, std::move(made.error()));
                return;
            }
            _engine = std::move(*made);
            _engineKind = picked;
        }
        _search = eastl::make_shared<Search>(_engine);
        _searchVersion = version;
        _searchEngine = picked;
        const SearchLimits limits = LimitsOf(picked, GetOpponentLevels().back());
        // Fire and forget: the task keeps itself alive until it ends, and the answer comes through _search, not through the handle
        SearchRequest request = SearchRequest::FromModel(model, limits);
        request.lines = kAnalysisLines;
        TaskManager::GetInstance().Run(RunSearch(_search, std::move(request)));
    }

    void PositionAnalyst::Fail(uint64_t version, OpponentEngine engine, EngineError error) {
        LOG_ERROR("Position analyst ({}): no analysis, {}: {}"_format(OpponentEngineId(engine), EnumUtils<EngineErrorKind>::ToString(error.kind), error.detail));
        _error = std::move(error);
        _errorVersion = version;
        _errorEngine = engine;
        ++_revision;
    }

    void PositionAnalyst::ClearError() {
        if (_error) {
            _error.reset();
            ++_revision;
        }
    }

}  // namespace BECore::Chess
