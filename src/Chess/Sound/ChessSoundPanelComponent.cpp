#include "ChessSoundPanelComponent.h"

#include <BECore/Audio/AudioManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UIVars.h>
#include <Chess/Sound/SoundActions.h>
#include <Chess/Sound/SoundLevels.h>
#include <Events/InputEvents.h>
#include <Generated/ChessSoundPanelComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        struct Ids {
            static constexpr auto chessSetMute = "ChessSetMute"_intern;
            static constexpr auto soundMuted = "soundMuted"_intern;
            static constexpr auto soundVolume = "soundVolume"_intern;
            static constexpr auto soundVolumeText = "soundVolumeText"_intern;
        };

    }  // namespace

    void ChessSoundPanelComponent::OnAttached() {
        SubscribeInScene<InputEvents::ActionEvent, &ChessSoundPanelComponent::OnAction>(this);
    }

    void ChessSoundPanelComponent::OnAction(const InputEvents::ActionEvent& event) {
        const bool volume = event.action == SoundActions::setVolume;
        if (!volume && event.action != Ids::chessSetMute) {
            return;
        }
        const StringView text = event.value.ToStringView();
        AudioManager& audio = CoreManager::GetAudio();
        if (volume) {
            if (const Optional<float> level = ParseVolume(text)) {
                audio.SetMasterVolume(*level);
                return;
            }
        } else if (const Optional<bool> muted = ParseMuted(text)) {
            audio.SetMasterMuted(*muted);
            return;
        }
        LOG_WARNING("{} ignored: '{}' is not a level", event.action.ToStringView(), text);
    }

    void ChessSoundPanelComponent::OnDraw() {
        AudioManager& audio = CoreManager::GetAudio();
        const uint64_t revision = audio.GetLevelsRevision();
        if (_published && revision == _publishedRevision) {
            return;
        }
        _published = true;
        _publishedRevision = revision;

        const AudioLevel& master = audio.GetSettings()._master;
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set(Ids::soundVolume, StringView(VolumeVarText(master._volume)));
        vars.Set(Ids::soundVolumeText, StringView(VolumePercentText(master._volume)));
        vars.Set(Ids::soundMuted, StringView(master._muted ? "1" : "0"));
    }

}  // namespace BECore::Chess
