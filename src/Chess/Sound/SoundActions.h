#pragma once

namespace BECore::Chess {

    /// The actions of the sound section that the click of a button has to know
    struct SoundActions {
        static constexpr auto setVolume = "ChessSetVolume"_intern;
    };

}  // namespace BECore::Chess
