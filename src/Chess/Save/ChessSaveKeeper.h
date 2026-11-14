#pragma once

#include <BECore/Save/SaveGame.h>
#include <Chess/Engine/OpponentChoice.h>
#include <Chess/Game/ChessGameModel.h>

namespace BECore::Chess {

    /**
     * @brief Keeps the game between runs: reads the save once in Start(), writes it when the game changes and always when the game stops. Owned by ChessGame. Main thread only. Not copyable.
     *
     * Start() asks ResolveSaveDirectory() (SaveSettings.h); with no folder, or one that cannot be made (one warning), the keeper is off: every call does nothing and touches no file. A save is loaded
     * into a temporary (ChessSaveState), checked completely and only then applied, so a refused file changes nothing. A file that is not there is silent (the first run); any other refusal is one
     * warning with the slot and the reason, never the contents, and the file is left alone: only the game's own next change rewrites it.
     *
     * What the game would write is told from a key of three numbers, with no allocation: the model's GetVersion(), the controller's GetRevision() and the person's color. Start() takes the key of
     * the game as it leaves it (restored or fresh) as already written.
     *
     * Update() writes when the key moved, no sooner than kSaveDebounceSeconds of game time (CoreManager::GetTime()) after the last write; the first write of a run waits for nothing. Flush() writes
     * now when the key moved since the last write (ChessGame::Stop() calls it; a browser tab that closes does not, which is why the debounced write exists). A failed write is logged once and tried
     * again at the next change; Flush() tries it even without one.
     *
     * Not saved: see ChessSaveState.
     */
    class ChessSaveKeeper {
    public:
        /// The least game time between two debounced writes
        static constexpr double kSaveDebounceSeconds = 1.0;

        ChessSaveKeeper();
        ~ChessSaveKeeper();
        ChessSaveKeeper(const ChessSaveKeeper&) = delete;
        ChessSaveKeeper& operator=(const ChessSaveKeeper&) = delete;

        /// Decides whether this run saves, and restores a good save into the three. Call it last in the start of the game, with the game fresh (nobody plays); calling it again starts over
        void Start(ChessGameModel& model, OpponentController& controller, OpponentChoice& choice);

        /// Once per frame, on the main thread, never from a draw: writes the game when it changed and the debounce has passed
        void Update(const ChessGameModel& model, const OpponentController& controller, const OpponentChoice& choice);

        /// Writes the game now when it changed since the last write; the end of the game goes through it
        void Flush(const ChessGameModel& model, const OpponentController& controller, const OpponentChoice& choice);

        /// Whether this run reads and writes a save
        [[nodiscard]] bool IsOn() const noexcept {
            return _service != nullptr;
        }

    private:
        /// The three numbers that tell a changed game from the same one
        struct Key {
            uint64_t modelVersion = 0;
            uint64_t controllerRevision = 0;
            PieceColor person = PieceColor::White;

            [[nodiscard]] bool operator==(const Key&) const noexcept = default;
        };

        [[nodiscard]] static Key KeyOf(const ChessGameModel& model, const OpponentController& controller, const OpponentChoice& choice);
        void Write(const ChessGameModel& model, const OpponentController& controller, const OpponentChoice& choice, const Key& key);

        UniquePtr<SaveGameService> _service;  ///< null when this run saves nothing
        Key _written;                         ///< the game as the file holds it (or as Start() found it)
        Key _attempted;                       ///< the key of the last write that failed; it is not retried until the key moves
        bool _attemptFailed = false;
        bool _hasWritten = false;
        double _lastWriteTime = 0.0;  ///< game time of the last write, meaningful when _hasWritten
        bool _failureLogged = false;
    };

}  // namespace BECore::Chess
