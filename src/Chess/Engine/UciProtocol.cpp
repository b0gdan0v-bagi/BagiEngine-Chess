#include "UciProtocol.h"

namespace BECore::Chess {

    namespace UciProtocol {

        namespace {

            constexpr eastl::string_view kLineWhitespace = " \r\n";
            constexpr char kTokenSeparator = ' ';
            constexpr eastl::string_view kBestMoveToken = "bestmove";
            constexpr eastl::string_view kNoMoveText = "(none)";
            constexpr eastl::string_view kNullMoveText = "0000";

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

        eastl::string_view TrimLine(eastl::string_view text) {
            const size_t first = text.find_first_not_of(kLineWhitespace);
            if (first == eastl::string_view::npos) {
                return {};
            }
            const size_t last = text.find_last_not_of(kLineWhitespace);
            return text.substr(first, last - first + 1);
        }

    }  // namespace UciProtocol

}  // namespace BECore::Chess
