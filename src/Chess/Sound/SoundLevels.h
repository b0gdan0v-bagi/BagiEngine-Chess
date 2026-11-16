#pragma once

#include <BECore/Types/Types.h>

namespace BECore::Chess {

    /// The step of the volume slider of the Sound section (the `step` of `SoundPanelVolume` in config/nodes/ChessSoundPanel.xml is the same number)
    inline constexpr float kVolumeStep = 0.1f;

    /// The volume a `ChessSetVolume` value asks for: a finite number, clamped to 0..1. Anything else (empty, text, nan, inf) is nothing
    [[nodiscard]] Optional<float> ParseVolume(StringView text);

    /// The mute a `ChessSetMute` value asks for: "1" is muted, "0" is not, anything else is nothing
    [[nodiscard]] Optional<bool> ParseMuted(StringView text);

    /// The text the slider reads its position from, short and exact ("0.3", never "0.30000001": UISliderComponent::FormatValue)
    [[nodiscard]] String VolumeVarText(float volume);

    /// The number beside the slider: the volume as a whole percent ("70%")
    [[nodiscard]] String VolumePercentText(float volume);

}  // namespace BECore::Chess
