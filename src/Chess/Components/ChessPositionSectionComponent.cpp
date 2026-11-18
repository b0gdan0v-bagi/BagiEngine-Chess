#include "ChessPositionSectionComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UIVars.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/MenuActions.h>
#include <Events/InputEvents.h>
#include <Generated/ChessPositionSectionComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        struct Ids {
            static constexpr auto chessEditBegin = "ChessEditBegin"_intern;
            static constexpr auto chessEditCancel = "ChessEditCancel"_intern;
            static constexpr auto chessEditCastle = "ChessEditCastle"_intern;
            static constexpr auto chessEditPlay = "ChessEditPlay"_intern;
            static constexpr auto chessEditSide = "ChessEditSide"_intern;
            static constexpr auto editorBlackToMove = "editorBlackToMove"_intern;
            static constexpr auto editorCastleK = "editorCastleK"_intern;
            static constexpr auto editorCastleKb = "editorCastleKb"_intern;
            static constexpr auto editorCastleQ = "editorCastleQ"_intern;
            static constexpr auto editorCastleQb = "editorCastleQb"_intern;
            static constexpr auto editorMessage = "editorMessage"_intern;
            static constexpr auto positionSectionCastleKRow = "PositionSectionCastleKRow"_intern;
            static constexpr auto positionSectionCastleKbRow = "PositionSectionCastleKbRow"_intern;
            static constexpr auto positionSectionCastleQRow = "PositionSectionCastleQRow"_intern;
            static constexpr auto positionSectionCastleQbRow = "PositionSectionCastleQbRow"_intern;
            static constexpr auto positionSectionCancel = "PositionSectionCancel"_intern;
            static constexpr auto positionSectionPlay = "PositionSectionPlay"_intern;
            static constexpr auto positionSectionSideRow = "PositionSectionSideRow"_intern;
        };

        /// A castling switch of the section: its right, the letter of the right in a FEN, the element of its row and the variable it shows
        struct CastleSwitch {
            CastlingRight right;
            char letter;
            PoolString row;
            PoolString variable;
        };

        const CastleSwitch kCastleSwitches[] = {
            {CastlingRight::WhiteKingSide, 'K', Ids::positionSectionCastleKRow, Ids::editorCastleK},
            {CastlingRight::WhiteQueenSide, 'Q', Ids::positionSectionCastleQRow, Ids::editorCastleQ},
            {CastlingRight::BlackKingSide, 'k', Ids::positionSectionCastleKbRow, Ids::editorCastleKb},
            {CastlingRight::BlackQueenSide, 'q', Ids::positionSectionCastleQbRow, Ids::editorCastleQb},
        };

        constexpr StringView kWhite = "white";
        constexpr StringView kBlack = "black";
        constexpr char kOnSign = '+';
        constexpr char kOffSign = '-';
        constexpr size_t kCastleTextChars = 2;  // a sign and a letter

        Optional<EditStart> ParseStart(StringView text) {
            if (text == "current") {
                return EditStart::Current;
            }
            if (text == "empty") {
                return EditStart::Empty;
            }
            if (text == "start") {
                return EditStart::Start;
            }
            return std::nullopt;
        }

        /// A castling value ("+K": turn the right on): the switch and whether to turn it on; nothing for any other text
        Optional<Pair<const CastleSwitch*, bool>> ParseCastle(StringView text) {
            if (text.size() != kCastleTextChars || (text[0] != kOnSign && text[0] != kOffSign)) {
                return std::nullopt;
            }
            for (const CastleSwitch& castle : kCastleSwitches) {
                if (text[1] == castle.letter) {
                    return Pair<const CastleSwitch*, bool>{&castle, text[0] == kOnSign};
                }
            }
            return std::nullopt;
        }

        void SetDisabled(UICanvasComponent& canvas, PoolString name, bool disabled) {
            UIElement* element = canvas.GetRoot().FindByName(name);
            if (UIInputClicker* clicker = element ? element->GetComponent<UIInputClicker>() : nullptr) {
                clicker->disabled = disabled;
            }
        }

        void CloseTheMenu() {
            InputEvents::ActionEvent::Emit(MenuActions::closeMenu);
        }

    }  // namespace

    void ChessPositionSectionComponent::OnAttached() {
        SubscribeInScene<InputEvents::ActionEvent, &ChessPositionSectionComponent::OnAction>(this);
    }

    void ChessPositionSectionComponent::OnAction(const InputEvents::ActionEvent& event) {
        const PoolString action = event.action;
        const bool begin = action == Ids::chessEditBegin;
        const bool side = action == Ids::chessEditSide;
        const bool castle = action == Ids::chessEditCastle;
        const bool play = action == Ids::chessEditPlay;
        const bool cancel = action == Ids::chessEditCancel;
        ChessGame* game = (begin || side || castle || play || cancel) ? ChessGame::GetActive() : nullptr;
        if (!game) {
            return;
        }
        const StringView value = event.value.ToStringView();
        if (begin) {
            if (const Optional<EditStart> start = ParseStart(value)) {
                game->BeginEditing(*start);
                CloseTheMenu();
                return;
            }
        } else if (play) {
            if (game->PlayFromHere()) {
                CloseTheMenu();
            }
            return;
        } else if (cancel) {
            if (game->IsEditing()) {
                game->CancelEditing();
                CloseTheMenu();
            }
            return;
        } else if (!game->IsEditing()) {
            return;
        } else if (side) {
            if (value == kWhite || value == kBlack) {
                game->GetEditor().SetSideToMove(value == kBlack ? PieceColor::Black : PieceColor::White);
                game->EditChanged();
                return;
            }
        } else if (const auto parsed = ParseCastle(value)) {
            game->GetEditor().SetCastling(parsed->first->right, parsed->second);
            game->EditChanged();
            return;
        }
        LOG_WARNING("{} ignored: '{}' is not one of its values"_format(action.ToStringView(), value));
    }

    void ChessPositionSectionComponent::OnDraw() {
        const auto canvas = GetNode().GetComponent<UICanvasComponent>();
        if (!canvas) {
            return;
        }
        const ChessGame* game = ChessGame::GetActive();
        const bool editing = game && game->IsEditing();
        const uint64_t revision = game ? game->GetEditor().GetRevision() : 0;
        const StringView message = game ? game->GetEditorMessage() : StringView();
        if (_published && game == _publishedGame && editing == _publishedEditing && revision == _publishedRevision && message == StringView(_publishedMessage)) {
            return;
        }
        _published = true;
        _publishedGame = game;
        _publishedEditing = editing;
        _publishedRevision = revision;
        _publishedMessage = String(message);

        UIVars& vars = CoreManager::GetUIVars();
        vars.Set(Ids::editorMessage, message);
        SetDisabled(*canvas, Ids::positionSectionPlay, !editing);
        SetDisabled(*canvas, Ids::positionSectionCancel, !editing);
        SetDisabled(*canvas, Ids::positionSectionSideRow, !editing);
        if (!game) {
            return;
        }
        const PositionEditor& editor = game->GetEditor();
        vars.Set(Ids::editorBlackToMove, editor.GetSideToMove() == PieceColor::Black ? kBlack : kWhite);
        for (const CastleSwitch& castle : kCastleSwitches) {
            const char text[kCastleTextChars] = {editor.GetCastling(castle.right) ? kOnSign : kOffSign, castle.letter};
            vars.Set(castle.variable, StringView(text, kCastleTextChars));
            SetDisabled(*canvas, castle.row, !editing || !editor.CanCastle(castle.right));
        }
    }

}  // namespace BECore::Chess
