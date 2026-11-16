#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }

}  // namespace BECore

namespace BECore::Chess {

    /**
     * @brief The Sound section, on the node `SoundPanel` before its canvas: answers `ChessSetVolume` (the slider's text in `value`) and `ChessSetMute` (`1` or `0`) with the master level of
     * `CoreManager::GetAudio()`, and publishes `soundVolume`, `soundVolumeText` and `soundMuted` for the widgets.
     *
     * The widgets own no level: the manager does. The variables are written in `OnDraw` when `GetLevelsRevision()` differs from the last one published (and at the first draw), so a level
     * set by the control API or restored from a save shows at the next draw and a steady frame writes nothing. A muted master keeps its volume, so the slider still shows it. A value that
     * does not parse changes nothing and logs one warning.
     */
    class ChessSoundPanelComponent : public IComponent {
        BE_CLASS(ChessSoundPanelComponent)
    public:
        ChessSoundPanelComponent() = default;
        ~ChessSoundPanelComponent() override = default;

        void OnAttached() override;
        void OnDraw() override;

    private:
        void OnAction(const InputEvents::ActionEvent& event);

        bool _published = false;
        uint64_t _publishedRevision = 0;
    };

}  // namespace BECore::Chess
