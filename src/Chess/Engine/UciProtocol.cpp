#include "UciProtocol.h"

#include <BECore/File/TextReader.h>

namespace BECore::Chess {

    namespace UciProtocol {

        namespace {

            constexpr StringView kLineWhitespace = " \r\n";
            constexpr StringView kBestMoveToken = "bestmove";
            constexpr StringView kNoMoveText = "(none)";
            constexpr StringView kNullMoveText = "0000";
            constexpr StringView kInfoToken = "info";
            constexpr StringView kStringToken = "string";
            constexpr StringView kDepthToken = "depth";
            constexpr StringView kMultiPvToken = "multipv";
            constexpr StringView kScoreToken = "score";
            constexpr StringView kCentipawnsToken = "cp";
            constexpr StringView kMateToken = "mate";
            constexpr StringView kLowerBoundToken = "lowerbound";
            constexpr StringView kUpperBoundToken = "upperbound";
            constexpr StringView kPvToken = "pv";

            /// `cp N` or `mate N` after `score`. Nothing for a kind we do not know, a missing or non-numeric value (which stays unread, so `score cp lowerbound` still sees `lowerbound`), and for a
            /// mate in 0
            Optional<AnalysisScore> TakeScore(TextReader& reader) {
                const Optional<StringView> kind = reader.ReadToken();
                if (!kind) {
                    return std::nullopt;
                }
                if (*kind == kCentipawnsToken) {
                    if (const Optional<int> value = reader.Read<int>()) {
                        return AnalysisScore{AnalysisScoreKind::Centipawns, *value};
                    }
                } else if (*kind == kMateToken) {
                    if (const Optional<int> value = reader.Read<int>(); value && *value != 0) {
                        return AnalysisScore{AnalysisScoreKind::Mate, *value};
                    }
                }
                return std::nullopt;
            }

        }  // namespace

        bool BestMove::IsNone() const noexcept {
            const StringView text(move.data(), move.size());
            return text.empty() || text == kNoMoveText || text == kNullMoveText;
        }

        String PositionCommand(const SearchRequest& request) {
            String command = Format("position fen {}", request.startFen);
            if (!request.moves.empty()) {
                command.append(" moves");
                for (const Move& move : request.moves) {
                    command.push_back(' ');
                    command.append(move.ToUci());
                }
            }
            return command;
        }

        String GoCommand(const SearchLimits& limits) {
            String command = "go";
            if (limits.depth > 0) {
                command.append(Format(" depth {}", limits.depth));
            }
            if (limits.moveTimeMs > 0) {
                command.append(Format(" movetime {}", limits.moveTimeMs));
            }
            return command;
        }

        String SetOptionCommand(StringView name, StringView value) {
            return Format("setoption name {} value {}", name, value);
        }

        Optional<BestMove> ParseBestMove(StringView line) {
            TextReader reader(TrimLine(line));
            if (reader.ReadToken() != kBestMoveToken) {
                return std::nullopt;
            }
            const StringView move = reader.ReadToken().value_or(StringView());
            return BestMove{String(move.data(), move.size())};
        }

        Optional<Info> ParseInfo(StringView line) {
            TextReader reader(TrimLine(line));
            if (reader.ReadToken() != kInfoToken) {
                return std::nullopt;
            }
            Info info;
            while (const Optional<StringView> token = reader.ReadToken()) {
                if (*token == kStringToken) {
                    break;  // the rest of the line is text for a person, whatever words are in it
                }
                if (*token == kPvToken) {
                    const StringView move = reader.ReadToken().value_or(StringView());
                    info.firstPvMove = String(move.data(), move.size());
                    break;  // the other moves of the line are of no use here
                }
                if (*token == kDepthToken) {
                    if (const Optional<int> depth = reader.Read<int>(); depth && *depth > 0) {
                        info.depth = *depth;
                    }
                } else if (*token == kMultiPvToken) {
                    if (const Optional<int> multiPv = reader.Read<int>(); multiPv && *multiPv > 0) {
                        info.multiPv = *multiPv;
                    }
                } else if (*token == kScoreToken) {
                    info.score = TakeScore(reader);
                } else if (*token == kLowerBoundToken) {
                    info.lowerBound = true;
                } else if (*token == kUpperBoundToken) {
                    info.upperBound = true;
                }
            }
            return info;
        }

        StringView TrimLine(StringView text) {
            const size_t first = text.find_first_not_of(kLineWhitespace);
            if (first == StringView::npos) {
                return {};
            }
            const size_t last = text.find_last_not_of(kLineWhitespace);
            return text.substr(first, last - first + 1);
        }

        LineCollector::LineCollector(int lineCount) : _exact(static_cast<size_t>(std::max(lineCount, 1))), _bound(static_cast<size_t>(std::max(lineCount, 1))) {}

        void LineCollector::Offer(StringView line) {
            const Optional<Info> info = ParseInfo(line);
            if (!info || !info->score || info->multiPv < 1 || static_cast<size_t>(info->multiPv) > _exact.size()) {
                return;
            }
            const size_t slot = static_cast<size_t>(info->multiPv) - 1;
            if (info->lowerBound || info->upperBound) {
                _bound[slot] = info;
            } else {
                _exact[slot] = info;
            }
        }

        Vector<Info> LineCollector::Lines() const {
            Vector<Info> lines;
            for (size_t slot = 0; slot < _exact.size(); ++slot) {
                if (const Optional<Info>& kept = _exact[slot] ? _exact[slot] : _bound[slot]) {
                    lines.push_back(*kept);
                }
            }
            return lines;
        }

    }  // namespace UciProtocol

}  // namespace BECore::Chess
