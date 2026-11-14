#include "UciProtocol.h"

namespace BECore::Chess {

    namespace UciProtocol {

        namespace {

            constexpr StringView kLineWhitespace = " \r\n";
            constexpr char kTokenSeparator = ' ';
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

            /// The text up to the next separator, cut off @p rest (which then starts at the separator or is empty)
            StringView TakeToken(StringView& rest) {
                while (!rest.empty() && rest.front() == kTokenSeparator) {
                    rest.remove_prefix(1);
                }
                size_t length = 0;
                while (length < rest.size() && rest[length] != kTokenSeparator) {
                    ++length;
                }
                const StringView token = rest.substr(0, length);
                rest.remove_prefix(length);
                return token;
            }

            /// Reads @p text as a whole decimal number (a leading '-' allowed); false for an empty token, any other character or a value out of int
            bool ParseInt(StringView text, int& out) {
                if (text.empty()) {
                    return false;
                }
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
                return error == std::errc{} && end == text.data() + text.size();
            }

            /// Reads the number that follows in @p rest and takes it off only when it is one, so a keyword that follows a missing value (`score cp lowerbound`) is still seen
            bool TakeInt(StringView& rest, int& out) {
                StringView after = rest;
                if (!ParseInt(TakeToken(after), out)) {
                    return false;
                }
                rest = after;
                return true;
            }

            /// `cp N` or `mate N` after `score`. Nothing for a kind we do not know, a missing or non-numeric value, and for a mate in 0
            Optional<AnalysisScore> TakeScore(StringView& rest) {
                const StringView kind = TakeToken(rest);
                int value = 0;
                if (kind == kCentipawnsToken && TakeInt(rest, value)) {
                    return AnalysisScore{AnalysisScoreKind::Centipawns, value};
                }
                if (kind == kMateToken && TakeInt(rest, value) && value != 0) {
                    return AnalysisScore{AnalysisScoreKind::Mate, value};
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
                    command.push_back(kTokenSeparator);
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
            StringView rest = TrimLine(line);
            if (TakeToken(rest) != kBestMoveToken) {
                return std::nullopt;
            }
            const StringView move = TakeToken(rest);
            return BestMove{String(move.data(), move.size())};
        }

        Optional<Info> ParseInfo(StringView line) {
            StringView rest = TrimLine(line);
            if (TakeToken(rest) != kInfoToken) {
                return std::nullopt;
            }
            Info info;
            for (StringView token = TakeToken(rest); !token.empty(); token = TakeToken(rest)) {
                if (token == kStringToken) {
                    break;  // the rest of the line is text for a person, whatever words are in it
                }
                if (token == kPvToken) {
                    const StringView move = TakeToken(rest);
                    info.firstPvMove = String(move.data(), move.size());
                    break;  // the other moves of the line are of no use here
                }
                if (token == kDepthToken) {
                    int depth = 0;
                    if (TakeInt(rest, depth) && depth > 0) {
                        info.depth = depth;
                    }
                } else if (token == kMultiPvToken) {
                    int multiPv = 0;
                    if (TakeInt(rest, multiPv) && multiPv > 0) {
                        info.multiPv = multiPv;
                    }
                } else if (token == kScoreToken) {
                    info.score = TakeScore(rest);
                } else if (token == kLowerBoundToken) {
                    info.lowerBound = true;
                } else if (token == kUpperBoundToken) {
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
