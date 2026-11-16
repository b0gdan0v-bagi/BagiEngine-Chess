#include "MenuSections.h"

namespace BECore::Chess {

    namespace {

        constexpr Array<MenuSection, 4> kSections{{
            {kOpponentSectionId, "OpponentPanel", "Opponent"},
            {"moves", "MovesSection", "Moves"},
            {"game", "GameSection", "Resign / Draw"},
            {"sound", "SoundPanel", "Sound"},
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
