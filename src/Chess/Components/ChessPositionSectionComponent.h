#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Game/ChessTypes.h>

namespace BECore::InputEvents {
    struct ActionEvent;
}

namespace BECore::Chess {

    /**
     * @brief The menu section "Position", on the node `PositionSection` before its canvas: answers `ChessEditBegin` (`value` `current`, `empty` or `start`), `ChessEditSide` (`white` or `black`),
     * `ChessEditCastle` (`+K`, `-K`, `+Q`, ...: the letter of a FEN's right, the sign the state the person asks for), `ChessEditPlay` and `ChessEditCancel` (the editor bar presses the same two).
     *
     * Beginning an editor, and ending it by Play (when the rules take the draft) or Cancel, close the menu; a refused Play keeps it open and its reason shows in `{editorMessage}`. The component owns
     * no state: the side and the castling switches show `editorBlackToMove` and `editorCastle<K|Q|Kb|Qb>`, written from the draft when the game, the editing, the draft's revision or the message
     * changed (never per frame). What needs an open editor is greyed while there is none, and a castling switch while its king and rook are not on their squares (`PositionEditor::CanCastle`).
     * An edit action that comes while no position is edited changes nothing; a value in no table does not either and logs one warning.
     */
    class ChessPositionSectionComponent : public IComponent {
        BE_CLASS(ChessPositionSectionComponent)
    public:
        ChessPositionSectionComponent() = default;
        ~ChessPositionSectionComponent() override = default;

        void OnAttached() override;
        void OnDraw() override;

    private:
        void OnAction(const InputEvents::ActionEvent& event);

        bool _published = false;
        const void* _publishedGame = nullptr;
        bool _publishedEditing = false;
        uint64_t _publishedRevision = 0;
        String _publishedMessage;
    };

}  // namespace BECore::Chess
