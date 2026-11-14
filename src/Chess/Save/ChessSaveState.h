#pragma once

#include <BECore/Save/SaveGame.h>
#include <Chess/Engine/OpponentChoice.h>
#include <Chess/Game/ChessGameModel.h>

namespace BECore::Chess {

    /// What Chess writes into a save: the game id and the version of ChessSaveState. A new field needs no version (a file without it keeps the default); raise `version` when the meaning of a
    /// field changes and `minVersion` when an old file can no longer be read
    inline constexpr SaveSchema kChessSaveSchema{"Chess", 1, 1};

    /// The one slot of the game
    inline constexpr StringView kChessSaveSlot = "game";

    /**
     * @brief The game and the opponent choice as a save file holds them. Ids and text only, so the file does not depend on the order of an enum.
     *
     * Not saved: the selection, a pending promotion, the analysis (its switches, hints, ratings) and the takeback stack: they all start as at a new game. Every field is a string the
     * restore checks (Validate()), so a file that was edited or damaged is refused and never applied in part.
     */
    struct ChessSaveState {
        BE_CLASS(ChessSaveState)

        /// The FEN the game began from (ChessGameModel::GetStartFen())
        BE_REFLECT_FIELD String startFen;
        /// The moves played since, in UCI, one string with a single space between moves; empty for none
        BE_REFLECT_FIELD String moves;
        /// The colors the program plays: "none" (two people), "white", "black" or "both" (two programs)
        BE_REFLECT_FIELD String plays;
        /// The engine id ("stockfish", "own") and the level id ("easy", "medium", "hard", "max") of each seat, also of a seat nobody plays
        BE_REFLECT_FIELD String whiteEngine;
        BE_REFLECT_FIELD String whiteLevel;
        BE_REFLECT_FIELD String blackEngine;
        BE_REFLECT_FIELD String blackLevel;
        /// The color the person plays, "white" or "black" (OpponentChoice::Color())
        BE_REFLECT_FIELD String personColor;
    };

    /// A save that Validate() has checked completely: everything Apply() needs, already parsed
    struct RestorePlan {
        Position start = Position::Start();  ///< the position the game began from
        Vector<Move> moves;                  ///< legal in their turn, one after the other, from @p start
        bool finished = false;               ///< the game of the file is over: Apply() restores the opponent choice only
        ProgramSides plays;
        SeatChoice white;
        SeatChoice black;
        PieceColor person = PieceColor::White;
    };

    /// What the game and the opponent hold now, as a save. Reads only
    [[nodiscard]] ChessSaveState Capture(const ChessGameModel& model, const OpponentController& controller, const OpponentChoice& choice);

    /**
     * @brief The first half of a restore: checks the whole state and changes nothing.
     *
     * The FEN goes through Position::FromFen(), every move is parsed and played in its turn on a scratch model (the same rules as a game: nothing after the end), and every id is looked up in its
     * table (the colors the program plays, the engine, the level, the person's color; a person's color that is the program's one is a contradiction). Nothing of the live game is touched, and the file
     * is not either: this class has no file system. A refusal is a `Corrupt` with one sentence that names the reason and the field or the move number, never the contents of the file. A file that is a
     * finished game passes (`finished`).
     */
    [[nodiscard]] std::expected<RestorePlan, SaveError> Validate(const ChessSaveState& state);

    /**
     * @brief The second half: puts a checked plan into the live game, and cannot fail halfway.
     *
     * The position is started from the plan's start and every move is played through ChessGameModel::TryMove() (a finished game is not played: the model keeps what it has). Then the
     * opponent: one ChooseSeats() with the sides and the seats' choices, and the person's color when the program plays no single side (the color of one side derives from it). A pick the
     * controller refuses (Stockfish saved, none found now) is not a failure of the restore: the position stands, the program plays nobody, GetError() has the reason through the usual
     * route, nothing falls back to the other engine, and one warning says the saved opponent was not restored. Meant for a game that has just started (nobody plays); the selection, the
     * analysis and its switches are left as they are.
     */
    void Apply(const RestorePlan& plan, ChessGameModel& model, OpponentController& controller, OpponentChoice& choice);

}  // namespace BECore::Chess
