#include "ChessBoardComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UISegmented.h>
#include <Chess/ChessGame.h>
#include <Chess/Engine/OpponentText.h>
#include <Chess/Game/StatusText.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
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

        // The choice groups of the opponent panel are found by name in every canvas of the scene (the panel is a canvas of its own); a scene without such a group has nothing to grey
        void SetGroupDisabledIn(SceneNode& node, PoolString group, bool disabled) {
            if (const auto canvas = node.GetComponent<UICanvasComponent>()) {
                if (UIElement* element = canvas->GetRoot().FindByName(group)) {
                    if (UISegmented* segmented = element->Cast<UISegmented>()) {
                        segmented->disabled = disabled;
                    }
                }
            }
            for (const auto& child : node.GetChildren()) {
                SetGroupDisabledIn(*child, group, disabled);
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
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &ChessBoardComponent::OnUpdate>(this);
    }

    void ChessBoardComponent::OnUpdate(const SceneEvents::SceneUpdateEvent&) {
        ChessGame* game = ChessGame::GetActive();
        if (!game) {
            return;
        }
        // The opponent plays here, on the main thread, and not in OnDraw(): it changes the model
        game->GetOpponent().Update();
        static_cast<void>(game->GetChoice().Color());  // reading it remembers the person's color of a side set from outside (the control API) before that side is cleared
        LogResultOnce(game->GetModel());
    }

    void ChessBoardComponent::LogResultOnce(const ChessGameModel& model) {
        if (!model.IsGameOver() || (&model == _loggedModel && model.GetVersion() == _loggedVersion)) {
            return;
        }
        _loggedModel = &model;
        _loggedVersion = model.GetVersion();
        LogResult(model);
    }

    void ChessBoardComponent::PublishUiVars() {
        const ChessGame* game = ChessGame::GetActive();
        if (!game) {
            return;
        }
        const ChessGameModel& model = game->GetModel();
        const OpponentController& opponent = game->GetOpponent();
        const OpponentChoice& choice = game->GetChoice();
        // Read on every refresh, also when nothing else changed: it remembers the person's color, which a set_state of the control API could otherwise take with it
        const PieceColor personColor = choice.Color();
        // A search starts and ends without a move of the game, and a side or a level changes without one: all of it is a reason to say the words again. The person's
        // color changes without any of them while two people play
        const bool thinking = opponent.IsThinking();
        const bool opponentChanged = &model != _publishedModel || opponent.GetRevision() != _publishedRevision;
        if (!opponentChanged && model.GetVersion() == _publishedVersion && thinking == _publishedThinking && personColor == _publishedColor) {
            return;
        }
        _publishedModel = &model;
        _publishedVersion = model.GetVersion();
        _publishedRevision = opponent.GetRevision();
        _publishedThinking = thinking;
        _publishedColor = personColor;

        // While an error of the opponent stands the game is stalled by it, so its sentence wins over "Check"
        const std::optional<EngineError>& error = opponent.GetError();
        const eastl::string turn = StatusText::Turn(model, thinking);
        const eastl::string status = error ? OpponentText::Error(*error) : StatusText::Status(model);
        const eastl::string side = OpponentText::SideLabel(opponent.GetPlays());
        const eastl::string level = OpponentText::LevelLabel(GetOpponentLevels()[opponent.GetLevel()]);
        const eastl::string engine = OpponentText::EngineLabel(opponent.GetEngine());
        const eastl::string opponentError = error ? OpponentText::Error(*error) : eastl::string();
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("turn"_intern, eastl::string_view(turn.data(), turn.size()));
        vars.Set("status"_intern, eastl::string_view(status.data(), status.size()));
        vars.Set("side"_intern, eastl::string_view(side.data(), side.size()));
        vars.Set("level"_intern, eastl::string_view(level.data(), level.size()));
        vars.Set("engine"_intern, eastl::string_view(engine.data(), engine.size()));
        // The ids of the opponent panel, those of the control API (the person's color, not the program's); the words are the text of the segments in the scene
        vars.Set("opponentMode"_intern, choice.ModeId());
        vars.Set("opponentColor"_intern, choice.ColorId());
        vars.Set("opponentEngine"_intern, choice.EngineId());
        vars.Set("opponentLevel"_intern, choice.LevelId());
        vars.Set("opponentError"_intern, eastl::string_view(opponentError.data(), opponentError.size()));

        if (opponentChanged) {
            // The strength means something only when the program plays; the engine button has no such rule (a failed Stockfish pick leaves nobody, and the person needs it then).
            // A scene without an element of that name has nothing to show or hide
            UICanvasComponent::SetElementVisibleByName(GetScene(), "Level"_intern, opponent.GetPlays().has_value());
            // The color and the level of the panel mean something only when the program plays; the engine group is never greyed for the reason above
            const bool twoPlayers = choice.Mode() == OpponentMode::Two;
            SetGroupDisabledIn(GetScene().GetRootNode(), "ColorGroup"_intern, twoPlayers);
            SetGroupDisabledIn(GetScene().GetRootNode(), "LevelGroup"_intern, twoPlayers);
        }
    }

    bool ChessBoardComponent::AnswerSetAction(const InputEvents::ActionEvent& event) {
        // The actions of the segments of the opponent panel; `value` is the id the person chose
        OpponentChoice* choice = ChessGame::GetActiveChoice();
        if (!choice) {
            return false;
        }
        const eastl::string_view value = event.value.ToStringView();
        std::expected<void, ChoiceError> picked;
        if (event.action == "ChessSetMode"_intern) {
            picked = choice->SetMode(value);
        } else if (event.action == "ChessSetColor"_intern) {
            picked = choice->SetColor(value);
        } else if (event.action == "ChessSetEngine"_intern) {
            picked = choice->SetEngine(value);
        } else if (event.action == "ChessSetLevel"_intern) {
            picked = choice->SetLevel(value);
        } else {
            return false;
        }
        // A refused pick (no Stockfish found) is kept by the controller and shown through the variables, so only an id that is in no table (a hand-made press) is said here
        if (!picked && picked.error().kind == ChoiceErrorKind::BadId) {
            LOG_WARNING("{} ignored: {}"_format(event.action.ToStringView(), picked.error().detail));
        }
        return true;
    }

    void ChessBoardComponent::OnAction(const InputEvents::ActionEvent& event) {
        // The actions of the buttons of the HUD in config/SceneConfig.xml
        const PoolString resetAction = "ChessReset"_intern;
        const PoolString cycleSideAction = "ChessCycleSide"_intern;
        const PoolString cycleEngineAction = "ChessCycleEngine"_intern;
        const PoolString cycleLevelAction = "ChessCycleLevel"_intern;
        if (event.action == resetAction) {
            if (ChessGameModel* model = ChessGame::GetActiveModel()) {
                model->Reset();
            }
            return;
        }
        OpponentController* opponent = ChessGame::GetActiveOpponent();
        if (!opponent) {
            return;
        }
        if (AnswerSetAction(event)) {
            return;
        }
        // A pick that finds no engine returns its error and keeps it in the controller; the Status label shows it, so there is nothing to handle here
        if (event.action == cycleSideAction) {
            opponent->CycleSide();
        } else if (event.action == cycleEngineAction) {
            opponent->CycleEngine();
        } else if (event.action == cycleLevelAction) {
            opponent->CycleLevel();
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

        // The program's turn: the person has nothing to move, and a selection would only fight the search that is under way
        const OpponentController* opponent = ChessGame::GetActiveOpponent();
        if (opponent && opponent->PlaysFor(model->GetTurn())) {
            return;
        }

        if (model->OnSquareClicked(Square{static_cast<int8_t>(file), static_cast<int8_t>(rank)})) {
            LogResultOnce(*model);
        }
    }

}  // namespace BECore::Chess
