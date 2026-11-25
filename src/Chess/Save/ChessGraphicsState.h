#pragma once

#include <BECore/Save/SaveGame.h>
#include <Chess/Graphics/GraphicsChoice.h>

namespace BECore::Chess {

    /// What Chess writes into the graphics slot: the game id and the version of ChessGraphicsState. A new field needs no version (a file without it keeps the default)
    inline constexpr SaveSchema kChessGraphicsSchema{"ChessGraphics", 1, 1};

    /// The slot of the look, apart from the `game` one: that one is not written while a puzzle is on and a finished game is not restored, and the look must survive both
    inline constexpr StringView kChessGraphicsSlot = "graphics";

    /// The look as a save file holds it: the id of the preset (PresetId()), so the file does not depend on the order of the enum
    struct ChessGraphicsState {
        BE_CLASS(ChessGraphicsState)

        BE_REFLECT_FIELD String preset;
    };

    /// What the choice holds now, as a save. Reads only
    [[nodiscard]] ChessGraphicsState Capture(const GraphicsChoice& choice);

    /// The first half of a restore: the preset of the file, or a `Corrupt` that names the field and never the contents. Changes nothing
    [[nodiscard]] std::expected<GraphicsPreset, SaveError> Validate(const ChessGraphicsState& state);

    /// The second half: puts a checked preset into the choice (a revision moves only when the preset differs)
    void Apply(GraphicsPreset preset, GraphicsChoice& choice);

}  // namespace BECore::Chess
