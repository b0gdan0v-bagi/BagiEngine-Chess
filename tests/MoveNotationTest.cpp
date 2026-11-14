#include "ChessTestUtil.h"

#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/MoveNotation.h>
#include <EASTL/algorithm.h>

/**
 * SanOf as a pure function: the notation of every kind of move, the disambiguation by the legal moves, the suffixes, and a property over the perft
 * positions. No engine services.
 */
namespace BECore::Chess::Tests {

    namespace {

        /// The SAN of the legal move @p uci in @p position; a spelling that is no legal move fails the test
        eastl::string San(const Position& position, eastl::string_view uci) {
            const Optional<Move> move = MoveGenerator::ParseUci(position, uci);
            if (!move) {
                ADD_FAILURE() << "Not a legal move: " << eastl::string(uci).c_str() << " in " << position.ToFen().c_str();
                return {};
            }
            return SanOf(position, *move);
        }

        eastl::string San(eastl::string_view fen, eastl::string_view uci) {
            return San(PositionFromFen(fen), uci);
        }

        /// The SANs of a game played from @p fen, joined by spaces
        eastl::string SanLine(eastl::string_view fen, eastl::vector<eastl::string_view> moves) {
            Position position = PositionFromFen(fen);
            eastl::string line;
            for (const eastl::string_view uci : moves) {
                const Optional<Move> move = MoveGenerator::ParseUci(position, uci);
                if (!move) {
                    ADD_FAILURE() << "Not a legal move: " << eastl::string(uci).c_str() << " in " << position.ToFen().c_str();
                    break;
                }
                if (!line.empty()) {
                    line.push_back(' ');
                }
                line += SanOf(position, *move);
                position.DoMove(*move);
            }
            return line;
        }

        constexpr eastl::string_view kStartFen = Position::kStartFen;
        constexpr eastl::string_view kAllowedCharacters = "KQRBNabcdefgh12345678xO-=+#";

        /// The checks of the property for one position: every legal move has a SAN that is not empty, made of the allowed characters, different from the
        /// others of the position, and its suffix says what the position after the move says
        void ExpectSansAreSound(const Position& position) {
            const std::string fen = position.ToFen().c_str();
            eastl::vector<eastl::string> seen;
            for (const Move move : MoveGenerator::GenerateLegal(position)) {
                const eastl::string san = SanOf(position, move);
                ASSERT_FALSE(san.empty()) << fen << " " << move.ToUci().c_str();
                for (const char c : san) {
                    EXPECT_NE(kAllowedCharacters.find(c), eastl::string_view::npos) << fen << " " << san.c_str();
                }
                EXPECT_EQ(eastl::find(seen.begin(), seen.end(), san), seen.end()) << fen << " twice " << san.c_str();
                seen.push_back(san);

                Position after = position;
                after.DoMove(move);
                const bool mate = after.IsInCheck() && MoveGenerator::GenerateLegal(after).empty();
                const char last = san.back();
                EXPECT_EQ(last == '+' || last == '#', after.IsInCheck()) << fen << " " << san.c_str();
                EXPECT_EQ(last == '#', mate) << fen << " " << san.c_str();
            }
            EXPECT_EQ(std::string(position.ToFen().c_str()), fen) << "SanOf must not change the position";
        }

    }  // namespace

    // ---- Quiet moves and captures ----

    TEST(MoveNotationTest, QuietMovesOfTheStartPosition) {
        EXPECT_EQ(San(kStartFen, "e2e4"), "e4");
        EXPECT_EQ(San(kStartFen, "e2e3"), "e3");
        EXPECT_EQ(San(kStartFen, "g1f3"), "Nf3");
        EXPECT_EQ(San(kStartFen, "b1c3"), "Nc3");
    }

    TEST(MoveNotationTest, BlackKnightAfterE4) {
        EXPECT_EQ(San("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", "g8f6"), "Nf6");
    }

    TEST(MoveNotationTest, KingMove) {
        EXPECT_EQ(San("4k3/8/8/8/8/8/3P4/3K4 w - - 0 1", "d1c1"), "Kc1");
    }

