#include <Chess/Game/Bitboard.h>
#include <Chess/Game/MoveGenerator.h>
#include <Chess/Sound/SoundCue.h>
#include <tests/ChessTestUtil.h>

/// Which clip a move gets (Sound/SoundCue.h): the rule over real positions, the order of its rows and the table of the clips
namespace BECore::Chess::Tests {

    namespace {

        /// Plays @p uci on @p position and describes what it left behind, the way the component sees it
        MoveOutcome PlayAndDescribe(Position& position, StringView uci) {
            const Optional<Move> move = MoveGenerator::ParseUci(position, uci);
            if (!move) {
                ADD_FAILURE() << "not a legal move here: " << String(uci).c_str();
                return {};
            }
            MoveOutcome outcome;
            outcome.move = *move;
            outcome.piecesBefore = PopCount(position.Pieces());
            position.DoMove(*move);
            outcome.piecesAfter = PopCount(position.Pieces());
            outcome.check = position.IsInCheck();
            outcome.status = EvaluateStatus(position);
            return outcome;
        }

        /// The cue of the last of @p moves played from @p fen
        SoundCue CueAfter(StringView fen, std::initializer_list<StringView> moves) {
            Position position = PositionFromFen(fen);
            MoveOutcome last;
            for (const StringView uci : moves) {
                last = PlayAndDescribe(position, uci);
            }
            return CueOfMove(last);
        }

        constexpr StringView kStart = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        constexpr StringView kBothCastles = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
        constexpr StringView kCastleGivingCheck = "5k2/8/8/8/8/8/8/R3K2R w KQ - 0 1";
        constexpr StringView kPromotionAhead = "k7/4P3/8/8/8/8/7P/4K3 w - - 0 1";  // the pawn on h2 keeps a promoted knight from being a draw by material

    }  // namespace

    TEST(SoundCueTest, AQuietMoveIsAMove) {
        EXPECT_EQ(CueAfter(kStart, {"e2e4"}), SoundCue::Move);
        EXPECT_EQ(CueAfter(kStart, {"g1f3"}), SoundCue::Move);
    }

    TEST(SoundCueTest, ACaptureIsACapture) {
        EXPECT_EQ(CueAfter("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", {"e4d5"}), SoundCue::Capture);
    }

    TEST(SoundCueTest, EnPassantIsACaptureThoughTheTargetSquareIsEmpty) {
        EXPECT_EQ(CueAfter("rnbqkbnr/ppp1pppp/8/8/3pP3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 3", {"d4e3"}), SoundCue::Capture);
    }

    TEST(SoundCueTest, CastlingEitherWayIsACastle) {
        EXPECT_EQ(CueAfter(kBothCastles, {"e1g1"}), SoundCue::Castle);
        EXPECT_EQ(CueAfter(kBothCastles, {"e1c1"}), SoundCue::Castle);
    }

    TEST(SoundCueTest, CastlingThatGivesCheckIsACheck) {
        EXPECT_EQ(CueAfter(kCastleGivingCheck, {"e1g1"}), SoundCue::Check);
    }

    TEST(SoundCueTest, ACheckIsACheck) {
        EXPECT_EQ(CueAfter("4k3/8/8/8/8/8/8/R3K3 w Q - 0 1", {"a1a8"}), SoundCue::Check);
    }

    TEST(SoundCueTest, ACaptureThatGivesCheckIsACheck) {
        EXPECT_EQ(CueAfter("r3k3/8/8/8/8/8/8/R3K3 w - - 0 1", {"a1a8"}), SoundCue::Check);
    }

    TEST(SoundCueTest, APromotionIsAPromotionWithOrWithoutCapture) {
        EXPECT_EQ(CueAfter(kPromotionAhead, {"e7e8n"}), SoundCue::Promotion);
        EXPECT_EQ(CueAfter("k2r4/4P3/8/8/8/8/7P/4K3 w - - 0 1", {"e7d8n"}), SoundCue::Promotion);
    }

    TEST(SoundCueTest, APromotionThatGivesCheckIsStillAPromotion) {
        EXPECT_EQ(CueAfter(kPromotionAhead, {"e7e8q"}), SoundCue::Promotion);
    }

    TEST(SoundCueTest, TheEndOfTheGameBeatsEveryOtherCue) {
        EXPECT_EQ(CueAfter(kStart, {"f2f3", "e7e5", "g2g4", "d8h4"}), SoundCue::GameEnd) << "checkmate";
        EXPECT_EQ(CueAfter("k7/4P3/1K6/8/8/8/8/8 w - - 0 1", {"e7e8q"}), SoundCue::GameEnd) << "a promotion that mates";
        EXPECT_EQ(CueAfter("7k/8/6K1/8/8/8/8/5Q2 w - - 0 1", {"f1f7"}), SoundCue::GameEnd) << "stalemate";
        EXPECT_EQ(CueAfter("4k3/8/8/8/8/8/1b6/B3K3 w - - 0 1", {"a1b2"}), SoundCue::GameEnd) << "insufficient material, by a capture";
        EXPECT_EQ(CueAfter(kStart, {"g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8"}), SoundCue::GameEnd) << "the third time the start position stands";
    }

    TEST(SoundCueTest, TheRepetitionThatIsNotYetThreefoldPlaysAMove) {
        EXPECT_EQ(CueAfter(kStart, {"g1f3", "g8f6", "f3g1", "f6g8"}), SoundCue::Move);
    }

    TEST(SoundCueTest, EveryCueHasItsOwnClipAndAVolumeThatCanBeHeard) {
        HashSet<PoolString> seen;
        for (const SoundCue cue : EnumUtils<SoundCue>::Values()) {
            SCOPED_TRACE(String(EnumUtils<SoundCue>::ToString(cue)).c_str());
            const PoolString path = ClipPath(cue);
            EXPECT_FALSE(path.Empty());
            EXPECT_TRUE(seen.insert(path).second) << "another cue has this clip";
            EXPECT_GT(CueVolume(cue), 0.0f);
            EXPECT_LE(CueVolume(cue), 1.0f);
        }
        EXPECT_EQ(ClipPath(SoundCue::GameEnd), PoolString::Intern("assets/sounds/game_end.wav"));
    }

}  // namespace BECore::Chess::Tests
