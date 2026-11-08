#include "UciProtocol.h"

namespace BECore::Chess {

    namespace UciProtocol {

        namespace {

            constexpr eastl::string_view kLineWhitespace = " \r\n";
            constexpr char kTokenSeparator = ' ';
            constexpr eastl::string_view kBestMoveToken = "bestmove";
            constexpr eastl::string_view kNoMoveText = "(none)";
            constexpr eastl::string_view kNullMoveText = "0000";
            constexpr eastl::string_view kInfoToken = "info";
            constexpr eastl::string_view kStringToken = "string";
            constexpr eastl::string_view kDepthToken = "depth";
            constexpr eastl::string_view kMultiPvToken = "multipv";
            constexpr eastl::string_view kScoreToken = "score";
            constexpr eastl::string_view kCentipawnsToken = "cp";
            constexpr eastl::string_view kMateToken = "mate";
            constexpr eastl::string_view kLowerBoundToken = "lowerbound";
            constexpr eastl::string_view kUpperBoundToken = "upperbound";
            constexpr eastl::string_view kPvToken = "pv";

            /// The text up to the next separator, cut off @p rest (which then starts at the separator or is empty)
            eastl::string_view TakeToken(eastl::string_view& rest) {
                while (!rest.empty() && rest.front() == kTokenSeparator) {
                    rest.remove_prefix(1);
                }
                size_t length = 0;
                while (length < rest.size() && rest[length] != kTokenSeparator) {
                    ++length;
                }
                const eastl::string_view token = rest.substr(0, length);
                rest.remove_prefix(length);
                return token;
            }

            /// Reads @p text as a whole decimal number (a leading '-' allowed); false for an empty token, any other character or a value out of int
            bool ParseInt(eastl::string_view text, int& out) {
                if (text.empty()) {
                    return false;
                }
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
                return error == std::errc{} && end == text.data() + text.size();
            }

            /// Reads the number that follows in @p rest and takes it off only when it is one, so a keyword that follows a missing value (`score cp lowerbound`) is still seen
            bool TakeInt(eastl::string_view& rest, int& out) {
                eastl::string_view after = rest;
                if (!ParseInt(TakeToken(after), out)) {
                    return false;
                }
                rest = after;
                return true;
            }

            /// `cp N` or `mate N` after `score`. Nothing for a kind we do not know, a missing or non-numeric value, and for a mate in 0
            std::optional<AnalysisScore> TakeScore(eastl::string_view& rest) {
                const eastl::string_view kind = TakeToken(rest);
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
            const eastl::string_view text(move.data(), move.size());
            return text.empty() || text == kNoMoveText || text == kNullMoveText;
        }

        eastl::string PositionCommand(const SearchRequest& request) {
            eastl::string command = Format("position fen {}", request.startFen);
            if (!request.moves.empty()) {
                command.append(" moves");
                for (const Move& move : request.moves) {
                    command.push_back(kTokenSeparator);
                    command.append(move.ToUci());
                }
            }
            return command;
        }

        eastl::string GoCommand(const SearchLimits& limits) {
            eastl::string command = "go";
            if (limits.depth > 0) {
                command.append(Format(" depth {}", limits.depth));
            }
            if (limits.moveTimeMs > 0) {
                command.append(Format(" movetime {}", limits.moveTimeMs));
            }
            return command;
        }

        eastl::string SetOptionCommand(eastl::string_view name, eastl::string_view value) {
            return Format("setoption name {} value {}", name, value);
        }

        std::optional<BestMove> ParseBestMove(eastl::string_view line) {
            eastl::string_view rest = TrimLine(line);
            if (TakeToken(rest) != kBestMoveToken) {
                return std::nullopt;
            }
            const eastl::string_view move = TakeToken(rest);
            return BestMove{eastl::string(move.data(), move.size())};
        }

        std::optional<Info> ParseInfo(eastl::string_view line) {
            eastl::string_view rest = TrimLine(line);
            if (TakeToken(rest) != kInfoToken) {
                return std::nullopt;
            }
            Info info;
            for (eastl::string_view token = TakeToken(rest); !token.empty(); token = TakeToken(rest)) {
                if (token == kStringToken) {
                    break;  // the rest of the line is text for a person, whatever words are in it
                }
                if (token == kPvToken) {
                    const eastl::string_view move = TakeToken(rest);
                    info.firstPvMove = eastl::string(move.data(), move.size());
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

        eastl::string_view TrimLine(eastl::string_view text) {
            const size_t first = text.find_first_not_of(kLineWhitespace);
            if (first == eastl::string_view::npos) {
                return {};
            }
            const size_t last = text.find_last_not_of(kLineWhitespace);
            return text.substr(first, last - first + 1);
        }

        LineCollector::LineCollector(int lineCount) : _exact(static_cast<size_t>(std::max(lineCount, 1))), _bound(static_cast<size_t>(std::max(lineCount, 1))) {}

        void LineCollector::Offer(eastl::string_view line) {
            const std::optional<Info> info = ParseInfo(line);
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

        eastl::vector<Info> LineCollector::Lines() const {
            eastl::vector<Info> lines;
            for (size_t slot = 0; slot < _exact.size(); ++slot) {
                if (const std::optional<Info>& kept = _exact[slot] ? _exact[slot] : _bound[slot]) {
                    lines.push_back(*kept);
                }
            }
            return lines;
        }

    }  // namespace UciProtocol

}  // namespace BECore::Chess
