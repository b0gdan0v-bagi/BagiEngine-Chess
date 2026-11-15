#pragma once

#include <Chess/Engine/EngineOpponent.h>
#include <Chess/Engine/OpponentLevels.h>
#include <Chess/Engine/OpponentSettings.h>

namespace BECore::Chess {

    /// Makes the engine for one kind and level, or says why there is none. The default makes a UciEngine over the Stockfish the settings point at for Stockfish and an OwnEngine
    /// for Own (which cannot fail); a test gives a double
    //-NoStd: no callable alias yet (callables-move-only)
    using EngineFactory = eastl::function<std::expected<SharedPtr<IChessEngine>, EngineError>(OpponentEngine, const OpponentLevel&)>;

    /// Why no Stockfish was found: names the path that was tried when @p path is not empty, else the three places that are looked at. Shared with the analysis (PositionAnalyst)
    [[nodiscard]] String NoStockfishDetail(const String& path);

    /// The error for an engine this build cannot run (IsOpponentEngineAvailable is false: Stockfish with CHESS_NO_STOCKFISH): `Unsupported`, with a sentence that says why. Shared with the analysis
    [[nodiscard]] EngineError UnavailableEngineError(OpponentEngine engine);

    /// Which colors the program plays: nobody (two people), one side, or both (two programs)
    struct ProgramSides {
        bool white = false;
        bool black = false;

        [[nodiscard]] constexpr bool Plays(PieceColor color) const noexcept {
            return color == PieceColor::White ? white : black;
        }

        [[nodiscard]] static constexpr ProgramSides Of(Optional<PieceColor> computer) noexcept {
            return ProgramSides{computer == PieceColor::White, computer == PieceColor::Black};
        }

        [[nodiscard]] constexpr bool operator==(const ProgramSides&) const noexcept = default;
    };

    /// What a seat plays with: the engine and the strength (an index of GetOpponentLevels())
    struct SeatChoice {
        OpponentEngine engine = kDefaultOpponentEngine;
        size_t level = kDefaultOpponentLevel;

        [[nodiscard]] constexpr bool operator==(const SeatChoice&) const noexcept = default;
    };

    /**
     * @brief The seats of the computer opponent: which colors the program plays (nobody, one side or both), with which engine and strength each, and the EngineOpponents behind that choice.
     *
     * A seat is a color with a SeatChoice (engine and level). Both seats always have a choice, also when nobody plays them; a seat that plays has an EngineOpponent made for exactly that choice,
     * with an engine object of its own (never shared: an engine searches one request at a time). The program plays no side (two people on one screen, how a game starts: nothing is looked up or
     * started), one side (the side of the program, not of the person) or both (two programs). ChooseSeats() is the one pick; SetSide(), SetLevel(), SetEngine(), Choose() and the Cycle*() calls
     * are the single-seat API built on it and write the engine and level they are given into BOTH seat choices.
     *
     * GetPlays() is "the one color the program plays": nothing for nobody AND for both, so two programs leave White at the bottom. PlaysFor(color) is true for any seat of that color.
     * GetLevel() and GetEngine() are the choice of the seat that plays when exactly one plays, else of White's seat.
     *
     * A pick asks the factory for every seat that plays and is new, has another choice or whose opponent has failed, all BEFORE it changes anything. A pick that finds no engine is REFUSED as a whole:
     * nothing changes (the sides, the choices, the running opponents and their searches stay), the error is kept in GetError(), the revision grows and one warning is logged. Nothing falls back to the
     * other engine: the person picks it (SetEngine(), which clears the error), then a side. While nobody plays, SetEngine() only changes the choice and cannot be refused (except a pick of Stockfish
     * that cannot run, below). A change destroys the old EngineOpponent of a changed seat first (it cancels its search and returns at once, and a late answer is never played); a seat
     * whose color and choice are unchanged keeps its opponent and its search. A pick that changes nothing does nothing, except that it clears a standing refusal and remakes an opponent whose search
     * failed (the retry).
     *
     * In a build without Stockfish (CHESS_NO_STOCKFISH) a pick of Stockfish is refused the same way (`Unsupported`), for any seat and also while nobody plays it, as long as the controller uses its
     * default factory: a factory given by SetEngineFactory() is the caller's own world and is asked as before. With Stockfish in the build but no file found (FindStockfish() over the settings,
     * looked up afresh at every pick that moves a seat onto Stockfish) the pick is refused the same way, as `LaunchFailed` with NoStockfishDetail(), also for a seat nobody plays; a seat that
     * already holds Stockfish keeps it (a Stockfish that disappears after it was picked stays picked and its search fails, the factory says so when the seat plays).
     *
     * The error rule: an error belongs to the version of the game it came from (ChessGameModel::GetVersion()). When the version moves (a reset, a position set from outside, a move of someone else)
     * Update() clears the error, the opponents' too, and a new pick clears it as well: the person has acted, so they have seen it. The program's own move does not take a refused pick's error away
     * (the opponent that kept playing would otherwise erase the sentence before anyone read it). Until then the game waits and no search starts, so a broken engine is one error and not a try per
     * frame; the error of either seat stalls the game, since it is that seat's turn. A person who does not want to wait picks nobody.
     *
     * GetRevision() grows whenever something a display shows changes: the sides, a choice, an error that appears or goes; one pick grows it once. It does not grow with a search that starts or ends
     * (IsThinking()) or with a move of the game (GetVersion() of the model); a display that publishes these watches the three.
     *
     * Threads: main thread only (every call, Update() included). The engine is searched on the Background pool by EngineOpponent, which says more of stale answers and shutdown. Dropping an engine
     * closes its child process on the thread that drops the last reference to it: the one that destroys the opponent when no search is running (so a stuck child can block the main thread for
     * kQuitGraceMs plus the signal grace of UciProcess::Close() at most), the pool worker otherwise. A search whose engine ignores its CancellationToken keeps that engine alive on the pool until it
     * returns. Call Shutdown() (or destroy the controller) before TaskManager::Shutdown().
     *
     * The premoves of the person (ChessGameModel::GetPremoves()) are played here: Update() calls PlayPremove() right after the seats' Update, while the side to move is not a program's, so the premove
     * goes in the frame in which the program's answer lands. A pick that changes a side or a choice and Shutdown() drop the queue (a refused pick and a pick that changes nothing keep it). Who may
     * premove is the board's decision (ChessBoardComponent).
     *
     * This class, EngineOpponent, PositionAnalyst and MoveRater are the only ones of Engine/ that log.
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

        /// Where the default factory looks for Stockfish; read at the next pick, an opponent that already plays keeps its engine. Looks for the file again (RefreshStockfish())
        void SetSettings(OpponentSettings settings);

        /**
         * @brief Whether Stockfish is offered to the person: the build can run it (kStockfishAvailable) and a factory of a test is set or a Stockfish file was found.
         *
         * Answers from a cache, so it is cheap enough for every frame; FindStockfish() walks PATH and is asked only by RefreshStockfish() and by a pick of Stockfish. Never changes GetRevision():
         * what is installed is not the state of the game.
         */
        [[nodiscard]] bool OffersStockfish() const noexcept {
            return kStockfishAvailable && (_factory || _stockfishFound);
        }

