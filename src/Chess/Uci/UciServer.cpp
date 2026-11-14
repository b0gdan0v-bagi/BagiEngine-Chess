#include "UciServer.h"

#include "UciClock.h"

#include <Chess/Engine/BlunderEngine.h>
#include <Chess/Engine/OpponentLevels.h>
#include <Chess/Engine/Search.h>
#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/Position.h>

namespace BECore::Chess::Uci {

    namespace {

        constexpr StringView kInfoStringPrefix = "info string ";
        constexpr StringView kBestMovePrefix = "bestmove ";
        constexpr StringView kNoMoveText = "(none)";
        constexpr StringView kBusyText = "a search is running: the command was ignored";

        /// The depth of the search that answers a `stop` when no depth has finished (and of the bounded `go` that was stopped)
        constexpr int kStopFallbackDepth = 1;

        char Lower(char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool EqualsIgnoreCase(StringView a, StringView b) {
            if (a.size() != b.size()) {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i) {
                if (Lower(a[i]) != Lower(b[i])) {
                    return false;
                }
            }
            return true;
        }

        String Lowered(StringView text) {
            String result;
            for (const char c : text) {
                result.push_back(Lower(c));
            }
            return result;
        }

        /// The game of a `position`: the start FEN and the moves as Move values. The text of the reason when the FEN or a move is refused
        std::expected<SearchRequest, String> BuildGame(const PositionParams& params) {
            const auto start = Position::FromFen(StringView(params.startFen));
            if (!start) {
                return std::unexpected(Format("position refused: '{}' is not a position this game can play ({})", params.startFen, EnumUtils<FenError>::ToString(start.error())));
            }
            SearchRequest game;
            game.startFen = params.startFen;
            Position position = *start;
            for (size_t index = 0; index < params.moves.size(); ++index) {
                const String& text = params.moves[index];
                const Optional<Move> move = MoveGenerator::ParseUci(position, StringView(text));
                if (!move) {
                    return std::unexpected(Format("position refused: move {} ({}) is not legal", index + 1, text));
                }
                position.DoMove(*move);
                game.moves.push_back(*move);
            }
            return game;
        }

        /// The first legal move of the game @p request leads to, for the `bestmove` after a failure: a GUI must get a move when there is one
        Optional<Move> FirstLegalMove(const SearchRequest& request) {
            const auto position = request.FinalPosition();
            if (!position) {
                return std::nullopt;
            }
            const MoveList legal = MoveGenerator::GenerateLegal(*position);
            if (legal.empty()) {
                return std::nullopt;
            }
            return legal.front();
        }

    }  // namespace

    UciServer::UciServer(SharedPtr<IChessEngine> engine, Sink sink) : _engine(std::move(engine)), _sink(std::move(sink)) {
        ASSERT(_engine != nullptr, "UciServer needs an engine");
        _game.startFen.assign(Position::kStartFen.data(), Position::kStartFen.size());
    }

    UciServer::~UciServer() {
        _token.Cancel();
        if (_worker.joinable()) {
            _worker.join();
        }
    }

    void UciServer::Say(StringView line) {
        const std::lock_guard lock(_sinkMutex);
        _sink(line);
    }

    void UciServer::SayInfo(StringView text) {
        Say(Format("{}{}", kInfoStringPrefix, text));
    }

    void UciServer::SayBestMove(const Move* move) {
        if (move == nullptr) {
            Say(Format("{}{}", kBestMovePrefix, kNoMoveText));
        } else {
            Say(Format("{}{}", kBestMovePrefix, move->ToUci()));
        }
    }

    void UciServer::WaitIdle() {
        if (_worker.joinable()) {
            _worker.join();
        }
    }

    void UciServer::StopSearch() {
        _token.Cancel();
    }

    void UciServer::HandleLine(StringView line) {
        const UciCommand command = ParseCommand(line);
        switch (command.kind) {
            case UciCommandKind::Uci:
                Say(Format("id name {}", kEngineName));
                Say(Format("id author {}", kEngineAuthor));
                Say(Format("option name {} type spin default 0 min 0 max {}", kDepthOption, kMaxDepthOption));
                Say(Format("option name {} type spin default 0 min 0 max {}", kBlunderPercentOption, kMaxBlunderPercentOption));
                {
                    String levels = Format("option name {} type combo default {} var {}", kLevelOption, kCustomLevel, kCustomLevel);
                    for (const OpponentLevel& level : GetOpponentLevels()) {
                        levels.append(" var ");
                        levels.append(level.id.data(), level.id.size());
                    }
                    Say(levels);
                }
                Say("uciok");
                break;
            case UciCommandKind::IsReady:
                Say("readyok");
                break;
            case UciCommandKind::NewGame:
                if (_searching.load()) {
                    SayInfo(kBusyText);
                } else {
                    _game.moves.clear();
                    _game.startFen.assign(Position::kStartFen.data(), Position::kStartFen.size());
                }
                break;
            case UciCommandKind::SetOption:
                HandleSetOption(command.setOption);
                break;
            case UciCommandKind::Position:
                HandlePosition(command.position);
                break;
            case UciCommandKind::Go:
                HandleGo(command.go);
                break;
            case UciCommandKind::Stop:
                // Synchronous like `quit`: a GUI may send `position` and `go` right after `stop`, before it has read the bestmove
                StopSearch();
                WaitIdle();
                break;
            case UciCommandKind::Quit:
                _quit.store(true);
                StopSearch();
                WaitIdle();
                break;
            case UciCommandKind::PonderHit:
            case UciCommandKind::Unknown:
                break;
        }
    }

