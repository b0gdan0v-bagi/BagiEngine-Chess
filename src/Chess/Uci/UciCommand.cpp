#include "UciCommand.h"

#include <Chess/Engine/UciProtocol.h>
#include <Chess/Game/Position.h>

namespace BECore::Chess::Uci {

    namespace {

        constexpr StringView kLineSeparators = " \t";
        constexpr StringView kUciToken = "uci";
        constexpr StringView kIsReadyToken = "isready";
        constexpr StringView kNewGameToken = "ucinewgame";
        constexpr StringView kSetOptionToken = "setoption";
        constexpr StringView kPositionToken = "position";
        constexpr StringView kGoToken = "go";
        constexpr StringView kStopToken = "stop";
        constexpr StringView kPonderHitToken = "ponderhit";
        constexpr StringView kQuitToken = "quit";
        constexpr StringView kNameToken = "name";
        constexpr StringView kValueToken = "value";
        constexpr StringView kStartPosToken = "startpos";
        constexpr StringView kFenToken = "fen";
        constexpr StringView kMovesToken = "moves";
        constexpr StringView kDepthToken = "depth";
        constexpr StringView kMoveTimeToken = "movetime";
        constexpr StringView kWTimeToken = "wtime";
        constexpr StringView kBTimeToken = "btime";
        constexpr StringView kWIncToken = "winc";
        constexpr StringView kBIncToken = "binc";
        constexpr StringView kMovesToGoToken = "movestogo";
        constexpr StringView kInfiniteToken = "infinite";
        constexpr StringView kPonderToken = "ponder";

        using Tokens = eastl::vector<StringView>;

        /// The words of @p line, views into it
        Tokens Split(StringView line) {
            Tokens tokens;
            size_t start = 0;
            while (start < line.size()) {
                const size_t begin = line.find_first_not_of(kLineSeparators, start);
                if (begin == StringView::npos) {
                    break;
                }
                size_t end = line.find_first_of(kLineSeparators, begin);
                if (end == StringView::npos) {
                    end = line.size();
                }
                tokens.push_back(line.substr(begin, end - begin));
                start = end;
            }
            return tokens;
        }

        eastl::string ToString(StringView text) {
            return eastl::string(text.data(), text.size());
        }

        /// The field a `go` keyword fills, nothing for a word that is not one
        Optional<int>* GoField(GoParams& params, StringView token) {
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
                const StringView token = tokens[i];
                if (token == kInfiniteToken || token == kPonderToken) {
                    params.infinite = true;
                } else if (Optional<int>* field = GoField(params, token)) {
                    if (i + 1 < tokens.size()) {
                        if (const Optional<int> value = ParseInt(tokens[i + 1])) {
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

    Optional<int> ParseInt(StringView text) {
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

    UciCommand ParseCommand(StringView line) {
        UciCommand command;
        const Tokens tokens = Split(UciProtocol::TrimLine(line));
        if (tokens.empty()) {
            return command;
        }
        const StringView first = tokens.front();
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
