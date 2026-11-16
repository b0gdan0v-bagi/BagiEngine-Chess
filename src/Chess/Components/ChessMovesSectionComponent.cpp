#include "ChessMovesSectionComponent.h"

#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Chess/ChessGame.h>
#include <Chess/Game/ChessGameModel.h>
#include <Generated/ChessMovesSectionComponent.gen.hpp>

namespace BECore::Chess {

    void ChessMovesSectionComponent::OnDraw() {
        const ChessGameModel* model = ChessGame::GetActiveModel();
        const uint64_t version = model ? model->GetVersion() : 0;
        if (_published && model == _publishedModel && version == _publishedVersion) {
            return;
        }
        _published = true;
        _publishedModel = model;
        _publishedVersion = version;
        const Vector<MovePanelRow> rows = CurrentMovePanelRows(model);
        PublishMovePanelWords(rows);
        if (const auto canvas = GetNode().GetComponent<UICanvasComponent>()) {
            _rows.Show(*canvas, rows);
        }
    }

}  // namespace BECore::Chess
