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
     * @brief The promotion picker, on the node `PromotionPanel` before its canvas: shows the canvas exactly while the model has a promotion pending and answers `ChessPromote` (the piece
     * id of Game/PromotionChoices.* in `value`) and `ChessCancelPromotion` (the pawn stays selected).
     *
     * It compares on every update and draw, so a reset, a set_state or someone's move closes a stale picker. The board calls SyncActive() right after a click or a drop, so the picker
     * is shown before anything else reads the scene: the order in which two components get one event is not defined.
     */
    class ChessPromotionPanelComponent : public IComponent {
        BE_CLASS(ChessPromotionPanelComponent)
    public:
        ChessPromotionPanelComponent() = default;
        ~ChessPromotionPanelComponent() override;

        void OnAttached() override;
        void OnDraw() override;

        /// Sync() of the attached picker, if any (Chess assumes one is alive at a time)
        static void SyncActive();

    private:
        void OnAction(const InputEvents::ActionEvent& event);
        void OnUpdate(const SceneEvents::SceneUpdateEvent&);

        /// Shows the canvas of the node while the active model has a promotion pending and hides it otherwise (only when that differs from what was set last)
        void Sync();

        bool _shown = false;  // the canvas starts hidden

        static ChessPromotionPanelComponent* s_active;
    };

}  // namespace BECore::Chess
