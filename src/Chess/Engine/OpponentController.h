#pragma once

#include <Chess/Engine/EngineOpponent.h>
#include <Chess/Engine/OpponentLevels.h>
#include <Chess/Engine/OpponentSettings.h>

namespace BECore::Chess {

    /// Makes the engine for one kind and level, or says why there is none. The default makes a UciEngine over the Stockfish the settings point at for Stockfish and an OwnEngine
    /// for Own (which cannot fail); a test gives a double
    using EngineFactory = eastl::function<std::expected<eastl::shared_ptr<IChessEngine>, EngineError>(OpponentEngine, const OpponentLevel&)>;

    /// Why no Stockfish was found: names the path that was tried when @p path is not empty, else the three places that are looked at. Shared with the analysis (PositionAnalyst)
    [[nodiscard]] eastl::string NoStockfishDetail(const eastl::string& path);

    /**
     * @brief The seat of the computer opponent: which side the program plays (or none), which engine, at which strength, and the EngineOpponent behind that choice.
     *
     * A game starts with nobody as the opponent: two people on one screen, and nothing is looked up or started. SetSide() picks the side the program plays
     * (the side of the program, not of the person: the HUD turns it around), asks the engine factory for an engine and makes an EngineOpponent for it.
     *
     * A pick that finds no engine is REFUSED: the engine is asked for first, and when there is none nothing changes (the side, the level, the engine and the running opponent with
     * its search stay as they were), the error is kept in GetError(), the revision grows and one warning is logged. An engine that cannot be had cannot be saved, so a missing
     * Stockfish never switches the program off; and a game that started with nobody stays with two people, so no one is locked out. Choose() is all or nothing. SetLevel()
     * picks the strength, SetEngine() the engine (Stockfish or the own one; a game starts with kDefaultOpponentEngine). A change of side, level or engine destroys the old EngineOpponent first (it
     * cancels its search and returns at once, and a late answer of that search is never played) and makes another, because the side is fixed by the EngineOpponent constructor and the Skill Level
     * option is sent at the handshake. A pick that changes nothing (the same side, level and engine, no error) does nothing.
     *
     * The engine is part of the choice and nothing else picks it: there is no fallback from one engine to the other. A refused Stockfish pick keeps the old opponent and
     * puts the error in GetError(); the person picks the other engine (SetEngine()), which clears the error, and then a side. While nobody plays, SetEngine() only changes the
     * choice: no factory call, nothing is looked up or started (so it cannot be refused).
     *
     * The error rule: an error belongs to the version of the game it came from (ChessGameModel::GetVersion()). When the version moves (a reset, a position set from
     * outside, a move of someone else) Update() clears the error, the opponent's too, and a new pick clears it as well: the person has acted, so they have seen it.
     * The program's own move does not take a refused pick's error away (the opponent that kept playing would otherwise erase the sentence before anyone read it).
     * Until then the game waits and no search starts, so a broken engine is one error and not a try per frame. A person who does not want to wait picks nobody.
     *
     * GetRevision() grows whenever something a display shows changes: the side, the level, the engine, an error that appears or goes. It does not grow with a search that
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

        /// The factory that makes the engine at a pick, asked for (engine, level); an empty one puts the default back (the Stockfish of the settings, the OwnEngine, each under the handicap of the
        /// level: WithHandicap; a factory given here is never wrapped)
        void SetEngineFactory(EngineFactory factory);

        /**
         * @brief The side the program plays, or nobody (two people).
         *
         * Remakes the opponent and clears the error, as described above. When the program takes the side to move, a half-made move of the person
         * is dropped (ChessGameModel::ClearSelection()). Returns the engine factory's error when there is no engine: the pick is refused and nothing changed, GetError() has the error.
         */
        std::expected<void, EngineError> SetSide(std::optional<PieceColor> computer);

        /// The strength, an index of GetOpponentLevels(); BadRequest (nothing changes) for one outside it. Remakes the opponent when the program plays
        std::expected<void, EngineError> SetLevel(size_t level);

        /// Nobody, then the program plays Black (the person has White), then the program plays White, then nobody again
        std::expected<void, EngineError> CycleSide();

        /// Side and strength in one pick, the engine kept: one engine is made, not two. The same rules as SetSide() and SetLevel(); BadRequest, with nothing changed, for a level outside
        /// GetOpponentLevels()
        std::expected<void, EngineError> Choose(std::optional<PieceColor> computer, size_t level);

        /// Side, strength and engine in one pick: one engine is made, not three. BadRequest, with nothing changed, for a level outside GetOpponentLevels()
        std::expected<void, EngineError> Choose(std::optional<PieceColor> computer, size_t level, OpponentEngine engine);

        /// The engine; remakes the opponent when the program plays (the factory is asked once for (engine, level)), else only changes the choice and clears a standing pick error
        std::expected<void, EngineError> SetEngine(OpponentEngine engine);

        /// The next engine: Stockfish, Own, and back to Stockfish
        std::expected<void, EngineError> CycleEngine();

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

        /// The picked engine; the one that would be used when a side is picked, also while nobody plays
        [[nodiscard]] OpponentEngine GetEngine() const noexcept {
            return _engine;
        }

        /// The program is searching for its move
        [[nodiscard]] bool IsThinking() const {
            return _opponent && _opponent->IsThinking();
        }

        /// Why the program does not play as asked: the refused pick, else the failed search of the opponent; empty when nothing is wrong. A refused pick does not stop a running opponent
        [[nodiscard]] const std::optional<EngineError>& GetError() const noexcept {
            if (_pickError) {
                return _pickError;
            }
            return _opponent ? _opponent->GetLastError() : _pickError;
        }

        /// Grows with every change of the side, the level, the engine or the error that a display shows; never goes back
        [[nodiscard]] uint64_t GetRevision() const noexcept {
            return _revision;
        }

    private:
        std::expected<void, EngineError> Remake(std::optional<PieceColor> computer, size_t level, OpponentEngine engine);
        [[nodiscard]] std::expected<eastl::shared_ptr<IChessEngine>, EngineError> MakeEngine(OpponentEngine engine, const OpponentLevel& level) const;
        void FailPick(EngineError error, PieceColor computer, OpponentEngine engine, const OpponentLevel& level);
        void DropStaleError();
        void NoteOpponentError();

        ChessGameModel& _model;
        OpponentSettings _settings;
        EngineFactory _factory;  ///< empty: the Stockfish of _settings, the OwnEngine, each wrapped by WithHandicap
        OpponentEngine _engine = kDefaultOpponentEngine;
        size_t _level = kDefaultOpponentLevel;        ///< an index of GetOpponentLevels()
        eastl::unique_ptr<EngineOpponent> _opponent;  ///< null: nobody, two people
        std::optional<EngineError> _pickError;        ///< the factory's failure of the last pick; the old opponent (if any) goes on playing while it stands
        bool _opponentErrorNoted = false;             ///< the error in _opponent has been counted in the revision
        uint64_t _errorVersion = 0;                   ///< GetVersion() of the model when the standing error came
        uint64_t _revision = 0;
    };

}  // namespace BECore::Chess
