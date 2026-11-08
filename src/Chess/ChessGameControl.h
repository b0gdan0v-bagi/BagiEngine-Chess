#pragma once

#include <Chess/Engine/OpponentController.h>
#include <Chess/Engine/PositionAnalyst.h>
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
        /// set_state asked for a hint on a side that the program plays: a hint is for the person (HintRefusalFor). On a finished game the code is game_over
        inline constexpr eastl::string_view kNotYourTurn = "not_your_turn";
    }  // namespace ChessControlErrors

    /**
     * @brief The external control API of Chess: the position, the legal moves, a way to play one, to load a position and to pick the computer opponent.
     *
     * State: {"fen", "board", "turn", "status", "winner", "inCheck", "legalMoveCount", "startFen", "moves", "opponent", "material", "analysis"}.
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
     * and capturedByBlack the White ones, each {"queen", "rook", "bishop", "knight", "pawn"} with a count (never negative). Position-only: a piece beyond
     * the starting set is a promoted pawn (it counts in the points and is not a missing pawn), a pawn that promoted into a piece that was missing shows as
     * a pawn gone, and a promoted piece that is taken later shows as a missing pawn.
     *
     * analysis is {"evaluationOn", "hintRequested", "thinking", "evaluation", "hint", "error"} (the analysis of the position by the engine the person picked, never
     * handicapped, at the limits of the strongest level; PositionAnalyst): evaluationOn whether the evaluation is switched on (off at the start of every game), hintRequested
     * whether a hint was asked for the position now on the board (a hint belongs to its position: a move, a reset or a set_state drops the request), thinking whether a search runs.
     * evaluation is null while it is off or not yet computed for this position, else {"score": {"kind": "centipawns"|"mate", "value": int}, "text": "+0.35"|"#3", "depth": int}
     * **from White's view** (value positive: White is better, or White mates; the text is of the same score). hint is null until the answer for this very position has come, else
     * {"move": "e2e4", "text": .., "depth": ..}: the move, and the score in the view of the side to move (as the engine gave it). error is null or {"kind", "detail"} with the kinds of
     * opponent.error; it belongs to the position and the engine, and goes with the next move, the next pick of the engine or a new hint request.
     *
     * Actions: {"type": "move", "uci": "e2e4"}, every legal move once; none once the game is over. The list does not change with the opponent: the program's
     * side is listed and accepted like the other (a controller may play for it; a search that the move makes stale is dropped). Castling is the king's move
     * ("e1g1") and a promotion has the piece in lower case ("e7e8q", "e7e8n"). PerformAction answers {"move", "status",
     * "turn", "winner", "inCheck"} and fails with game_over, promotion_required, illegal_action (well-formed, not legal) or
     * invalid_params (not a UCI move). Reset takes no options and keeps the opponent.
     *
     * SetState takes {"fen": ".."} (any position that Position::FromFen() accepts, which may be a finished game), {"opponent": {"plays": .., "level": ..,
     * "engine": ..}} (inside it a missing member means unchanged; plays is "white", "black" or null; level and engine an id, "stockfish" or "own" for the engine,
     * exactly as written) or both; none of fen, opponent, analysis is invalid_params. Everything is checked before anything changes, so a refused call (invalid_params) changes nothing.
     * The opponent is applied first, then the position. SetState also takes {"analysis": {"evaluation": bool, "hint": bool}} (a missing member means unchanged; {} changes
     * nothing; "evaluation" switches the evaluation on or off, "hint": true asks for a hint for the position now on the board and "hint": false drops it); a wrong type or a member
     * that is not one of the two is invalid_params. **analysis cannot be combined with fen or opponent in one call** (invalid_params: the hint belongs to the position after them,
     * two calls say it plainly). A hint on the side that the program plays is not_your_turn, on a finished game game_over; nothing is changed then, the evaluation switch of the same call
     * included. list_actions is not changed by any of it. A pick that finds no engine (only Stockfish can fail) is engine_unavailable (with the sentence of the
     * engine's error) and is refused as a whole: the position is not replaced and the opponent is unchanged (side, level, engine and a running search stay as they
     * were), the error is in the state, so a client can ask again with "engine": "own". A pick that changes nothing (the same side, level and engine, no error standing) does nothing: a running
     * Stockfish is not restarted. With an error standing, the same "plays" asks for an engine again, and plays null clears it.
     *
     * The state version is the version of the game plus the revision of the opponent (a pick, an error that appears or goes) plus the revision of the analyst (a switch,
     * a hint asked or dropped, an answer that arrives, an error that appears or goes), so a push reports a change of the opponent and a hint that came as well; it never goes back.
     *
     * The model, the opponent and the analyst belong to ChessGame, which owns this adapter, so all three are always there. The analyst is told of a changed engine pick by SetState
     * at once, so a get_state right after never shows the old engine's answer.
     */
    class ChessGameControl : public IControllableGame {
    public:
        /// All three must outlive the adapter (ChessGame declares them before it)
        ChessGameControl(ChessGameModel& model, OpponentController& opponent, PositionAnalyst& analyst);

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
        [[nodiscard]] Json AnalysisJson() const;
        std::expected<void, ControlError> SetAnalysis(const Json& analysis);

        ChessGameModel& _model;
        OpponentController& _opponent;
        PositionAnalyst& _analyst;
    };

}  // namespace BECore::Chess
