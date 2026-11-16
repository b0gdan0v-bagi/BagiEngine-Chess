#pragma once

#include <BECore/Audio/AudioTypes.h>
#include <BECore/PoolString/PoolString.h>
#include <Chess/Game/GameStatus.h>
#include <Chess/Game/Move.h>

namespace BECore::Chess {

    /// The sound of a move or of a button: one clip per cue (SoundCue.cpp holds the path, the bus and the volume of each). Click is no move's sound: CueOfMove() never returns it
    CORE_ENUM(SoundCue, uint8_t, Move, Capture, Castle, Check, Promotion, GameEnd, Click)

    /// What a played move left behind: all the rule of the cue needs, so a test feeds it positions and no scene is involved
    struct MoveOutcome {
        Move move;
        int piecesBefore = 0;  ///< Pieces on the board before the move
        int piecesAfter = 0;   ///< Pieces on the board after it; fewer than before: something was captured, en passant included
        bool check = false;    ///< The side to move after the move is in check
        GameStatus status = GameStatus::Ongoing;
    };

    /// The one cue of a move, the first row that holds: the game is over, a promotion, a check, castling, a capture, else a quiet move
    [[nodiscard]] SoundCue CueOfMove(const MoveOutcome& outcome);

    /// The virtual path of the clip of a cue. Interned on the first call, so a caller that asks once while it loads keeps every later frame free of allocations
    [[nodiscard]] PoolString ClipPath(SoundCue cue);

    /// The bus a cue plays on: Sfx for the board, Ui for the button. It sits in the table of the clips, so a clip and its bus cannot drift apart
    [[nodiscard]] AudioBus CueBus(SoundCue cue);

    /// How loud a cue plays, 0 to 1 (before the bus and the master level)
    [[nodiscard]] float CueVolume(SoundCue cue);

    /// Whether an action of the scene is answered by the click sound. Every button is, but the volume slider: it emits on every step of a drag, and a click per step is noise
    [[nodiscard]] bool ActionClicks(PoolString action);

}  // namespace BECore::Chess
