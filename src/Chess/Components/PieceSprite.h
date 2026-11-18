#pragma once

#include <Chess/Game/ChessTypes.h>

namespace BECore::Chess {

    /// The sprite of a piece in `TextureLibrary.xml`, named after the color and the type: "WhiteQueen", "BlackKnight", ...
    [[nodiscard]] inline PoolString PieceSpriteId(Piece piece) {
        return PoolString::Intern(Format("{}{}", EnumUtils<PieceColor>::ToString(piece.color), EnumUtils<PieceType>::ToString(piece.type)));
    }

}  // namespace BECore::Chess
