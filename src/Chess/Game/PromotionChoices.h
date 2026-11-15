#pragma once

#include <Chess/Game/ChessTypes.h>

namespace BECore::Chess {

    /// The pieces a pawn can become on the last rank, queen first (the order of the buttons of the promotion panel)
    [[nodiscard]] Span<const PieceType> PromotionPieces();

    /// The name of a promotion piece in the scene and in the actions (lower case); empty for any other piece
    [[nodiscard]] StringView PromotionId(PieceType type);

    /// The word on the button of the promotion panel; empty for any other piece
    [[nodiscard]] StringView PromotionLabel(PieceType type);

    /// The promotion piece with this id (exactly as written: ids are lower case), or nothing
    [[nodiscard]] Optional<PieceType> FindPromotion(StringView id);

}  // namespace BECore::Chess
