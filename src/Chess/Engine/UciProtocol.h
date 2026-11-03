#pragma once

#include <Chess/Engine/SearchRequest.h>

namespace BECore::Chess {

    /// The text of the UCI protocol (the commands we send, the one line we read). Pure: no process, no thread, nothing to log
    namespace UciProtocol {

        /// The answer of an engine to `go`: `bestmove e2e4`, `bestmove e2e4 ponder e7e5` (the ponder move is dropped)
        struct BestMove {
            /// The move as the engine spelled it ("e2e4", "e7e8q", "(none)", "0000"); empty when the line named none
            eastl::string move;

            /// The engine has no move to give: `(none)` (mate or stalemate), `0000`, or no token at all
            [[nodiscard]] bool IsNone() const noexcept;
        };

        /// `position fen <startFen>`, and ` moves m1 m2 ...` when the request has moves. The same text as `position_command` of tools/stockfish_bridge.py
        [[nodiscard]] eastl::string PositionCommand(const SearchRequest& request);

        /// `go movetime 100`, `go depth 8`, or both (`go depth 8 movetime 100`). The limits are expected to be valid (SearchRequest::Validate()):
        /// without any limit the text is a plain `go`, which an engine answers with a search that never ends
        [[nodiscard]] eastl::string GoCommand(const SearchLimits& limits);

        /// `setoption name Skill Level value 5`; a name may hold spaces, the value is taken as it is
        [[nodiscard]] eastl::string SetOptionCommand(eastl::string_view name, eastl::string_view value);

        /// The answer in @p line when it is a `bestmove` line (an end of line left on it does no harm), nothing for any other line (`info ...`, an empty one)
        [[nodiscard]] std::optional<BestMove> ParseBestMove(eastl::string_view line);

        /// @p text without the spaces, `\r` and `\n` at both ends: a child on Windows ends its lines with `\r\n`. A view into @p text
        [[nodiscard]] eastl::string_view TrimLine(eastl::string_view text);

    }  // namespace UciProtocol

}  // namespace BECore::Chess
