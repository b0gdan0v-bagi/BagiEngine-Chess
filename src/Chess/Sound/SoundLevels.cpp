#include "SoundLevels.h"

#include <BECore/UI/UISliderComponent.h>
#include <cmath>

namespace BECore::Chess {

    namespace {

        constexpr float kPercentPerUnit = 100.0f;

    }  // namespace

    Optional<float> ParseVolume(StringView text) {
        const Optional<float> value = StringUtils::ParseNumber<float>(text);
        if (!value || !std::isfinite(*value)) {
            return std::nullopt;
        }
        return std::clamp(*value, 0.0f, 1.0f);
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
