#include "OpponentController.h"

#include <Chess/Engine/UciEngine.h>

namespace BECore::Chess {

    namespace {

        /// Why no Stockfish was found; names the path that was tried when the settings gave one, else the three places that are looked at
        eastl::string NoStockfishDetail(const eastl::string& path) {
            if (!path.empty()) {
                return Format(
                    "no Stockfish file at '{}' (stockfishPath in config/ChessSettings.local.xml or ChessSettings.xml): the path is used as written, '~' and environment variables are not expanded, "
                    "a relative path is taken from the working directory",
                    path);
            }
            return "no Stockfish found: put its path in stockfishPath of config/ChessSettings.local.xml, or set the environment variable STOCKFISH to it (when it is set it must name a file, "
                   "there is no fallback), or put stockfish on PATH. The game does not ship Stockfish; install it yourself";
        }

        /// The default factory: a UciEngine over the Stockfish that FindStockfish() finds for the settings, with the Skill Level of the level
        std::expected<eastl::shared_ptr<IChessEngine>, EngineError> MakeStockfishEngine(const OpponentSettings& settings, const OpponentLevel& level) {
            const std::optional<eastl::string> program = FindStockfish(settings.stockfishPath);
            if (!program) {
                return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, NoStockfishDetail(settings.stockfishPath)});
            }
            UciEngineSettings engine;
            engine.launch.path = *program;
            engine.options = EngineOptionsOf(level);
            engine.quitGraceMs = OpponentController::kQuitGraceMs;
            return eastl::make_shared<UciEngine>(std::move(engine));
        }

    }  // namespace

    OpponentController::OpponentController(ChessGameModel& model, OpponentSettings settings) : _model(model), _settings(std::move(settings)) {}

    OpponentController::~OpponentController() {
        Shutdown();
    }

    void OpponentController::SetSettings(OpponentSettings settings) {
        _settings = std::move(settings);
    }

    void OpponentController::SetEngineFactory(EngineFactory factory) {
        _factory = std::move(factory);
    }

    std::expected<void, EngineError> OpponentController::SetSide(std::optional<PieceColor> computer) {
        return Remake(computer, _level);
    }

    std::expected<void, EngineError> OpponentController::SetLevel(size_t level) {
        return Remake(GetPlays(), level);
    }

    std::expected<void, EngineError> OpponentController::CycleSide() {
        const std::optional<PieceColor> plays = GetPlays();
        if (!plays) {
            return SetSide(PieceColor::Black);
        }
        return SetSide(*plays == PieceColor::Black ? std::optional<PieceColor>(PieceColor::White) : std::nullopt);
    }

    std::expected<void, EngineError> OpponentController::CycleLevel() {
        return SetLevel((_level + 1) % GetOpponentLevels().size());
    }

    void OpponentController::Update() {
        DropStaleError();
        if (_opponent) {
            _opponent->Update(_model);
            NoteOpponentError();
        }
    }

    void OpponentController::Shutdown() {
        if (_opponent || _pickError) {
            ++_revision;
        }
        _opponent.reset();
        _pickError.reset();
        _opponentErrorNoted = false;
    }

    std::expected<void, EngineError> OpponentController::Remake(std::optional<PieceColor> computer, size_t level) {
        const eastl::span<const OpponentLevel> levels = GetOpponentLevels();
        if (level >= levels.size()) {
            return std::unexpected(EngineError{EngineErrorKind::BadRequest, Format("there is no level {}, the levels are 0 to {}", level, levels.size() - 1)});
        }
        if (computer == GetPlays() && level == _level && !GetError().has_value()) {
            return {};
        }

        _opponent.reset();  // cancels the search of the old one and returns at once; its late answer stays with the worker
        _pickError.reset();
        _opponentErrorNoted = false;
        _level = level;
        ++_revision;
        if (!computer) {
            return {};
        }

        const OpponentLevel& chosen = levels[level];
        std::expected<eastl::shared_ptr<IChessEngine>, EngineError> engine = MakeEngine(chosen);
        if (engine && !*engine) {
            engine = std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "the engine factory made no engine"});
        }
        if (!engine) {
            FailPick(std::move(engine.error()), *computer, chosen);
            return std::unexpected(*_pickError);
        }
        _opponent = eastl::make_unique<EngineOpponent>(std::move(*engine), *computer, LimitsOf(chosen));
        if (_model.GetTurn() == *computer) {
            _model.ClearSelection();
        }
        return {};
    }

    std::expected<eastl::shared_ptr<IChessEngine>, EngineError> OpponentController::MakeEngine(const OpponentLevel& level) const {
        return _factory ? _factory(level) : MakeStockfishEngine(_settings, level);
    }

    void OpponentController::FailPick(EngineError error, PieceColor computer, const OpponentLevel& level) {
        LOG_WARNING("Opponent: no engine to play {} at level {}, the game stays with two players, {}: {}"_format(EnumUtils<PieceColor>::ToString(computer), level.id,
                                                                                                                 EnumUtils<EngineErrorKind>::ToString(error.kind), error.detail));
        _pickError = std::move(error);
        _errorVersion = _model.GetVersion();
    }

    void OpponentController::DropStaleError() {
        if (!GetError().has_value() || _errorVersion == _model.GetVersion()) {
            return;
        }
        _pickError.reset();
        if (_opponent) {
            _opponent->ClearError();
        }
        _opponentErrorNoted = false;
        ++_revision;
    }

    void OpponentController::NoteOpponentError() {
        if (_opponentErrorNoted || !_opponent->GetLastError().has_value()) {
            return;
        }
        _opponentErrorNoted = true;
        _errorVersion = _model.GetVersion();
        ++_revision;
    }

}  // namespace BECore::Chess
