#include "ChessFuzzTargets.h"

#include <cstdio>
#include <cstdlib>

namespace BECore::Chess::Fuzzing {

    Span<const ChessFuzzTarget> ChessFuzzTargetList() {
        static const ChessFuzzTarget targets[] = {
            {"Fen", &FuzzFen, &FenIsAccepted, "fen", 1},
            {"UciMove", &FuzzUciMove, &UciMoveIsAccepted, "uci_move", 2},
        };
        return targets;
    }

    void ChessFuzzCheckFailed(const char* condition, const char* file, int line) {
        std::fprintf(stderr, "CHESS_FUZZ_CHECK failed: %s (%s:%d)\n", condition, file, line);
        std::fflush(stderr);
        std::abort();
    }

}  // namespace BECore::Chess::Fuzzing
