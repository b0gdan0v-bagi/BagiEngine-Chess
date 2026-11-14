#include "UciEngine.h"

#include <Chess/Engine/UciProtocol.h>
#include <Chess/Game/MoveGenerator.h>
#include <cstdlib>

namespace BECore::Chess {

    namespace {

        using std::chrono::milliseconds;
        using Clock = std::chrono::steady_clock;
        using LinePredicate = bool (*)(eastl::string_view);

        constexpr milliseconds kWaitSlice{20};  // the longest single wait for a line: how often a cancelled token is looked at
        constexpr eastl::string_view kUciCommand = "uci";
        constexpr eastl::string_view kIsReadyCommand = "isready";
        constexpr eastl::string_view kNewGameCommand = "ucinewgame";
        constexpr eastl::string_view kStopCommand = "stop";
        constexpr eastl::string_view kQuitCommand = "quit";
        constexpr eastl::string_view kUciOkReply = "uciok";
        constexpr eastl::string_view kReadyOkReply = "readyok";
        constexpr eastl::string_view kBestMoveReply = "bestmove";
        constexpr eastl::string_view kMultiPvOption = "MultiPV";

#if !CHESS_NO_STOCKFISH
        constexpr const char* kStockfishVariable = "STOCKFISH";
        constexpr const char* kPathVariable = "PATH";
#if defined(_WIN32)
        constexpr const char* kStockfishFileName = "stockfish.exe";
        constexpr char kPathSeparator = ';';
#else
        constexpr const char* kStockfishFileName = "stockfish";
        constexpr char kPathSeparator = ':';
#endif
#endif  // !CHESS_NO_STOCKFISH

        std::unexpected<EngineError> Fail(EngineErrorKind kind, eastl::string detail) {
            return std::unexpected(EngineError{kind, std::move(detail)});
        }

        bool IsUciOk(eastl::string_view line) {
            return line == kUciOkReply;
        }

        bool IsReadyOk(eastl::string_view line) {
            return line == kReadyOkReply;
        }

        bool IsBestMove(eastl::string_view line) {
            return UciProtocol::ParseBestMove(line).has_value();
        }

        /**
         * Reads the lines of @p process until @p wanted likes one (the trimmed line is the answer), skipping `info` and every line it does not know.
         * Cancelled as soon as @p token is, Timeout after @p timeoutMs; the failures of the process (ProcessDied, ProtocolError) come back with
         * what was waited for in the sentence. A token that was never made (the default one) is never cancelled. @p skipped, when given, sees every
         * line that is skipped (the wait for `bestmove` passes one; the handshake and the answer to `stop` pass none).
         */
        std::expected<eastl::string, EngineError> ReadUntil(UciProcess& process, LinePredicate wanted, eastl::string_view what, int timeoutMs, const CancellationToken& token,
                                                            UciProtocol::LineCollector* skipped = nullptr) {
            const Clock::time_point deadline = Clock::now() + milliseconds(std::max(timeoutMs, 0));
            for (;;) {
                if (token.IsCancelled()) {
                    return Fail(EngineErrorKind::Cancelled, Format("cancelled while waiting for '{}'", what));
                }
                const Clock::duration remaining = deadline - Clock::now();
                if (remaining <= Clock::duration::zero()) {
                    return Fail(EngineErrorKind::Timeout, Format("the engine did not send '{}' in {} ms", what, timeoutMs));
                }

                auto line = process.ReadLine(std::min(kWaitSlice, std::chrono::ceil<milliseconds>(remaining)));
                if (line) {
                    const eastl::string_view text = UciProtocol::TrimLine(eastl::string_view(line->data(), line->size()));
                    if (wanted(text)) {
                        return eastl::string(text.data(), text.size());
                    }
                    if (skipped != nullptr) {
                        skipped->Offer(text);
                    }
                } else if (line.error().kind != EngineErrorKind::Timeout) {  // a timeout of one slice is the loop's pace, not an error
                    return Fail(line.error().kind, Format("while waiting for '{}': {}", what, line.error().detail));
                }
            }
        }

