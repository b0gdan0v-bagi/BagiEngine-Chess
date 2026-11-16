#pragma once

#include <BECore/Audio/AudioTypes.h>
#include <BECore/PoolString/PoolString.h>
#include <Chess/Game/GameStatus.h>
#include <Chess/Game/Move.h>

namespace BECore::Chess {

    /// The sound a move gets: one clip per cue (SoundCue.cpp holds the path and the volume of each)
    CORE_ENUM(SoundCue, uint8_t, Move, Capture, Castle, Check, Promotion, GameEnd)

    /// The bus every cue of the board plays on
    inline constexpr AudioBus kSoundCueBus = AudioBus::Sfx;

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

    /// How loud a cue plays, 0 to 1 (before the bus and the master level)
    [[nodiscard]] float CueVolume(SoundCue cue);

}  // namespace BECore::Chess
