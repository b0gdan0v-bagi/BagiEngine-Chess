#pragma once

namespace BECore::Chess {

    /// The box of the lists of captured pieces (the rows of the node `Material` of SceneConfig.xml) in the left margin of the view, next to the board: the lists are
    /// shown only while this whole box is inside the safe area of the canvas. SceneConfig.xml has to agree with these numbers. In view units
    constexpr float kMaterialListLeft = 8.0f;
    constexpr float kMaterialListTop = 84.0f;
    constexpr float kMaterialListRight = 72.0f;
    constexpr float kMaterialListBottom = 716.0f;

    /// The two groups of the display (the upper and the lower one) and the pieces in a group (`CapturableTypes()`)
    constexpr size_t kMaterialGroupCount = 2;
    constexpr size_t kMaterialRowsPerGroup = 5;

    /// Whether the box of the lists lies inside the safe area (its edges in view units; the edge itself counts as inside). A narrow window with insets at the
    /// sides (a phone held sideways) leaves the margin too small, and the lists are then hidden; the balance boxes stay, whatever this says
    [[nodiscard]] constexpr bool MaterialListFits(float safeLeft, float safeTop, float safeRight, float safeBottom) noexcept {
        return kMaterialListLeft >= safeLeft && kMaterialListTop >= safeTop && kMaterialListRight <= safeRight && kMaterialListBottom <= safeBottom;
    }

    static_assert(MaterialListFits(0.0f, 0.0f, 840.0f, 800.0f));
    static_assert(!MaterialListFits(75.6f, 0.0f, 764.4f, 800.0f), "the safe area of the narrowest window of the size matrix starts at x = 75.6");

}  // namespace BECore::Chess
