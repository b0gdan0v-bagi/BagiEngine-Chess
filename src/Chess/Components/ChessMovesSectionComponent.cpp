#include "ChessMovesSectionComponent.h"

#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Chess/ChessGame.h>
#include <Chess/Game/ChessGameModel.h>
#include <Generated/ChessMovesSectionComponent.gen.hpp>

namespace BECore::Chess {

    void ChessMovesSectionComponent::OnDraw() {
        const ChessGameModel* model = ChessGame::GetActiveModel();
        const auto canvas = GetNode().GetComponent<UICanvasComponent>();
        if (canvas) {
            _takeBack.Sync(*canvas, model);
        }
        if (!_published.Changed(model)) {
            return;
        }
        const Vector<MovePanelRow> rows = CurrentMovePanelRows(model);
        PublishMovePanelWords(rows);
        if (canvas) {
            _rows.Show(*canvas, rows);
        }
    }

}  // namespace BECore::Chess
