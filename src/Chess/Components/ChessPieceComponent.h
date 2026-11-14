#pragma once

#include <BECore/Animation/ScopedTween.h>
#include <BECore/Scene/IComponent.h>
#include <Chess/Game/ChessTypes.h>

namespace BECore {

    namespace SceneEvents {
        struct SceneUpdateEvent;
    }

}  // namespace BECore

namespace BECore::Chess {

    class ChessGameModel;

    /**
     * @brief Logical handle for a single piece on the active chess board.
     *
     * Owns nothing visual: pair it on a SceneNode with a SpriteRendererComponent (the artwork) and a TransformComponent, which it drives from the model on every SceneUpdateEvent.
     * OnAttached registers it with the active ChessGameModel at its starting (file, rank).
     *
     * A move that the game plays (one more move and one more version than the component saw, the same start position, a frame with time in it) is slid from the old square to the new one
     * over _slideDuration with QuadOut. Every other change (the first sighting, a reset, a new position, a frame with no time, the model gone) places the piece at once and ends a slide
     * under way. A captured piece stays on its square while the slide of the capturing move runs, then goes off-screen. When the model reports another piece type (a promotion, or the reset
     * that undoes it) the sprite switches to that piece, after the slide.
     *
     * A piece that the board reports as lifted (ChessBoardComponent::GetLiftedPiece(), a drag past the threshold) is placed under the pointer instead, its node goes to kDragLayer and
     * nothing slides it; the first update that does not lift it gives the layer back. A move played by the drop is not slid either: the piece stands on the square at once.
     *
     * The tween runs a progress 0..1, not a pixel: while it runs, the position is a Lerp of the pixels of the two squares taken from the board on every update, so a board that turns or
     * a changed geometry cannot leave the piece in a wrong place.
     */
    class ChessPieceComponent : public IComponent {
        BE_CLASS(ChessPieceComponent)
    public:
        /// The slide time in seconds when the scene does not say
        static constexpr float kDefaultSlideDuration = 0.15f;
        /// The square of a piece that is not on the board, as the model reports a captured piece
        static constexpr Square kOffBoard{-1, -1};
        /// The layer of the node of a piece the person holds (a drag): over the pieces, the result bar and the captured material, under the HUD and the panels
        static constexpr int32_t kDragLayer = 6;

        ChessPieceComponent() = default;
        ~ChessPieceComponent() override;

        BE_REFLECT_FIELD int _file = 0;                                 ///< Starting file 0..7 (a..h).
        BE_REFLECT_FIELD int _rank = 0;                                 ///< Starting rank 0..7 (1..8).
        BE_REFLECT_FIELD float _slideDuration = kDefaultSlideDuration;  ///< Seconds a played move takes to slide; 0 or less: no slide.

        void OnAttached() override;

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);
        void RegisterWithModel();
        void UnregisterFromModel();
        void ShowPieceType(Piece piece);

        /// Compares the model with what was seen last frame: starts a slide for a played move, ends it for any other change.
        void Observe(const ChessGameModel& model, Square current, float deltaTime);
        void StartSlide(Square from, Square to);
        void EndSlide();
        /// Gives the node the layer it had before the lift, when it was lifted
        void EndLift();
        /// Forgets what was seen, so the next sighting places the piece at once.
        void ForgetSeen();

        /// True from the move that started the slide until the tween ends (completes, is replaced or cleared): read from the handle, so a TweenManager::Clear() cannot leave it set.
        [[nodiscard]] bool IsSliding() const;

        bool _registered = false;
        PieceType _shownType = PieceType::None;

        ScopedTween _slide;             ///< The running slide; the destructor cancels it (its setter points at this).
        float _slideProgress = 0.0f;    ///< 0..1, written by the tween.
        Square _slideFrom = kOffBoard;  ///< The square the slide leaves (a captured piece stays here).
        Square _slideTo = kOffBoard;    ///< The square the slide goes to; outside the board for a captured piece.

        bool _lifted = false;  ///< True from the update that put the piece in the pointer's hand until the first one that does not: a move seen then is a drop and does not slide.
        int32_t _layerBeforeLift = 0;

        bool _seen = false;
        Square _lastSquare = kOffBoard;  ///< Outside the board when captured
        size_t _seenMoves = 0;
        uint64_t _seenVersion = 0;  ///< A played move is exactly one more
        String _seenStartFen;
    };

}  // namespace BECore::Chess
