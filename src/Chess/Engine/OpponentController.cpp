#include "OpponentController.h"

#include <Chess/Engine/OwnEngine.h>
#include <Chess/Engine/UciEngine.h>

namespace BECore::Chess {

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

    EngineError UnavailableEngineError(OpponentEngine engine) {
        return EngineError{EngineErrorKind::Unsupported, Format("{} cannot run in this build (CHESS_NO_STOCKFISH): the opponent is the own engine", OpponentEngineName(engine))};
    }

    namespace {

        /// The default factory: a UciEngine over the Stockfish that FindStockfish() finds for the settings, with the Skill Level of the level, under the handicap of the level (WithHandicap)
        std::expected<eastl::shared_ptr<IChessEngine>, EngineError> MakeStockfishEngine(const OpponentSettings& settings, const OpponentLevel& level) {
            if (!IsOpponentEngineAvailable(OpponentEngine::Stockfish)) {
                return std::unexpected(UnavailableEngineError(OpponentEngine::Stockfish));
            }
            const std::optional<eastl::string> program = FindStockfish(settings.stockfishPath);
            if (!program) {
                return std::unexpected(EngineError{EngineErrorKind::LaunchFailed, NoStockfishDetail(settings.stockfishPath)});
            }
            UciEngineSettings engine;
            engine.launch.path = *program;
            engine.options = EngineOptionsOf(level);
            engine.quitGraceMs = OpponentController::kQuitGraceMs;
            return WithHandicap(eastl::make_shared<UciEngine>(std::move(engine)), level);
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
        // A side takes the choice of its seat for both; nobody keeps the choices as they are
        const SeatChoice white = computer ? _seats[SeatIndex(*computer)].choice : _seats[kWhite].choice;
        const SeatChoice black = computer ? _seats[SeatIndex(*computer)].choice : _seats[kBlack].choice;
        return ChooseSeats(ProgramSides::Of(computer), white, black);
    }

    std::expected<void, EngineError> OpponentController::ChooseLevel(ProgramSides plays, size_t level) {
        SeatChoice white = _seats[kWhite].choice;
        SeatChoice black = _seats[kBlack].choice;
        white.level = level;
        black.level = level;
        return ChooseSeats(plays, white, black);
    }

    std::expected<void, EngineError> OpponentController::SetLevel(size_t level) {
        return ChooseLevel(GetSides(), level);
    }

    std::expected<void, EngineError> OpponentController::Choose(std::optional<PieceColor> computer, size_t level) {
        return ChooseLevel(ProgramSides::Of(computer), level);
    }

    std::expected<void, EngineError> OpponentController::Choose(std::optional<PieceColor> computer, size_t level, OpponentEngine engine) {
        const SeatChoice choice{engine, level};
        return ChooseSeats(ProgramSides::Of(computer), choice, choice);
    }

    std::expected<void, EngineError> OpponentController::SetEngine(OpponentEngine engine) {
        SeatChoice white = _seats[kWhite].choice;
        SeatChoice black = _seats[kBlack].choice;
        white.engine = engine;
        black.engine = engine;
        return ChooseSeats(GetSides(), white, black);
    }

    std::expected<void, EngineError> OpponentController::CycleEngine() {
        return SetEngine(NextOpponentEngine(GetEngine()));
    }

    std::expected<void, EngineError> OpponentController::CycleSide() {
        const std::optional<PieceColor> plays = GetPlays();
        if (!plays) {
            return SetSide(IsWatching() ? std::nullopt : std::optional<PieceColor>(PieceColor::Black));
        }
        return SetSide(*plays == PieceColor::Black ? std::optional<PieceColor>(PieceColor::White) : std::nullopt);
    }

    std::expected<void, EngineError> OpponentController::CycleLevel() {
        return SetLevel((GetLevel() + 1) % GetOpponentLevels().size());
    }

    void OpponentController::Update() {
        DropStaleError();
        const uint64_t version = _model.GetVersion();
        for (Seat& seat : _seats) {
            if (seat.opponent) {
                seat.opponent->Update(_model);
            }
        }
        // The answer of the program is on the board now: the person's next premove plays in this very frame. Before the error bookkeeping, since the premove moves the version like any move
        if (!PlaysFor(_model.GetTurn())) {
            _model.PlayPremove();
        }
        if (_pickError && _errorVersion == version) {
            // Only an opponent can have moved the game in the calls above, and a refused pick that left it playing must not lose its sentence to the program's own move
            _errorVersion = _model.GetVersion();
        }
        for (Seat& seat : _seats) {
            if (seat.opponent) {
                NoteOpponentError(seat);
            }
        }
    }

    void OpponentController::Shutdown() {
        _model.ClearPremoves();
        if (_seats[kWhite].opponent || _seats[kBlack].opponent || _pickError) {
            ++_revision;
        }
        for (Seat& seat : _seats) {
            seat.opponent.reset();
            seat.errorNoted = false;
        }
        _pickError.reset();
    }