        /// Looks for the Stockfish file again (FindStockfish() over the settings), for a Stockfish installed since the last look
        void RefreshStockfish();

        /// The engine a new game starts with: kDefaultOpponentEngine while it is offered, else the own engine
        [[nodiscard]] OpponentEngine DefaultEngine() const noexcept {
            return OffersStockfish() ? kDefaultOpponentEngine : OpponentEngine::Own;
        }

        /// The analysis reads these at the time of its search, so the ones `ChessGame::Start()` loads are seen
        [[nodiscard]] const OpponentSettings& GetSettings() const noexcept {
            return _settings;
        }

        /// The factory asked at a pick for (engine, level); an empty one puts the default back (each engine under WithHandicap). A factory given here is never wrapped, and is asked
        /// for an engine this build could not run too
        void SetEngineFactory(EngineFactory factory);

        /**
         * @brief The one pick: the colors the program plays and the choice of each seat.
         *
         * BadRequest, with nothing changed, for a level outside GetOpponentLevels() in either choice. The factory is asked for White's seat first and no more once one fails; a refused pick
         * returns the error that GetError() has. When a program takes the side to move, a half-made move of the person is dropped (ChessGameModel::ClearSelection()). The choice of a seat
         * that nobody plays is remembered, not used.
         */
        std::expected<void, EngineError> ChooseSeats(ProgramSides plays, SeatChoice white, SeatChoice black);

        /**
         * @brief The side the program plays, or nobody (two people).
         *
         * A side takes the choice of its seat and writes it into the other seat as well; nobody keeps both choices as they are. Returns the engine factory's error when there is no engine.
         */
        std::expected<void, EngineError> SetSide(Optional<PieceColor> computer);

        /// The strength, an index of GetOpponentLevels(), for both seats; BadRequest (nothing changes) for one outside it
        std::expected<void, EngineError> SetLevel(size_t level);

        /// Nobody, then the program plays Black (the person has White), then the program plays White, then nobody again. Two programs go to nobody
        std::expected<void, EngineError> CycleSide();

        /// Side and strength in one pick, the engine kept: the rules of SetSide() and SetLevel()
        std::expected<void, EngineError> Choose(Optional<PieceColor> computer, size_t level);

        /// Side, strength and engine in one pick; BadRequest for a level outside GetOpponentLevels(), as the other picks
        std::expected<void, EngineError> Choose(Optional<PieceColor> computer, size_t level, OpponentEngine engine);

        /// The engine, for both seats; with nobody playing it only changes the choice and clears a standing pick error (a pick of Stockfish is still refused when it cannot run: no Stockfish
        /// in the build, or no file found)
        std::expected<void, EngineError> SetEngine(OpponentEngine engine);

        /// The next engine of the cycle; Stockfish is skipped while it is not offered (OffersStockfish())
        std::expected<void, EngineError> CycleEngine();

        /// The next level, the first one after the last
        std::expected<void, EngineError> CycleLevel();

        /// One step per frame; it never waits for the engine
        void Update();

