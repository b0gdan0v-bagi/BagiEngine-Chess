#include "SoundLevels.h"

#include <BECore/UI/UISliderComponent.h>
#include <cmath>

namespace BECore::Chess {

    namespace {

        constexpr float kPercentPerUnit = 100.0f;

    }  // namespace

    Optional<float> ParseVolume(StringView text) {
        if (text.empty()) {
            return std::nullopt;
        }
        float value = 0.0f;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) {
            return std::nullopt;
        }
        return std::clamp(value, 0.0f, 1.0f);
    }

    Optional<bool> ParseMuted(StringView text) {
        if (text == "1") {
            return true;
        }
        if (text == "0") {
            return false;
        }
        return std::nullopt;
    }

    String VolumeVarText(float volume) {
        return UISliderComponent::FormatValue(volume);
    }

    String VolumePercentText(float volume) {
        return Format("{}%", static_cast<int>(std::lround(volume * kPercentPerUnit)));
    }

}  // namespace BECore::Chess
