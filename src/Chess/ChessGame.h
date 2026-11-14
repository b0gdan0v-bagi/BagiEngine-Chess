#pragma once

#include <Chess/ChessGameControl.h>
#include <Chess/Engine/OpponentChoice.h>
#include <Chess/Engine/OpponentController.h>
#include <Chess/Engine/PositionAnalyst.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Save/ChessSaveKeeper.h>
#include <Games/IGame.h>

namespace BECore::Chess {

    /**
     * @brief Concrete IGame for chess.
     *
     * Owns the shared ChessGameModel that ChessBoardComponent and ChessPieceComponent
     * read/write through GameManager::GetActive(). Activates "ChessScene" when started.
     *
     * Auto-registered into AbstractFactory<IGame> via the BE_CLASS reflection
     * machinery — no extra hand-written registrar required.
     *
     * Owns the adapter of the external control API (ChessGameControl) next to the model it drives, and the seat of the computer opponent
     * (OpponentController). A game starts with nobody as the opponent: Start() loads the settings (where Stockfish is); the opponent
     * is made when a side is picked on GetOpponent(). The choice the opponent panel makes (OpponentChoice: a mode, the person's color, an engine and a level by id) is owned
     * here too, over the same controller, and remembers the person's color while nobody plays. The analysis of the position (PositionAnalyst: the evaluation, the hint and, through its MoveRater, the
     * ratings of the played moves) is owned here as well, with the default factory over the settings the controller holds; it starts switched off and Start() puts it back to that. IGame has no
     * per-frame hook, so ChessBoardComponent runs GetOpponent().Update() and GetAnalyst().Update() every frame. Stop() destroys the opponent and shuts the analyst down, which happens before the task
     * system goes down (Application::Cleanup()): a search that nobody cancelled would hold the join of the pool.
     *
     * The game remembers itself (ChessSaveKeeper, when the run asks for it: Chess/Save/SaveSettings.h): Start() ends by restoring the saved game and the opponent choice into the fresh ones, Stop()
     * starts by writing them (before the opponent goes), and ChessBoardComponent calls UpdateKeeper() every frame for the debounced write in between.
     */
    class ChessGame : public IGame {
        BE_CLASS(ChessGame)
    public:
        ChessGame();
        ~ChessGame() override = default;

        PoolString GetName() const override;
        PoolString GetSceneName() const override;

        void Start() override;
        void Stop() override;
        void Reset() override;

        IControllableGame* AsControllable() override;

        ChessGameModel& GetModel() noexcept {
            return _model;
        }
        const ChessGameModel& GetModel() const noexcept {
            return _model;
        }

        OpponentController& GetOpponent() noexcept {
            return _opponent;
        }
        const OpponentController& GetOpponent() const noexcept {
            return _opponent;
        }

        OpponentChoice& GetChoice() noexcept {
            return _choice;
        }
        const OpponentChoice& GetChoice() const noexcept {
            return _choice;
        }

        PositionAnalyst& GetAnalyst() noexcept {
            return _analyst;
        }
        const PositionAnalyst& GetAnalyst() const noexcept {
            return _analyst;
        }

        const ChessSaveKeeper& GetKeeper() const noexcept {
            return _keeper;
        }

        /// Once per frame from ChessBoardComponent::OnUpdate (never from a draw): writes the game when it changed and the debounce of ChessSaveKeeper has passed. Does nothing in a run that saves
        /// nothing
        void UpdateKeeper();

        /// The active game, or nullptr when no game is active or the active game is not Chess.
        static ChessGame* GetActive();

        /// The model of the active game, or nullptr when no game is active or the active game is not Chess.
        static ChessGameModel* GetActiveModel();

        /// The opponent of the active game, or nullptr when no game is active or the active game is not Chess.
        static OpponentController* GetActiveOpponent();

        /// The choice of the opponent of the active game, or nullptr when no game is active or the active game is not Chess.
        static OpponentChoice* GetActiveChoice();

        /// The analyst of the active game, or nullptr when no game is active or the active game is not Chess.
        static PositionAnalyst* GetActiveAnalyst();

    private:
        ChessGameModel _model;
        OpponentController _opponent;  ///< after the model (it holds a reference to it) and before the adapter
        OpponentChoice _choice;        ///< after the controller (it holds a reference to it)
        PositionAnalyst _analyst;      ///< after the controller (its factory reads the controller's settings) and before the adapter
        ChessSaveKeeper _keeper;       ///< after everything it reads (it is handed the model, the controller and the choice in each call, and destroyed first)
        ChessGameControl _control;
        bool _stopped = false;  ///< between Stop() and the next Start(): UpdateKeeper() writes nothing
    };

}  // namespace BECore::Chess
