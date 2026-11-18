#include <Chess/Components/ChessHudComponent.h>
#include <tests/ChessUiHarness.h>
#include <tests/ShippedOpenings.h>

/// The name of the opening in the bottom-left corner of the HUD (the element `Opening` of ChessHud.xml). The scene, the helpers and the fixture ChessUiTest are
/// tests/ChessUiHarness.h; the book a game loads depends on the folder the test runs in, so each case sets its own before its first frame.
namespace BECore::Chess::Tests {

    namespace {

        constexpr StringView kNajdorfMoves[] = {"e2e4", "c7c5", "g1f3", "d7d6", "d2d4", "c5d4", "f3d4", "g8f6", "b1c3", "a7a6"};

        void GiveTheShippedBook() {
            ChessGame* game = ChessGame::GetActive();
            ASSERT_NE(game, nullptr);
            game->SetOpenings(ShippedOpeningBook());
        }

        bool PlayAll(ChessGameModel& model, std::initializer_list<StringView> moves) {
            for (const StringView uci : moves) {
                if (!Play(model, uci)) {
                    return false;
                }
            }
            return true;
        }

        const String kNajdorfShown = OpeningDisplayName("Sicilian Defense: Najdorf Variation", kOpeningMaxChars);

    }  // namespace

    TEST_F(ChessUiTest, TheOpeningLineIsEmptyAtTheStartAndNamesTheLineOfTheGame) {
        GiveTheShippedBook();
        Draw();
        EXPECT_EQ(ShownText(Label("Opening")), "");

        for (const StringView uci : kNajdorfMoves) {
            ASSERT_TRUE(Play(Model(), uci)) << String(uci.data(), uci.size()).c_str();
        }
        Draw();
        EXPECT_EQ(ShownText(Label("Opening")), kNajdorfShown);
        EXPECT_EQ(kNajdorfShown, "Sicilian Defense") << "the family: the whole name is longer than kOpeningMaxChars";
    }

    TEST_F(ChessUiTest, TheOpeningLineKeepsTheLastNameAfterTheGameLeavesTheBook) {
        GiveTheShippedBook();
        for (const StringView uci : kNajdorfMoves) {
            ASSERT_TRUE(Play(Model(), uci));
        }
        ASSERT_TRUE(PlayAll(Model(), {"a2a3", "h7h6"}));
        Draw();
        EXPECT_EQ(ShownText(Label("Opening")), kNajdorfShown);
    }

    TEST_F(ChessUiTest, TheOpeningLineNamesTheViewedPositionAndTheGameAgainAfterTheView) {
        GiveTheShippedBook();
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "c7c5", "g1f3", "d7d6", "d2d4", "c5d4", "f3d4"}));
        Draw();
        const String gameName = ShownText(Label("Opening"));
        EXPECT_EQ(gameName, "Sicilian Defense");

        ASSERT_TRUE(Model().ShowMoves(1));
        Draw();
        EXPECT_EQ(ShownText(Label("Opening")), "King's Pawn Game") << "after 1.e4, as the board shows it";

        ASSERT_TRUE(Model().ShowMoves(0));
        Draw();
        EXPECT_EQ(ShownText(Label("Opening")), "") << "the start position has no name";

        ASSERT_TRUE(Model().EndViewing());
        Draw();
        EXPECT_EQ(ShownText(Label("Opening")), gameName);
    }

    TEST_F(ChessUiTest, TheOpeningLineIsEmptyAfterResetAndFollowsATakeBack) {
        GiveTheShippedBook();
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "c7c5"}));
        Draw();
        EXPECT_EQ(ShownText(Label("Opening")), "Sicilian Defense");

        EXPECT_EQ(Model().TakeBack(1), 1u);
        Draw();
        EXPECT_EQ(ShownText(Label("Opening")), "King's Pawn Game");

        Model().Reset();
        Draw();
        EXPECT_EQ(ShownText(Label("Opening")), "");
    }

    TEST_F(ChessUiTest, TheOpeningLineStaysEmptyWithAnEmptyBook) {
        ChessGame::GetActive()->SetOpenings(OpeningBook());
        ASSERT_TRUE(PlayAll(Model(), {"e2e4", "c7c5"}));
        Draw();
        EXPECT_EQ(ShownText(Label("Opening")), "") << "no name outside a book";
    }

    TEST_F(ChessUiTest, TheOpeningLineLiesInTheBottomMarginLeftOfTheStatusInEveryView) {
        GiveTheShippedBook();
        for (const ViewShape& shape : kViewShapes) {
            SCOPED_TRACE(shape.name);
            DrawAt(shape.output.first, shape.output.second);
            ASSERT_EQ(PortraitIsActive(), shape.portrait);
            const UIElement& opening = Label("Opening");
            const UIElement& status = Label("Status");
            ASSERT_FALSE(opening.rect.IsEmpty());
            ASSERT_FALSE(status.rect.IsEmpty());
            const float boardBottom = shape.boardTop + kBoardUnits;
            EXPECT_GE(opening.rect.y, boardBottom - kPixelTolerance) << "under the board";
            EXPECT_LE(opening.rect.x + opening.rect.w, status.rect.x - kMinGap + kPixelTolerance) << "left of Status, with the gap of the HUD";
            EXPECT_NEAR(opening.rect.y, status.rect.y, kPixelTolerance) << "in the row of Status";
            EXPECT_NEAR(opening.rect.h, status.rect.h, kPixelTolerance);
        }
    }

}  // namespace BECore::Chess::Tests
