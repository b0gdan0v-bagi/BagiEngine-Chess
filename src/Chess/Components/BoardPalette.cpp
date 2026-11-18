#include "BoardPalette.h"

namespace BECore::Chess {

    namespace {

        struct Tokens {
            static constexpr auto boardCheck = "boardCheck"_intern;
            static constexpr auto boardDark = "boardDark"_intern;
            static constexpr auto boardHintFrom = "boardHintFrom"_intern;
            static constexpr auto boardHintTo = "boardHintTo"_intern;
            static constexpr auto boardLastMove = "boardLastMove"_intern;
            static constexpr auto boardLegalCapture = "boardLegalCapture"_intern;
            static constexpr auto boardLegalMove = "boardLegalMove"_intern;
            static constexpr auto boardLight = "boardLight"_intern;
            static constexpr auto boardMate = "boardMate"_intern;
            static constexpr auto boardPremove = "boardPremove"_intern;
            static constexpr auto boardSelected = "boardSelected"_intern;
        };

    }  // namespace

    BoardPalette::BoardPalette()
        : _light(UIColor::FromToken(Tokens::boardLight)), _dark(UIColor::FromToken(Tokens::boardDark)), _selected(UIColor::FromToken(Tokens::boardSelected)),
          _legalMove(UIColor::FromToken(Tokens::boardLegalMove)), _legalCapture(UIColor::FromToken(Tokens::boardLegalCapture)), _check(UIColor::FromToken(Tokens::boardCheck)),
          _mate(UIColor::FromToken(Tokens::boardMate)), _hintFrom(UIColor::FromToken(Tokens::boardHintFrom)), _hintTo(UIColor::FromToken(Tokens::boardHintTo)),
          _lastMove(UIColor::FromToken(Tokens::boardLastMove)), _premove(UIColor::FromToken(Tokens::boardPremove)) {}

    void BoardPalette::Refresh(UIThemes& themes) {
        if (_version == themes.GetVersion()) {
            return;
        }
        _version = themes.GetVersion();
        ++_resolveCount;
        for (UIColor* color : {&_light, &_dark, &_selected, &_legalMove, &_legalCapture, &_check, &_mate, &_hintFrom, &_hintTo, &_lastMove, &_premove}) {
            themes.Resolve(*color);
        }
    }

}  // namespace BECore::Chess
