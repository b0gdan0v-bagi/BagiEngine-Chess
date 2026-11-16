#include "MenuSections.h"

namespace BECore::Chess {

    namespace {

        constexpr Array<MenuSection, 2> kSections{{
            {kOpponentSectionId, "OpponentPanel", "Opponent"},
            {"moves", "MovesSection", "Moves"},
        }};

    }  // namespace

    Span<const MenuSection> MenuSections() {
        return kSections;
    }

    Optional<MenuSection> FindMenuSection(StringView id) {
        for (const MenuSection& section : kSections) {
            if (section.id == id) {
                return section;
            }
        }
        return std::nullopt;
    }

}  // namespace BECore::Chess
