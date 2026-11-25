#include "GraphicsPreset.h"

namespace BECore::Chess {

    namespace {

        struct PresetEntry {
            GraphicsPreset preset;
            StringView id;
            StringView theme;
            StringView spritePrefix;
        };

        constexpr Array<PresetEntry, 2> kPresets{{
            {GraphicsPreset::Classic, "classic", "classic", "Classic"},
            {GraphicsPreset::Beer, "beer", "tavern", ""},
        }};

        const PresetEntry& Find(GraphicsPreset preset) {
            for (const PresetEntry& entry : kPresets) {
                if (entry.preset == preset) {
                    return entry;
                }
            }
            return kPresets[0];
        }

    }  // namespace

    StringView PresetId(GraphicsPreset preset) {
        return Find(preset).id;
    }

    Optional<GraphicsPreset> ParsePreset(StringView id) {
        for (const PresetEntry& entry : kPresets) {
            if (entry.id == id) {
                return entry.preset;
            }
        }
        return std::nullopt;
    }

    StringView ThemeOf(GraphicsPreset preset) {
        return Find(preset).theme;
    }

    StringView SpritePrefix(GraphicsPreset preset) {
        return Find(preset).spritePrefix;
    }

}  // namespace BECore::Chess
