#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Game/PositionEditor.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }

    namespace SceneEvents {
        struct SceneUpdateEvent;
    }

}  // namespace BECore

namespace BECore::Chess {

    class ChessGame;

    /**
     * @brief The tool bar of the position editor, on the node `EditorBar` before its canvas: shows the canvas exactly while `ChessGame::IsEditing()` and answers `ChessEditPick` (the id of
     * Game/PositionEditor.h's tool table in `value`) and `ChessEditColor` (the other color for the pieces that are put).
     *
     * It compares on every update and draw, like the promotion picker, so a reset, a `set_state` or a game that starts from the draft hides a stale bar. The picked tool and its color
     * live in the editor: the component only shows them, writing `editorColor`, the sprites of the six piece buttons and the selected look when the game, the editing or the tool changed (never
     * per frame). Play and More (the section) are plain buttons of other components' actions. An action that comes while no position is edited changes nothing.
     */
    class ChessEditorBarComponent : public IComponent {
        BE_CLASS(ChessEditorBarComponent)
    public:
        ChessEditorBarComponent() = default;
        ~ChessEditorBarComponent() override = default;

        void OnAttached() override;
        void OnDraw() override;

    private:
        void OnAction(const InputEvents::ActionEvent& event);
        void OnUpdate(const SceneEvents::SceneUpdateEvent&);

        /// Shows or hides the canvas as the editing wants and, while it is shown, brings what the buttons show up to the editor's tool
        void Sync();

        bool _shown = false;  // the canvas starts hidden
        const ChessGame* _syncedGame = nullptr;
        bool _synced = false;
        EditorTool _syncedTool;
    };

}  // namespace BECore::Chess
