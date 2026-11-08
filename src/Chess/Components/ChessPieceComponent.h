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
     * Owns nothing visual — pair this on a SceneNode with a SpriteRendererComponent
     * (for the piece artwork) and a TransformComponent (which this component
     * drives).
     *
     * Lifecycle:
     *   - OnAttached registers this component with the active ChessGameModel,
     *     binding it to its starting (file, rank).
     *   - On every SceneUpdateEvent it asks the model for its current square,
     *     reads board geometry from ChessBoardComponent::GetActive(), and
     *     writes pixel coordinates into the sibling TransformComponent.
     *   - A move that the game plays (one more move than the component saw, the same
     *     start position, a frame with time in it) is slid: the piece goes from its old
     *     square to the new one over _slideDuration with QuadOut. Every other change
     *     (the first sighting, a reset, a new position, a frame with no time, the model
     *     gone) places the piece at once and ends a slide under way.
     *   - A captured piece (the model reports it inactive) stays on its square while the
     *     slide of the capturing move runs, then its transform is moved off-screen so the
     *     sprite renderer draws nothing visible.
     *   - When the model reports another piece type (a promotion, or the reset
     *     that undoes it), the sibling SpriteRendererComponent switches to the
     *     sprite of that piece; during a slide it waits for the end of it.
     *
     * The tween runs a progress 0..1, not a pixel: while it runs, the position is a Lerp of the
     * pixels of the two squares taken from the board on every update, so a board that turns
     * or a changed geometry cannot leave the piece in a wrong place.
     */
    class ChessPieceComponent : public IComponent {
        BE_CLASS(ChessPieceComponent)
    public:
        /// The slide time of a piece in seconds when the scene does not say.
        static constexpr float kDefaultSlideDuration = 0.15f;
        /// The square of a piece that is not on the board (as the model reports a captured piece).
        static constexpr Square kOffBoard{-1, -1};

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
        /// Forgets what was seen, so the next sighting places the piece at once.
        void ForgetSeen();

        /// True from the move that started the slide until the tween ends (completes, is replaced or cleared): read from the handle, so a TweenManager::Clear() cannot leave it set.
        [[nodiscard]] bool IsSliding() const;

        bool _registered = false;
        PieceType _shownType = PieceType::None;  ///< Type of the piece the sprite shows now.

        ScopedTween _slide;             ///< The running slide; the destructor cancels it (its setter points at this).
        float _slideProgress = 0.0f;    ///< 0..1, written by the tween.
        Square _slideFrom = kOffBoard;  ///< The square the slide leaves (a captured piece stays here).
        Square _slideTo = kOffBoard;    ///< The square the slide goes to; outside the board for a captured piece.

        bool _seen = false;              ///< True once an update saw the model and the board.
        Square _lastSquare = kOffBoard;  ///< The square the model gave last update (outside the board when captured).
        size_t _seenMoves = 0;           ///< Number of moves played when seen last.
        uint64_t _seenVersion = 0;       ///< Version of the model when seen last: a played move is exactly one more.
        eastl::string _seenStartFen;     ///< Start position of the game when seen last.
    };

}  // namespace BECore::Chess
