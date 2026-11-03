#pragma once

#include <Chess/Engine/EngineOpponent.h>
#include <Chess/Engine/OpponentLevels.h>
#include <Chess/Engine/OpponentSettings.h>

namespace BECore::Chess {

    /// Makes the engine for one level, or says why there is none. The default makes a UciEngine over the Stockfish the settings point at; a test gives a double
    using EngineFactory = eastl::function<std::expected<eastl::shared_ptr<IChessEngine>, EngineError>(const OpponentLevel&)>;

    /**
     * @brief The seat of the computer opponent: which side the program plays (or none), at which strength, and the EngineOpponent behind that choice.
     *
     * A game starts with nobody as the opponent: two people on one screen, and nothing is looked up or started. SetSide() picks the side the program plays
     * (the side of the program, not of the person: the HUD turns it around), asks the engine factory for an engine and makes an EngineOpponent for it; a pick
     * that finds no engine leaves nobody as the opponent, keeps the error in GetError() and logs one warning, so a missing Stockfish locks no one out. SetLevel()
     * picks the strength. A change of side or level destroys the old EngineOpponent first (it cancels its search and returns at once, and a late answer of
     * that search is never played) and makes another, because the side is fixed by the EngineOpponent constructor and the Skill Level option is sent at the
     * handshake. A pick that changes nothing (the same side and level, no error) does nothing.
     *
     * The error rule: an error belongs to the version of the game it came from (ChessGameModel::GetVersion()). When the version moves (a reset, a position set from
     * outside, a move of someone else) Update() clears the error, the opponent's too, and a new pick clears it as well: the person has acted, so they have seen it.
     * Until then the game waits and no search starts, so a broken engine is one error and not a try per frame. A person who does not want to wait picks nobody.
     *
     * GetRevision() grows whenever something a display shows changes: the side, the level, an error that appears or goes. It does not grow with a search that
     * starts or ends (IsThinking()) or with a move of the game (GetVersion() of the model); a display that publishes these watches the three.
     *
     * Threads: main thread only (every call, Update() included). The engine is searched on the Background pool by EngineOpponent, which says more of stale answers
     * and shutdown. Dropping an engine closes its child process on the thread that drops the last reference to it: the one that destroys the
     * opponent when no search is running (so a stuck child can block the main thread for kQuitGraceMs plus 0.5 s at most), the pool worker otherwise. A search
     * whose engine ignores its CancellationToken keeps that engine alive on the pool until it returns. Call Shutdown() (or destroy the controller) before
     * TaskManager::Shutdown().
     *
     * This class and EngineOpponent are the only ones of Engine/ that log.
     */
    class OpponentController {
    public:
        /// How long a closed Stockfish gets to leave after `quit` before it is sent a signal. Short on purpose: changing the level on the main thread destroys the old engine there
        static constexpr int kQuitGraceMs = 500;

        /// @p model must outlive the controller
        explicit OpponentController(ChessGameModel& model, OpponentSettings settings = {});

        /// Shutdown()
        ~OpponentController();

        OpponentController(const OpponentController&) = delete;
        OpponentController& operator=(const OpponentController&) = delete;
        OpponentController(OpponentController&&) = delete;
        OpponentController& operator=(OpponentController&&) = delete;

        /// Where the default factory looks for Stockfish; read at the next pick, an opponent that already plays keeps its engine
        void SetSettings(OpponentSettings settings);

        /// The factory that makes the engine at a pick; an empty one puts the default back (the Stockfish of the settings)
        void SetEngineFactory(EngineFactory factory);

        /**
         * @brief The side the program plays, or nobody (two people).
         *
         * Remakes the opponent and clears the error, as described above. When the program takes the side to move, a half-made move of the person
         * is dropped (ChessGameModel::ClearSelection()). Returns the engine factory's error when there is no engine: the opponent is then nobody, GetError() has it.
         */
        std::expected<void, EngineError> SetSide(std::optional<PieceColor> computer);

        /// The strength, an index of GetOpponentLevels(); BadRequest (nothing changes) for one outside it. Remakes the opponent when the program plays
        std::expected<void, EngineError> SetLevel(size_t level);

        /// Nobody, then the program plays Black (the person has White), then the program plays White, then nobody again
        std::expected<void, EngineError> CycleSide();

        /// The next level, the first one after the last
        std::expected<void, EngineError> CycleLevel();

        /// One step per frame, for the main thread; it never waits for the engine. Drops an error of an earlier version, runs EngineOpponent::Update() and notes a new error
        void Update();

        /// Destroys the opponent (it cancels its search and returns at once); nobody plays afterwards and Update() does nothing, until a new pick
        void Shutdown();

        /// The side the program plays; nothing for two people
        [[nodiscard]] std::optional<PieceColor> GetPlays() const noexcept {
            return _opponent ? std::optional<PieceColor>(_opponent->GetSide()) : std::nullopt;
        }

        /// The program plays @p color. Stays true while an error stands: that side is still the program's, and the game waits for the person to act
        [[nodiscard]] bool PlaysFor(PieceColor color) const noexcept {
            return _opponent && _opponent->GetSide() == color;
        }

        /// The index of the picked strength in GetOpponentLevels(); the level that would be used when a side is picked, also while nobody plays
        [[nodiscard]] size_t GetLevel() const noexcept {
            return _level;
        }

        /// The program is searching for its move
        [[nodiscard]] bool IsThinking() const {
            return _opponent && _opponent->IsThinking();
        }

        /// Why the program does not play: the failed pick, else the failed search of the opponent; empty when nothing is wrong
        [[nodiscard]] const std::optional<EngineError>& GetError() const noexcept {
            return _opponent ? _opponent->GetLastError() : _pickError;
        }

        /// Grows with every change of the side, the level or the error that a display shows; never goes back
        [[nodiscard]] uint64_t GetRevision() const noexcept {
            return _revision;
        }

    private:
        std::expected<void, EngineError> Remake(std::optional<PieceColor> computer, size_t level);
        [[nodiscard]] std::expected<eastl::shared_ptr<IChessEngine>, EngineError> MakeEngine(const OpponentLevel& level) const;
        void FailPick(EngineError error, PieceColor computer, const OpponentLevel& level);
        void DropStaleError();
        void NoteOpponentError();

        ChessGameModel& _model;
        OpponentSettings _settings;
        EngineFactory _factory;                       ///< empty: the Stockfish of _settings
        size_t _level = kDefaultOpponentLevel;        ///< an index of GetOpponentLevels()
        eastl::unique_ptr<EngineOpponent> _opponent;  ///< null: nobody, two people
        std::optional<EngineError> _pickError;        ///< the factory's failure; the opponent is null while it stands
        bool _opponentErrorNoted = false;             ///< the error in _opponent has been counted in the revision
        uint64_t _errorVersion = 0;                   ///< GetVersion() of the model when the standing error came
        uint64_t _revision = 0;
    };

}  // namespace BECore::Chess
