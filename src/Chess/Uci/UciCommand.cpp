#include "UciCommand.h"

#include <Chess/Engine/UciProtocol.h>
#include <Chess/Game/Position.h>

namespace BECore::Chess::Uci {

    namespace {

        constexpr eastl::string_view kLineSeparators = " \t";
        constexpr eastl::string_view kUciToken = "uci";
        constexpr eastl::string_view kIsReadyToken = "isready";
        constexpr eastl::string_view kNewGameToken = "ucinewgame";
        constexpr eastl::string_view kSetOptionToken = "setoption";
        constexpr eastl::string_view kPositionToken = "position";
        constexpr eastl::string_view kGoToken = "go";
        constexpr eastl::string_view kStopToken = "stop";
        constexpr eastl::string_view kPonderHitToken = "ponderhit";
        constexpr eastl::string_view kQuitToken = "quit";
        constexpr eastl::string_view kNameToken = "name";
        constexpr eastl::string_view kValueToken = "value";
        constexpr eastl::string_view kStartPosToken = "startpos";
        constexpr eastl::string_view kFenToken = "fen";
        constexpr eastl::string_view kMovesToken = "moves";
        constexpr eastl::string_view kDepthToken = "depth";
        constexpr eastl::string_view kMoveTimeToken = "movetime";
        constexpr eastl::string_view kWTimeToken = "wtime";
        constexpr eastl::string_view kBTimeToken = "btime";
        constexpr eastl::string_view kWIncToken = "winc";
        constexpr eastl::string_view kBIncToken = "binc";
        constexpr eastl::string_view kMovesToGoToken = "movestogo";
        constexpr eastl::string_view kInfiniteToken = "infinite";
        constexpr eastl::string_view kPonderToken = "ponder";

        using Tokens = eastl::vector<eastl::string_view>;

        /// The words of @p line, views into it
        Tokens Split(eastl::string_view line) {
            Tokens tokens;
            size_t start = 0;
            while (start < line.size()) {
                const size_t begin = line.find_first_not_of(kLineSeparators, start);
                if (begin == eastl::string_view::npos) {
                    break;
                }
                size_t end = line.find_first_of(kLineSeparators, begin);
                if (end == eastl::string_view::npos) {
                    end = line.size();
                }
                tokens.push_back(line.substr(begin, end - begin));
                start = end;
            }
            return tokens;
        }

        eastl::string ToString(eastl::string_view text) {
            return eastl::string(text.data(), text.size());
        }

        /// The field a `go` keyword fills, nothing for a word that is not one
        std::optional<int>* GoField(GoParams& params, eastl::string_view token) {
            if (token == kDepthToken) {
                return &params.depth;
            }
            if (token == kMoveTimeToken) {
                return &params.moveTime;
            }
            if (token == kWTimeToken) {
                return &params.wTime;
            }
            if (token == kBTimeToken) {
                return &params.bTime;
            }
            if (token == kWIncToken) {
                return &params.wInc;
            }
            if (token == kBIncToken) {
                return &params.bInc;
            }
            if (token == kMovesToGoToken) {
                return &params.movesToGo;
            }
            return nullptr;
        }

        /// The tokens after `go`. A number is taken only when it follows its keyword, so a keyword after a missing value (`go depth wtime 5`) is still seen;
        /// the words of a keyword we do not know (`nodes 1000`, `searchmoves e2e4`) are skipped one by one
        GoParams ParseGo(const Tokens& tokens) {
            GoParams params;
            for (size_t i = 1; i < tokens.size(); ++i) {
                const eastl::string_view token = tokens[i];
                if (token == kInfiniteToken || token == kPonderToken) {
                    params.infinite = true;
                } else if (std::optional<int>* field = GoField(params, token)) {
                    if (i + 1 < tokens.size()) {
                        if (const std::optional<int> value = ParseInt(tokens[i + 1])) {
                            *field = value;
                            ++i;
                        }
                    }
                }
            }
            return params;
        }

        /// `startpos` or `fen <fields>`, then optionally `moves ...`. False when the first word is neither
        bool ParsePosition(const Tokens& tokens, PositionParams& params) {
            if (tokens.size() < 2) {
                return false;
            }
            size_t next = 2;
            if (tokens[1] == kStartPosToken) {
                params.startFen = ToString(Position::kStartFen);
            } else if (tokens[1] == kFenToken) {
                for (; next < tokens.size() && tokens[next] != kMovesToken; ++next) {
                    if (!params.startFen.empty()) {
                        params.startFen.push_back(' ');
                    }
                    params.startFen.append(tokens[next].data(), tokens[next].size());
                }
            } else {
                return false;
            }
            if (next < tokens.size() && tokens[next] == kMovesToken) {
                for (++next; next < tokens.size(); ++next) {
                    params.moves.push_back(ToString(tokens[next]));
                }
            }
            return true;
        }

        /// `name <words> value <words>`: the words between the two keywords are the name, the rest of the line is the value
        SetOptionParams ParseSetOption(const Tokens& tokens) {
            SetOptionParams params;
            size_t i = 1;
            if (i < tokens.size() && tokens[i] == kNameToken) {
                for (++i; i < tokens.size() && tokens[i] != kValueToken; ++i) {
                    if (!params.name.empty()) {
                        params.name.push_back(' ');
                    }
                    params.name.append(tokens[i].data(), tokens[i].size());
                }
            }
            if (i < tokens.size() && tokens[i] == kValueToken) {
                for (++i; i < tokens.size(); ++i) {
                    if (!params.value.empty()) {
                        params.value.push_back(' ');
                    }
                    params.value.append(tokens[i].data(), tokens[i].size());
                }
            }
            return params;
        }

    }  // namespace

    std::optional<int> ParseInt(eastl::string_view text) {
        int value = 0;
        if (text.empty()) {
            return std::nullopt;
        }
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size()) {
            return std::nullopt;
        }
        return value;
    }

    UciCommand ParseCommand(eastl::string_view line) {
        UciCommand command;
        const Tokens tokens = Split(UciProtocol::TrimLine(line));
        if (tokens.empty()) {
            return command;
        }
        const eastl::string_view first = tokens.front();
        if (first == kUciToken) {
            command.kind = UciCommandKind::Uci;
        } else if (first == kIsReadyToken) {
            command.kind = UciCommandKind::IsReady;
        } else if (first == kNewGameToken) {
            command.kind = UciCommandKind::NewGame;
        } else if (first == kSetOptionToken) {
            command.kind = UciCommandKind::SetOption;
            command.setOption = ParseSetOption(tokens);
        } else if (first == kPositionToken) {
            if (ParsePosition(tokens, command.position)) {
                command.kind = UciCommandKind::Position;
            }
        } else if (first == kGoToken) {
            command.kind = UciCommandKind::Go;
            command.go = ParseGo(tokens);
        } else if (first == kStopToken) {
            command.kind = UciCommandKind::Stop;
        } else if (first == kPonderHitToken) {
            command.kind = UciCommandKind::PonderHit;
        } else if (first == kQuitToken) {
            command.kind = UciCommandKind::Quit;
        }
        return command;
    }

}  // namespace BECore::Chess::Uci
