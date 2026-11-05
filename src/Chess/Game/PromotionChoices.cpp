#include "PromotionChoices.h"

namespace BECore::Chess {

    namespace {

        struct PromotionEntry {
            PieceType type;
            eastl::string_view id;
            eastl::string_view label;
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

    eastl::string_view PromotionId(PieceType type) {
        const PromotionEntry* entry = Find(type);
        return entry ? entry->id : eastl::string_view{};
    }

    eastl::string_view PromotionLabel(PieceType type) {
        const PromotionEntry* entry = Find(type);
        return entry ? entry->label : eastl::string_view{};
    }

    std::optional<PieceType> FindPromotion(eastl::string_view id) {
        for (const PromotionEntry& entry : kPromotions) {
            if (entry.id == id) {
                return entry.type;
            }
        }
        return std::nullopt;
    }

}  // namespace BECore::Chess
