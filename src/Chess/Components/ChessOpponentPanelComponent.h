#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }

    namespace SceneEvents {
        struct SceneUpdateEvent;
    }

}  // namespace BECore

namespace BECore::Chess {

    /**
     * @brief The opponent panel, on the node `OpponentPanel` before its canvas: answers the four actions of its choice groups (`ChessSetMode`, `ChessSetColor`, `ChessSetEngine`,
     * `ChessSetLevel`, the id in `value`) and shows the segment `EngineStockfish` exactly while the opponent offers Stockfish.
     *
     * The group owns no choice: a press goes to ChessGame::GetActiveChoice() and ChessHudComponent publishes what the game has. An id in no table logs one warning; a refused pick is shown
     * through the variables. The Stockfish file is looked up again only when the menu opens this panel's section (it watches `ChessOpenSection`, which ChessMenuComponent answers); every
     * update and draw compares the cached answer of OffersStockfish().
     */
    class ChessOpponentPanelComponent : public IComponent {
        BE_CLASS(ChessOpponentPanelComponent)
    public:
        ChessOpponentPanelComponent() = default;
        ~ChessOpponentPanelComponent() override = default;

        void OnAttached() override;
        void OnDraw() override;

    private:
        void OnAction(const InputEvents::ActionEvent& event);
        void OnUpdate(const SceneEvents::SceneUpdateEvent&);

        /// Hides the segment of Stockfish exactly while the opponent does not offer it (no Stockfish in the build or none found), so Own engine fills the group, and shows it again when it
        /// is offered. The scene file is the same in every build and on every machine
        void SyncStockfishSegment();

        bool _stockfishShown = true;  // the segment starts visible in the scene file
    };

}  // namespace BECore::Chess
