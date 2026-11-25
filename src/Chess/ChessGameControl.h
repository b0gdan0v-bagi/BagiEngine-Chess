#pragma once

#include <Chess/Engine/DrawOfferJudge.h>
#include <Chess/Engine/OpponentController.h>
#include <Chess/Engine/PositionAnalyst.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/OpeningBook.h>
#include <Chess/Game/PositionEditor.h>
#include <Chess/Game/PuzzleSession.h>
#include <Games/IControllableGame.h>

namespace BECore::Chess {

    /// Error codes of Chess besides the shared ones (GameControlErrors, ControlErrors)
    namespace ChessControlErrors {
        /// The game is finished (checkmate, stalemate, a draw or a resignation): it takes no move and no resignation until reset or set_state
        inline constexpr StringView kGameOver = "game_over";
        /// The move is a pawn move onto the last rank and does not say what to promote to: "e7e8" instead of "e7e8q"
        inline constexpr StringView kPromotionRequired = "promotion_required";
        /// set_state asked for a computer opponent and there is no engine to make it (no Stockfish found; the own engine cannot fail): the pick is refused as a whole, so the position and the opponent
        /// stay as they were and the error is in the state
        inline constexpr StringView kEngineUnavailable = "engine_unavailable";
        /// set_state asked for a hint on a side that the program plays (either side while two programs play, the reply of a puzzle): a hint is for the person (HintRefusalFor). On a finished game the
        /// code is game_over
        inline constexpr StringView kNotYourTurn = "not_your_turn";
    }  // namespace ChessControlErrors