    void UciServer::HandleSetOption(const SetOptionParams& params) {
        if (_searching.load()) {
            SayInfo(kBusyText);
            return;
        }
        const StringView name(params.name.data(), params.name.size());
        const StringView value(params.value.data(), params.value.size());
        if (EqualsIgnoreCase(name, kLevelOption)) {
            if (EqualsIgnoreCase(value, kCustomLevel)) {
                return;
            }
            const Optional<size_t> found = FindOpponentLevel(Lowered(value));
            if (!found) {
                SayInfo(Format("option refused: '{}' is not a level", value));
                return;
            }
            const OpponentLevel& level = GetOpponentLevels()[*found];
            _depth = level.ownDepth;
            _blunderPercent = level.blunderPercent;
            return;
        }
        const bool isDepth = EqualsIgnoreCase(name, kDepthOption);
        if (!isDepth && !EqualsIgnoreCase(name, kBlunderPercentOption)) {
            SayInfo(Format("option refused: '{}' is not an option of this engine", name));
            return;
        }
        const int top = isDepth ? kMaxDepthOption : kMaxBlunderPercentOption;
        const Optional<int> number = ParseInt(value);
        if (!number || *number < 0 || *number > top) {
            SayInfo(Format("option refused: '{}' is not a number from 0 to {}", value, top));
            return;
        }
        if (isDepth) {
            _depth = *number;
        } else {
            _blunderPercent = *number;
        }
    }

    void UciServer::HandlePosition(const PositionParams& params) {
        if (_searching.load()) {
            SayInfo(kBusyText);
            return;
        }
        auto game = BuildGame(params);
        if (!game) {
            SayInfo(game.error());
            return;
        }
        _game = std::move(*game);
    }

    SearchLimits UciServer::LimitsOf(const GoParams& params, bool whiteToMove) const {
        SearchLimits limits;
        limits.depth = _depth;
        if (params.depth && *params.depth > 0) {
            limits.depth = limits.depth > 0 ? std::min(limits.depth, *params.depth) : *params.depth;
        }
        if (params.moveTime && *params.moveTime > 0) {
            limits.moveTimeMs = *params.moveTime;
        } else if (const Optional<int>& clock = whiteToMove ? params.wTime : params.bTime; clock) {
            const Optional<int>& increment = whiteToMove ? params.wInc : params.bInc;
            limits.moveTimeMs = MoveTimeFor(*clock, increment.value_or(0), params.movesToGo);
        }
        return limits;
    }

    void UciServer::HandleGo(const GoParams& params) {
        if (_searching.load()) {
            SayInfo(kBusyText);
            return;
        }
        WaitIdle();

        SearchJob job;
        job.request = _game;
        job.engine = MakeShared<BlunderEngine>(_engine, _blunderPercent);
        job.token = CancellationToken::Create();

        const auto position = _game.FinalPosition();
        const bool whiteToMove = !position || position->SideToMove() == PieceColor::White;
        job.request.limits = LimitsOf(params, whiteToMove);
        job.deepen = params.infinite || (job.request.limits.depth == 0 && job.request.limits.moveTimeMs == 0);

        _token = job.token;
        _searching.store(true);
        _worker = std::thread([this, job = std::move(job)]() { RunSearch(job); });
    }

    void UciServer::RunSearch(const SearchJob& job) {
        Optional<Move> best;
        Optional<EngineError> failure;

        if (job.deepen) {
            // The answer of a search that nobody stops is the move of the deepest finished iteration
            SearchRequest request = job.request;
            for (int depth = 1; depth <= kMaxSearchDepth; ++depth) {
                request.limits = SearchLimits{0, depth};
                auto analysed = job.engine->Analyse(request, job.token);
                if (!analysed) {
                    failure = std::move(analysed.error());
                    break;
                }
                best = analysed->Best().move;
                if (analysed->Best().score.kind == AnalysisScoreKind::Mate) {
                    break;
                }
            }
        } else {
            auto found = job.engine->FindMove(job.request, job.token);
            if (found) {
                best = *found;
            } else {
                failure = std::move(found.error());
            }
        }

        if (failure && failure->kind == EngineErrorKind::Cancelled) {
            failure.reset();
            if (!best) {
                SearchRequest request = job.request;
                request.limits = SearchLimits{0, kStopFallbackDepth};
                auto found = job.engine->FindMove(request, CancellationToken::Create());
                if (found) {
                    best = *found;
                } else {
                    failure = std::move(found.error());
                }
            }
        }

        if (!best && failure && failure->kind != EngineErrorKind::NoMove) {
            SayInfo(failure->detail);
            best = FirstLegalMove(job.request);
        }
        // Idle again before the line is out: the next command of a GUI that has read it finds a server that is free
        _searching.store(false);
        SayBestMove(best ? &*best : nullptr);
    }

}  // namespace BECore::Chess::Uci
