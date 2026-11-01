#include "ChessTestUtil.h"

#include <Chess/Game/MoveGenerator.h>

/**
 * Perft counts the leaf nodes of the legal move tree to a fixed depth. The numbers below are the published reference
 * values (https://www.chessprogramming.org/Perft_Results); one wrong castling, en passant, promotion or pin rule changes
 * them, so matching them is the proof that the generator and DoMove / UndoMove are right.
 *
 * The depths are what a Debug build walks in well under a second per position (each case runs in its own process under
 * ctest). Deeper walks, such as the 193 million leaves of Kiwipete at depth 5, are not worth the wait here.
 */
namespace BECore::Chess::Tests {

    namespace {

        uint64_t Perft(Position& position, int depth) {
            const MoveList moves = MoveGenerator::GenerateLegal(position);
            if (depth <= 1) {
                return moves.size();
            }
            uint64_t nodes = 0;
            for (const Move move : moves) {
                position.DoMove(move);
                nodes += Perft(position, depth - 1);
                position.UndoMove();
            }
            return nodes;
        }

        struct PerftCase {
            int depth;
            uint64_t nodes;
        };

        void ExpectPerft(eastl::string_view fen, std::initializer_list<PerftCase> cases) {
            Position position = PositionFromFen(fen);
            const eastl::string startFen = position.ToFen();
            const ZobristKey startKey = position.Key();
            for (const PerftCase& perftCase : cases) {
                EXPECT_EQ(Perft(position, perftCase.depth), perftCase.nodes) << "depth " << perftCase.depth << " of " << startFen.c_str();
            }
            // Every DoMove was taken back, so nothing may have drifted.
            EXPECT_EQ(position.ToFen(), startFen);
            EXPECT_EQ(position.Key(), startKey);
        }

    }  // namespace

    TEST(PerftTest, StartPosition) {
        ExpectPerft(Position::kStartFen, {{1, 20}, {2, 400}, {3, 8902}, {4, 197281}, {5, 4865609}});
    }

    TEST(PerftTest, Kiwipete) {
        // Castling both ways, pins, en passant and plenty of captures all at once.
        ExpectPerft(kKiwipeteFen, {{1, 48}, {2, 2039}, {3, 97862}, {4, 4085603}});
    }

    TEST(PerftTest, Position3EndgameWithEnPassantPins) {
        ExpectPerft(kPosition3Fen, {{1, 14}, {2, 191}, {3, 2812}, {4, 43238}, {5, 674624}});
    }

    TEST(PerftTest, Position4Promotions) {
        ExpectPerft(kPosition4Fen, {{1, 6}, {2, 264}, {3, 9467}, {4, 422333}});
    }

    TEST(PerftTest, Position4MirroredForBlack) {
        ExpectPerft("r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", {{1, 6}, {2, 264}, {3, 9467}, {4, 422333}});
    }

    TEST(PerftTest, Position5) {
        ExpectPerft(kPosition5Fen, {{1, 44}, {2, 1486}, {3, 62379}, {4, 2103487}});
    }

    TEST(PerftTest, Position6) {
        ExpectPerft(kPosition6Fen, {{1, 46}, {2, 2079}, {3, 89890}, {4, 3894594}});
    }

}  // namespace BECore::Chess::Tests
