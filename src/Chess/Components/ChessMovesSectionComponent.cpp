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
        const PositionAnalyst* analyst = ChessGame::GetActiveAnalyst();
        if (!_published.Changed(model, analyst)) {
            return;
        }
        const Vector<MovePanelRow> rows = CurrentMovePanelRows(model, analyst);
        PublishMovePanelWords(rows);
        if (canvas) {
            _rows.Show(*canvas, rows);
        }
    }

}  // namespace BECore::Chess
