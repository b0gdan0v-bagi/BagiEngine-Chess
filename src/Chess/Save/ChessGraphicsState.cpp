#include "ChessGraphicsState.h"

#include <Generated/ChessGraphicsState.gen.hpp>

namespace BECore::Chess {

    ChessGraphicsState Capture(const GraphicsChoice& choice) {
        const StringView id = PresetId(choice.Get());
        ChessGraphicsState state;
        state.preset = String(id.data(), id.size());
        return state;
    }

    std::expected<GraphicsPreset, SaveError> Validate(const ChessGraphicsState& state) {
        const Optional<GraphicsPreset> preset = ParsePreset(StringView(state.preset));
        if (!preset) {
            return std::unexpected(SaveError{SaveErrorKind::Corrupt, "the saved graphics preset is not a preset of this game"});
        }
        return *preset;
    }

    void Apply(GraphicsPreset preset, GraphicsChoice& choice) {
        choice.Set(preset);
    }

}  // namespace BECore::Chess
