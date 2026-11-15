#pragma once

#include <Chess/Game/ChessTypes.h>

namespace BECore::Chess {

    /// One section of the menu: a modal canvas that the list panel `MenuPanel` opens by a button of its own
    struct MenuSection {
        /// The name of the section in the action `ChessOpenSection` (its `value`), lower case
        StringView id;
        /// The scene node that holds the canvas of the section
        StringView node;
        /// The word on the button of the list
        StringView label;
    };

    /// The id of the section of the opponent panel, for code that has to know when it opens
    inline constexpr StringView kOpponentSectionId = "opponent";

    /// Every section of the menu, in the order of the buttons of the list. A feature that has no panel yet has no row
    [[nodiscard]] Span<const MenuSection> MenuSections();

    /// The section with this id (exactly as written: ids are lower case), or nothing
    [[nodiscard]] Optional<MenuSection> FindMenuSection(StringView id);

}  // namespace BECore::Chess