    /**
     * @brief The external control API of Chess: the position, the legal moves, a way to play one, to load a position and to pick the computer opponent.
     *
     * State: {"fen", "board", "turn", "status", "winner", "inCheck", "legalMoveCount", "startFen", "moves", "sanMoves", "opening", "takeBackPlies", "opponent", "material", "analysis", "drawOffer",
     * "editor", "puzzle", "review"}. board is 8 strings, rank 8 first, file a first: white pieces are upper case (PNBRQK), black ones lower case, '.' is an empty square. turn and winner are "white"
     * or "black" (winner is null until a side has won). status is one of "ongoing", "checkmate", "stalemate", "draw_insufficient_material", "draw_threefold", "draw_fifty_moves", "resigned",
     * "draw_agreed"; everything but "ongoing" ends the game. After a resignation winner is the other color; an agreed draw has none. startFen is the position the game began from and moves the UCI
     * moves played since, so the pair rebuilds the game (and its repetitions); sanMoves is the same moves in standard algebraic notation ("e4", "Nf3", "O-O", "e8=Q#"), one per entry of moves, as the
     * move list shows them. takeBackPlies is how many moves {"takeBack": {}} (and the button of the move list) would take back now, 0 when nothing can be (see SetState). What the move list of the
     * screen views (an earlier position) is not in the state: like the selection it is the person's look, not the game, and it moves no version.
     *
     * opponent is {"plays", "level", "engine", "seats", "seed", "thinking", "error"}: plays is the color the PROGRAM plays ("white" or "black"; "both" while two programs play
     * each other; null for two people, the start of every game), level the id of the strength ("easy", "medium", "hard", "max"; the one that would be used, also while
     * plays is null), engine the id of the program that plays ("stockfish", the start of every game when a Stockfish file is found, else "own", the engine of the game itself; also the one that would
     * be used while plays is null; one never stands in for the other); level and engine are those of the seat that plays when exactly one plays, else of White's seat. seats is
     * {"white": {"engine", "level"}, "black": {"engine", "level"}}, always there: the choice of each color, also for a color that nobody plays. seed is the integer 0..4294967295 of the game
     * that varies the own engine's pick among near-equal moves and the choice and move of every handicap (not Stockfish's own picks): 0 is no variety (every engine plays as it always did), a
     * reset and a takeback draw a new one, a set_state fen keeps it; the same position, seed and depth give the same answer. thinking is whether a program
     * (either) is searching for its move, error null or {"kind", "detail"} (kind in snake_case:
     * "launch_failed", "timeout", "process_died", "protocol_error", "illegal_move", "no_move", "bad_request", "cancelled", "unsupported"; detail is the sentence
     * of the log). While an error stands the program does not play and waits for the version of the game to move (a reset, set_state, a move of someone else) or
     * for another pick. A pick of Stockfish succeeds whenever a Stockfish file is found and is engine_unavailable when none is (a launch that fails shows only at the first search, as error
     * "launch_failed"); a pick of "own" always succeeds.
     *
     * opening is {"eco", "name"} (OpeningBook: "B90", "Sicilian Defense: Najdorf Variation", the full name of the table, not the HUD's cut one) or null: the opening of the **game's** position, not
     * of the one the screen views (like sanMoves), derived from startFen and moves at every call (it has no version of its own). It is the name of the last position of the game that the book
     * knows, so it stays once the game has left the book, and a transposition has the name of its position; null when no position of the game is in the book (the start of every game, a
     * set_state fen outside it) and when the game has no book (its file is missing).
     *
     * material is {"balance", "capturedByWhite", "capturedByBlack"}, derived from the position at every call (it has no version of its own): balance is White's points minus Black's on the board (pawn
     * 1, knight 3, bishop 3, rook 5, queen 9; a piece made by a promotion counts, so it is not a count of what was taken), positive when White is ahead; capturedByWhite is the Black pieces that are
     * gone and capturedByBlack the White ones, each {"queen", "rook", "bishop", "knight", "pawn"} with a count (never negative). Position-only: a piece beyond the starting set is a promoted pawn (it
     * counts in the points and is not a missing pawn), and a promoted piece that is taken later shows as a missing pawn.
     *
     * analysis is {"evaluationOn", "hintRequested", "thinking", "evaluation", "hint", "lines", "ratingsOn", "ratings", "error"} (PositionAnalyst: the engine the person picked, never
     * handicapped, at the limits of the strongest level): evaluationOn whether the evaluation is switched on (off at the start of every game), hintRequested
     * whether a hint was asked for the position now on the board (a hint belongs to its position: a move, a reset or a set_state drops the request), thinking whether a search runs.
     * evaluation is null while it is off or not yet computed for this position, else {"score": {"kind": "centipawns"|"mate", "value": int}, "text": "+0.35"|"#3", "depth": int}
     * **from White's view** (value positive: White is better, or White mates; the text is of the same score). hint is null until the answer for this very position has come, else
     * {"move": "e2e4", "text": .., "depth": ..}: the move, and the score in the view of the side to move (as the engine gave it). lines is an array, best first, of
     * {"move": "e2e4", "score": {"kind", "value"}, "text", "depth"} (the engine is asked for two lines): the first is the line of the evaluation and of the hint, the second the next best
     * move and how the position stands if it is played; a position with one legal move has one line. **The scores are from White's view, as for evaluation** (not the side to move's
     * as in hint.text), so a consumer that compares the two lines (the margin between the best and the second move) subtracts them and flips the sign for Black. The array is empty
     * unless the evaluation is on or a hint was asked for this position, and the answer for this very position has come; it goes with the hint (a move, a reset, a set_state, a
     * change of the engine pick) and nothing starts only to fill it. error is null or {"kind", "detail"} with the kinds of
     * opponent.error; it belongs to the position and the engine, and goes with the next move, the next pick of the engine or a new hint request.
     * ratingsOn is whether the moves are being rated (off at the start of every game, it costs CPU; a reset keeps the switch). ratings has one entry per move of "moves", in the same
     * order: null while the move is not rated, else {"brilliant": bool, "reason": "brilliant"|"not_best"|"forced"|"obvious"} (MoveRater, MoveRating.h). A move is brilliant when it is the
     * best move of the own engine at depth 5 in the position it was played in (the engine's first line before the move) and it was a choice: "forced" is a position with one legal move,
     * "obvious" one where the second line is 200 centipawns or more behind (a forced recapture, a mate in one); "not_best" is any other move. Rated in the background, the oldest move first, always by
     * the own engine, whatever opponent.engine is, so the same game gets the same ratings twice; the ratings of earlier moves stay after a new move, and after a takeback those of the moves that
     * remain stay; they go with a reset or a set_state of a fen. Independent of hintRequested, of the evaluation and of the turn of the person. The rater's failure (an engine that cannot be made, a
     * search that fails) is not in analysis.error: it is logged, and the move stays null.
     *
     * Actions: {"type": "move", "uci": "e2e4"}, every legal move once; none once the game is over. The list does not change with the opponent: the program's
     * side is listed and accepted like the other (a controller may play for it; a search that the move makes stale is dropped). Castling is the king's move
     * ("e1g1") and a promotion has the piece in lower case ("e7e8q", "e7e8n"). PerformAction answers {"move", "status",
     * "turn", "winner", "inCheck"} and fails with game_over, promotion_required, illegal_action (well-formed, not legal) or
     * invalid_params (not a UCI move). Reset takes no options, keeps the opponent and draws a new seed (a client that compares answers across resets sets the seed after each).
     *
     * SetState takes {"fen": ".."} (any position that Position::FromFen() accepts, which may be a finished game), {"opponent": {"plays": .., "level": ..,
     * "engine": .., "white": {"engine": .., "level": ..}, "black": {..}, "seed": n}} (inside it a missing member means unchanged, per seat; plays is "white", "black", "both" or null;
     * seed is an integer 0..4294967295, anything else is invalid_params, and it is a valid pick alone; a seed that equals the current one changes nothing;
     * level and engine an id, "stockfish" or "own" for the engine, exactly as written) or both; none of fen, opponent, analysis, takeBack, resign, draw, puzzle, review is invalid_params. level and
     * engine set BOTH seats; the seat objects "white" and "black" (members optional, nothing else allowed in them) then override one seat; plays says who plays and a missing plays keeps the sides as
     * they are. "both" starts a game of two programs, each with an engine object of its own; "plays": "white" while both play is a takeover (the person has Black, White's program and its search
     * stay), null ends it. Everything is checked before anything changes, so a refused call (invalid_params) changes nothing. The opponent (the seed with it, after a pick that can fail) is applied
     * first, then the position. SetState also takes {"analysis": {"evaluation": bool, "hint": bool, "ratings": bool}} (a missing member means unchanged; {} changes nothing; "evaluation" switches the
     * evaluation on or off, "hint": true asks for a hint for the position now on the board and "hint": false drops it, "ratings" switches the rating of the moves, also on a finished game); a wrong
     * type or a member that is not one of the three is invalid_params. **analysis cannot be combined with fen or opponent in one call** (invalid_params: the hint belongs to the position after them,
     * two calls say it plainly). A hint on a side that the program plays (with a puzzle on, any turn but the solver's; so on either turn while two programs play) is not_your_turn, on a finished game
     * game_over; nothing is changed then, the evaluation and ratings switches of the same call included. list_actions is not changed by any of it. A pick that finds no engine (only Stockfish can
     * fail) is engine_unavailable (with the sentence of the engine's error) and is refused as a whole, whichever seat has no engine: the position is not replaced and the opponent is unchanged (sides,
     * seats and a running search stay as they were), the error is in the state, so a client can ask again with "engine": "own". A pick that changes nothing (the same sides and seats, no error
     * standing) does nothing: a running Stockfish is not restarted. With an error standing, the same "plays" asks for an engine again, and plays null clears it. Without Stockfish (no Stockfish file
     * found when the pick is made, or a CHESS_NO_STOCKFISH build) "stockfish" is engine_unavailable in "engine" and in the seat objects, also while nobody plays, and the default engine is "own" (a
     * game that starts with no file found shows "own").
     *
     * SetState also takes {"takeBack": {"plies": n}} **alone** (combined with fen, opponent or analysis it is invalid_params): takes back the last n moves in one step, plies 1 up to the
     * moves played; without plies, what the person's takeback button takes (takeBackPlies: one move with two people or two programs; against the program the person's last move with its
     * answer, or only the person's move while the program is to move). Nothing can be taken back (no move; the only move is the program's) or a plies out of range is invalid_params and
     * changes nothing. A takeback is a change of the game: the version moves, a finished game is on again, a running search of the program and a standing opponent error go at the next frame,
     * and the program answers again when it is its turn; the hint goes and the kept moves are rated again. A new seed is drawn as at a reset, so the own engine may answer otherwise among
     * near-equal moves (a client that compares answers across a takeback sets the seed after it).
     *
     * SetState also takes {"resign": {"color": "white"|"black"}} **alone** (combined with another member it is invalid_params): the color ends the game as "resigned" and the other one wins. color
     * is optional when it follows from the opponent: against the program (it plays one color) the person's color, whoever is to move; with two people the side to move; with two programs there
     * is no such color and a missing one is invalid_params. A color that is not "white" or "black", another member inside resign or a resign that is not an object is invalid_params and changes
     * nothing; a finished game is game_over. The version moves once; takeBack puts the game on again (a resignation is not in the moves), list_actions is empty after it, as after a mate.
     *
     * SetState also takes {"draw": {}} **alone** (a member inside it, or draw combined with another member, is invalid_params): the person offers a draw. With two people the game ends at once
     * as "draw_agreed" (the client stands for both). Against the program (it plays one color) the call answers ok and the own engine judges the position in the background (the verdict comes
     * with the frames, not with the call): drawOffer is {"state": "pending"} while it judges, then either status "draw_agreed" (no winner; drawOffer null again) or {"state": "declined"}, which
     * stays until the version of the game moves (a move, a reset, a takeback, set_state). The program accepts when its own score for the position is at most 50 centipawns in its favour; a
     * mate against it accepts, a mate for it refuses, and a search that fails declines. An offer for a position that was offered already (pending or declined) changes nothing. With two
     * programs, and while the program is to move (the offer is the person's, made on their turn), draw is invalid_params; a finished game is game_over. drawOffer is null when there is no offer
     * for the position now on the board.
     *
     * editor is null unless the person is editing a position in the window (Menu, Position), else {"fen", "error", "tool"}: fen the draft's text (en passant "-", clocks "0 1"; a position the rules
     * refuse included), error null or the short reason "Play from here" would give (EditorText), tool {"piece": "king"|"queen"|"rook"|"bishop"|"knight"|"pawn"|"erase", "color": "white"|"black"|null}
     * (null for the eraser). The draft is not the game: fen, board, moves, turn and the rest above stay the game's, a draft moves no version of the game, and the game is played from it only by
     * "Play from here". A client edits it with ui_click; there is no set_state for it, and a set_state fen, a reset, a move or a takeback ends it (editor null again).
     *
     * puzzle is null unless a puzzle is on (PuzzleSession::IsActiveFor(): the game is the puzzle's line, judged from the game at every call, so a reset or a set_state fen makes it null at
     * once), else {"id", "index", "count", "rating", "themes", "solver", "state", "mistakes", "wrong", "step", "steps"}: id the lichess id, index the place in the set (ordered by rating, ascending)
     * and count its size, themes an array of words, solver "white" or "black" (the person's color: the side to move after the program's setup move, which is the first move of moves; startFen
     * is the row's position before it), state "your_move", "replying" (the program's answer waits for its short delay of game time) or "solved" (the line is complete or the person gave a mate),
     * mistakes how many wrong moves were taken back since the puzzle began, wrong whether the last move of the person was one of them (until the next move), step the person's right moves
     * on the board and steps the person's moves of the line. The person's move is read as it stands (step and state follow it at once); a wrong one is taken back at the next frame (mistakes, wrong),
     * and the program's reply is played after kPuzzleReplySeconds of game time of the frames, so a client steps the clock. While a puzzle is on takeBackPlies is 0, the opponent gets no frame (its
     * pick is kept and used again after the puzzle) and the game is not saved.
     *
     * review is null unless a review of the game is held (PositionAnalyst, GameReviewer.h), else {"state", "done", "total", "depth", "positions", "moves", "error"}: state "running", "done" or
     * "failed", done how many positions the own engine has evaluated of total (the positions to search: the moves plus one, minus the last one when it has no legal move), depth the fixed depth
     * of every search (5; one line per position, no move time, so the same game gives the same review twice), error null or {"kind", "detail"} with the kinds of opponent.error (a failed
     * review stops, keeps what came and is started again by start). positions has one entry per position of the game, the start first (the moves plus one), null until computed, else
     * {"score": {"kind", "value"}, "text", "best": "e2e4"|null}: the evaluation of the position **from White's view** (as analysis.evaluation) and the engine's best move there; the last position
     * of a game that ended with no legal move is not searched and is there from the start: checkmate is {"kind": "mate", "value": 0} ("#0"; the side to move, turn, is the mated one) and stalemate
     * centipawns 0, and best is null. moves has one entry per move of "moves", null until both positions the move touches are evaluated, else {"loss": cp, "quality", "best": "e2e4"}: loss is how many
     * centipawns the move gave away against the best move of its position (0 for the best move and never negative; both scores are clamped to +-1000 centipawns first, a mate included, so a mate in 3
     * against a mate in 5 is no loss, and a mate thrown away for +300 is 700), quality "best" (the best move), "good" (a loss under 50), "inaccuracy" (under 100), "mistake" (under 300) or
     * "blunder"; best is the engine's best move in the position the move was played in (the played move when quality is "best"). A review is of one exact game, a finished one with moves: any
     * change of the moves or the start position (a takeback, a reset, set_state fen) makes review null at once, and so does a cancel. It runs in the background while the frames run, always by
     * the own engine whatever opponent.engine is, and never delays the analysis of the position.
     *
     * The state version is the version of the game (a changed seed moves it) plus the revision of the opponent (a pick, an error that appears or goes) plus the revision of the analyst (a switch of
     * the evaluation or the ratings, a hint asked or dropped, an answer or a rating that arrives or is dropped, an error that appears or goes, also the rater's, which is not in get_state, and the
     * review's: a start, a position that arrives, a cancel or a drop, a failure) plus that of the draw offer (an offer, its verdict, a drop) plus the puzzle's (a begin, a verdict, a mistake, the
     * reply, a retry, an end) plus the editor's count of changes (every change of a draft, its tool, its start and its cancelling; a count of all the drafts of the run, so it does not fall when a
     * draft ends), so a push reports a change of the opponent, a hint, a rating, a verdict or a draft that came as well; it never goes back.
     *
     * SetState also takes {"puzzle": {...}} **alone**, the object with exactly one member: {"index": n} or {"id": ".."} begins that puzzle (a new game from its position and the setup move; one
     * that is on is replaced), {"next": true} the one after it (the first after the last), {"retry": true} the same one again (mistakes 0) and {"end": true} ends it with a plain reset of the
     * game (the opponent as picked, a new seed); next, retry and end need a puzzle on, and false for them is not a way to say nothing. An index outside the set, an unknown id, an empty set, no
     * puzzle on for next, retry or end, a wrong type, another or a second member, or puzzle combined with another member is invalid_params and changes nothing. **While a puzzle is on takeBack,
     * resign and draw are invalid_params** (each would break the line); the call sets no opponent, and list_actions and perform_action are as always (a move for the program's side is
     * accepted, the judge decides at the frames).
     *
     * SetState also takes {"review": {"start": true}} or {"review": {"cancel": true}} **alone** (the object with exactly one member whose value is true; false, another or a second member, a wrong
     * type or review combined with another member is invalid_params): start begins the review of the finished game; a game that is not finished, a game with no moves or a puzzle on is invalid_params
     * with the reason in the message, and nothing changes. Asking again while it runs or is done changes nothing, after "failed" it starts again from the first position. cancel drops the review
     * with what came; with no review it is ok and does nothing.
     *
     * The analyst is told of a changed engine pick by SetState at once, so a get_state right after never shows the old engine's answer.
     */
    class ChessGameControl : public IControllableGame {
    public:
        /// All seven must outlive the adapter (ChessGame declares them before it); the editor is read only, and is a draft the model shows (ChessGameModel::IsDrafting()) or not; the puzzle
        /// session judges @p model
        ChessGameControl(ChessGameModel& model, OpponentController& opponent, PositionAnalyst& analyst, DrawOfferJudge& drawJudge, const OpeningBook& openings, const PositionEditor& editor,
                         PuzzleSession& puzzle);

