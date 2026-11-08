#include "ChessTestUtil.h"

#include <Chess/Engine/TranspositionTable.h>

/// The transposition table: a pure container over a key, nothing to start
namespace BECore::Chess::Tests {

    namespace {

        // Keys that differ only above the low bits share a slot in a table of four
        constexpr ZobristKey kKeyA = 0x1111'0000'0000'0001ULL;
        constexpr ZobristKey kKeyB = 0x2222'0000'0000'0001ULL;
        constexpr ZobristKey kKeyC = 0x3333'0000'0000'0002ULL;

        constexpr Move kMoveE2E4{Square::FromIndex(12), Square::FromIndex(28)};
        constexpr Move kMoveG1F3{Square::FromIndex(6), Square::FromIndex(21)};

    }  // namespace

    TEST(TranspositionTableTest, AnEmptyTableMissesEveryKey) {
        const TranspositionTable table(4);

        EXPECT_EQ(table.Probe(kKeyA), nullptr);
        EXPECT_EQ(table.Probe(kKeyC), nullptr);
        EXPECT_EQ(table.Probe(0), nullptr);
    }

    TEST(TranspositionTableTest, AStoredEntryComesBack) {
        TranspositionTable table(4);

        table.Store(kKeyA, kMoveE2E4, 35, 6, TableBound::Lower);

        const TableEntry* entry = table.Probe(kKeyA);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->key, kKeyA);
        EXPECT_EQ(entry->move, kMoveE2E4);
        EXPECT_EQ(entry->score, 35);
        EXPECT_EQ(entry->depth, 6);
        EXPECT_EQ(entry->bound, TableBound::Lower);
        EXPECT_EQ(table.Probe(kKeyC), nullptr);
    }

    TEST(TranspositionTableTest, AnEntryOfAnotherKeyInTheSameSlotIsAMiss) {
        TranspositionTable table(4);

        table.Store(kKeyA, kMoveE2E4, 10, 3, TableBound::Exact);

        EXPECT_EQ(table.Probe(kKeyB), nullptr);
        EXPECT_NE(table.Probe(kKeyA), nullptr);
    }

    TEST(TranspositionTableTest, AKeyOfZeroIsNotFoundInAnEmptySlot) {
        TranspositionTable table(4);

        EXPECT_EQ(table.Probe(0), nullptr);
        table.Store(0, kMoveE2E4, 1, 0, TableBound::Exact);
        EXPECT_NE(table.Probe(0), nullptr);
    }

    TEST(TranspositionTableTest, AShallowerEntryOfAnotherKeyDoesNotEvictADeeperOne) {
        TranspositionTable table(4);
        table.Store(kKeyA, kMoveE2E4, 10, 5, TableBound::Exact);

        table.Store(kKeyB, kMoveG1F3, 99, 4, TableBound::Exact);

        const TableEntry* entry = table.Probe(kKeyA);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->score, 10);
        EXPECT_EQ(entry->depth, 5);
        EXPECT_EQ(table.Probe(kKeyB), nullptr);
    }

    TEST(TranspositionTableTest, AnEqualOrDeeperEntryOfAnotherKeyEvicts) {
        TranspositionTable table(4);
        table.Store(kKeyA, kMoveE2E4, 10, 5, TableBound::Exact);

        table.Store(kKeyB, kMoveG1F3, 20, 5, TableBound::Upper);
        ASSERT_NE(table.Probe(kKeyB), nullptr);
        EXPECT_EQ(table.Probe(kKeyA), nullptr);
        EXPECT_EQ(table.Probe(kKeyB)->score, 20);

        table.Store(kKeyA, kMoveE2E4, 30, 9, TableBound::Lower);
        ASSERT_NE(table.Probe(kKeyA), nullptr);
        EXPECT_EQ(table.Probe(kKeyB), nullptr);
        EXPECT_EQ(table.Probe(kKeyA)->score, 30);
    }

    TEST(TranspositionTableTest, TheSameKeyAlwaysReplacesAndKeepsTheMoveWhenTheNewOneIsNull) {
        TranspositionTable table(4);
        table.Store(kKeyA, kMoveE2E4, 10, 8, TableBound::Exact);

        // Shallower, with no move: the score, depth and bound are replaced, the move stays
        table.Store(kKeyA, Move{}, -40, 2, TableBound::Upper);

        const TableEntry* entry = table.Probe(kKeyA);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(entry->move, kMoveE2E4);
        EXPECT_EQ(entry->score, -40);
        EXPECT_EQ(entry->depth, 2);
        EXPECT_EQ(entry->bound, TableBound::Upper);

        // A new move replaces the old one
        table.Store(kKeyA, kMoveG1F3, 5, 3, TableBound::Lower);
        EXPECT_EQ(table.Probe(kKeyA)->move, kMoveG1F3);
    }

    TEST(TranspositionTableTest, ASizeThatIsNotAPowerOfTwoIsRoundedUp) {
        EXPECT_EQ(TranspositionTable(0).Size(), 1U);
        EXPECT_EQ(TranspositionTable(1).Size(), 1U);
        EXPECT_EQ(TranspositionTable(2).Size(), 2U);
        EXPECT_EQ(TranspositionTable(3).Size(), 4U);
        EXPECT_EQ(TranspositionTable(1000).Size(), 1024U);
        EXPECT_EQ(TranspositionTable(1024).Size(), 1024U);
    }

    TEST(TranspositionTableTest, ATableOfOneSlotHoldsOneKey) {
        TranspositionTable table(1);

        table.Store(kKeyA, kMoveE2E4, 1, 1, TableBound::Exact);
        table.Store(kKeyC, kMoveG1F3, 2, 1, TableBound::Exact);

        EXPECT_EQ(table.Probe(kKeyA), nullptr);
        EXPECT_NE(table.Probe(kKeyC), nullptr);
    }

    TEST(TranspositionTableTest, ClearEmptiesTheTable) {
        TranspositionTable table(4);
        table.Store(kKeyA, kMoveE2E4, 10, 5, TableBound::Exact);
        table.Store(kKeyC, kMoveG1F3, 20, 5, TableBound::Exact);

        table.Clear();

        EXPECT_EQ(table.Size(), 4U);
        EXPECT_EQ(table.Probe(kKeyA), nullptr);
        EXPECT_EQ(table.Probe(kKeyC), nullptr);
        // An emptied slot takes a shallow entry again
        table.Store(kKeyB, kMoveG1F3, 1, 0, TableBound::Exact);
        EXPECT_NE(table.Probe(kKeyB), nullptr);
    }

    TEST(TranspositionTableTest, AnOrdinaryScoreIsNotChangedByThePly) {
        EXPECT_EQ(ScoreToTable(123, 7), 123);
        EXPECT_EQ(ScoreFromTable(123, 7), 123);
        EXPECT_EQ(ScoreToTable(-480, 3), -480);
        EXPECT_EQ(ScoreFromTable(-480, 9), -480);
        EXPECT_EQ(ScoreToTable(0, 5), 0);
    }

    TEST(TranspositionTableTest, AMateScoreRoundTripsAtTheSamePly) {
        for (int ply : {0, 1, 3, 10, 60}) {
            const int mateForMover = MateIn(ply + 4);
            const int mateAgainst = -MateIn(ply + 5);
            EXPECT_EQ(ScoreFromTable(ScoreToTable(mateForMover, ply), ply), mateForMover) << ply;
            EXPECT_EQ(ScoreFromTable(ScoreToTable(mateAgainst, ply), ply), mateAgainst) << ply;
        }
    }

    TEST(TranspositionTableTest, AMateIsCountedFromTheNodeNotFromTheRoot) {
        // Mate for the mover found at ply 3, five plies below the node (so MateIn(8) from the root): the table keeps "5 plies"
        EXPECT_EQ(ScoreToTable(MateIn(8), 3), MateIn(5));
        EXPECT_EQ(ScoreToTable(-MateIn(8), 3), -MateIn(5));
    }

    TEST(TranspositionTableTest, AMateStoredAtPly3AndReadAtPly5IsTwoPliesFurtherFromTheRoot) {
        const int forMover = ScoreFromTable(ScoreToTable(MateIn(8), 3), 5);
        const int against = ScoreFromTable(ScoreToTable(-MateIn(8), 3), 5);

        EXPECT_EQ(forMover, MateIn(10));
        EXPECT_EQ(against, -MateIn(10));
        // Read nearer the root, it is nearer
        EXPECT_EQ(ScoreFromTable(ScoreToTable(MateIn(8), 3), 1), MateIn(6));
        EXPECT_EQ(ScoreFromTable(ScoreToTable(-MateIn(8), 3), 1), -MateIn(6));
    }

    TEST(TranspositionTableTest, TheExtremeScoresFitSixteenBits) {
        for (int score : {kMateScore, -kMateScore}) {
            const int stored = ScoreToTable(score, kMaxMatePly);
            EXPECT_GE(stored, INT16_MIN);
            EXPECT_LE(stored, INT16_MAX);
        }
        // The shortest mate at the deepest node and the mate score nearest to an evaluation
        EXPECT_LE(ScoreToTable(MateIn(0), 64), INT16_MAX);
        EXPECT_GE(ScoreToTable(-MateIn(0), 64), INT16_MIN);

        TranspositionTable table(2);
        table.Store(kKeyA, kMoveE2E4, ScoreToTable(kMateScore, 64), 3, TableBound::Exact);
        table.Store(kKeyC, kMoveE2E4, ScoreToTable(-kMateScore, 64), 3, TableBound::Exact);
        EXPECT_EQ(ScoreFromTable(table.Probe(kKeyA)->score, 64), kMateScore);
        EXPECT_EQ(ScoreFromTable(table.Probe(kKeyC)->score, 64), -kMateScore);
    }

    TEST(TranspositionTableTest, AnEntryIsSixteenBytes) {
        EXPECT_EQ(sizeof(TableEntry), 16U);
    }

}  // namespace BECore::Chess::Tests
