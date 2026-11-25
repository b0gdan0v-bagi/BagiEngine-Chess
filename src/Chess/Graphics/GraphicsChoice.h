#pragma once

#include <Chess/Graphics/GraphicsPreset.h>

namespace BECore::Chess {

    /// The look the person picked. It is no part of a game: a reset leaves it alone. Components compare `GetRevision()` with the one they showed
    class GraphicsChoice {
    public:
        [[nodiscard]] GraphicsPreset Get() const noexcept {
            return _preset;
        }

        /// Returns whether the preset changed; the same preset changes nothing and keeps the revision
        bool Set(GraphicsPreset preset) noexcept {
            if (preset == _preset) {
                return false;
            }
            _preset = preset;
            ++_revision;
            return true;
        }

        /// Starts at 1 like the other revisions of the state version, grows on every change and never goes back
        [[nodiscard]] uint64_t GetRevision() const noexcept {
            return _revision;
        }

    private:
        GraphicsPreset _preset = GraphicsPreset::Classic;
        uint64_t _revision = 1;
    };

}  // namespace BECore::Chess