        [[nodiscard]] std::expected<void, ControlError> WriteState(JsonWriter& writer) const override;
        [[nodiscard]] std::expected<void, ControlError> WriteActions(JsonWriter& writer) const override;
        std::expected<void, ControlError> PerformAction(JsonView action, JsonWriter& outcome) override;
        std::expected<void, ControlError> Reset(JsonView options) override;
        std::expected<void, ControlError> SetState(JsonView state) override;
        [[nodiscard]] uint64_t GetStateVersion() const override;

    private:
        /// Empty once the game is over
        [[nodiscard]] MoveList LegalMoves() const;

        /// Each writes one value; the keys go in byte order, as the old tree sorted them, so the bytes of every answer stay
        void WriteOpponent(JsonWriter& writer) const;
        void WriteAnalysis(JsonWriter& writer) const;
        void WriteDrawOffer(JsonWriter& writer) const;
        void WriteEditor(JsonWriter& writer) const;
        void WritePuzzle(JsonWriter& writer) const;
        void WriteReview(JsonWriter& writer) const;
        std::expected<void, ControlError> SetAnalysis(JsonView analysis);
        std::expected<void, ControlError> SetTakeBack(JsonView takeBack);
        std::expected<void, ControlError> SetResign(JsonView resign);
        std::expected<void, ControlError> SetDraw(JsonView draw);
        std::expected<void, ControlError> SetPuzzle(JsonView puzzle);
        std::expected<void, ControlError> SetReview(JsonView review);

        ChessGameModel& _model;
        OpponentController& _opponent;
        PositionAnalyst& _analyst;
        DrawOfferJudge& _drawJudge;
        const OpeningBook& _openings;
        const PositionEditor& _editor;
        PuzzleSession& _puzzle;
    };

}  // namespace BECore::Chess
