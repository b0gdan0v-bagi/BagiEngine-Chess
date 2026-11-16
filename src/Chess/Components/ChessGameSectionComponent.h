#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Game/ChessTypes.h>

namespace BECore::InputEvents {
    struct ActionEvent;
}

namespace BECore::Chess {

    class ChessGame;

    /// What the menu section "Game" is asking the person before it ends the game
    CORE_ENUM(GameQuestion, uint8_t, None, Resign, Draw)

    /**
     * @brief The menu section "Game" (Resign / Draw), on the node `GameSection` before its canvas: answers `ChessResign`, `ChessDraw`, `ChessGameYes` and `ChessGameNo`.
     *
     * Resign asks "Resign?" and Draw between two people "Agree a draw?" first (Yes / No in the same panel); Draw against the program is offered at once, and the answer shows on the HUD.
     * The HUD's Resign button emits the same `ChessResign`: the section shows itself with the question open, so both buttons share one confirmation and one rule (ChessGame::ResigningNow()).
     * Yes asks the rule again, because the grey look of the buttons is from the last draw, ends the game and closes the menu (`ChessCloseMenu`). The open question is the one UI-only
     * state of the scene: it is dropped by the menu opening, closing or switching a section, by No, and by any change of the game's version or of the opponent, so a program's move
     * never leaves a question about a position that is gone. Resign and Draw are greyed while the game cannot take them; the words and the greyed look are written only when they change,
     * so a steady frame allocates nothing. Keeps no game state.
     */
    class ChessGameSectionComponent : public IComponent {
        BE_CLASS(ChessGameSectionComponent)
    public:
        ChessGameSectionComponent() = default;
        ~ChessGameSectionComponent() override = default;

        void OnAttached() override;
        void OnDraw() override;

    private:
        void OnAction(const InputEvents::ActionEvent& event);

        /// Opens the question for the game as it is now
        void Ask(GameQuestion question, const ChessGame& game);

        /// Yes: what the open question asked, if the rule still allows it, and the menu closes
        void Answer(ChessGame& game);

        GameQuestion _asking = GameQuestion::None;
        GameQuestion _shownAsking = GameQuestion::None;  ///< what the canvas shows: the XML starts with Resign and Draw
        const void* _askedModel = nullptr;
        uint64_t _askedVersion = 0;
        uint64_t _askedRevision = 0;

        bool _greyKnown = false;
        const void* _greyModel = nullptr;
        uint64_t _greyVersion = 0;
        uint64_t _greyRevision = 0;
        bool _resignDisabled = false;
        bool _drawDisabled = false;
    };

}  // namespace BECore::Chess
