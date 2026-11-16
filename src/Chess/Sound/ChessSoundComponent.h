#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace SceneEvents {
        struct SceneUpdateEvent;
    }

    namespace InputEvents {
        struct ActionEvent;
    }

}  // namespace BECore

namespace BECore::Chess {

    class ChessGameModel;

    /**
     * @brief Plays the sound of a move (SoundCue.h) when the active game's move list grows, whoever made the move, and the click of a button on the Ui bus on every action but the volume slider's.
     *
     * Draws nothing and keeps no game state: it remembers the model, its move count and its piece count of the last frame, and plays when the same model has more moves now. A first sighting
     * of a model, a smaller or equal count (a reset, a takeback, a position set from a FEN, a restored game before the first frame) only takes the new numbers, so nothing is asked for.
     * Several moves in one frame play the cue of the last one, judged by the pieces across the frame. Viewing an earlier position changes no move list and is silent. The clips are loaded
     * when the component attaches, so the first move does not wait for a decode.
     */
    class ChessSoundComponent : public IComponent {
        BE_CLASS(ChessSoundComponent)
    public:
        ChessSoundComponent() = default;
        ~ChessSoundComponent() override = default;

        void OnAttached() override;

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);
        void OnAction(const InputEvents::ActionEvent& event);

        /// Only compared, never read: a model that is gone is noticed by the active model being null or another one
        const ChessGameModel* _seenModel = nullptr;
        size_t _seenMoves = 0;
        int _seenPieces = 0;
    };

}  // namespace BECore::Chess
