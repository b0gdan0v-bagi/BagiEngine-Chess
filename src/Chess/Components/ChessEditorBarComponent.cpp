#include "ChessEditorBarComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIImageComponent.h>
#include <BECore/UI/UIVars.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/PieceSprite.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/ChessEditorBarComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        struct Ids {
            static constexpr auto chessEditColor = "ChessEditColor"_intern;
            static constexpr auto chessEditPick = "ChessEditPick"_intern;
            static constexpr auto editorBar = "EditorBar"_intern;
            static constexpr auto editorBarErase = "EditorBarErase"_intern;
            static constexpr auto editorColor = "editorColor"_intern;
        };

        constexpr StringView kButtonPrefix = "EditorBar";

        /// The button of a piece tool: EditorBarKing, EditorBarQueen, ...
        PoolString ButtonOf(PieceType type) {
            return PoolString::Intern(Format("{}{}", kButtonPrefix, EnumUtils<PieceType>::ToString(type)));
        }

    }  // namespace

    void ChessEditorBarComponent::OnAttached() {
        SubscribeInScene<InputEvents::ActionEvent, &ChessEditorBarComponent::OnAction>(this);
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &ChessEditorBarComponent::OnUpdate>(this);
    }

    void ChessEditorBarComponent::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        Sync();
    }

    void ChessEditorBarComponent::OnDraw() {
        Sync();
    }

    void ChessEditorBarComponent::OnAction(const InputEvents::ActionEvent& event) {
        const bool pick = event.action == Ids::chessEditPick;
        ChessGame* game = (pick || event.action == Ids::chessEditColor) ? ChessGame::GetActive() : nullptr;
        if (!game || !game->IsEditing()) {
            return;
        }
        PositionEditor& editor = game->GetEditor();
        EditorTool tool = editor.GetTool();
        if (!pick) {
            tool.color = Opposite(tool.color);
        } else if (const Optional<PieceType> type = FindEditorTool(event.value.ToStringView())) {
            tool.type = *type;
        } else {
            LOG_WARNING("ChessEditPick ignored: '{}' is not a tool"_format(event.value.ToStringView()));
            return;
        }
        editor.SetTool(tool);
        Sync();
    }

    void ChessEditorBarComponent::Sync() {
        const ChessGame* game = ChessGame::GetActive();
        const bool wanted = game && game->IsEditing();
        if (wanted != _shown) {
            _shown = wanted;
            UICanvasComponent::SetVisibleByName(GetScene(), Ids::editorBar, wanted);
        }
        const auto canvas = GetNode().GetComponent<UICanvasComponent>();
        if (!wanted || !canvas) {
            _synced = false;
            return;
        }
        const EditorTool tool = game->GetEditor().GetTool();
        if (_synced && game == _syncedGame && tool.type == _syncedTool.type && tool.color == _syncedTool.color) {
            return;
        }
        _synced = true;
        _syncedGame = game;
        _syncedTool = tool;

        CoreManager::GetUIVars().Set(Ids::editorColor, EnumUtils<PieceColor>::ToString(tool.color));
        for (const PieceType type : EditorToolPieces()) {
            UIElement* button = canvas->GetRoot().FindByName(ButtonOf(type));
            if (!button) {
                continue;
            }
            button->selected = !tool.IsEraser() && tool.type == type;
            if (UIImageComponent* icon = button->GetComponent<UIImageComponent>()) {
                icon->sprite = PieceSpriteId(Piece{type, tool.color});
            }
        }
        if (UIElement* eraser = canvas->GetRoot().FindByName(Ids::editorBarErase)) {
            eraser->selected = tool.IsEraser();
        }
    }

}  // namespace BECore::Chess