        /// Destroys the opponents (they cancel their searches and return at once); nobody plays afterwards and Update() does nothing, until a new pick
        void Shutdown();

        /// The one color the program plays; nothing for two people AND for two programs (see IsWatching())
        [[nodiscard]] Optional<PieceColor> GetPlays() const noexcept {
            if (_seats[kWhite].opponent && !_seats[kBlack].opponent) {
                return PieceColor::White;
            }
            if (_seats[kBlack].opponent && !_seats[kWhite].opponent) {
                return PieceColor::Black;
            }
            return std::nullopt;
        }

        [[nodiscard]] ProgramSides GetSides() const noexcept {
            return ProgramSides{_seats[kWhite].opponent != nullptr, _seats[kBlack].opponent != nullptr};
        }

        /// The program plays @p color (either seat). Stays true while an error stands: that side is still the program's, and the game waits for the person to act
        [[nodiscard]] bool PlaysFor(PieceColor color) const noexcept {
            return _seats[SeatIndex(color)].opponent != nullptr;
        }

        [[nodiscard]] bool IsWatching() const noexcept {
            return _seats[kWhite].opponent && _seats[kBlack].opponent;
        }

        /// What would be used when @p color is picked, also while nobody plays it
        [[nodiscard]] const SeatChoice& GetSeatChoice(PieceColor color) const noexcept {
            return _seats[SeatIndex(color)].choice;
        }

        /// The index of the picked strength in GetOpponentLevels(), of the seat that plays when exactly one plays, else of White's, also while nobody plays
        [[nodiscard]] size_t GetLevel() const noexcept {
            return SingleSeatChoice().level;
        }

        /// The picked engine, of the same seat as GetLevel()
        [[nodiscard]] OpponentEngine GetEngine() const noexcept {
            return SingleSeatChoice().engine;
        }

        /// The program is searching for its move (either seat)
        [[nodiscard]] bool IsThinking() const {
            return (_seats[kWhite].opponent && _seats[kWhite].opponent->IsThinking()) || (_seats[kBlack].opponent && _seats[kBlack].opponent->IsThinking());
        }

        /// Why the program does not play as asked: the refused pick, else the failed search of an opponent (White's seat first); empty when nothing is wrong
        [[nodiscard]] const Optional<EngineError>& GetError() const noexcept {
            if (_pickError) {
                return _pickError;
            }
            for (const Seat& seat : _seats) {
                if (seat.opponent && seat.opponent->GetLastError().has_value()) {
                    return seat.opponent->GetLastError();
                }
            }
            return _pickError;
        }

        /// What grows it is in the class comment; never goes back
        [[nodiscard]] uint64_t GetRevision() const noexcept {
            return _revision;
        }

    private:
        struct Seat {
            SeatChoice choice;
            UniquePtr<EngineOpponent> opponent;
            bool errorNoted = false;  ///< the error in `opponent` has been counted in the revision
        };

        static constexpr size_t kSeatCount = EnumUtils<PieceColor>::Count();  ///< one seat per color
        static constexpr size_t kWhite = 0;                                   ///< the index of White's seat; Black's is 1
        static constexpr size_t kBlack = 1;

        [[nodiscard]] static constexpr size_t SeatIndex(PieceColor color) noexcept {
            return color == PieceColor::White ? kWhite : kBlack;
        }

        [[nodiscard]] const SeatChoice& SingleSeatChoice() const noexcept {
            return _seats[GetPlays() == PieceColor::Black ? kBlack : kWhite].choice;
        }

        [[nodiscard]] std::expected<SharedPtr<IChessEngine>, EngineError> MakeEngine(OpponentEngine engine, const OpponentLevel& level) const;
        /// The color of the first seat (White's first) whose picked engine this build cannot run, while the default factory is in use; nothing for a factory given by a test
        [[nodiscard]] Optional<PieceColor> FindUnavailableSeat(const Array<SeatChoice, kSeatCount>& picked) const;
        /// The color of the first seat that is moved onto Stockfish while no file is found, while the default factory is in use; looks afresh (RefreshStockfish()) only for such a seat
        [[nodiscard]] Optional<PieceColor> FindSeatWithoutStockfish(const Array<SeatChoice, kSeatCount>& picked);
        std::expected<void, EngineError> ChooseLevel(ProgramSides plays, size_t level);
        void FailPick(EngineError error, PieceColor computer, OpponentEngine engine, const OpponentLevel& level);
        void DropStaleError();
        void NoteOpponentError(Seat& seat);

        ChessGameModel& _model;
        OpponentSettings _settings;
        EngineFactory _factory;        ///< empty: the Stockfish of _settings, the OwnEngine, each wrapped by WithHandicap
        bool _stockfishFound = false;  ///< the last look of RefreshStockfish(); FindStockfish() walks PATH, so it is not asked per frame
        Array<Seat, kSeatCount> _seats;
        Optional<EngineError> _pickError;  ///< the factory's failure of the last pick; the old opponents (if any) go on playing while it stands
        uint64_t _errorVersion = 0;        ///< GetVersion() of the model when the standing error came
        uint64_t _revision = 0;
    };

}  // namespace BECore::Chess
