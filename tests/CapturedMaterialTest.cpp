#include "ChessTestUtil.h"

#include <Chess/Game/CapturedMaterial.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>
#include <Common/Printers.h>

/// The material of a position: which pieces are gone, the points on the board and the lead. Pure, nothing to start
namespace BECore::Chess::Tests {

    namespace {

        constexpr StringView kUnreachableNinePawnsFen = "4k3/8/8/8/8/P7/PPPPPPPP/4K3 w - - 0 1";
        constexpr StringView kUnreachableThreeQueensFen = "4k3/8/8/8/8/QQQ5/PPPPPPPP/4K3 w - - 0 1";

        std::string Text(StringView text) {
            return std::string(text.data(), text.size());
        }

        eastl::array<StringView, 6> PerftFens() {
            return {Position::kStartFen, kKiwipeteFen, kPosition3Fen, kPosition4Fen, kPosition5Fen, kPosition6Fen};
        }

        MaterialSummary SummaryOf(StringView fen) {
            return SummarizeMaterial(PositionFromFen(fen));
        }

        /// The move of the mirrored position: ranks flipped ("a7a8q" -> "a2a1q")
        std::string MirrorUci(StringView uci) {
            std::string mirrored(uci.data(), uci.size());
            mirrored[1] = static_cast<char>('1' + '8' - mirrored[1]);
            mirrored[3] = static_cast<char>('1' + '8' - mirrored[3]);
            return mirrored;
        }

        bool Play(ChessGameModel& model, StringView uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

        void PlayAll(ChessGameModel& model, std::initializer_list<const char*> moves) {
            for (const char* uci : moves) {
                ASSERT_TRUE(Play(model, uci)) << uci;
            }
        }

        /// Counts of one color in the order of CapturableTypes(): queen, rook, bishop, knight, pawn
        struct Gone {
            int queen;
            int rook;
            int bishop;
            int knight;
            int pawn;
        };

        void ExpectGone(const MaterialSummary& summary, PieceColor color, const Gone& expected, const char* what) {
            EXPECT_EQ(summary.GetMissing(color, PieceType::Queen), expected.queen) << what << " queen";
            EXPECT_EQ(summary.GetMissing(color, PieceType::Rook), expected.rook) << what << " rook";
            EXPECT_EQ(summary.GetMissing(color, PieceType::Bishop), expected.bishop) << what << " bishop";
            EXPECT_EQ(summary.GetMissing(color, PieceType::Knight), expected.knight) << what << " knight";
            EXPECT_EQ(summary.GetMissing(color, PieceType::Pawn), expected.pawn) << what << " pawn";
        }

    }  // namespace

    // ---- Values and ids ----

    TEST(CapturedMaterialTest, MaterialPointsArePinned) {
        EXPECT_EQ(MaterialPoints(PieceType::Pawn), 1);
        EXPECT_EQ(MaterialPoints(PieceType::Knight), 3);
        EXPECT_EQ(MaterialPoints(PieceType::Bishop), 3);
        EXPECT_EQ(MaterialPoints(PieceType::Rook), 5);
        EXPECT_EQ(MaterialPoints(PieceType::Queen), 9);
        EXPECT_EQ(MaterialPoints(PieceType::King), 0);
        EXPECT_EQ(MaterialPoints(PieceType::None), 0);
    }

    TEST(CapturedMaterialTest, TheCapturableTypesAreFiveQueenFirst) {
        const auto types = CapturableTypes();
        ASSERT_EQ(types.size(), 5u);
        EXPECT_TRUE(types[0] == PieceType::Queen);
        EXPECT_TRUE(types[1] == PieceType::Rook);
        EXPECT_TRUE(types[2] == PieceType::Bishop);
        EXPECT_TRUE(types[3] == PieceType::Knight);
        EXPECT_TRUE(types[4] == PieceType::Pawn);
    }

    TEST(CapturedMaterialTest, IdsArePinnedAndRoundTrip) {
        EXPECT_EQ(Text(CapturedId(PieceType::Queen)), "queen");
        EXPECT_EQ(Text(CapturedId(PieceType::Rook)), "rook");
        EXPECT_EQ(Text(CapturedId(PieceType::Bishop)), "bishop");
        EXPECT_EQ(Text(CapturedId(PieceType::Knight)), "knight");
        EXPECT_EQ(Text(CapturedId(PieceType::Pawn)), "pawn");

        for (const PieceType type : CapturableTypes()) {
            const StringView id = CapturedId(type);
            const auto found = FindCaptured(id);
            ASSERT_TRUE(found.has_value()) << Text(id);
            EXPECT_TRUE(*found == type);
            for (const char c : id) {
                EXPECT_TRUE(c >= 'a' && c <= 'z') << "ids are lower case: " << Text(id);
            }
        }
    }