        /// `movetime + slack`, which for a huge `movetime` (Validate() accepts any positive int) is more than an int holds: it stops at the largest
        int AnswerDeadlineMs(int moveTimeMs, int slackMs) {
            const int64_t total = int64_t{moveTimeMs} + int64_t{slackMs};
            return static_cast<int>(std::min<int64_t>(total, std::numeric_limits<int>::max()));
        }

#if !CHESS_NO_STOCKFISH
        /// The first of the explicit path and $STOCKFISH that is set decides (see FindStockfish); nothing when it is not a file
        Optional<eastl::string> ExistingFile(const std::string& path) {
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error)) {
                return std::nullopt;
            }
            return eastl::string(path.c_str(), path.size());
        }

        /// The value of an environment variable; an empty string for one that is not set
        std::string EnvironmentValue(const char* name) {
#if defined(_WIN32)
            std::string text;
            char* value = nullptr;
            size_t length = 0;
            if (_dupenv_s(&value, &length, name) == 0 && value != nullptr) {
                text = value;
            }
            std::free(value);
            return text;
#else
            const char* value = std::getenv(name);
            return value != nullptr ? std::string(value) : std::string();
#endif
        }

        bool IsExecutableFile(const std::filesystem::path& path) {
            using std::filesystem::perms;
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error)) {
                return false;
            }
            const perms permissions = std::filesystem::status(path, error).permissions();
            return !error && (permissions & (perms::owner_exec | perms::group_exec | perms::others_exec)) != perms::none;
        }

        Optional<eastl::string> StockfishOnPath() {
            const std::string directories = EnvironmentValue(kPathVariable);
            size_t begin = 0;
            while (begin <= directories.size()) {
                size_t end = directories.find(kPathSeparator, begin);
                if (end == std::string::npos) {
                    end = directories.size();
                }
                if (end > begin) {  // an empty entry would mean the working directory: not looked at
                    const std::filesystem::path candidate = std::filesystem::path(directories.substr(begin, end - begin)) / kStockfishFileName;
                    if (IsExecutableFile(candidate)) {
                        const std::string text = candidate.string();
                        return eastl::string(text.c_str(), text.size());
                    }
                }
                begin = end + 1;
            }
            return std::nullopt;
        }
