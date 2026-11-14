#include "MenuSections.h"

namespace BECore::Chess {

    namespace {

        constexpr Array<MenuSection, 1> kSections{{
            {"opponent", "OpponentPanel", "Opponent"},
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
