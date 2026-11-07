#pragma once

#include <Chess/Engine/OpponentController.h>
#include <Chess/Game/ChessGameModel.h>
#include <Games/IControllableGame.h>

namespace BECore::Chess {

    /// Error codes of Chess besides the shared ones (GameControlErrors, ControlErrors)
    namespace ChessControlErrors {
        /// The game is finished (checkmate, stalemate or a draw): it takes no move until reset or set_state
        inline constexpr eastl::string_view kGameOver = "game_over";
        /// The move is a pawn move onto the last rank and does not say what to promote to: "e7e8" instead of "e7e8q"
        inline constexpr eastl::string_view kPromotionRequired = "promotion_required";
        /// set_state asked for a computer opponent and there is no engine to make it (no Stockfish found; the own engine cannot fail): the pick is refused as a whole, so the position and the opponent
        /// stay as they were and the error is in the state
        inline constexpr eastl::string_view kEngineUnavailable = "engine_unavailable";
    }  // namespace ChessControlErrors

    /**
     * @brief The external control API of Chess: the position, the legal moves, a way to play one, to load a position and to pick the computer opponent.
     *
     * State: {"fen", "board", "turn", "status", "winner", "inCheck", "legalMoveCount", "startFen", "moves", "opponent", "material"}.
     * board is 8 strings, rank 8 first, file a first: white pieces are upper case (PNBRQK), black ones lower case, '.' is an
     * empty square. turn and winner are "white" or "black" (winner is null until a side has won). status is one of
     * "ongoing", "checkmate", "stalemate", "draw_insufficient_material", "draw_threefold", "draw_fifty_moves"; everything but
     * "ongoing" ends the game. startFen is the position the game began from and moves the UCI moves played since, so the pair
     * rebuilds the game (and its repetitions).
     *
     * opponent is {"plays", "level", "engine", "thinking", "error"}: plays is the color the PROGRAM plays ("white" or "black"; null for two people, the start of
     * every game), level the id of the strength ("easy", "medium", "hard", "max"; the one that would be used, also while plays is null), engine the id of the
     * program that plays ("stockfish", the start of every game, or "own", the engine of the game itself; also the one that would be used while plays is null;
     * one never stands in for the other), thinking whether the program is searching for its move, error null or {"kind", "detail"} (kind in snake_case:
     * "launch_failed", "timeout", "process_died", "protocol_error", "illegal_move", "no_move", "bad_request", "cancelled", "unsupported"; detail is the sentence
     * of the log). While an error stands the program does not play and waits for the version of the game to move (a reset, set_state, a move of someone else) or
     * for another pick. A pick of Stockfish succeeds whenever a Stockfish file is found (a launch that fails shows only at the first search, as error
     * "launch_failed"); a pick of "own" always succeeds.
     *
     * material is {"balance", "capturedByWhite", "capturedByBlack"}, derived from the position at every call (it has no version of its own; a move that
     * changes it changes the state version anyway): balance is White's points minus Black's on the board (pawn 1, knight 3, bishop 3, rook 5, queen 9; a
     * piece made by a promotion counts, so it is not a count of what was taken), positive when White is ahead; capturedByWhite is the Black pieces that are gone
     * and capturedByBlack the White ones, each {"queen", "rook", "bishop", "knight", "pawn"} with a count (never negative). Position-only: a pawn that
     * promoted counts as a pawn that is gone, and a promoted piece that is taken later shows as a missing pawn.
     *
     * Actions: {"type": "move", "uci": "e2e4"}, every legal move once; none once the game is over. The list does not change with the opponent: the program's
     * side is listed and accepted like the other (a controller may play for it; a search that the move makes stale is dropped). Castling is the king's move
     * ("e1g1") and a promotion has the piece in lower case ("e7e8q", "e7e8n"). PerformAction answers {"move", "status",
     * "turn", "winner", "inCheck"} and fails with game_over, promotion_required, illegal_action (well-formed, not legal) or
     * invalid_params (not a UCI move). Reset takes no options and keeps the opponent.
     *
     * SetState takes {"fen": ".."} (any position that Position::FromFen() accepts, which may be a finished game), {"opponent": {"plays": .., "level": ..,
     * "engine": ..}} (inside it a missing member means unchanged; plays is "white", "black" or null; level and engine an id, "stockfish" or "own" for the engine,
     * exactly as written) or both; neither is invalid_params. Everything is checked before anything changes, so a refused call (invalid_params) changes nothing.
     * The opponent is applied first, then the position. A pick that finds no engine (only Stockfish can fail) is engine_unavailable (with the sentence of the
     * engine's error) and is refused as a whole: the position is not replaced and the opponent is unchanged (side, level, engine and a running search stay as they
     * were), the error is in the state, so a client can ask again with "engine": "own". A pick that changes nothing (the same side, level and engine, no error standing) does nothing: a running
     * Stockfish is not restarted. With an error standing, the same "plays" asks for an engine again, and plays null clears it.
     *
     * The state version is the version of the game plus the revision of the opponent (a pick, an error that appears or goes), so a push reports a change of the
     * opponent as well; it never goes back.
     *
     * The model and the opponent belong to ChessGame, which owns this adapter, so both are always there.
     */
    class ChessGameControl : public IControllableGame {
    public:
        /// Both must outlive the adapter (ChessGame declares them before it)
        ChessGameControl(ChessGameModel& model, OpponentController& opponent);

        [[nodiscard]] std::expected<Json, ControlError> GetState() const override;
        [[nodiscard]] std::expected<Json, ControlError> GetActions() const override;
        std::expected<Json, ControlError> PerformAction(const Json& action) override;
        std::expected<void, ControlError> Reset(const Json& options) override;
        std::expected<void, ControlError> SetState(const Json& state) override;
        [[nodiscard]] uint64_t GetStateVersion() const override;

    private:
        /// The legal moves of the side to move; empty once the game is over
        [[nodiscard]] MoveList LegalMoves() const;

        [[nodiscard]] Json OpponentJson() const;

        ChessGameModel& _model;
        OpponentController& _opponent;
    };

}  // namespace BECore::Chess
