#pragma once

#include <BECore/Save/SaveGame.h>
#include <Chess/Engine/OpponentChoice.h>
#include <Chess/Game/ChessGameModel.h>

namespace BECore::Chess {

    /**
     * @brief Keeps the game between runs: reads the save once at the start, writes it when the game changes, and always when the game stops. Owned by ChessGame.
     *
     * **The gate.** Start() asks ResolveSaveDirectory() (SaveSettings.h) with the command line and `rememberGame`; with no folder the keeper is off and every call does nothing and touches no file. A
     * folder that cannot be made is one warning and the same: the game runs without a memory.
     *
     * **The restore.** One read in Start(): the slot is loaded into a temporary (ChessSaveState), checked completely (Validate()) and only then applied, so a refused file changes nothing. A file that
     * is not there is silent (the first run); any other refusal is one warning with the slot and the reason, never the contents, and **the file is left alone**: the game starts fresh and the file is
     * rewritten only by the game's own next change.
     *
     * **The key.** What the game would write is known from three numbers, with no allocation: the model's GetVersion(), the controller's GetRevision() and the person's color. Update() compares them
     * every frame. Start() takes the key of the game it leaves (restored or fresh) as already written, so a restored game and a first start write nothing until something changes.
     *
     * **When it writes.** Update(): when the key moved, no sooner than kSaveDebounceSeconds of game time (CoreManager::GetTime()) after the last write; the first write of a run waits for nothing, and
     * a pending change waits for the clock, so a watch of two fast programs writes about once a second and a clock that does not move writes nothing more. Flush(): now, when the key moved since the
     * last write (ChessGame::Stop() calls it; the browser tab that closes does not, which is why the debounced write exists). A failed write is logged once and tried again at the next change; Flush()
     * tries it even without one.
     *
     * Not saved: see ChessSaveState. Main thread only. Not copyable.
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
