#pragma once

#include <Chess/Game/ChessTypes.h>
#include <Chess/Game/Position.h>

namespace BECore::Chess {

    /// The value of a piece in points, the way people count material: pawn 1, knight 3, bishop 3, rook 5, queen 9; the king and an empty cell 0.
    /// Not the centipawns of Engine/Evaluation.h, which are tuned for the search (knight and bishop differ there) and which Game/ must not depend on
    [[nodiscard]] int MaterialPoints(PieceType type);

    /// The pieces that can be taken, in the order of the rows of the material display: queen, rook, bishop, knight, pawn
    [[nodiscard]] eastl::span<const PieceType> CapturableTypes();

    /// The name of a capturable piece in the scene and in the control API: "queen", "rook", "bishop", "knight", "pawn" (lower case); empty for any other piece
    [[nodiscard]] eastl::string_view CapturedId(PieceType type);

    /// The capturable piece with this id (exactly as written: ids are lower case), or nothing
    [[nodiscard]] std::optional<PieceType> FindCaptured(eastl::string_view id);

    /**
     * @brief What a position says about the material: which pieces of each color are gone and how many points each side has on the board.
     *
     * Position-only: the history is not known, so a promoted pawn is a pawn that is gone (and the new piece is on the board), and a promoted piece that is
     * taken later shows as a missing pawn, not as a missing queen. Counts are never negative, also for a position the rules cannot reach.
     */
    class MaterialSummary {
    public:
        /// How many pieces of @p type of @p color are gone (taken, or turned into another piece by a promotion; for a pawn the promotions are not counted twice).
        /// 0 for a type that is not capturable
        [[nodiscard]] int GetMissing(PieceColor color, PieceType type) const;

        /// The points of the pieces @p color has on the board (the king does not count; a piece made by a promotion does)
        [[nodiscard]] int GetPoints(PieceColor color) const;

        /// White's points minus Black's: positive when White is ahead
        [[nodiscard]] int GetBalance() const;

    private:
        friend MaterialSummary SummarizeMaterial(const Position& position);

        eastl::array<eastl::array<int, 7>, 2> _missing{};
        eastl::array<int, 2> _points{};
    };

    /// The material of @p position: the pieces each side started with minus those on the board, promotions counted right
    [[nodiscard]] MaterialSummary SummarizeMaterial(const Position& position);

    /// The text of the lead of one side: "+N" for 1..99, "+99" for more, "" for no lead (zero or a deficit). The caller passes the lead of the side whose box this is
    [[nodiscard]] eastl::string BalanceText(int pointsAhead);

}  // namespace BECore::Chess
