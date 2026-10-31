#pragma once

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
     */
    class ChessGame : public IGame {
        BE_CLASS(ChessGame)
    public:
        ChessGame() = default;
        ~ChessGame() override = default;

        PoolString GetName() const override;
        PoolString GetSceneName() const override;

        void Start() override;
        void Stop() override;
        void Reset() override;

        ChessGameModel& GetModel() noexcept {
            return _model;
        }
        const ChessGameModel& GetModel() const noexcept {
            return _model;
        }

    private:
        ChessGameModel _model;
    };

}  // namespace BECore::Chess
