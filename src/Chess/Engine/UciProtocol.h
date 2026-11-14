#pragma once

#include <Chess/Engine/SearchRequest.h>

namespace BECore::Chess {

    /// The text of the UCI protocol (the commands we send, the lines we read: `bestmove` and `info`). Pure: no process, no thread, nothing to log
    namespace UciProtocol {

        /// The answer of an engine to `go`: `bestmove e2e4`, `bestmove e2e4 ponder e7e5` (the ponder move is dropped)
        struct BestMove {
            /// The move as the engine spelled it ("e2e4", "e7e8q", "(none)", "0000"); empty when the line named none
            eastl::string move;

            /// The engine has no move to give: `(none)` (mate or stalemate), `0000`, or no token at all
            [[nodiscard]] bool IsNone() const noexcept;
        };

        /// One `info` line of an engine, as far as an analysis needs it. Every field has a value when the line did not say: the line is read tolerantly
        struct Info {
            /// `depth`; 0 when absent or not a number
            int depth = 0;

            /// `multipv`; 1 when absent (a single-line search does not send it) or not a number
            int multiPv = 1;

            /// `score cp N` or `score mate N` (a negative number allowed), from the side to move. Nothing when the line has no score, or a truncated or non-numeric one,
            /// and for `score mate 0` (the position is already mated: no move, no distance, see AnalysisScore)
            Optional<AnalysisScore> score;

            /// `lowerbound` / `upperbound` after the score: the score is a bound, not the exact value
            bool lowerBound = false;
            bool upperBound = false;

            /// The first move after `pv` (the rest of the line is dropped); empty when there is none
            eastl::string firstPvMove;
        };

        /// `position fen <startFen>`, and ` moves m1 m2 ...` when the request has moves. The same text as `position_command` of tools/stockfish_bridge.py.
        /// The request is expected to have passed SearchRequest::Validate() and FinalPosition() (UciEngine does both first): the start FEN goes into the text as it is
        [[nodiscard]] eastl::string PositionCommand(const SearchRequest& request);

        /// `go movetime 100`, `go depth 8`, or both (`go depth 8 movetime 100`). The limits are expected to be valid (SearchRequest::Validate()):
        /// without any limit the text is a plain `go`, which an engine answers with a search that never ends
        [[nodiscard]] eastl::string GoCommand(const SearchLimits& limits);

        /// `setoption name Skill Level value 5`; a name may hold spaces, the value is taken as it is
        [[nodiscard]] eastl::string SetOptionCommand(eastl::string_view name, eastl::string_view value);

        /// The answer in @p line when it is a `bestmove` line (an end of line left on it does no harm), nothing for any other line (`info ...`, an empty one)
        [[nodiscard]] Optional<BestMove> ParseBestMove(eastl::string_view line);

        /// The fields of @p line when it is an `info` line, nothing for any other line (`bestmove ...`, an empty one). Tolerant: `info string ...` is text to the end of
        /// the line (a `score` inside it is not read), unknown tokens are skipped, and a missing or non-numeric value leaves its field as it was and never fails the line
        [[nodiscard]] Optional<Info> ParseInfo(eastl::string_view line);

        /// @p text without the spaces, `\r` and `\n` at both ends: a child on Windows ends its lines with `\r\n`. A view into @p text
        [[nodiscard]] eastl::string_view TrimLine(eastl::string_view text);

        /**
         * @brief Keeps, from the `info` lines an engine sends while it searches, the line of each `multipv` index 1..N that an analysis is read from. Pure: no process.
         *
         * Offer() every line that is not `bestmove` (any line, in the order it came: what is not an `info` line, has no score, is `info string` text or has an index above N is
         * ignored). Per index the latest exact score wins (the engine deepens: a later line is the deeper one); a bound (`lowerbound` / `upperbound`) is the answer for an index only
         * when no exact score came for it. An index that never had a usable line is missing from Lines(). A single-line search sends no `multipv` word, which ParseInfo reads as 1.
         */
        class LineCollector {
        public:
            /// @p lineCount is how many lines the search was asked for (the value of `MultiPV`), at least 1
            explicit LineCollector(int lineCount);

            void Offer(eastl::string_view line);

            /// The kept lines, in the order of their `multipv` index (Info::multiPv, from 1), the indices that had no usable line left out
            [[nodiscard]] eastl::vector<Info> Lines() const;

        private:
            eastl::vector<Optional<Info>> _exact;  ///< by index - 1
            eastl::vector<Optional<Info>> _bound;
        };

    }  // namespace UciProtocol

}  // namespace BECore::Chess
