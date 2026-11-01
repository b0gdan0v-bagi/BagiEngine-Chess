#pragma once

#include <Chess/Game/ChessTypes.h>

namespace BECore::Chess {

    /// What a move does besides relocating a piece.
    CORE_ENUM(MoveKind, uint8_t, Normal, Promotion, EnPassant, Castling)

    /**
     * @brief A move packed into 16 bits: from (6) | to (6) | promotion piece (2) | kind (2).
     *
     * Castling is stored as the king's move (e1g1), which is also its UCI spelling; the rook follows from
     * the destination. The promotion piece is meaningful only for MoveKind::Promotion. A default-constructed
     * Move is the null move (a1a1), which is never legal.
     */
    class Move {
    public:
        constexpr Move() noexcept = default;

        constexpr Move(Square from, Square to, MoveKind kind = MoveKind::Normal, PieceType promotion = PieceType::Knight) noexcept
            : _data(static_cast<uint16_t>(from.Index() | (to.Index() << kToShift) | ((static_cast<int>(promotion) - kFirstPromotion) << kPromotionShift) | (static_cast<int>(kind) << kKindShift))) {}

        [[nodiscard]] constexpr Square From() const noexcept {
            return Square::FromIndex(FromIndex());
        }
        [[nodiscard]] constexpr Square To() const noexcept {
            return Square::FromIndex(ToIndex());
        }
        [[nodiscard]] constexpr int FromIndex() const noexcept {
            return _data & kSquareMask;
        }
        [[nodiscard]] constexpr int ToIndex() const noexcept {
            return (_data >> kToShift) & kSquareMask;
        }
        [[nodiscard]] constexpr MoveKind Kind() const noexcept {
            return static_cast<MoveKind>(_data >> kKindShift);
        }
        [[nodiscard]] constexpr bool IsPromotion() const noexcept {
            return Kind() == MoveKind::Promotion;
        }
        /// Knight, Bishop, Rook or Queen for a promotion; Knight for every other kind of move.
        [[nodiscard]] constexpr PieceType PromotionType() const noexcept {
            return static_cast<PieceType>(((_data >> kPromotionShift) & kPromotionMask) + kFirstPromotion);
        }

        [[nodiscard]] constexpr bool IsNull() const noexcept {
            return _data == 0;
        }
        [[nodiscard]] constexpr uint16_t Raw() const noexcept {
            return _data;
        }

        constexpr bool operator==(const Move& other) const noexcept = default;

        /// Long algebraic form used by UCI: "e2e4", "e7e8q"; "0000" for the null move.
        [[nodiscard]] eastl::string ToUci() const {
            eastl::string text;
            if (IsNull()) {
                text = "0000";
                return text;
            }
            AppendSquare(text, From());
            AppendSquare(text, To());
            if (IsPromotion()) {
                text.push_back(PromotionLetter(PromotionType()));
            }
            return text;
        }

    private:
        static constexpr int kToShift = 6;
        static constexpr int kPromotionShift = 12;
        static constexpr int kKindShift = 14;
        static constexpr int kSquareMask = 0x3F;
        static constexpr int kPromotionMask = 0x3;
        static constexpr int kFirstPromotion = static_cast<int>(PieceType::Knight);

        static void AppendSquare(eastl::string& text, Square s) {
            text.push_back(static_cast<char>('a' + s.file));
            text.push_back(static_cast<char>('1' + s.rank));
        }

        static constexpr char PromotionLetter(PieceType type) noexcept {
            switch (type) {
                case PieceType::Knight:
                    return 'n';
                case PieceType::Bishop:
                    return 'b';
                case PieceType::Rook:
                    return 'r';
                default:
                    return 'q';
            }
        }

        uint16_t _data = 0;
    };

}  // namespace BECore::Chess
