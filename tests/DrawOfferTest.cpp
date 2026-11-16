#include <Chess/Engine/DrawOffer.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveGenerator.h>
#include <tests/ChessTestUtil.h>

/// The rule of the draw offer: who may offer, and when the program takes it. Pure, no engine
namespace BECore::Chess::Tests {

    namespace {

        constexpr PieceColor kWhite = PieceColor::White;
        constexpr PieceColor kBlack = PieceColor::Black;

        AnalysisScore Cp(int value) {
            return AnalysisScore{AnalysisScoreKind::Centipawns, value};
        }

        AnalysisScore Mate(int moves) {
            return AnalysisScore{AnalysisScoreKind::Mate, moves};
        }

        bool Play(ChessGameModel& model, StringView uci) {
            const auto move = MoveGenerator::ParseUci(model.GetPosition(), uci);
            return move.has_value() && model.TryMove(*move).has_value();
        }

    }  // namespace

    TEST(DrawOfferTest, TheThresholdsAreTheOwnersNumbers) {
        EXPECT_EQ(kDrawAcceptCentipawns, 50);
        EXPECT_EQ(kDrawOfferDepth, 5);
    }

    TEST(DrawOfferTest, TheProgramAcceptsExactlyUpToTheThresholdInItsFavour) {
        EXPECT_TRUE(ProgramAcceptsDraw(Cp(0), kWhite, kWhite));
        EXPECT_TRUE(ProgramAcceptsDraw(Cp(kDrawAcceptCentipawns), kWhite, kWhite));
        EXPECT_FALSE(ProgramAcceptsDraw(Cp(kDrawAcceptCentipawns + 1), kWhite, kWhite));
        EXPECT_TRUE(ProgramAcceptsDraw(Cp(-kDrawAcceptCentipawns - 1), kWhite, kWhite)) << "worse for the program: a draw is welcome";
        EXPECT_TRUE(ProgramAcceptsDraw(Cp(-900), kWhite, kWhite));
        EXPECT_FALSE(ProgramAcceptsDraw(Cp(900), kWhite, kWhite));
    }

    TEST(DrawOfferTest, AScoreIsReadFromTheSideToMovesViewForBothProgramColors) {
        // The person is to move, so the score is theirs: +51 for the person is -51 for the program
        EXPECT_TRUE(ProgramAcceptsDraw(Cp(51), kBlack, kWhite)) << "the person (Black) is better, the program (White) takes the draw";
        EXPECT_TRUE(ProgramAcceptsDraw(Cp(51), kWhite, kBlack));
        EXPECT_TRUE(ProgramAcceptsDraw(Cp(-kDrawAcceptCentipawns), kBlack, kWhite)) << "+50 for the program";
        EXPECT_FALSE(ProgramAcceptsDraw(Cp(-kDrawAcceptCentipawns - 1), kBlack, kWhite)) << "+51 for the program";
        EXPECT_FALSE(ProgramAcceptsDraw(Cp(-kDrawAcceptCentipawns - 1), kWhite, kBlack));
        EXPECT_TRUE(ProgramAcceptsDraw(Cp(-kDrawAcceptCentipawns), kWhite, kBlack));
        EXPECT_TRUE(ProgramAcceptsDraw(Cp(0), kBlack, kBlack));
        EXPECT_TRUE(ProgramAcceptsDraw(Cp(kDrawAcceptCentipawns), kBlack, kBlack));
        EXPECT_FALSE(ProgramAcceptsDraw(Cp(kDrawAcceptCentipawns + 1), kBlack, kBlack));
    }

    TEST(DrawOfferTest, AMateForTheProgramRefusesAndAMateAgainstItAccepts) {
        EXPECT_FALSE(ProgramAcceptsDraw(Mate(3), kWhite, kWhite));
        EXPECT_TRUE(ProgramAcceptsDraw(Mate(-3), kWhite, kWhite));
        EXPECT_FALSE(ProgramAcceptsDraw(Mate(1), kBlack, kBlack));
        EXPECT_TRUE(ProgramAcceptsDraw(Mate(-1), kBlack, kBlack));
        EXPECT_TRUE(ProgramAcceptsDraw(Mate(3), kBlack, kWhite)) << "the person to move mates: against the program";
        EXPECT_FALSE(ProgramAcceptsDraw(Mate(-3), kBlack, kWhite)) << "the person to move is mated: for the program";
        EXPECT_TRUE(ProgramAcceptsDraw(Mate(1), kWhite, kBlack));
        EXPECT_FALSE(ProgramAcceptsDraw(Mate(-1), kWhite, kBlack));
    }

    TEST(DrawOfferTest, AMateIsNeverComparedWithTheThreshold) {
        EXPECT_FALSE(ProgramAcceptsDraw(Mate(1), kWhite, kWhite)) << "a mate in 1 is more than 50, a mate is never a number of centipawns";
        EXPECT_TRUE(ProgramAcceptsDraw(Mate(-60), kWhite, kWhite));
    }

    TEST(DrawOfferTest, TwoPeopleMayAlwaysOffer) {
        ChessGameModel model;
        EXPECT_EQ(DrawOfferRefusalFor(model, ProgramSides{}), std::nullopt);
        ASSERT_TRUE(Play(model, "e2e4"));
        EXPECT_EQ(DrawOfferRefusalFor(model, ProgramSides{}), std::nullopt) << "either turn";
    }

    TEST(DrawOfferTest, AgainstTheProgramOnlyOnThePersonsTurn) {
        ChessGameModel model;
        EXPECT_EQ(DrawOfferRefusalFor(model, ProgramSides::Of(kBlack)), std::nullopt) << "White, the person, is to move";
        EXPECT_EQ(DrawOfferRefusalFor(model, ProgramSides::Of(kWhite)), DrawOfferRefusal::NotYourTurn);
        ASSERT_TRUE(Play(model, "e2e4"));
        EXPECT_EQ(DrawOfferRefusalFor(model, ProgramSides::Of(kBlack)), DrawOfferRefusal::NotYourTurn);
        EXPECT_EQ(DrawOfferRefusalFor(model, ProgramSides::Of(kWhite)), std::nullopt);
    }

    TEST(DrawOfferTest, TwoProgramsHaveNoOneToOffer) {
        ChessGameModel model;
        const ProgramSides both{true, true};
        EXPECT_EQ(DrawOfferRefusalFor(model, both), DrawOfferRefusal::NoPerson);
        ASSERT_TRUE(Play(model, "e2e4"));
        EXPECT_EQ(DrawOfferRefusalFor(model, both), DrawOfferRefusal::NoPerson) << "on either turn";
    }

    TEST(DrawOfferTest, AFinishedGameIsRefusedForEveryone) {
        ChessGameModel model;
        ASSERT_TRUE(model.Resign(kWhite).has_value());
        EXPECT_EQ(DrawOfferRefusalFor(model, ProgramSides{}), DrawOfferRefusal::GameOver);
        EXPECT_EQ(DrawOfferRefusalFor(model, ProgramSides::Of(kBlack)), DrawOfferRefusal::GameOver);
        EXPECT_EQ(DrawOfferRefusalFor(model, ProgramSides{true, true}), DrawOfferRefusal::GameOver);
    }

}  // namespace BECore::Chess::Tests