    std::expected<void, EngineError> OpponentController::ChooseSeats(ProgramSides plays, SeatChoice white, SeatChoice black) {
        const eastl::span<const OpponentLevel> levels = GetOpponentLevels();
        const eastl::array<SeatChoice, kSeatCount> picked = {white, black};
        for (const SeatChoice& choice : picked) {
            if (choice.level >= levels.size()) {
                return std::unexpected(EngineError{EngineErrorKind::BadRequest, Format("there is no level {}, the levels are 0 to {}", choice.level, levels.size() - 1)});
            }
        }

        // An engine this build cannot run is refused for every seat, the one nobody plays included (a pick of it would only fail at the first search)
        if (const std::optional<PieceColor> refused = FindUnavailableSeat(picked)) {
            const SeatChoice& choice = picked[SeatIndex(*refused)];
            FailPick(UnavailableEngineError(choice.engine), *refused, choice.engine, levels[choice.level]);
            return std::unexpected(*_pickError);
        }

        bool unchanged = true;
        bool opponentFailed = false;
        for (const PieceColor color : EnumUtils<PieceColor>::Values()) {
            const Seat& seat = _seats[SeatIndex(color)];
            unchanged = unchanged && (seat.opponent != nullptr) == plays.Plays(color) && seat.choice == picked[SeatIndex(color)];
            opponentFailed = opponentFailed || (seat.opponent && seat.opponent->GetLastError().has_value());
        }
        if (unchanged && !GetError().has_value()) {
            return {};
        }
        if (unchanged && _pickError && !opponentFailed) {
            // The person picks what already plays and only a refused pick is standing: they have seen it, nothing is remade and the running searches stay
            _pickError.reset();
            ++_revision;
            return {};
        }

        // The engines first: a pick that finds none for any seat is refused and everything stays as it was, the running opponents and their searches included
        eastl::array<bool, kSeatCount> remake = {};
        eastl::array<eastl::shared_ptr<IChessEngine>, kSeatCount> made;
        for (const PieceColor color : EnumUtils<PieceColor>::Values()) {
            const size_t index = SeatIndex(color);
            const Seat& seat = _seats[index];
            remake[index] = plays.Plays(color) && (!seat.opponent || seat.choice != picked[index] || seat.opponent->GetLastError().has_value());
            if (!remake[index]) {
                continue;
            }
            const OpponentLevel& chosen = levels[picked[index].level];
            std::expected<eastl::shared_ptr<IChessEngine>, EngineError> engine = MakeEngine(picked[index].engine, chosen);
            if (engine && !*engine) {
                engine = std::unexpected(EngineError{EngineErrorKind::LaunchFailed, "the engine factory made no engine"});
            }
            if (!engine) {
                FailPick(std::move(engine.error()), color, picked[index].engine, chosen);
                return std::unexpected(*_pickError);
            }
            made[index] = std::move(*engine);
        }

        // Past this point nothing can fail. The old opponents of the changed seats go first: they cancel their searches and return at once; a late answer stays with the worker
        for (const PieceColor color : EnumUtils<PieceColor>::Values()) {
            const size_t index = SeatIndex(color);
            if (remake[index] || !plays.Plays(color)) {
                _seats[index].opponent.reset();
            }
            _seats[index].errorNoted = false;
            _seats[index].choice = picked[index];
        }
        _pickError.reset();
        ++_revision;
        if (!unchanged) {
            _model.ClearPremoves();  // the sides or a choice changed: what was queued for the old program goes (a retry of a failed engine keeps it)
        }
        for (const PieceColor color : EnumUtils<PieceColor>::Values()) {
            const size_t index = SeatIndex(color);
            if (!remake[index]) {
                continue;
            }
            _seats[index].opponent = eastl::make_unique<EngineOpponent>(std::move(made[index]), color, LimitsOf(picked[index].engine, levels[picked[index].level]));
            if (_model.GetTurn() == color) {
                _model.ClearSelection();
            }
        }
        return {};
    }

    std::expected<eastl::shared_ptr<IChessEngine>, EngineError> OpponentController::MakeEngine(OpponentEngine engine, const OpponentLevel& level) const {
        if (_factory) {
            return _factory(engine, level);
        }
        switch (engine) {
            case OpponentEngine::Stockfish:
                return MakeStockfishEngine(_settings, level);
            case OpponentEngine::Own:
                return WithHandicap(eastl::make_shared<OwnEngine>(), level);
        }
        return std::unexpected(EngineError{EngineErrorKind::BadRequest, "unknown engine"});
    }

    std::optional<PieceColor> OpponentController::FindUnavailableSeat(const eastl::array<SeatChoice, kSeatCount>& picked) const {
        if (_factory) {
            return std::nullopt;
        }
        for (const PieceColor color : EnumUtils<PieceColor>::Values()) {
            if (!IsOpponentEngineAvailable(picked[SeatIndex(color)].engine)) {
                return color;
            }
        }
        return std::nullopt;
    }

    void OpponentController::FailPick(EngineError error, PieceColor computer, OpponentEngine engine, const OpponentLevel& level) {
        LOG_WARNING("Opponent: no {} engine for the {} seat at level {}, the pick is refused and the opponent stays as it was, {}: {}"_format(
            OpponentEngineId(engine), EnumUtils<PieceColor>::ToString(computer), level.id, EnumUtils<EngineErrorKind>::ToString(error.kind), error.detail));
        _pickError = std::move(error);
        _errorVersion = _model.GetVersion();
        ++_revision;
    }

    void OpponentController::DropStaleError() {
        if (!GetError().has_value() || _errorVersion == _model.GetVersion()) {
            return;
        }
        _pickError.reset();
        for (Seat& seat : _seats) {
            if (seat.opponent) {
                seat.opponent->ClearError();
            }
            seat.errorNoted = false;
        }
        ++_revision;
    }

    void OpponentController::NoteOpponentError(Seat& seat) {
        if (seat.errorNoted || !seat.opponent->GetLastError().has_value()) {
            return;
        }
        seat.errorNoted = true;
        _errorVersion = _model.GetVersion();
        ++_revision;
    }

}  // namespace BECore::Chess