    TEST(CapturedMaterialTest, IdsAreUnique) {
        for (const PieceType a : CapturableTypes()) {
            for (const PieceType b : CapturableTypes()) {
                if (!(a == b)) {
                    EXPECT_NE(Text(CapturedId(a)), Text(CapturedId(b)));
                }
            }
        }
    }

    TEST(CapturedMaterialTest, MatchingIsExactAndKingAndNoneHaveNoId) {
        for (const char* wrong : {"", "Queen", "QUEEN", "quee", "queen ", " queen", "pawns", "king", "none", "Pawn"}) {
            EXPECT_FALSE(FindCaptured(wrong).has_value()) << "'" << wrong << "'";
        }
        EXPECT_TRUE(CapturedId(PieceType::King).empty());
        EXPECT_TRUE(CapturedId(PieceType::None).empty());
    }

    // ---- BalanceText ----

    TEST(CapturedMaterialTest, BalanceTextIsPlusNOrNothing) {
        EXPECT_EQ(Text(BalanceText(1).c_str()), "+1");
        EXPECT_EQ(Text(BalanceText(9).c_str()), "+9");
        EXPECT_EQ(Text(BalanceText(39).c_str()), "+39");
        EXPECT_EQ(Text(BalanceText(99).c_str()), "+99");
        EXPECT_EQ(Text(BalanceText(100).c_str()), "+99");
        EXPECT_EQ(Text(BalanceText(100000).c_str()), "+99");
        EXPECT_TRUE(BalanceText(0).empty());
        EXPECT_TRUE(BalanceText(-1).empty());
        EXPECT_TRUE(BalanceText(-50).empty());
    }

    // ---- Summaries ----

    TEST(CapturedMaterialTest, TheStartPositionHasNothingGoneAndNoLead) {
        const MaterialSummary summary = SummarizeMaterial(Position::Start());
        for (const PieceColor color : {PieceColor::White, PieceColor::Black}) {
            ExpectGone(summary, color, {0, 0, 0, 0, 0}, "start");
            EXPECT_EQ(summary.GetPoints(color), 39);
        }
        EXPECT_EQ(summary.GetBalance(), 0);
    }

    TEST(CapturedMaterialTest, ACapturedPawnIsOnePointForTheTaker) {
        ChessGameModel model;
        PlayAll(model, {"e2e4", "d7d5", "e4d5"});

        const MaterialSummary summary = SummarizeMaterial(model.GetPosition());
        ExpectGone(summary, PieceColor::Black, {0, 0, 0, 0, 1}, "black");
        ExpectGone(summary, PieceColor::White, {0, 0, 0, 0, 0}, "white");
        EXPECT_EQ(summary.GetPoints(PieceColor::White), 39);
        EXPECT_EQ(summary.GetPoints(PieceColor::Black), 38);
        EXPECT_EQ(summary.GetBalance(), 1);
    }

    TEST(CapturedMaterialTest, ATradeOfQueensTakesBothAndTheLeadIsWhatIsLeft) {
        ChessGameModel model;
        // 1.e4 e5 2.Qh5 g6 3.Qxe5+ Qe7 4.Qxe7+ Bxe7: White took a pawn and a queen, Black took a queen
        PlayAll(model, {"e2e4", "e7e5", "d1h5", "g7g6", "h5e5", "d8e7", "e5e7", "f8e7"});

        const MaterialSummary summary = SummarizeMaterial(model.GetPosition());
        ExpectGone(summary, PieceColor::Black, {1, 0, 0, 0, 1}, "black");
        ExpectGone(summary, PieceColor::White, {1, 0, 0, 0, 0}, "white");
        EXPECT_EQ(summary.GetPoints(PieceColor::White), 30);
        EXPECT_EQ(summary.GetPoints(PieceColor::Black), 29);
        EXPECT_EQ(summary.GetBalance(), 1);
    }