#endif  // !CHESS_NO_STOCKFISH

    }  // namespace

    UciEngine::UciEngine(UciEngineSettings settings) : _settings(std::move(settings)) {}

    UciEngine::~UciEngine() {
        CloseProcess();
    }

    std::expected<Move, EngineError> UciEngine::FindMove(const SearchRequest& request, const CancellationToken& token) {
        const std::lock_guard lock(_mutex);
        auto searched = RunSearch(request, kSingleLine, token);
        if (!searched) {
            return std::unexpected(std::move(searched.error()));
        }
        return searched->move;
    }

    std::expected<Analysis, EngineError> UciEngine::Analyse(const SearchRequest& request, const CancellationToken& token) {
        const std::lock_guard lock(_mutex);
        auto searched = RunSearch(request, request.lines, token);
        if (!searched) {
            return std::unexpected(std::move(searched.error()));
        }
        // The process is in step: it answered. A missing score is the engine's fault, not a reason to throw the process away
        if (searched->infos.empty() || searched->infos.front().multiPv != 1) {
            return Fail(EngineErrorKind::ProtocolError, "the engine sent no score before its bestmove");
        }
        const UciProtocol::Info& main = searched->infos.front();
        Analysis analysis{{AnalysisLine{searched->move, *main.score, main.depth}}};

        // The other lines: the first move of the `pv` of their index, in the order of the index. A move that is not legal is the engine's fault (the process is kept, as for
        // the `bestmove`); a repeat of a move already in the answer, and an index whose `pv` named nothing, are dropped, so the lines are always different moves
        for (size_t at = 1; at < searched->infos.size(); ++at) {
            const UciProtocol::Info& info = searched->infos[at];
            if (info.firstPvMove.empty()) {
                continue;
            }
            const Optional<Move> move = MoveGenerator::ParseUci(searched->position, eastl::string_view(info.firstPvMove.data(), info.firstPvMove.size()));
            if (!move) {
                return Fail(EngineErrorKind::IllegalMove, Format("the engine answered '{}' for line {}, which is not legal in {}", info.firstPvMove, info.multiPv, searched->position.ToFen()));
            }
            const bool repeated = std::any_of(analysis.lines.begin(), analysis.lines.end(), [&move](const AnalysisLine& line) { return line.move == *move; });
            if (!repeated) {
                analysis.lines.push_back(AnalysisLine{*move, *info.score, info.depth});
            }
        }
        return analysis;
    }

    std::expected<UciEngine::Searched, EngineError> UciEngine::RunSearch(const SearchRequest& request, int lineCount, const CancellationToken& token) {
        if (auto valid = request.Validate(); !valid) {
            return std::unexpected(std::move(valid.error()));
        }
        const auto position = request.FinalPosition();
        if (!position) {
            return std::unexpected(position.error());
        }
        if (MoveGenerator::GenerateLegal(*position).empty()) {
            return Fail(EngineErrorKind::NoMove, "the side to move has no legal move: the engine was not asked");
        }
        if (token.IsCancelled()) {
            return Fail(EngineErrorKind::Cancelled, "cancelled before the search started");
        }

        if (auto begun = Begin(request, lineCount, token); !begun) {
            return CloseAndFail(std::move(begun.error()));
        }
        const SearchLimits& limits = request.limits;
        const int timeoutMs = limits.moveTimeMs > 0 ? AnswerDeadlineMs(limits.moveTimeMs, _settings.answerSlackMs) : _settings.depthOnlyTimeoutMs;
        UciProtocol::LineCollector infos(lineCount);
        const auto answer = ReadUntil(*_process, IsBestMove, kBestMoveReply, timeoutMs, token, &infos);
        if (!answer) {
            if (answer.error().kind == EngineErrorKind::Cancelled) {
                return StopSearch();
            }
            return CloseAndFail(answer.error());
        }

        // The process is alive and in step from here on: it answered, so whatever is wrong with the answer, the next search can use it
        const Optional<UciProtocol::BestMove> best = UciProtocol::ParseBestMove(eastl::string_view(answer->data(), answer->size()));
        if (!best || best->IsNone()) {
            return Fail(EngineErrorKind::NoMove, Format("the engine has no move here (it answered '{}') but the game has legal moves", *answer));
        }
        const Optional<Move> move = MoveGenerator::ParseUci(*position, eastl::string_view(best->move.data(), best->move.size()));
        if (!move) {
            return Fail(EngineErrorKind::IllegalMove, Format("the engine answered '{}', which is not legal in {}", best->move, position->ToFen()));
        }
        return Searched{*move, *position, infos.Lines()};
    }

    std::expected<void, EngineError> UciEngine::Begin(const SearchRequest& request, int lineCount, const CancellationToken& token) {
        if (_process && !_process->IsRunning()) {
            CloseProcess();  // it left while nobody was asking: the next one is a new process
        }
        if (!_process) {
            auto started = UciProcess::Start(_settings.launch);
            if (!started) {
                return std::unexpected(std::move(started.error()));
            }
            _process = std::move(*started);
            if (auto shaken = Handshake(token); !shaken) {
                return shaken;
            }
        }

        if (IsNewGame(request)) {
            if (auto sent = Send(kNewGameCommand); !sent) {
                return sent;
            }
            if (auto ready = WaitUntilReady(token); !ready) {
                return ready;
            }
        }
        // The engine reads its input in order, so the option needs no `isready` of its own. A process has 1 line until told otherwise (the handshake does not touch the option),
        // and a search for one line puts it back, so the opponent's own search is never slowed by a line count an analysis left
        if (lineCount != _multiPv) {
            const eastl::string value = Format("{}", lineCount);
            if (auto sent = Send(UciProtocol::SetOptionCommand(kMultiPvOption, eastl::string_view(value.data(), value.size()))); !sent) {
                return sent;
            }
            _multiPv = lineCount;
        }
        if (auto sent = Send(UciProtocol::PositionCommand(request)); !sent) {
            return sent;
        }
        _previous = request;
        return Send(UciProtocol::GoCommand(request.limits));
    }

    std::expected<void, EngineError> UciEngine::Handshake(const CancellationToken& token) {
        if (auto sent = Send(kUciCommand); !sent) {
            return sent;
        }
        if (auto reply = ReadUntil(*_process, IsUciOk, kUciOkReply, _settings.startupTimeoutMs, token); !reply) {
            return std::unexpected(std::move(reply.error()));
        }
        for (const auto& [name, value] : _settings.options) {
            const eastl::string command = UciProtocol::SetOptionCommand(eastl::string_view(name.data(), name.size()), eastl::string_view(value.data(), value.size()));
            if (auto sent = Send(eastl::string_view(command.data(), command.size())); !sent) {
                return sent;
            }
        }
        return WaitUntilReady(token);
    }

    std::expected<void, EngineError> UciEngine::WaitUntilReady(const CancellationToken& token) {
        if (auto sent = Send(kIsReadyCommand); !sent) {
            return sent;
        }
        if (auto reply = ReadUntil(*_process, IsReadyOk, kReadyOkReply, _settings.startupTimeoutMs, token); !reply) {
            return std::unexpected(std::move(reply.error()));
        }
        return {};
    }

    std::expected<void, EngineError> UciEngine::Send(eastl::string_view command) {
        return _process->WriteLine(command);
    }

    std::unexpected<EngineError> UciEngine::StopSearch() {
        if (Send(kStopCommand)) {
            // The engine answers `stop` with the bestmove of the search so far: it is not wanted, but it has to be read so that the next search starts in step
            if (ReadUntil(*_process, IsBestMove, kBestMoveReply, _settings.stopGraceMs, CancellationToken())) {
                return Fail(EngineErrorKind::Cancelled, "the search was cancelled and the engine's answer was dropped");
            }
        }
        CloseProcess();
        return Fail(EngineErrorKind::Cancelled, "the search was cancelled and the engine did not answer 'stop' in time: it was closed");
    }

    bool UciEngine::IsNewGame(const SearchRequest& request) const {
        if (!_previous || _previous->startFen != request.startFen || request.moves.size() < _previous->moves.size()) {
            return true;
        }
        return !std::equal(_previous->moves.begin(), _previous->moves.end(), request.moves.begin());
    }

    std::unexpected<EngineError> UciEngine::CloseAndFail(EngineError error) {
        CloseProcess();
        return std::unexpected(std::move(error));
    }

    void UciEngine::CloseProcess() {
        if (!_process) {
            return;
        }
        if (_process->IsRunning()) {
            [[maybe_unused]] const auto said = _process->WriteLine(kQuitCommand);  // the polite request; the engine may be gone or deaf, Close() copes with both
        }
        _process->Close(milliseconds(std::max(_settings.quitGraceMs, 0)));
        _process.reset();
        _previous.reset();
        _multiPv = kSingleLine;
    }

    Optional<eastl::string> FindStockfish([[maybe_unused]] eastl::string_view explicitPath) {
#if CHESS_NO_STOCKFISH
        return std::nullopt;  // nothing is looked for: no file system walk, no environment, and no process could be started from what was found
#else
        if (!explicitPath.empty()) {
            return ExistingFile(std::string(explicitPath.data(), explicitPath.size()));
        }
        const std::string fromEnvironment = EnvironmentValue(kStockfishVariable);
        if (!fromEnvironment.empty()) {
            return ExistingFile(fromEnvironment);
        }
        return StockfishOnPath();
#endif
    }

}  // namespace BECore::Chess
