#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Game/ChessTypes.h>

namespace BECore::InputEvents {
    struct ActionEvent;
}

namespace BECore::Chess {

    /**
     * @brief The HUD of the scene, on the node `Hud` before its canvas: publishes the UIVars that the HUD and the opponent panel show and answers `ChessReset`.
     *
     * The variables (`turn`, `status`, `opponentError` and the panel bindings `opponentMode`, `opponentColor`, `opponentEngine`, `opponentLevel`) are written in OnDraw(), before the canvas of
     * the node draws and after the board's update of the frame has let the program move, and only when the model's version, the controller's revision, IsThinking() or the person's color
     * changed. `opponentColor` is the PERSON's color, the opposite of the control API's `opponent.plays`. Owns no game state.
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
        bool _publishedThinking = false;                 // a search starts and ends without a move of the game
        PieceColor _publishedColor = PieceColor::White;  // changes with no revision of the controller while two people play
    };

}  // namespace BECore::Chess
