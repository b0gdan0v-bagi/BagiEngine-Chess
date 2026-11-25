#pragma once

#include <BECore/Save/SaveGame.h>
#include <BECore/Utils/FunctionRef.h>
#include <Chess/Engine/OpponentChoice.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Graphics/GraphicsChoice.h>

namespace BECore::Chess {

    /**
     * @brief Keeps the game and the player's levels between runs: reads the save once in Start(), writes it when it changes and always when the game stops. Owned by ChessGame. Main thread only. Not
     * copyable.
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
     * The player's audio levels (the engine's `audio` slot, AudioPreferences.h) go through the same folder and gate: Start() reads them into the audio manager, Update() writes them when
     * GetLevelsRevision() moved, with a debounce of their own that neither delays nor is delayed by the game's, and Flush() writes them too. A refused `audio` file is one warning and is left alone.
     *
     * The person's look (ChessGraphicsState, the slot `graphics`) is kept the same way by the revision of the GraphicsChoice that Start() is given: read in Start(), written by Update() with a
     * debounce and a retry state of its own (so are the levels') and by Flush(). A refused `graphics` file is one warning, the file is left alone and the look stays Classic.
     *
     * While a puzzle is on nothing of the game is written, so a restart brings back the game from before it. Not saved: see ChessSaveState.
     */
    class ChessSaveKeeper {
    public:
        /// The least game time between two debounced writes
        static constexpr double kSaveDebounceSeconds = 1.0;

        ChessSaveKeeper();
        ~ChessSaveKeeper();
        ChessSaveKeeper(const ChessSaveKeeper&) = delete;
        ChessSaveKeeper& operator=(const ChessSaveKeeper&) = delete;

        /// Decides whether this run saves, and restores a good save into the four. Call it last in the start of the game, with the game fresh (nobody plays); calling it again starts over.
        /// @p graphics is kept (a reference that outlives the keeper) for Update() and Flush()
        void Start(ChessGameModel& model, OpponentController& controller, OpponentChoice& choice, GraphicsChoice& graphics);

        /// Once per frame, on the main thread, never from a draw: writes the game when it changed and the debounce has passed. @p puzzleOn: a puzzle is the game on the board, which is
        /// not the person's game, so the game is not written (the levels still are); the key is not marked as written, so the first frame after the puzzle writes what the game became
        void Update(const ChessGameModel& model, const OpponentController& controller, const OpponentChoice& choice, bool puzzleOn);

        /// Writes the game now when it changed since the last write, unless @p puzzleOn as above; the end of the game goes through it
        void Flush(const ChessGameModel& model, const OpponentController& controller, const OpponentChoice& choice, bool puzzleOn);

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

        /// The write state of a slot kept by a revision number (the audio levels, the look): the game's, with the revision in place of a Key
        struct SlotWrite {
            uint64_t written = 0;    ///< the revision the file holds (or Start() found)
            uint64_t attempted = 0;  ///< the revision of the last write that failed; not retried until it moves
            bool attemptFailed = false;
            bool hasWritten = false;
            double lastTime = 0.0;  ///< game time of the last write, meaningful when hasWritten
            bool failureLogged = false;
        };

        using SlotSaver = FunctionRef<std::expected<void, SaveError>()>;

        void UpdateSlot(SlotWrite& slot, uint64_t revision, StringView name, SlotSaver save);
        void FlushSlot(SlotWrite& slot, uint64_t revision, StringView name, SlotSaver save);
        void WriteSlot(SlotWrite& slot, uint64_t revision, StringView name, SlotSaver save);
        void UpdateSlots();
        void FlushSlots();

        UniquePtr<SaveGameService> _service;  ///< null when this run saves nothing
        Key _written;                         ///< the game as the file holds it (or as Start() found it)
        Key _attempted;                       ///< the key of the last write that failed; it is not retried until the key moves
        bool _attemptFailed = false;
        bool _hasWritten = false;
        double _lastWriteTime = 0.0;  ///< game time of the last write, meaningful when _hasWritten
        bool _failureLogged = false;
        SlotWrite _audio;
        SlotWrite _look;
        GraphicsChoice* _graphics = nullptr;  ///< set by Start(); the choice outlives the keeper
    };

}  // namespace BECore::Chess