    TEST(MoveNotationTest, PawnCaptureNamesItsFile) {
        EXPECT_EQ(San("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "e4d5"), "exd5");
    }

    TEST(MoveNotationTest, PieceCaptureHasX) {
        EXPECT_EQ(San("rnbqkbnr/pppp1ppp/8/4p3/8/5N2/PPPPPPPP/RNBQKB1R w KQkq - 0 2", "f3e5"), "Nxe5");
        EXPECT_EQ(San("4k3/8/8/8/7q/8/8/4K2Q w - - 0 1", "h1h4"), "Qxh4");
    }

    TEST(MoveNotationTest, EnPassantIsAPawnCapture) {
        const Position position = PositionFromFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2");
        EXPECT_EQ(SanOf(position, EnPassantMove("e5d6")), "exd6");
    }

    // ---- Castling ----

    TEST(MoveNotationTest, CastlingOfBothColors) {
        EXPECT_EQ(San("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1"), "O-O");
        EXPECT_EQ(San("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1c1"), "O-O-O");
        EXPECT_EQ(San("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8g8"), "O-O");
        EXPECT_EQ(San("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8c8"), "O-O-O");
    }

    TEST(MoveNotationTest, CastlingThatGivesCheck) {
        // The rook lands on the file of the enemy king
        EXPECT_EQ(San("5k2/8/8/8/8/8/8/R3K2R w KQ - 0 1", "e1g1"), "O-O+");
        EXPECT_EQ(San("5k2/8/8/8/8/8/8/R3K2R w KQ - 0 1", "e1c1"), "O-O-O");
        EXPECT_EQ(San("3k4/8/8/8/8/8/8/R3K2R w KQ - 0 1", "e1c1"), "O-O-O+");
        EXPECT_EQ(San("r3k2r/8/8/8/8/8/8/5K2 b kq - 0 1", "e8g8"), "O-O+");
        EXPECT_EQ(San("r3k2r/8/8/8/8/8/8/3K4 b kq - 0 1", "e8c8"), "O-O-O+");
        EXPECT_EQ(San("r3k2r/8/8/8/8/8/8/3K4 b kq - 0 1", "e8g8"), "O-O");
    }

    // ---- Promotion ----

    TEST(MoveNotationTest, PromotionNamesThePiece) {
        const char* fen = "8/4P1k1/8/8/8/8/8/4K3 w - - 0 1";
        EXPECT_EQ(San(fen, "e7e8q"), "e8=Q");
        EXPECT_EQ(San(fen, "e7e8r"), "e8=R");
        EXPECT_EQ(San(fen, "e7e8b"), "e8=B");
        EXPECT_EQ(San(fen, "e7e8n"), "e8=N+") << "the knight on e8 attacks g7";
    }

    TEST(MoveNotationTest, CapturePromotionWithMate) {
        EXPECT_EQ(San("3r3k/4P1pp/8/8/8/8/8/4K3 w - - 0 1", "e7d8q"), "exd8=Q#");
        EXPECT_EQ(San("3r3k/4P1pp/8/8/8/8/8/4K3 w - - 0 1", "e7d8n"), "exd8=N");
    }

    TEST(MoveNotationTest, BlackPromotion) {
        EXPECT_EQ(San("4k3/8/8/8/8/7K/p7/8 b - - 0 1", "a2a1q"), "a1=Q");
    }

    // ---- Check and mate ----

    TEST(MoveNotationTest, DiscoveredCheck) {
        EXPECT_EQ(San("4k3/8/8/8/4B3/8/8/4RK2 w - - 0 1", "e4d5"), "Bd5+");
    }

    TEST(MoveNotationTest, FoolsMate) {
        EXPECT_EQ(SanLine(kStartFen, {"f2f3", "e7e5", "g2g4", "d8h4"}), "f3 e5 g4 Qh4#");
    }

    TEST(MoveNotationTest, ScholarsMate) {
        EXPECT_EQ(SanLine(kStartFen, {"e2e4", "e7e5", "f1c4", "b8c6", "d1h5", "g8f6", "h5f7"}), "e4 e5 Bc4 Nc6 Qh5 Nf6 Qxf7#");
    }

    TEST(MoveNotationTest, StalemateHasNoSuffix) {
        const Position position = PositionFromFen("7k/8/6K1/8/8/8/8/5Q2 w - - 0 1");
        const Optional<Move> move = MoveGenerator::ParseUci(position, "f1f7");
        ASSERT_TRUE(move.has_value());
        Position after = position;
        after.DoMove(*move);
        ASSERT_FALSE(after.IsInCheck());
        ASSERT_TRUE(MoveGenerator::GenerateLegal(after).empty()) << "the position must be a stalemate";
        EXPECT_EQ(SanOf(position, *move), "Qf7");
    }

    // ---- Disambiguation ----

    TEST(MoveNotationTest, TwoKnightsAreToldApartByFile) {
        const char* fen = "4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1";
        EXPECT_EQ(San(fen, "b1d2"), "Nbd2");
        EXPECT_EQ(San(fen, "f1d2"), "Nfd2");
    }

    TEST(MoveNotationTest, PiecesToDifferentTargetsNeedNoDisambiguation) {
        const char* fen = "4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1";
        EXPECT_EQ(San(fen, "b1a3"), "Na3");
        EXPECT_EQ(San(fen, "f1e3"), "Ne3");
        EXPECT_EQ(San(kStartFen, "g1f3"), "Nf3") << "the other knight cannot go there";
    }

    TEST(MoveNotationTest, TwoRooksOnOneFileAreToldApartByRank) {
        const char* fen = "4k3/8/8/R7/8/8/8/R3K3 w - - 0 1";
        EXPECT_EQ(San(fen, "a1a3"), "R1a3");
        EXPECT_EQ(San(fen, "a5a3"), "R5a3");
    }

    TEST(MoveNotationTest, ThreeQueensUseFileRankOrBoth) {
        const char* fen = "8/k7/8/8/4Q2Q/8/K7/4r2Q w - - 0 1";
        EXPECT_EQ(San(fen, "h4e1"), "Qh4xe1") << "shares the file with h1 and the rank with e4";
        EXPECT_EQ(San(fen, "h1e1"), "Q1xe1") << "shares the file with h4 only";
        EXPECT_EQ(San(fen, "e4e1"), "Qexe1") << "shares the rank with h4 only";
    }

    TEST(MoveNotationTest, ThePinnedKnightDoesNotCount) {
        // The knight on e2 is pinned by the rook on e8, so the knight on b1 is the only one that can go to c3
        EXPECT_EQ(San("4r2k/8/8/8/8/8/4N3/1N2K3 w - - 0 1", "b1c3"), "Nc3");
    }

    TEST(MoveNotationTest, TheSamePositionWithoutThePinNeedsTheFile) {
        const char* fen = "7k/8/8/8/8/8/4N3/1N2K3 w - - 0 1";
        EXPECT_EQ(San(fen, "b1c3"), "Nbc3");
        EXPECT_EQ(San(fen, "e2c3"), "Nec3");
    }

    // ---- The position is left alone ----

    TEST(MoveNotationTest, TheInputPositionIsUnchanged) {
        const Position position = PositionFromFen(kKiwipeteFen);
        const eastl::string fen = position.ToFen();
        const ZobristKey key = position.Key();
        for (const Move move : MoveGenerator::GenerateLegal(position)) {
            (void)SanOf(position, move);
        }
        EXPECT_EQ(position.ToFen(), fen);
        EXPECT_EQ(position.Key(), key);
        EXPECT_FALSE(position.CanUndo());
    }

    // ---- Property over the perft positions ----

    TEST(MoveNotationTest, EveryMoveOfThePerftPositionsHasASoundSan) {
        for (const eastl::string_view fen : {kStartFen, kKiwipeteFen, kPosition3Fen, kPosition4Fen, kPosition5Fen, kPosition6Fen}) {
            SCOPED_TRACE(eastl::string(fen).c_str());
            ExpectSansAreSound(PositionFromFen(fen));
        }
    }

    TEST(MoveNotationTest, EveryReplyInThePerftPositionsHasASoundSan) {
        for (const eastl::string_view fen : {kStartFen, kKiwipeteFen, kPosition3Fen, kPosition4Fen, kPosition5Fen, kPosition6Fen}) {
            SCOPED_TRACE(eastl::string(fen).c_str());
            const Position position = PositionFromFen(fen);
            for (const Move move : MoveGenerator::GenerateLegal(position)) {
                Position after = position;
                after.DoMove(move);
                ExpectSansAreSound(after);
            }
        }
    }

}  // namespace BECore::Chess::Tests
