#pragma once

#include <Chess/Game/ChessTypes.h>
#include <Chess/Game/Position.h>

namespace BECore::Chess {

    /// The value of a piece in points, the way people count material; the king and an empty cell 0.
    /// Not the centipawns of Engine/Evaluation.h, which are tuned for the search and which Game/ must not depend on
    [[nodiscard]] int MaterialPoints(PieceType type);

    /// The pieces that can be taken, in the order of the rows of the material display (queen first)
    [[nodiscard]] Span<const PieceType> CapturableTypes();

    /// The name of a capturable piece in the scene and in the control API (lower case); empty for any other piece
    [[nodiscard]] StringView CapturedId(PieceType type);

    /// The capturable piece with this id (exactly as written: ids are lower case), or nothing
    [[nodiscard]] Optional<PieceType> FindCaptured(StringView id);

    /**
     * @brief What a position says about the material: which pieces of each color are gone and how many points each side has on the board.
     *
     * Position-only: the history is not known, so a piece beyond the starting set is a promoted pawn (it counts in the points and is not a missing pawn),
     * a pawn that promoted into a piece that was missing shows as a pawn that is gone, and a promoted piece that is taken later shows as a missing pawn.
     * Counts are never negative, also for a position the rules cannot reach.
     */
    class MaterialSummary {
    public:
        /// How many pieces of @p type of @p color are gone: the starting set minus the board (for a pawn also minus the promoted ones). 0 for a type that is not capturable
        [[nodiscard]] int GetMissing(PieceColor color, PieceType type) const;

        /// The points of the pieces @p color has on the board (a piece made by a promotion counts)
        [[nodiscard]] int GetPoints(PieceColor color) const;

        /// White's points minus Black's
        [[nodiscard]] int GetBalance() const;

    private:
        friend MaterialSummary SummarizeMaterial(const Position& position);

        Array<Array<int, 7>, 2> _missing{};
        Array<int, 2> _points{};
    };

    [[nodiscard]] MaterialSummary SummarizeMaterial(const Position& position);

    /// The text of the lead of one side: "+N" capped at 99, "" for no lead (zero or a deficit). The caller passes the lead of the side whose box this is
    [[nodiscard]] String BalanceText(int pointsAhead);

}  // namespace BECore::Chess