    TEST(CapturedMaterialTest, SeveralPiecesGoneForBothColors) {
        // White: six pawns, a knight, two rooks (no queen, no bishop). Black: five pawns, a knight, two rooks (no queen, no bishop)
        const MaterialSummary summary = SummaryOf("r3k2r/pp3ppp/2n5/8/8/5N2/PPP2PPP/R3K2R w KQkq - 0 1");
        ExpectGone(summary, PieceColor::White, {1, 0, 2, 1, 2}, "white");
        ExpectGone(summary, PieceColor::Black, {1, 0, 2, 1, 3}, "black");
        EXPECT_EQ(summary.GetPoints(PieceColor::White), 6 + 3 + 10);
        EXPECT_EQ(summary.GetPoints(PieceColor::Black), 5 + 3 + 10);
        EXPECT_EQ(summary.GetBalance(), 1);
    }

    TEST(CapturedMaterialTest, ABehindSideHasANegativeBalance) {
        const MaterialSummary summary = SummaryOf("4k3/8/8/8/8/8/8/4K2R w - - 0 1");
        EXPECT_EQ(summary.GetBalance(), 5);
        const MaterialSummary mirrored = SummaryOf("4k2r/8/8/8/8/8/8/4K3 w - - 0 1");
        EXPECT_EQ(mirrored.GetBalance(), -5);
    }

    // ---- Promotions ----

    TEST(CapturedMaterialTest, APromotionToAnExtraPieceIsNotAMissingPawn) {
        // A full set minus the a-pawn, then the same set with an extra piece on a3 instead: seven pawns and the piece, so the pawn is not missing
        struct Case {
            const char* piece;
            int points;
            Gone whiteGone;
        };
        const Case cases[] = {
            {"Q", 8, {0, 0, 0, 0, 0}},
            {"R", 4, {0, 0, 0, 0, 0}},
            {"B", 2, {0, 0, 0, 0, 0}},
            {"N", 2, {0, 0, 0, 0, 0}},
        };
        for (const Case& test : cases) {
            const std::string withPawnGone = "rnbqkbnr/pppppppp/8/8/8/8/1PPPPPPP/RNBQKBNR w KQkq - 0 1";
            const std::string promoted = std::string("rnbqkbnr/pppppppp/8/8/8/") + test.piece + "7/1PPPPPPP/RNBQKBNR w KQkq - 0 1";

            const MaterialSummary before = SummaryOf(FromStd(withPawnGone));
            EXPECT_EQ(before.GetMissing(PieceColor::White, PieceType::Pawn), 1);
            EXPECT_EQ(before.GetBalance(), -1);

            const MaterialSummary after = SummaryOf(FromStd(promoted));
            ExpectGone(after, PieceColor::White, test.whiteGone, test.piece);
            ExpectGone(after, PieceColor::Black, {0, 0, 0, 0, 0}, test.piece);
            EXPECT_EQ(after.GetBalance(), test.points) << test.piece;

            // Black has the same, seen from the other side of the table
            const std::string mirrored = MirrorFen(FromStd(promoted));
            const MaterialSummary seen = SummaryOf(FromStd(mirrored));
            ExpectGone(seen, PieceColor::Black, test.whiteGone, test.piece);
            EXPECT_EQ(seen.GetBalance(), -test.points) << test.piece;
        }
    }

    TEST(CapturedMaterialTest, PromotingByMovesCountsThePieceAndNotAMissingPawn) {
        struct Case {
            const char* fen;
            const char* uci;
            int gain;  // the lead grows by the new piece minus the pawn
        };
        const Case cases[] = {
            {"4k3/P7/8/8/8/8/1PPPPPPP/3QK3 w - - 0 1", "a7a8q", 8},
            {"4k3/P7/8/8/8/8/1PPPPPPP/R3K2R w - - 0 1", "a7a8r", 4},
            {"4k3/P7/8/8/8/8/1PPPPPPP/2B1KB2 w - - 0 1", "a7a8b", 2},
            {"4k3/P7/8/8/8/8/1PPPPPPP/1N2K1N1 w - - 0 1", "a7a8n", 2},
        };
        for (const Case& test : cases) {
            for (const bool mirror : {false, true}) {
                std::string fen = test.fen;
                std::string uci = test.uci;
                if (mirror) {
                    fen = MirrorFen(FromStd(fen));
                    uci = MirrorUci(FromStd(uci));
                }
                const PieceColor mover = mirror ? PieceColor::Black : PieceColor::White;
                const int sign = mirror ? -1 : 1;

                ChessGameModel model;
                model.SetPosition(PositionFromFen(FromStd(fen)));
                const MaterialSummary before = SummarizeMaterial(model.GetPosition());
                ASSERT_TRUE(Play(model, FromStd(uci))) << fen << " " << uci;
                const MaterialSummary after = SummarizeMaterial(model.GetPosition());

                EXPECT_EQ(after.GetBalance() - before.GetBalance(), sign * test.gain) << fen << " " << uci;
                EXPECT_EQ(after.GetMissing(mover, PieceType::Pawn), 0) << fen << " " << uci << ": the pawn became a piece that was already counted";
                EXPECT_EQ(after.GetMissing(Opposite(mover), PieceType::Pawn), 8) << fen << " " << uci;
            }
        }
    }

