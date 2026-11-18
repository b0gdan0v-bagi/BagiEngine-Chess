#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/MoveNotation.h>
#include <EASTL/algorithm.h>
#include <tests/ChessTestUtil.h>

/**
 * SanOf as a pure function: the notation of every kind of move, the disambiguation by the legal moves, the suffixes, and a property over the perft
 * positions. No engine services.
 */
namespace BECore::Chess::Tests {

    namespace {

        /// The SAN of the legal move @p uci in @p position; a spelling that is no legal move fails the test
        String San(const Position& position, StringView uci) {
            const Optional<Move> move = MoveGenerator::ParseUci(position, uci);
            if (!move) {
                ADD_FAILURE() << "Not a legal move: " << String(uci).c_str() << " in " << position.ToFen().c_str();
                return {};
            }
            return SanOf(position, *move);
        }

        String San(StringView fen, StringView uci) {
            return San(PositionFromFen(fen), uci);
        }

        /// The SANs of a game played from @p fen, joined by spaces
        String SanLine(StringView fen, Vector<StringView> moves) {
            Position position = PositionFromFen(fen);
            String line;
            for (const StringView uci : moves) {
                const Optional<Move> move = MoveGenerator::ParseUci(position, uci);
                if (!move) {
                    ADD_FAILURE() << "Not a legal move: " << String(uci).c_str() << " in " << position.ToFen().c_str();
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

        constexpr StringView kStartFen = Position::kStartFen;
        constexpr StringView kAllowedCharacters = "KQRBNabcdefgh12345678xO-=+#";

        /// The checks of the property for one position: every legal move has a SAN that is not empty, made of the allowed characters, different from the
        /// others of the position, and its suffix says what the position after the move says
        void ExpectSansAreSound(const Position& position) {
            const String fen = position.ToFen().c_str();
            Vector<String> seen;
            for (const Move move : MoveGenerator::GenerateLegal(position)) {
                const String san = SanOf(position, move);
                ASSERT_FALSE(san.empty()) << fen << " " << move.ToUci().c_str();
                for (const char c : san) {
                    EXPECT_NE(kAllowedCharacters.find(c), StringView::npos) << fen << " " << san.c_str();
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
            EXPECT_EQ(String(position.ToFen().c_str()), fen) << "SanOf must not change the position";
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
        const String fen = position.ToFen();
        const ZobristKey key = position.Key();
        for (const Move move : MoveGenerator::GenerateLegal(position)) {
            static_cast<void>(SanOf(position, move));
        }
        EXPECT_EQ(position.ToFen(), fen);
        EXPECT_EQ(position.Key(), key);
        EXPECT_FALSE(position.CanUndo());
    }

    // ---- The overload with the legal moves ----

    TEST(MoveNotationTest, TheOverloadWithTheLegalMovesGivesTheSameText) {
        for (const StringView fen : {kStartFen, kKiwipeteFen, kPosition3Fen, kPosition4Fen, kPosition5Fen, kPosition6Fen}) {
            SCOPED_TRACE(String(fen).c_str());
            const Position position = PositionFromFen(fen);
            const MoveList legal = MoveGenerator::GenerateLegal(position);
            for (const Move move : legal) {
                EXPECT_EQ(SanOf(position, move, legal), SanOf(position, move)) << move.ToUci().c_str();
            }
        }
    }

    // ---- Property over the perft positions ----

    TEST(MoveNotationTest, EveryMoveOfThePerftPositionsHasASoundSan) {
        for (const StringView fen : {kStartFen, kKiwipeteFen, kPosition3Fen, kPosition4Fen, kPosition5Fen, kPosition6Fen}) {
            SCOPED_TRACE(String(fen).c_str());
            ExpectSansAreSound(PositionFromFen(fen));
        }
    }

    TEST(MoveNotationTest, EveryReplyInThePerftPositionsHasASoundSan) {
        for (const StringView fen : {kStartFen, kKiwipeteFen, kPosition3Fen, kPosition4Fen, kPosition5Fen, kPosition6Fen}) {
            SCOPED_TRACE(String(fen).c_str());
            const Position position = PositionFromFen(fen);
            for (const Move move : MoveGenerator::GenerateLegal(position)) {
                Position after = position;
                after.DoMove(move);
                ExpectSansAreSound(after);
            }
        }
    }

    // ---- ParseSan ----

    namespace {

        /// The move @p san names in @p fen as UCI, or "-" when it names none
        String Parsed(StringView fen, StringView san) {
            const Optional<Move> move = ParseSan(PositionFromFen(fen), san);
            return move ? move->ToUci() : String("-");
        }

        constexpr StringView kPromotionFen = "8/4P1k1/8/8/8/8/8/4K3 w - - 0 1";
        constexpr StringView kCastlingFen = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";

    }  // namespace

    TEST(MoveNotationTest, ParseSanRoundTripsEveryMoveOfThePerftPositions) {
        for (const StringView fen : {kStartFen, kKiwipeteFen, kPosition3Fen, kPosition4Fen, kPosition5Fen, kPosition6Fen}) {
            SCOPED_TRACE(String(fen).c_str());
            const Position position = PositionFromFen(fen);
            const MoveList legal = MoveGenerator::GenerateLegal(position);
            for (const Move move : legal) {
                const String san = SanOf(position, move, legal);
                const Optional<Move> parsed = ParseSan(position, san);
                ASSERT_TRUE(parsed.has_value()) << san.c_str();
                EXPECT_EQ(*parsed, move) << san.c_str();
                EXPECT_EQ(ParseSan(position, san, legal), parsed) << san.c_str();
            }
        }
    }

    TEST(MoveNotationTest, ParseSanRoundTripsEveryReplyOfThePerftPositions) {
        for (const StringView fen : {kStartFen, kKiwipeteFen, kPosition3Fen, kPosition4Fen, kPosition5Fen, kPosition6Fen}) {
            SCOPED_TRACE(String(fen).c_str());
            const Position start = PositionFromFen(fen);
            for (const Move first : MoveGenerator::GenerateLegal(start)) {
                Position position = start;
                position.DoMove(first);
                for (const Move move : MoveGenerator::GenerateLegal(position)) {
                    const String san = SanOf(position, move);
                    EXPECT_EQ(ParseSan(position, san), move) << position.ToFen().c_str() << " " << san.c_str();
                }
            }
        }
    }

    TEST(MoveNotationTest, ParseSanReadsPlainMoves) {
        EXPECT_EQ(Parsed(kStartFen, "e4"), "e2e4");
        EXPECT_EQ(Parsed(kStartFen, "Nf3"), "g1f3");
        EXPECT_EQ(Parsed("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "exd5"), "e4d5");
        EXPECT_EQ(Parsed("4k3/8/8/8/7q/8/8/4K2Q w - - 0 1", "Qxh4"), "h1h4");
    }

    TEST(MoveNotationTest, ParseSanReadsEnPassant) {
        EXPECT_EQ(ParseSan(PositionFromFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2"), "exd6"), EnPassantMove("e5d6"));
    }

    TEST(MoveNotationTest, ParseSanReadsCastlingBothWays) {
        EXPECT_EQ(Parsed(kCastlingFen, "O-O"), "e1g1");
        EXPECT_EQ(Parsed(kCastlingFen, "O-O-O"), "e1c1");
        EXPECT_EQ(Parsed("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "O-O"), "e8g8");
        EXPECT_EQ(Parsed("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "O-O-O"), "e8c8");
        EXPECT_EQ(Parsed(kCastlingFen, "0-0"), "e1g1");
        EXPECT_EQ(Parsed(kCastlingFen, "0-0-0"), "e1c1");
        EXPECT_EQ(Parsed("5k2/8/8/8/8/8/8/R3K2R w KQ - 0 1", "O-O+"), "e1g1");
    }

    TEST(MoveNotationTest, ParseSanCastlingNeedsTheRightToIt) {
        EXPECT_EQ(Parsed("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1", "O-O"), "-");
        EXPECT_EQ(Parsed(kStartFen, "O-O"), "-");
    }

    TEST(MoveNotationTest, ParseSanKingStepIsNotCastling) {
        // "Kg1" must not take the castling move e1g1 for a king step
        EXPECT_EQ(Parsed(kCastlingFen, "Kg1"), "-");
        EXPECT_EQ(Parsed(kCastlingFen, "Kd1"), "e1d1");
    }

    TEST(MoveNotationTest, ParseSanReadsEachPromotionPiece) {
        EXPECT_EQ(Parsed(kPromotionFen, "e8=Q"), "e7e8q");
        EXPECT_EQ(Parsed(kPromotionFen, "e8=R"), "e7e8r");
        EXPECT_EQ(Parsed(kPromotionFen, "e8=B"), "e7e8b");
        EXPECT_EQ(Parsed(kPromotionFen, "e8=N+"), "e7e8n");
        EXPECT_EQ(Parsed("3r3k/4P1pp/8/8/8/8/8/4K3 w - - 0 1", "exd8=Q#"), "e7d8q");
        EXPECT_EQ(Parsed("1r5k/P5pp/8/8/8/8/8/4K3 w - - 0 1", "axb8=N+"), "a7b8n");
    }

    TEST(MoveNotationTest, ParseSanReadsDisambiguation) {
        const char* knights = "4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1";
        EXPECT_EQ(Parsed(knights, "Nbd2"), "b1d2");
        EXPECT_EQ(Parsed(knights, "Nfd2"), "f1d2");
        const char* rooks = "4k3/8/8/R7/8/8/8/R3K3 w - - 0 1";
        EXPECT_EQ(Parsed(rooks, "R1a3"), "a1a3");
        EXPECT_EQ(Parsed(rooks, "R5a3"), "a5a3");
        const char* queens = "8/k7/8/8/4Q2Q/8/K7/4r2Q w - - 0 1";
        EXPECT_EQ(Parsed(queens, "Qh4xe1"), "h4e1");
        EXPECT_EQ(Parsed(queens, "Q1xe1"), "h1e1");
        EXPECT_EQ(Parsed(queens, "Qexe1"), "e4e1");
    }

    TEST(MoveNotationTest, ParseSanIgnoresTrailingAnnotations) {
        EXPECT_EQ(Parsed(kStartFen, "e4!"), "e2e4");
        EXPECT_EQ(Parsed(kStartFen, "Nf3?!"), "g1f3");
        EXPECT_EQ(Parsed(kStartFen, "e4+"), "e2e4");
    }

    TEST(MoveNotationTest, ParseSanRejectsWhatIsNotOneLegalMove) {
        const char* knights = "4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1";
        EXPECT_EQ(Parsed(knights, "Nd2"), "-") << "ambiguous: both knights go there";
        EXPECT_EQ(Parsed(knights, "Nbd2"), "b1d2");
        EXPECT_EQ(Parsed(knights, "Ndd2"), "-") << "no knight on the d file";
        EXPECT_EQ(Parsed(knights, "N1d2"), "-") << "two knights on the first rank";
        EXPECT_EQ(Parsed(knights, "Nb3d2"), "-") << "no knight on b3";
        EXPECT_EQ(Parsed(knights, "Nb1d2"), "b1d2") << "the full square names the one";
    }

    TEST(MoveNotationTest, ParseSanRejectsAPinnedPiece) {
        // The knight on e2 is pinned by the rook on e8; it cannot go to c3, so only b1 can
        const char* fen = "4r2k/8/8/8/8/8/4N3/1N2K3 w - - 0 1";
        EXPECT_EQ(Parsed(fen, "Nc3"), "b1c3");
        EXPECT_EQ(Parsed(fen, "Nec3"), "-");
        EXPECT_EQ(Parsed(fen, "Nbc3"), "b1c3");
    }

    TEST(MoveNotationTest, ParseSanRejectsMovesOfAnotherPosition) {
        EXPECT_EQ(Parsed(kStartFen, "Nf6"), "-") << "Black's move";
        EXPECT_EQ(Parsed(kStartFen, "e5"), "-") << "no pawn reaches e5 in one move";
        EXPECT_EQ(Parsed(kStartFen, "Nxf3"), "g1f3") << "the capture mark is not checked against the board";
        EXPECT_EQ(Parsed(kStartFen, "d5"), "-");
        EXPECT_EQ(Parsed(kStartFen, "Ke2"), "-");
        EXPECT_EQ(Parsed(kStartFen, "exd5"), "-");
    }

    TEST(MoveNotationTest, ParseSanRejectsAPawnCaptureWithoutItsFile) {
        EXPECT_EQ(Parsed("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "xd5"), "-");
        EXPECT_EQ(Parsed("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "d5"), "-");
    }

    TEST(MoveNotationTest, ParseSanRejectsGarbageAndPromotionMismatches) {
        EXPECT_EQ(Parsed(kStartFen, ""), "-");
        EXPECT_EQ(Parsed(kStartFen, "+"), "-");
        EXPECT_EQ(Parsed(kStartFen, "e9"), "-");
        EXPECT_EQ(Parsed(kStartFen, "Nf3x"), "-");
        EXPECT_EQ(Parsed(kStartFen, "x"), "-");
        EXPECT_EQ(Parsed(kStartFen, "N"), "-");
        EXPECT_EQ(Parsed(kStartFen, "=Q"), "-");
        EXPECT_EQ(Parsed(kStartFen, "O-O-O-O"), "-");
        EXPECT_EQ(Parsed(kStartFen, "e2e4"), "-");
        EXPECT_EQ(Parsed(kStartFen, "e4=Q"), "-") << "a promotion letter on a move that does not promote";
        EXPECT_EQ(Parsed(kPromotionFen, "e8"), "-") << "a promotion without its piece";
        EXPECT_EQ(Parsed(kPromotionFen, "e8=K"), "-");
        EXPECT_EQ(Parsed(kPromotionFen, "e8=P"), "-");
        EXPECT_EQ(Parsed(kPromotionFen, "e8=q"), "-");
    }

}  // namespace BECore::Chess::Tests
