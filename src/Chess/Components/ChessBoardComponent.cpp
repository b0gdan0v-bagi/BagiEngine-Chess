#include "ChessBoardComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <Chess/ChessGame.h>
#include <Chess/Game/StatusText.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Generated/ChessBoardComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        constexpr Color kLightSquare{240, 217, 181, 255};
        constexpr Color kDarkSquare{181, 136, 99, 255};
        constexpr Color kSelectedSquare{120, 200, 120, 200};
        constexpr Color kLegalMoveSquare{120, 200, 120, 120};
        constexpr Color kLegalCaptureSquare{220, 90, 90, 160};
        constexpr Color kCheckSquare{230, 60, 60, 210};
        constexpr Color kMateSquare{150, 20, 20, 240};

        // One line in the log when a game ends; the board has no text to show it in.
        void LogResult(const ChessGameModel& model) {
            const auto status = EnumUtils<GameStatus>::ToString(model.GetStatus());
            if (const auto winner = model.GetWinner()) {
                LOG_INFO("{}, {} wins"_format(status, EnumUtils<PieceColor>::ToString(*winner)));
            } else {
                LOG_INFO("Game over: {}"_format(status));
            }
        }

    }  // namespace

    ChessBoardComponent* ChessBoardComponent::s_active = nullptr;

    ChessBoardComponent::~ChessBoardComponent() {
        if (s_active == this) {
            s_active = nullptr;
        }
    }

    void ChessBoardComponent::OnAttached() {
        s_active = this;
        SubscribeInScene<SDLEvents::MouseButtonDownEvent, &ChessBoardComponent::OnMouseDown>(this);
        SubscribeInScene<InputEvents::ActionEvent, &ChessBoardComponent::OnAction>(this);
    }

    void ChessBoardComponent::PublishUiVars() {
        const ChessGameModel* model = ChessGame::GetActiveModel();
        if (!model || (model == _publishedModel && model->GetVersion() == _publishedVersion)) {
            return;
        }
        _publishedModel = model;
        _publishedVersion = model->GetVersion();
        UIVars& vars = CoreManager::GetUIVars();
        const eastl::string turn = StatusText::Turn(*model);
        const eastl::string status = StatusText::Status(*model);
        vars.Set("turn"_intern, eastl::string_view(turn.data(), turn.size()));
        vars.Set("status"_intern, eastl::string_view(status.data(), status.size()));
    }

    void ChessBoardComponent::OnAction(const InputEvents::ActionEvent& event) {
        // The action of the Reset button in config/SceneConfig.xml
        const PoolString resetAction = "ChessReset"_intern;
        if (event.action != resetAction) {
            return;
        }
        if (ChessGameModel* model = ChessGame::GetActiveModel()) {
            model->Reset();
        }
    }

    void ChessBoardComponent::SquareToPixel(int file, int rank, float& outX, float& outY) const {
        // rank 0 (White's back rank) is drawn at the bottom of the board.
        outX = _originX + static_cast<float>(file) * _squareSize;
        outY = _originY + static_cast<float>(7 - rank) * _squareSize;
    }

    void ChessBoardComponent::OnDraw() {
        PublishUiVars();
        auto& renderer = *CoreManager::GetRenderer();

        // Checkered base.
        for (int rank = 0; rank < 8; ++rank) {
            for (int file = 0; file < 8; ++file) {
                float px = 0.0f;
                float py = 0.0f;
                SquareToPixel(file, rank, px, py);
                const bool isLight = ((file + rank) % 2) != 0;
                renderer.DrawFilledRect(px, py, _squareSize, _squareSize, isLight ? kLightSquare : kDarkSquare);
            }
        }

        const ChessGameModel* model = ChessGame::GetActiveModel();
        if (!model) {
            return;
        }

        // The king that is attacked: bright red while the game goes on, dark red once it is mate.
        if (model->IsInCheck()) {
            const Square king = model->GetKingSquare(model->GetTurn());
            float kx = 0.0f;
            float ky = 0.0f;
            SquareToPixel(king.file, king.rank, kx, ky);
            renderer.DrawFilledRect(kx, ky, _squareSize, _squareSize, model->IsGameOver() ? kMateSquare : kCheckSquare);
        }

        if (model->HasSelection()) {
            const Square sel = model->GetSelected();
            float px = 0.0f;
            float py = 0.0f;
            SquareToPixel(sel.file, sel.rank, px, py);
            renderer.DrawFilledRect(px, py, _squareSize, _squareSize, kSelectedSquare);

            for (const auto target : model->GetLegalMoves()) {
                SquareToPixel(target.file, target.rank, px, py);
                const bool isCapture = !model->GetPieceAt(target).IsEmpty();
                renderer.DrawFilledRect(px, py, _squareSize, _squareSize, isCapture ? kLegalCaptureSquare : kLegalMoveSquare);
            }
        }
    }

    void ChessBoardComponent::OnMouseDown(const SDLEvents::MouseButtonDownEvent& e) {
        ChessGameModel* model = ChessGame::GetActiveModel();
        if (!model) {
            return;
        }

        const float boardSize = _squareSize * 8.0f;
        const float localX = GetScene().ScreenToSceneX(e.button.x) - _originX;
        const float localY = GetScene().ScreenToSceneY(e.button.y) - _originY;
        if (localX < 0.0f || localY < 0.0f || localX >= boardSize || localY >= boardSize) {
            return;
        }

        const int file = static_cast<int>(localX / _squareSize);
        const int rankFromTop = static_cast<int>(localY / _squareSize);
        const int rank = 7 - rankFromTop;
        if (!Square::IsInside(file, rank)) {
            return;
        }

        // After the game has ended a click on the board starts the next one.
        if (model->IsGameOver()) {
            model->Reset();
            return;
        }

        if (model->OnSquareClicked(Square{static_cast<int8_t>(file), static_cast<int8_t>(rank)}) && model->IsGameOver()) {
            LogResult(*model);
        }
    }

}  // namespace BECore::Chess