    TEST(CapturedMaterialTest, APromotionToAPieceThatWasMissingFillsItsSlotAndTheListShowsAPawn) {
        // The queen is the one the player lost: after a7a8q it is back (the list cannot tell a promoted queen from the first one), so the pawn shows as gone
        ChessGameModel model;
        model.SetPosition(PositionFromFen("4k3/P7/8/8/8/8/1PPPPPPP/4K3 w - - 0 1"));
        const MaterialSummary before = SummarizeMaterial(model.GetPosition());
        EXPECT_EQ(before.GetMissing(PieceColor::White, PieceType::Queen), 1);
        EXPECT_EQ(before.GetMissing(PieceColor::White, PieceType::Pawn), 0);

        ASSERT_TRUE(Play(model, "a7a8q"));
        const MaterialSummary after = SummarizeMaterial(model.GetPosition());
        EXPECT_EQ(after.GetMissing(PieceColor::White, PieceType::Queen), 0);
        EXPECT_EQ(after.GetMissing(PieceColor::White, PieceType::Pawn), 1);
        EXPECT_EQ(after.GetBalance() - before.GetBalance(), 8);
    }

    TEST(CapturedMaterialTest, ATakenPromotedPieceShowsAsAPawnWhichIsTheLimitOfAPositionOnlyCount) {
        // White has a queen and a pawn on a7, Black a queen. a7a8q (the new queen), then Black takes it: the position now looks like a lost pawn
        // and a queen that was never lost; the list cannot tell it from a game in which a pawn was taken
        ChessGameModel model;
        model.SetPosition(PositionFromFen("3qk3/P7/8/8/8/8/1PPPPPPP/3QK3 w - - 0 1"));
        EXPECT_EQ(SummarizeMaterial(model.GetPosition()).GetBalance(), 8);

        ASSERT_TRUE(Play(model, "a7a8q"));
        EXPECT_EQ(SummarizeMaterial(model.GetPosition()).GetBalance(), 16);
        EXPECT_EQ(SummarizeMaterial(model.GetPosition()).GetMissing(PieceColor::White, PieceType::Pawn), 0);

        ASSERT_TRUE(Play(model, "d8a8"));
        const MaterialSummary summary = SummarizeMaterial(model.GetPosition());
        ExpectGone(summary, PieceColor::White, {0, 2, 2, 2, 1}, "white");  // a pawn, where the game took the promoted queen
        EXPECT_EQ(summary.GetPoints(PieceColor::White), 16);
        EXPECT_EQ(summary.GetPoints(PieceColor::Black), 9);
        EXPECT_EQ(summary.GetBalance(), 7);
    }

    // ---- SetPosition, Reset ----

    TEST(CapturedMaterialTest, SetPositionGivesTheMaterialOfThatFen) {
        for (const StringView fen : PerftFens()) {
            ChessGameModel model;
            model.SetPosition(PositionFromFen(fen));
            const MaterialSummary fromModel = SummarizeMaterial(model.GetPosition());
            const MaterialSummary fromFen = SummaryOf(fen);
            for (const PieceColor color : {PieceColor::White, PieceColor::Black}) {
                EXPECT_EQ(fromModel.GetPoints(color), fromFen.GetPoints(color)) << Text(fen);
                for (const PieceType type : CapturableTypes()) {
                    EXPECT_EQ(fromModel.GetMissing(color, type), fromFen.GetMissing(color, type)) << Text(fen);
                }
            }
            EXPECT_EQ(fromModel.GetBalance(), fromFen.GetBalance()) << Text(fen);
        }
    }

    TEST(CapturedMaterialTest, ResetGoesBackToNothingGone) {
        ChessGameModel model;
        PlayAll(model, {"e2e4", "d7d5", "e4d5"});
        ASSERT_EQ(SummarizeMaterial(model.GetPosition()).GetBalance(), 1);

        model.Reset();
        const MaterialSummary summary = SummarizeMaterial(model.GetPosition());
        EXPECT_EQ(summary.GetBalance(), 0);
        for (const PieceColor color : {PieceColor::White, PieceColor::Black}) {
            ExpectGone(summary, color, {0, 0, 0, 0, 0}, "after reset");
        }
    }

