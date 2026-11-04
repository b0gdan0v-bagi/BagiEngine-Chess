#pragma once

#include <Chess/ChessGameControl.h>
#include <Chess/Engine/OpponentController.h>
#include <Chess/Game/ChessGameModel.h>
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
     * (OpponentController). A game starts with nobody as the opponent: Start() loads the settings (where Stockfish is) and nothing else; the opponent
     * is made when a side is picked on GetOpponent(). IGame has no per-frame hook, so ChessBoardComponent runs GetOpponent().Update() every frame. Stop()
     * destroys the opponent, which happens before the task system goes down (Application::Cleanup()): a search that nobody cancelled would hold the join of the pool.
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

        /// The active game, or nullptr when no game is active or the active game is not Chess.
        static ChessGame* GetActive();

        /// The model of the active game, or nullptr when no game is active or the active game is not Chess.
        static ChessGameModel* GetActiveModel();

        /// The opponent of the active game, or nullptr when no game is active or the active game is not Chess.
        static OpponentController* GetActiveOpponent();

    private:
        ChessGameModel _model;
        OpponentController _opponent;  ///< after the model (it holds a reference to it) and before the adapter
        ChessGameControl _control;
    };

}  // namespace BECore::Chess
