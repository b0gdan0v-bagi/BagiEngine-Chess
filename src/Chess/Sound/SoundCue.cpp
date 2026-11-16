#include "SoundCue.h"

namespace BECore::Chess {

    namespace {

        struct CueEntry {
            StringView path;
            float volume;
        };

        constexpr float kFullVolume = 1.0f;

        // Indexed by SoundCue; a clip that sits louder or softer than its neighbours is trimmed here, not in the file
        constexpr Array<CueEntry, 6> kCues{{
            {"assets/sounds/move.wav", kFullVolume},
            {"assets/sounds/capture.wav", kFullVolume},
            {"assets/sounds/castle.wav", kFullVolume},
            {"assets/sounds/check.wav", kFullVolume},
            {"assets/sounds/promotion.wav", kFullVolume},
            {"assets/sounds/game_end.wav", kFullVolume},
        }};
        static_assert(kCues.size() == EnumUtils<SoundCue>::Values().size(), "a cue needs its row");

        const CueEntry& EntryOf(SoundCue cue) {
            return kCues[static_cast<size_t>(cue)];
        }

    }  // namespace

    SoundCue CueOfMove(const MoveOutcome& outcome) {
        if (outcome.status != GameStatus::Ongoing) {
            return SoundCue::GameEnd;
        }
        if (outcome.move.IsPromotion()) {
            return SoundCue::Promotion;
        }
        if (outcome.check) {
            return SoundCue::Check;
        }
        if (outcome.move.Kind() == MoveKind::Castling) {
            return SoundCue::Castle;
        }
        if (outcome.piecesAfter < outcome.piecesBefore) {
            return SoundCue::Capture;
        }
        return SoundCue::Move;
    }

    PoolString ClipPath(SoundCue cue) {
        static const Array<PoolString, kCues.size()> paths = [] {
            Array<PoolString, kCues.size()> interned;
            for (size_t index = 0; index < kCues.size(); ++index) {
                interned[index] = PoolString::Intern(kCues[index].path);
            }
            return interned;
        }();
        return paths[static_cast<size_t>(cue)];
    }

    float CueVolume(SoundCue cue) {
        return EntryOf(cue).volume;
    }

}  // namespace BECore::Chess
