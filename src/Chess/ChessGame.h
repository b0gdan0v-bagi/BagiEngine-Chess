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
     * @brief Concrete IGame for chess; its scene is "ChessScene".
     *
     * Owns the model the scene shares (components reach it, the opponent, the choice and the analyst through the GetActive*() calls), the computer opponent (OpponentController), the choice the
     * opponent panel makes over it (OpponentChoice: it remembers the person's color while nobody plays), the analysis of the position (PositionAnalyst, with the default factory over the settings the
     * controller holds), the save keeper and the adapter of the control API (ChessGameControl). A game starts with nobody as the opponent (it is made when a side is picked) and the analysis off.
     *
     * IGame has no per-frame hook, so ChessBoardComponent runs GetOpponent().Update(), GetAnalyst().Update() and UpdateKeeper() every frame. Start() ends by restoring the saved game, the
     * opponent choice and the player's audio levels (ChessSaveKeeper, when the run asks for it). Stop() starts by writing them, then destroys the opponent and shuts the analyst down, which happens
     * before the task system goes down (Application::Cleanup()): a search that nobody cancelled would hold the join of the pool.
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

        /// The active game, or nullptr when no game is active or the active game is not Chess (the same for the GetActive*() below)
        static ChessGame* GetActive();

        /// The model of the active game
        static ChessGameModel* GetActiveModel();

        /// The opponent of the active game
        static OpponentController* GetActiveOpponent();

        /// The choice of the opponent of the active game
        static OpponentChoice* GetActiveChoice();

        /// The analyst of the active game
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
