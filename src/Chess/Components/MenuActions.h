#pragma once

namespace BECore::Chess {

    /// The actions of the menu that more than one panel answers
    struct MenuActions {
        static constexpr auto openMenu = "ChessOpenMenu"_intern;
        static constexpr auto closeMenu = "ChessCloseMenu"_intern;
        static constexpr auto openSection = "ChessOpenSection"_intern;
    };

}  // namespace BECore::Chess
