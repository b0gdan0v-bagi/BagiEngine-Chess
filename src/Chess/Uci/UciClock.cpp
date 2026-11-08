#include "UciClock.h"

namespace BECore::Chess::Uci {

    int MoveTimeFor(int remainingMs, int incrementMs, std::optional<int> movesToGo) {
        if (remainingMs <= 0) {
            return kMinMoveTimeMs;
        }
        const int divisor = movesToGo.has_value() && *movesToGo > 0 ? *movesToGo : kDefaultMovesToGo;
        // 64 bits: a GUI may send an increment of any size
        const int64_t increment = incrementMs > 0 ? incrementMs : 0;
        const int64_t wanted = remainingMs / divisor + increment * kIncrementShareNumerator / kIncrementShareDenominator;
        const int64_t marginCap = static_cast<int64_t>(remainingMs) - kClockMarginMs;
        const int64_t shareCap = remainingMs / kMaxClockShareDivisor;
        const int64_t cap = marginCap < shareCap ? marginCap : shareCap;
        const int64_t time = wanted < cap ? wanted : cap;
        return static_cast<int>(time < kMinMoveTimeMs ? kMinMoveTimeMs : time);
    }

}  // namespace BECore::Chess::Uci
