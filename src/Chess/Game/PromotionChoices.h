#pragma once

#include <Chess/Game/ChessTypes.h>

namespace BECore::Chess {

    /// The pieces a pawn can become on the last rank, queen first (the order of the buttons of the promotion panel)
    [[nodiscard]] eastl::span<const PieceType> PromotionPieces();

    /// The name of a promotion piece in the scene and in the actions: "queen", "rook", "bishop", "knight" (lower case); empty for any other piece
    [[nodiscard]] eastl::string_view PromotionId(PieceType type);

    /// The word on the button of the promotion panel for a promotion piece: "Queen", "Rook", "Bishop", "Knight"; empty for any other piece
    [[nodiscard]] eastl::string_view PromotionLabel(PieceType type);

    /// The promotion piece with this id (exactly as written: ids are lower case), or nothing
    [[nodiscard]] std::optional<PieceType> FindPromotion(eastl::string_view id);

}  // namespace BECore::Chess
