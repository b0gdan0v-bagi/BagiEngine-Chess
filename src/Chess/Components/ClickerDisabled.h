#pragma once

#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIInputClicker.h>

namespace BECore::Chess {

    /// Greys (or lifts the grey of) the clicker of the element @p name on @p canvas; false and nothing written when the canvas has no such element or it has no clicker, so a caller that
    /// remembers what it wrote keeps the old value until a draw finds the button
    inline bool SetClickerDisabled(UICanvasComponent& canvas, PoolString name, bool disabled) {
        UIElement* element = canvas.GetRoot().FindByName(name);
        UIInputClicker* clicker = element ? element->GetComponent<UIInputClicker>() : nullptr;
        if (!clicker) {
            return false;
        }
        clicker->disabled = disabled;
        return true;
    }

}  // namespace BECore::Chess
