#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace SceneEvents {
        struct SceneUpdateEvent;
    }

}  // namespace BECore

namespace BECore::Chess {

    /**
     * @brief Logical handle for a single piece on the active chess board.
     *
     * Owns nothing visual — pair this on a SceneNode with a SpriteRendererComponent
     * (for the piece artwork) and a TransformComponent (which this component
     * drives every frame).
     *
     * Lifecycle:
     *   - OnAttached registers this component with the active ChessGameModel,
     *     binding it to its starting (file, rank).
     *   - On every SceneUpdateEvent it asks the model for its current square,
     *     reads board geometry from ChessBoardComponent::GetActive(), and
     *     writes pixel coordinates into the sibling TransformComponent.
     *   - When captured (model reports inactive), the transform is moved
     *     off-screen so the sprite renderer draws nothing visible.
     */
    class ChessPieceComponent : public IComponent {
        BE_CLASS(ChessPieceComponent)
    public:
        ChessPieceComponent() = default;
        ~ChessPieceComponent() override;

        BE_REFLECT_FIELD int _file = 0;  ///< Starting file 0..7 (a..h).
        BE_REFLECT_FIELD int _rank = 0;  ///< Starting rank 0..7 (1..8).

        void OnAttached() override;

    private:
        void OnUpdate(const SceneEvents::SceneUpdateEvent&);
        void RegisterWithModel();
        void UnregisterFromModel();

        bool _registered = false;
    };

}  // namespace BECore::Chess
