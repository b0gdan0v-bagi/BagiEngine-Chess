#include "PromotionChoices.h"

namespace BECore::Chess {

    namespace {

        struct PromotionEntry {
            PieceType type;
            StringView id;
            StringView label;
        };

        constexpr eastl::array<PromotionEntry, 4> kPromotions{{
            {PieceType::Queen, "queen", "Queen"},
            {PieceType::Rook, "rook", "Rook"},
            {PieceType::Bishop, "bishop", "Bishop"},
            {PieceType::Knight, "knight", "Knight"},
        }};

        constexpr eastl::array<PieceType, 4> kPromotionPieces{kPromotions[0].type, kPromotions[1].type, kPromotions[2].type, kPromotions[3].type};

        const PromotionEntry* Find(PieceType type) {
            for (const PromotionEntry& entry : kPromotions) {
                if (entry.type == type) {
                    return &entry;
                }
            }
            return nullptr;
        }

    }  // namespace

    eastl::span<const PieceType> PromotionPieces() {
        return kPromotionPieces;
    }

    StringView PromotionId(PieceType type) {
        const PromotionEntry* entry = Find(type);
        return entry ? entry->id : StringView{};
    }

    StringView PromotionLabel(PieceType type) {
        const PromotionEntry* entry = Find(type);
        return entry ? entry->label : StringView{};
    }

    Optional<PieceType> FindPromotion(StringView id) {
        for (const PromotionEntry& entry : kPromotions) {
            if (entry.id == id) {
                return entry.type;
            }
        }
        return std::nullopt;
    }

}  // namespace BECore::Chess