    // ---- Properties over the published positions ----

    TEST(CapturedMaterialTest, AMirroredPositionSwapsTheSidesAndNegatesTheLead) {
        for (const StringView fen : PerftFens()) {
            const std::string mirrored = MirrorFen(fen);
            const MaterialSummary original = SummaryOf(fen);
            const MaterialSummary swapped = SummaryOf(FromStd(mirrored));

            EXPECT_EQ(swapped.GetBalance(), -original.GetBalance()) << Text(fen);
            EXPECT_EQ(swapped.GetPoints(PieceColor::White), original.GetPoints(PieceColor::Black)) << Text(fen);
            EXPECT_EQ(swapped.GetPoints(PieceColor::Black), original.GetPoints(PieceColor::White)) << Text(fen);
            for (const PieceType type : CapturableTypes()) {
                EXPECT_EQ(swapped.GetMissing(PieceColor::White, type), original.GetMissing(PieceColor::Black, type)) << Text(fen);
                EXPECT_EQ(swapped.GetMissing(PieceColor::Black, type), original.GetMissing(PieceColor::White, type)) << Text(fen);
            }
        }
    }

    TEST(CapturedMaterialTest, NoCountIsEverNegativeAndNothingExceedsTheStartingSet) {
        const StringView fens[] = {Position::kStartFen, kKiwipeteFen, kPosition3Fen, kPosition4Fen, kPosition5Fen, kPosition6Fen, kUnreachableNinePawnsFen, kUnreachableThreeQueensFen};
        for (const StringView fen : fens) {
            const MaterialSummary summary = SummaryOf(fen);
            for (const PieceColor color : {PieceColor::White, PieceColor::Black}) {
                EXPECT_GE(summary.GetPoints(color), 0) << Text(fen);
                EXPECT_GE(summary.GetMissing(color, PieceType::Queen), 0) << Text(fen);
                EXPECT_LE(summary.GetMissing(color, PieceType::Queen), 1) << Text(fen);
                EXPECT_GE(summary.GetMissing(color, PieceType::Rook), 0) << Text(fen);
                EXPECT_LE(summary.GetMissing(color, PieceType::Rook), 2) << Text(fen);
                EXPECT_GE(summary.GetMissing(color, PieceType::Bishop), 0) << Text(fen);
                EXPECT_LE(summary.GetMissing(color, PieceType::Bishop), 2) << Text(fen);
                EXPECT_GE(summary.GetMissing(color, PieceType::Knight), 0) << Text(fen);
                EXPECT_LE(summary.GetMissing(color, PieceType::Knight), 2) << Text(fen);
                EXPECT_GE(summary.GetMissing(color, PieceType::Pawn), 0) << Text(fen);
                EXPECT_LE(summary.GetMissing(color, PieceType::Pawn), 8) << Text(fen);
            }
        }
    }

    TEST(CapturedMaterialTest, APositionTheRulesCannotReachClampsAtZero) {
        const MaterialSummary nine = SummaryOf(kUnreachableNinePawnsFen);
        EXPECT_EQ(nine.GetMissing(PieceColor::White, PieceType::Pawn), 0) << "nine pawns: nothing is missing, and nothing is negative";
        EXPECT_EQ(nine.GetPoints(PieceColor::White), 9);

        const MaterialSummary queens = SummaryOf(kUnreachableThreeQueensFen);
        EXPECT_EQ(queens.GetMissing(PieceColor::White, PieceType::Queen), 0);
        EXPECT_EQ(queens.GetMissing(PieceColor::White, PieceType::Pawn), 0) << "eight pawns and two extra queens: more pieces than a game can give";
        EXPECT_EQ(queens.GetPoints(PieceColor::White), 8 + 27);
    }

    TEST(CapturedMaterialTest, ANonCapturableTypeHasNothingMissing) {
        const MaterialSummary summary = SummaryOf("4k3/8/8/8/8/8/8/4K3 w - - 0 1");
        EXPECT_EQ(summary.GetMissing(PieceColor::White, PieceType::King), 0);
        EXPECT_EQ(summary.GetMissing(PieceColor::White, PieceType::None), 0);
        EXPECT_EQ(summary.GetPoints(PieceColor::White), 0) << "a king is worth nothing here";
        EXPECT_EQ(summary.GetBalance(), 0);
    }

}  // namespace BECore::Chess::Tests
