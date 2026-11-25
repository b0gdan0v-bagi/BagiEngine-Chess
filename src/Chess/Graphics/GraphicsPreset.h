#pragma once

#include <Chess/Game/ChessTypes.h>

namespace BECore::Chess {

    /// The look of the game: which atlas draws the pieces and which board theme colours the squares. Classic is the default and the first value
    CORE_ENUM(GraphicsPreset, uint8_t, Classic, Beer)

    /// The id of the control API, the menu and the save: "classic", "beer"
    [[nodiscard]] StringView PresetId(GraphicsPreset preset);

    /// The preset of an id, exact and lower case; anything else is nothing
    [[nodiscard]] Optional<GraphicsPreset> ParsePreset(StringView id);

    /// The theme of `config/UIThemes.xml` that belongs to the preset
    [[nodiscard]] StringView ThemeOf(GraphicsPreset preset);

    /// What a sprite name of the preset starts with ("Classic" for the plain sprites of the classic atlas, nothing for the beer group, whose names the Generate panel's Accept fixes)
    [[nodiscard]] StringView SpritePrefix(GraphicsPreset preset);

}  // namespace BECore::Chess
