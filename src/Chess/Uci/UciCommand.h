#pragma once

namespace BECore::Chess::Uci {

    /// What a line from the GUI asks. Unknown is every line that names no command we answer, an empty line and a `position` we cannot read: UCI says to skip them
    CORE_ENUM(UciCommandKind, uint8_t, Uci, IsReady, NewGame, SetOption, Position, Go, Stop, PonderHit, Quit, Unknown)

    /// The limits of a `go`. A number that is absent or not a number leaves its field empty; a negative number is kept as given (the server decides what it means)
    struct GoParams {
        std::optional<int> depth;
        std::optional<int> moveTime;
        std::optional<int> wTime;
        std::optional<int> bTime;
        std::optional<int> wInc;
        std::optional<int> bInc;
        std::optional<int> movesToGo;

        /// `infinite` or `ponder` (a ponder search is run as an infinite one)
        bool infinite = false;
    };

    /// The game of a `position` command: the start FEN and the moves as text. The parser does not know the rules: the position checks their legality later
    struct PositionParams {
        /// Position::kStartFen for `startpos`; for `fen` the fields up to `moves`, as they came (fewer than six fields too)
        eastl::string startFen;

        eastl::vector<eastl::string> moves;
    };

    /// `setoption name <name> value <value>`; the name may hold spaces, the value is the rest of the line
    struct SetOptionParams {
        eastl::string name;

        /// Empty when the line has no `value`
        eastl::string value;
    };

    /// A parsed line: the kind, and the struct of that kind (the others stay empty)
    struct UciCommand {
        UciCommandKind kind = UciCommandKind::Unknown;
        GoParams go;
        PositionParams position;
        SetOptionParams setOption;
    };

    /// The command in @p line. Tolerant: spaces at both ends and a `\r` are ignored, unknown tokens inside `go` are skipped, a value that is not a number leaves
    /// its field empty. A first token that is no command, an empty line, and a `position` with neither `startpos` nor `fen` are Unknown
    [[nodiscard]] UciCommand ParseCommand(eastl::string_view line);

}  // namespace BECore::Chess::Uci
