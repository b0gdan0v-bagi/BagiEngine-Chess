#pragma once

#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIThemes.h>

namespace BECore::Chess {

    /**
     * @brief The colors the board draws, the tokens `board*` of `config/UIThemes.xml` resolved for the current theme.
     *
     * Refresh() resolves again only when the version of the table moved, so a steady frame does no work and allocates nothing, and `UIThemes::SetTheme` recolours the board at the next
     * draw. A token that no theme defines is drawn magenta and logged once by the engine; there is no fallback in code.
     */
    class BoardPalette {
    public:
        BoardPalette();

        void Refresh(UIThemes& themes);

        /// How many times Refresh() resolved the tokens: a steady frame adds none
        [[nodiscard]] uint64_t GetResolveCount() const noexcept {
            return _resolveCount;
        }

        [[nodiscard]] const Color& LightSquare() const noexcept {
            return _light;
        }
        [[nodiscard]] const Color& DarkSquare() const noexcept {
            return _dark;
        }
        [[nodiscard]] const Color& Selected() const noexcept {
            return _selected;
        }
        [[nodiscard]] const Color& LegalMove() const noexcept {
            return _legalMove;
        }
        [[nodiscard]] const Color& LegalCapture() const noexcept {
            return _legalCapture;
        }
        [[nodiscard]] const Color& Check() const noexcept {
            return _check;
        }
        [[nodiscard]] const Color& Mate() const noexcept {
            return _mate;
        }
        [[nodiscard]] const Color& HintFrom() const noexcept {
            return _hintFrom;
        }
        [[nodiscard]] const Color& HintTo() const noexcept {
            return _hintTo;
        }
        [[nodiscard]] const Color& LastMove() const noexcept {
            return _lastMove;
        }
        [[nodiscard]] const Color& Premove() const noexcept {
            return _premove;
        }

    private:
        UIColor _light;
        UIColor _dark;
        UIColor _selected;
        UIColor _legalMove;
        UIColor _legalCapture;
        UIColor _check;
        UIColor _mate;
        UIColor _hintFrom;
        UIColor _hintTo;
        UIColor _lastMove;
        UIColor _premove;

        uint64_t _version = 0;  // 0: never resolved (the table starts at 1)
        uint64_t _resolveCount = 0;
    };

}  // namespace BECore::Chess
