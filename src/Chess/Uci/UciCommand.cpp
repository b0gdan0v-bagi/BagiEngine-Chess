#include "UciCommand.h"

#include <BECore/File/TextReader.h>
#include <Chess/Engine/UciProtocol.h>
#include <Chess/Game/Position.h>

namespace BECore::Chess::Uci {

    namespace {

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

        /// The tokens after `go`. A number is taken only when it follows its keyword (`Read<int>` leaves a token that is none), so a keyword after a missing value
        /// (`go depth wtime 5`) is still seen; the words of a keyword we do not know (`nodes 1000`, `searchmoves e2e4`) are skipped one by one
        GoParams ParseGo(TextReader& reader) {
            GoParams params;
            while (const Optional<StringView> token = reader.ReadToken()) {
                if (*token == kInfiniteToken || *token == kPonderToken) {
                    params.infinite = true;
                } else if (Optional<int>* field = GoField(params, *token)) {
                    if (const Optional<int> value = reader.Read<int>()) {
                        *field = value;
                    }
                }
            }
            return params;
        }

        /// Appends @p word to @p text after one space, so the words of a field come back with single spaces
        void AppendWord(String& text, StringView word) {
            if (!text.empty()) {
                text.push_back(' ');
            }
            text.append(word.data(), word.size());
        }

        /// `startpos` or `fen <fields>`, then optionally `moves ...`. False when the first word is neither
        bool ParsePosition(TextReader& reader, PositionParams& params) {
            const Optional<StringView> source = reader.ReadToken();
            if (!source) {
                return false;
            }
            Optional<StringView> next;
            if (*source == kStartPosToken) {
                params.startFen = String(Position::kStartFen);
                next = reader.ReadToken();
            } else if (*source == kFenToken) {
                for (next = reader.ReadToken(); next && *next != kMovesToken; next = reader.ReadToken()) {
                    AppendWord(params.startFen, *next);
                }
            } else {
                return false;
            }
            if (next == kMovesToken) {
                while (const Optional<StringView> move = reader.ReadToken()) {
                    params.moves.push_back(String(*move));
                }
            }
            return true;
        }

        /// `name <words> value <words>`: the words between the two keywords are the name, the rest of the line is the value
        SetOptionParams ParseSetOption(TextReader& reader) {
            SetOptionParams params;
            Optional<StringView> token = reader.ReadToken();
            if (token == kNameToken) {
                for (token = reader.ReadToken(); token && *token != kValueToken; token = reader.ReadToken()) {
                    AppendWord(params.name, *token);
                }
            }
            if (token == kValueToken) {
                while (const Optional<StringView> word = reader.ReadToken()) {
                    AppendWord(params.value, *word);
                }
            }
            return params;
        }

    }  // namespace

    Optional<int> ParseInt(StringView text) {
        return StringUtils::ParseNumber<int>(text);
    }

    UciCommand ParseCommand(StringView line) {
        UciCommand command;
        TextReader reader(UciProtocol::TrimLine(line));
        const Optional<StringView> token = reader.ReadToken();
        if (!token) {
            return command;
        }
        const StringView first = *token;
        if (first == kUciToken) {
            command.kind = UciCommandKind::Uci;
        } else if (first == kIsReadyToken) {
            command.kind = UciCommandKind::IsReady;
        } else if (first == kNewGameToken) {
            command.kind = UciCommandKind::NewGame;
        } else if (first == kSetOptionToken) {
            command.kind = UciCommandKind::SetOption;
            command.setOption = ParseSetOption(reader);
        } else if (first == kPositionToken) {
            if (ParsePosition(reader, command.position)) {
                command.kind = UciCommandKind::Position;
            }
        } else if (first == kGoToken) {
            command.kind = UciCommandKind::Go;
            command.go = ParseGo(reader);
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
