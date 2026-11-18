#pragma once

#include <CoreSDL/SDLEvents.h>

namespace BECore::Chess {

    /// True for the first key-down of Escape; a held key repeats it, and a repeat must not close a panel the person opened in between
    inline bool IsEscapePress(const SDLEvents::KeyDownEvent& event) {
        return event.key == SDLK_ESCAPE && !event.repeat;
    }

}  // namespace BECore::Chess
