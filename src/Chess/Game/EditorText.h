#pragma once

#include <BECore/Clipboard/ClipboardTypes.h>
#include <Chess/Game/PositionEditor.h>

namespace BECore::Chess::EditorText {

    /// The longest refusal line, in characters: as long as the longest word the HUD's Turn line already shows ("Computer is thinking"), so it fits where that does.
    inline constexpr size_t kMaxRefusalChars = 20;

    /// Why a draft or a pasted text is not a playable position, in a short line (never empty). The syntax errors of a text are one line.
    [[nodiscard]] StringView Refusal(FenError error);

    /// Why the draft did not take an edit.
    [[nodiscard]] StringView Refusal(EditRefusal refusal);

    /// What the clipboard answered to a copy or a paste, in a short line. Ok is what a copy says; a read that gave a text needs no words
    [[nodiscard]] StringView Clipboard(ClipboardStatus status);

}  // namespace BECore::Chess::EditorText
