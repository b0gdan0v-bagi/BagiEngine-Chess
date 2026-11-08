#include "ChessTestUtil.h"

#include <Chess/Uci/UciClock.h>

/// The time of one move from the clock of a `go`: pure integer arithmetic
namespace BECore::Chess::Tests {

    using Uci::MoveTimeFor;

    TEST(UciClockTest, SuddenDeathSpendsAShareOfTheClock) {
        EXPECT_EQ(MoveTimeFor(60000, 0, std::nullopt), 60000 / Uci::kDefaultMovesToGo);
    }

    TEST(UciClockTest, AnIncrementAddsMostOfItself) {
        EXPECT_EQ(MoveTimeFor(60000, 1000, std::nullopt), 60000 / Uci::kDefaultMovesToGo + 750);
    }

    TEST(UciClockTest, MovesToGoDividesTheClock) {
        EXPECT_EQ(MoveTimeFor(60000, 0, 10), 6000);
        EXPECT_EQ(MoveTimeFor(60000, 2000, 20), 3000 + 1500);
    }

    TEST(UciClockTest, TheLastMoveOfAControlNeverTakesMoreThanHalfTheClock) {
        EXPECT_EQ(MoveTimeFor(10000, 0, 1), 10000 / Uci::kMaxClockShareDivisor);
        EXPECT_EQ(MoveTimeFor(10000, 5000, 1), 10000 / Uci::kMaxClockShareDivisor);
        EXPECT_EQ(MoveTimeFor(2000, 0, 1), 1000);
    }

    TEST(UciClockTest, ASmallClockKeepsTheMarginWhenItIsLargerThanTheHalf) {
        constexpr int kSmall = Uci::kClockMarginMs + Uci::kClockMarginMs / 2;
        ASSERT_LT(kSmall - Uci::kClockMarginMs, kSmall / Uci::kMaxClockShareDivisor);
        EXPECT_EQ(MoveTimeFor(kSmall, 0, 1), kSmall - Uci::kClockMarginMs);
    }

    TEST(UciClockTest, AHugeIncrementNeverTakesMoreThanHalfTheClock) {
        EXPECT_EQ(MoveTimeFor(1000, 1000000, std::nullopt), 1000 / Uci::kMaxClockShareDivisor);
        EXPECT_EQ(MoveTimeFor(1000, std::numeric_limits<int>::max(), std::nullopt), 1000 / Uci::kMaxClockShareDivisor);
    }

    TEST(UciClockTest, AClockThatHasRunOutStillGivesOneMillisecond) {
        EXPECT_EQ(MoveTimeFor(0, 0, std::nullopt), Uci::kMinMoveTimeMs);
        EXPECT_EQ(MoveTimeFor(-5, 1000, std::nullopt), Uci::kMinMoveTimeMs);
        EXPECT_EQ(MoveTimeFor(20, 0, 1), Uci::kMinMoveTimeMs);
    }

    TEST(UciClockTest, ABadMovesToGoIsTakenAsAbsent) {
        EXPECT_EQ(MoveTimeFor(60000, 0, 0), MoveTimeFor(60000, 0, std::nullopt));
        EXPECT_EQ(MoveTimeFor(60000, 0, -3), MoveTimeFor(60000, 0, std::nullopt));
    }

    TEST(UciClockTest, ANegativeIncrementIsTakenAsNone) {
        EXPECT_EQ(MoveTimeFor(60000, -1000, std::nullopt), MoveTimeFor(60000, 0, std::nullopt));
    }

    TEST(UciClockTest, AHugeClockDoesNotOverflow) {
        constexpr int kHuge = std::numeric_limits<int>::max();
        EXPECT_EQ(MoveTimeFor(kHuge, 0, std::nullopt), kHuge / Uci::kDefaultMovesToGo);
        EXPECT_EQ(MoveTimeFor(kHuge, 0, 1), kHuge / Uci::kMaxClockShareDivisor);
        EXPECT_GT(MoveTimeFor(kHuge, kHuge, 1), 0);
    }

    TEST(UciClockTest, MoreTimeNeverGivesASmallerAnswer) {
        for (const int increment : {0, 100, 5000}) {
            for (const std::optional<int> movesToGo : {std::optional<int>{}, std::optional<int>{1}, std::optional<int>{7}, std::optional<int>{40}}) {
                int previous = 0;
                for (int remaining = -10; remaining <= 200000; remaining += 37) {
                    const int time = MoveTimeFor(remaining, increment, movesToGo);
                    EXPECT_GE(time, previous) << remaining << " ms, increment " << increment;
                    previous = time;
                }
            }
        }
    }

}  // namespace BECore::Chess::Tests
