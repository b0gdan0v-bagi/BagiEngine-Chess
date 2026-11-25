#pragma once

#include <Chess/Game/ChessTypes.h>
#include <Chess/Graphics/GraphicsPreset.h>

namespace BECore::Chess {

    /// The sprite of a piece in `TextureLibrary.xml`: the preset's prefix, the color and the type ("ClassicWhiteQueen", "BlackKnight" for Beer)
    [[nodiscard]] inline PoolString PieceSpriteId(Piece piece, GraphicsPreset preset) {
        return PoolString::Intern(Format("{}{}{}", SpritePrefix(preset), EnumUtils<PieceColor>::ToString(piece.color), EnumUtils<PieceType>::ToString(piece.type)));
    }

}  // namespace BECore::Chess
