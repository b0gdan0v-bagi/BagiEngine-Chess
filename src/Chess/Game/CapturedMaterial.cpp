#include "CapturedMaterial.h"

#include <Chess/Game/Bitboard.h>

namespace BECore::Chess {

    namespace {

        struct CapturableEntry {
            PieceType type;
            eastl::string_view id;
            int startCount;
        };

        constexpr int kPawnStartCount = 8;

        constexpr eastl::array<CapturableEntry, 5> kCapturable{{
            {PieceType::Queen, "queen", 1},
            {PieceType::Rook, "rook", 2},
            {PieceType::Bishop, "bishop", 2},
            {PieceType::Knight, "knight", 2},
            {PieceType::Pawn, "pawn", kPawnStartCount},
        }};

        constexpr eastl::array<PieceType, 5> kCapturableTypes{kCapturable[0].type, kCapturable[1].type, kCapturable[2].type, kCapturable[3].type, kCapturable[4].type};

        constexpr int kMaxBalanceShown = 99;

        constexpr size_t Index(PieceColor color) {
            return static_cast<size_t>(color);
        }

        constexpr size_t Index(PieceType type) {
            return static_cast<size_t>(type);
        }

    }  // namespace

    int MaterialPoints(PieceType type) {
        switch (type) {
            case PieceType::Pawn:
                return 1;
            case PieceType::Knight:
            case PieceType::Bishop:
                return 3;
            case PieceType::Rook:
                return 5;
            case PieceType::Queen:
                return 9;
            case PieceType::King:
            case PieceType::None:
                return 0;
        }
        return 0;
    }

    eastl::span<const PieceType> CapturableTypes() {
        return kCapturableTypes;
    }

    eastl::string_view CapturedId(PieceType type) {
        for (const CapturableEntry& entry : kCapturable) {
            if (entry.type == type) {
                return entry.id;
            }
        }
        return {};
    }

    std::optional<PieceType> FindCaptured(eastl::string_view id) {
        for (const CapturableEntry& entry : kCapturable) {
            if (entry.id == id) {
                return entry.type;
            }
        }
        return std::nullopt;
    }

    int MaterialSummary::GetMissing(PieceColor color, PieceType type) const {
        return _missing[Index(color)][Index(type)];
    }

    int MaterialSummary::GetPoints(PieceColor color) const {
        return _points[Index(color)];
    }

    int MaterialSummary::GetBalance() const {
        return GetPoints(PieceColor::White) - GetPoints(PieceColor::Black);
    }

    MaterialSummary SummarizeMaterial(const Position& position) {
        MaterialSummary summary;
        for (const PieceColor color : {PieceColor::White, PieceColor::Black}) {
            int promoted = 0;  // pieces beyond the starting set: each one is a pawn that is not a pawn any more
            int pawns = 0;
            for (const CapturableEntry& entry : kCapturable) {
                const int onBoard = PopCount(position.Pieces(color, entry.type));
                summary._points[Index(color)] += onBoard * MaterialPoints(entry.type);
                if (entry.type == PieceType::Pawn) {
                    pawns = onBoard;
                    continue;
                }
                summary._missing[Index(color)][Index(entry.type)] = eastl::max(0, entry.startCount - onBoard);
                promoted += eastl::max(0, onBoard - entry.startCount);
            }
            summary._missing[Index(color)][Index(PieceType::Pawn)] = eastl::max(0, kPawnStartCount - pawns - promoted);
        }
        return summary;
    }

    eastl::string BalanceText(int pointsAhead) {
        if (pointsAhead <= 0) {
            return {};
        }
        return Format("+{}", eastl::min(pointsAhead, kMaxBalanceShown));
    }

}  // namespace BECore::Chess
