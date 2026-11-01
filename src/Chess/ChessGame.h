#pragma once

#include <Chess/ChessGameControl.h>
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
     * Owns the adapter of the external control API (ChessGameControl) next to the model it drives.
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

        /// The model of the active game, or nullptr when no game is active or the active game is not Chess.
        static ChessGameModel* GetActiveModel();

    private:
        ChessGameModel _model;
        ChessGameControl _control;
    };

}  // namespace BECore::Chess
