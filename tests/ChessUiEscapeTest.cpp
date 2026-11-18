#include <tests/ChessUiHarness.h>

/// Escape closes the open menu panel and cancels a pending promotion, each as its button does. The scene, the helpers and the fixture ChessUiTest are tests/ChessUiHarness.h.
namespace BECore::Chess::Tests {

    namespace {

        void PressKey(SDL_Keycode key, bool repeat = false) {
            SDLEvents::KeyDownEvent::Emit(key, SDL_KMOD_NONE, repeat);
        }

    }  // namespace

    TEST_F(ChessUiTest, EscapeClosesTheListOfTheMenuAsItsCloseButtonDoes) {
        ActionLog log;
        Press("ChessOpenMenu");
        ASSERT_TRUE(MenuList()->_visible);

        PressKey(SDLK_ESCAPE);

        EXPECT_FALSE(MenuList()->_visible);
        EXPECT_EQ(log.LastAction(), "ChessCloseMenu"_intern);
        EXPECT_EQ(log.Total(), 2u) << "the open and one close";
    }

    TEST_F(ChessUiTest, EscapeClosesEverySectionOfTheMenu) {
        for (const MenuSection& section : MenuSections()) {
            SCOPED_TRACE(String(section.id).c_str());
            Press("ChessOpenMenu");
            PressWith("ChessOpenSection", String(section.id).c_str());
            ASSERT_TRUE(CanvasOf(String(section.node).c_str())->_visible);
            ASSERT_FALSE(MenuList()->_visible) << "the section replaces the list";

            PressKey(SDLK_ESCAPE);

            EXPECT_FALSE(CanvasOf(String(section.node).c_str())->_visible);
            EXPECT_FALSE(MenuList()->_visible);
        }
    }

    TEST_F(ChessUiTest, EscapeWithNoPanelOpenDoesNothing) {
        Click("e2");
        ASSERT_TRUE(Model().HasSelection());
        const uint64_t version = Model().GetVersion();
        const String fen = Model().GetPosition().ToFen();
        ActionLog log;

        PressKey(SDLK_ESCAPE);
        _manager->UpdateAll();

        EXPECT_EQ(log.Total(), 0u) << "no action of the menu or of the picker";
        EXPECT_FALSE(MenuList()->_visible);
        EXPECT_FALSE(PromotionShown());
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_EQ(Model().GetPosition().ToFen(), fen);
        EXPECT_EQ(Model().GetSelected(), Sq("e2")) << "the selection is the board's, not a panel";
    }

    TEST_F(ChessUiTest, OnlyTheFirstPressOfEscapeActsAndNoOtherKeyDoes) {
        Press("ChessOpenMenu");
        ASSERT_TRUE(MenuList()->_visible);
        ActionLog log;

        PressKey(SDLK_ESCAPE, true);
        PressKey(SDLK_RETURN);
        PressKey(SDLK_SPACE);
        EXPECT_TRUE(MenuList()->_visible);
        EXPECT_EQ(log.Total(), 0u);

        PressKey(SDLK_ESCAPE);
        EXPECT_FALSE(MenuList()->_visible);
        EXPECT_EQ(log.Total(), 1u);
    }

    TEST_F(ChessUiTest, EscapeCancelsAPendingPromotionAsTheCancelButtonDoes) {
        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());
        const uint64_t version = Model().GetVersion();
        ActionLog log;

        PressKey(SDLK_ESCAPE);

        EXPECT_EQ(log.Total(), 1u);
        EXPECT_EQ(log.LastAction(), "ChessCancelPromotion"_intern);
        EXPECT_FALSE(PromotionShown());
        EXPECT_FALSE(Model().HasPendingPromotion());
        EXPECT_EQ(Model().GetVersion(), version);
        const String fen = Model().GetPosition().ToFen();
        EXPECT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("a7")) << "the pawn stays selected";

        // The button, from the same start, leaves the same game
        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(PromotionShown());
        const uint64_t startVersion = Model().GetVersion();
        ASSERT_TRUE(ClickPromotion("PromotionCancel").Both());
        EXPECT_EQ(Model().GetVersion(), startVersion);
        EXPECT_EQ(Model().GetPosition().ToFen(), fen);
        EXPECT_TRUE(Model().HasSelection());
        EXPECT_EQ(Model().GetSelected(), Sq("a7"));
        EXPECT_FALSE(Model().HasPendingPromotion());

        Click("a8");
        EXPECT_TRUE(PromotionShown()) << "the same target asks again";
    }

    TEST_F(ChessUiTest, EscapeAfterAPromotionWasChosenDoesNothingMore) {
        StartFrom(kPromotionFen);
        ClickMove("a7", "a8");
        ASSERT_TRUE(ClickPromotion("PromoteQueen").Both());
        const uint64_t version = Model().GetVersion();
        ActionLog log;

        PressKey(SDLK_ESCAPE);

        EXPECT_EQ(log.Total(), 0u);
        EXPECT_EQ(Model().GetVersion(), version);
        EXPECT_EQ(Model().GetPieceAt(Sq("a8")).type, PieceType::Queen);
    }

}  // namespace BECore::Chess::Tests
