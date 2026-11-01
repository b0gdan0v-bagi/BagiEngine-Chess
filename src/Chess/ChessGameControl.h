#pragma once

#include <Chess/Game/ChessGameModel.h>
#include <Games/IControllableGame.h>

namespace BECore::Chess {

    /// Error codes of Chess besides the shared ones (GameControlErrors, ControlErrors)
    namespace ChessControlErrors {
        /// The game is finished (checkmate, stalemate or a draw): it takes no move until reset or set_state
        inline constexpr eastl::string_view kGameOver = "game_over";
        /// The move is a pawn move onto the last rank and does not say what to promote to: "e7e8" instead of "e7e8q"
        inline constexpr eastl::string_view kPromotionRequired = "promotion_required";
    }  // namespace ChessControlErrors

    /**
     * @brief The external control API of Chess: the position, the legal moves, a way to play one and to load a position.
     *
     * State: {"fen", "board", "turn", "status", "winner", "inCheck", "legalMoveCount", "startFen", "moves"}.
     * board is 8 strings, rank 8 first, file a first: white pieces are upper case (PNBRQK), black ones lower case, '.' is an
     * empty square. turn and winner are "white" or "black" (winner is null until a side has won). status is one of
     * "ongoing", "checkmate", "stalemate", "draw_insufficient_material", "draw_threefold", "draw_fifty_moves"; everything but
     * "ongoing" ends the game. startFen is the position the game began from and moves the UCI moves played since, so the pair
     * rebuilds the game (and its repetitions).
     *
     * Actions: {"type": "move", "uci": "e2e4"}, every legal move once; none once the game is over. Castling is the king's move
     * ("e1g1") and a promotion has the piece in lower case ("e7e8q", "e7e8n"). PerformAction answers {"move", "status",
     * "turn", "winner", "inCheck"} and fails with game_over, promotion_required, illegal_action (well-formed, not legal) or
     * invalid_params (not a UCI move). Reset takes no options. SetState takes {"fen": ".."}: any position that
     * Position::FromFen() accepts, which may be a finished game.
     *
     * The model belongs to ChessGame, which owns this adapter, so the model is always there.
     */
    class ChessGameControl : public IControllableGame {
    public:
        explicit ChessGameControl(ChessGameModel& model);

        [[nodiscard]] std::expected<Json, ControlError> GetState() const override;
        [[nodiscard]] std::expected<Json, ControlError> GetActions() const override;
        std::expected<Json, ControlError> PerformAction(const Json& action) override;
        std::expected<void, ControlError> Reset(const Json& options) override;
        std::expected<void, ControlError> SetState(const Json& state) override;
        [[nodiscard]] uint64_t GetStateVersion() const override;

    private:
        /// The legal moves of the side to move; empty once the game is over
        [[nodiscard]] MoveList LegalMoves() const;

        ChessGameModel& _model;
    };

}  // namespace BECore::Chess
