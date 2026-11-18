#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Game/ChessTypes.h>

namespace BECore::InputEvents {
    struct ActionEvent;
}

namespace BECore::Chess {

    /// The longest name of an opening that the `Opening` element shows whole, in characters; a longer one is cut to its family (OpeningDisplayName). The width of the element and the font
    /// of config/nodes/ChessHud.xml hold the family names of the book at this limit in every view (the layout tests lay out the longest of them)
    constexpr size_t kOpeningMaxChars = 20;

    /**
     * @brief The HUD of the scene, on the node `Hud` before its canvas: publishes the UIVars that the HUD and the opponent panel show and answers `ChessReset`.
     *
     * The variables (`turn`, `status`, `opening` (the name of the shown position in the book, OpeningDisplayName(), "" outside it), `opponentError` and the panel bindings `opponentMode`,
     * `opponentColor`, `opponentEngine`, `opponentLevel`) are written in OnDraw(), before the canvas of the node draws and after the board's update of the frame has let the program move, and only
     * when the model's version, the controller's revision, the draw judge's revision, IsThinking(), the person's color or the view of an earlier position changed (`status` then says what is viewed,
     * StatusText::Viewing()). `turn` says the program is thinking also while it judges a draw offer; `status` says "Draw declined" for a refused one until the game's version moves. `opponentColor` is
     * the PERSON's color, the opposite of the control API's `opponent.plays`. Greys the `Resign` button (the clicker's disabled) while nobody can resign (ChessGame::ResigningNow()), with the same
     * change check; ChessGameSectionComponent answers its `ChessResign`. Owns no game state.
     */
    class ChessHudComponent : public IComponent {
        BE_CLASS(ChessHudComponent)
    public:
        ChessHudComponent() = default;
        ~ChessHudComponent() override = default;

        void OnAttached() override;
        void OnDraw() override;

    private:
        void OnAction(const InputEvents::ActionEvent& event);

        /// Gives the UI labels the words for the state of the game and of the opponent, only when something they say changed (a label checks the version of the variables every frame)
        void PublishUiVars();

        uint64_t _publishedVersion = 0;
        const void* _publishedModel = nullptr;  // another game starts the count of versions again
        uint64_t _publishedRevision = 0;
        uint64_t _publishedJudgeRevision = 0;            // the draw offer shows (pending, declined) without a version step of the game
        bool _publishedThinking = false;                 // a search starts and ends without a move of the game
        PieceColor _publishedColor = PieceColor::White;  // changes with no revision of the controller while two people play
        Optional<size_t> _publishedShown;                // the moves shown while viewing; empty: the game's own position
        bool _resignDisabled = false;                    // what the clicker of Resign has: the XML starts it enabled
    };

}  // namespace BECore::Chess
