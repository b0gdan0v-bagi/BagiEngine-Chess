#include "MoveList.h"

#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/MoveNotation.h>
#include <EASTL/algorithm.h>

namespace BECore::Chess {

    namespace {

        bool IsLegal(const Position& position, Move move) {
            const MoveList legal = MoveGenerator::GenerateLegal(position);
            return eastl::find(legal.begin(), legal.end(), move) != legal.end();
        }

    }  // namespace

    Vector<MoveListRow> BuildMoveList(StringView startFen, const Vector<Move>& moves) {
        Vector<MoveListRow> rows;
        auto parsed = Position::FromFen(startFen);
        if (!parsed) {
            return rows;
        }
        Position position = *parsed;

        for (size_t ply = 0; ply < moves.size(); ++ply) {
            const Move move = moves[ply];
            if (!IsLegal(position, move)) {
                break;
            }
            MoveListEntry entry{SanOf(position, move), static_cast<int>(ply)};
            if (position.SideToMove() == PieceColor::White) {
                rows.push_back(MoveListRow{position.FullmoveNumber(), eastl::move(entry), {}});
            } else {
                if (rows.empty()) {
                    rows.push_back(MoveListRow{position.FullmoveNumber(), {}, {}});
                }
                rows.back().black = eastl::move(entry);
            }
            position.DoMove(move);
        }
        return rows;
    }

}  // namespace BECore::Chess
