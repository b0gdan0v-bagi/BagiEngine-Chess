#pragma once

#include <Chess/Game/Position.h>

/// The board read cell by cell, so that a target can check what Position says about attacks and kings against a second, plain implementation (no bitboards, no tables)
namespace BECore::Chess::Fuzzing::BoardScan {

    inline constexpr int kBoardSide = 8;

    struct Step {
        int file;
        int rank;
    };

    inline constexpr Array<Step, 8> kKnightSteps{{{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}}};
    inline constexpr Array<Step, 8> kKingSteps{{{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}};
    inline constexpr Array<Step, 4> kRookDirections{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
    inline constexpr Array<Step, 4> kBishopDirections{{{1, 1}, {-1, 1}, {-1, -1}, {1, -1}}};

    [[nodiscard]] inline bool OnBoard(int file, int rank) {
        return file >= 0 && file < kBoardSide && rank >= 0 && rank < kBoardSide;
    }

    /// A piece of @p color and @p type stands on the cell
    [[nodiscard]] inline bool Holds(const Position& position, int file, int rank, PieceColor color, PieceType type) {
        if (!OnBoard(file, rank)) {
            return false;
        }
        const Piece piece = position.PieceAt(rank * kBoardSide + file);
        return !piece.IsEmpty() && piece.color == color && piece.type == type;
    }

    /// The squares of the pieces of @p by that attack @p target (a bit per square, a1 = bit 0), found by walking the cells; the pieces of @p by that are pinned still attack
    [[nodiscard]] inline uint64_t AttackersOf(const Position& position, int target, PieceColor by) {
        const int file = target % kBoardSide;
        const int rank = target / kBoardSide;
        uint64_t found = 0;
        const auto add = [&](int attackerFile, int attackerRank) { found |= uint64_t{1} << (attackerRank * kBoardSide + attackerFile); };

        for (const Step step : kKnightSteps) {
            if (Holds(position, file + step.file, rank + step.rank, by, PieceType::Knight)) {
                add(file + step.file, rank + step.rank);
            }
        }
        for (const Step step : kKingSteps) {
            if (Holds(position, file + step.file, rank + step.rank, by, PieceType::King)) {
                add(file + step.file, rank + step.rank);
            }
        }
        // A white pawn attacks the rank above it, so the one that attacks this cell stands on the rank below
        const int pawnRank = by == PieceColor::White ? rank - 1 : rank + 1;
        for (const int pawnFile : {file - 1, file + 1}) {
            if (Holds(position, pawnFile, pawnRank, by, PieceType::Pawn)) {
                add(pawnFile, pawnRank);
            }
        }
        const auto slide = [&](const Step& direction, PieceType slider) {
            for (int attackerFile = file + direction.file, attackerRank = rank + direction.rank; OnBoard(attackerFile, attackerRank); attackerFile += direction.file, attackerRank += direction.rank) {
                const Piece piece = position.PieceAt(attackerRank * kBoardSide + attackerFile);
                if (piece.IsEmpty()) {
                    continue;
                }
                if (piece.color == by && (piece.type == slider || piece.type == PieceType::Queen)) {
                    add(attackerFile, attackerRank);
                }
                return;
            }
        };
        for (const Step direction : kRookDirections) {
            slide(direction, PieceType::Rook);
        }
        for (const Step direction : kBishopDirections) {
            slide(direction, PieceType::Bishop);
        }
        return found;
    }

    /// The square of the one king of @p color, or -1 when there is none or more than one
    [[nodiscard]] inline int KingOf(const Position& position, PieceColor color) {
        int found = -1;
        for (int square = 0; square < kBoardSide * kBoardSide; ++square) {
            const Piece piece = position.PieceAt(square);
            if (piece.type == PieceType::King && piece.color == color) {
                if (found != -1) {
                    return -1;
                }
                found = square;
            }
        }
        return found;
    }

}  // namespace BECore::Chess::Fuzzing::BoardScan
