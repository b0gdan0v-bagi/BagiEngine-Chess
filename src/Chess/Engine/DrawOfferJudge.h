#pragma once

#include <Chess/Engine/AnalysisEngineFactory.h>
#include <Chess/Engine/DrawOffer.h>
#include <Chess/Engine/SearchRequest.h>
#include <TaskSystem/Task.h>

namespace BECore::Chess {

    /// What the program answered to a draw offer. Accepted: it takes the draw. Declined: it refuses, or could not judge
    CORE_ENUM(DrawVerdict, uint8_t, Accepted, Declined)

    /// What a display shows of the offer at a version of the game. Idle: no offer for it (also after an accepted one, which ends the game). Pending: the program is judging. Declined: it said no
    CORE_ENUM(DrawOfferState, uint8_t, Idle, Pending, Declined)

    /**
     * @brief Judges a draw offer of the person in the background: the program asks the own engine about the position and takes the draw by ProgramAcceptsDraw().
     *
     * Offer() copies the game into a SearchRequest and hands one task to TaskManager (Background pool, TaskManager must be running) that awaits IChessEngine::AnalyseAsync() at the fixed depth
     * kDrawOfferDepth with one line. The engine is always the own engine (the factory is asked for OpponentEngine::Own, whatever the person picked): deterministic, no process, an object of
     * its own, never the opponent's and never the analyst's. The owner of the model calls Update() once per frame on the main thread; it never waits and gives the verdict, once.
     *
     * **The version rule** (the pattern of EngineOpponent and PositionAnalyst). The answer is kept for the model's GetVersion() at the offer. When the version has moved (a move, a reset, a
     * takeback: any change of the game) the offer is dropped at the next Update() and its search cancelled; no verdict comes out of it, also when the engine ignores its token. A drop, a
     * Cancel() and Shutdown() forget the search for good: the worker keeps the only other reference to what it shares, so a late answer has nobody to reach. A second Offer() for the version
     * already offered does nothing, a declined one included.
     *
     * **Errors.** An engine that cannot be made and a search that fails (any EngineError, an empty answer included) are logged once (LOG_ERROR), kept in GetError() and **decline** the offer: the
     * program does not give a draw it could not judge. Nothing is retried by itself; the next offer, at a version that has not been offered, asks again.
     *
     * **Threads and shutdown** are PositionAnalyst's: main thread only; the worker sees a copy of the request and a state of its own (the engine, a CancellationToken, the answer behind a mutex, an
     * atomic `done`) shared through an IntrusivePtrAtomic, so nothing dangles whichever side ends first. The destructor, Cancel() and Shutdown() cancel the token and return at once; call
     * Shutdown() (or destroy the judge) before TaskManager::Shutdown(), which joins the pool and would wait for a search nobody cancelled. The destructor does not touch TaskManager.
     *
     * GetRevision() grows with an offer, an answer and a drop; never with a search that starts or ends, and never goes back. A display that publishes the offer adds it to the version of the model.
     * One of the classes of Engine/ that log.
     */
    class DrawOfferJudge {
    public:
        DrawOfferJudge() = default;

        /// Cancels the search in flight and returns at once
        ~DrawOfferJudge();

        DrawOfferJudge(const DrawOfferJudge&) = delete;
        DrawOfferJudge& operator=(const DrawOfferJudge&) = delete;
        DrawOfferJudge(DrawOfferJudge&&) = delete;
        DrawOfferJudge& operator=(DrawOfferJudge&&) = delete;

        /// How the engine is made; an empty one puts the default back (MakeAnalysisEngine(Own, {})). Read when the next engine is made
        void SetEngineFactory(AnalysisEngineFactory factory);

        /// The person offers a draw in the position of @p model; the program plays @p programColor. Whether the offer is allowed is DrawOfferRefusalFor()'s business: only a finished game is
        /// refused here (nothing changes). Does nothing for a version that was offered already. Replaces the offer of an older version
        void Offer(const ChessGameModel& model, PieceColor programColor);

        /// One step, for the main thread; it never waits for the engine. Drops what belongs to an older version and takes a finished search. The verdict comes once, in the Update() that has it
        [[nodiscard]] Optional<DrawVerdict> Update(const ChessGameModel& model);

        /// Drops the offer and cancels the search; nothing is asked again by itself. Returns at once
        void Cancel();

        /// Cancels and drops the engine. Call it before TaskManager::Shutdown()
        void Shutdown();

        /// The offer for the version of @p model; Idle for another version
        [[nodiscard]] DrawOfferState GetState(const ChessGameModel& model) const noexcept {
            return _state != DrawOfferState::Idle && _offerVersion == model.GetVersion() ? _state : DrawOfferState::Idle;
        }

        /// The program is judging: a search is under way and wanted
        [[nodiscard]] bool IsThinking() const noexcept {
            return _state == DrawOfferState::Pending;
        }

        /// Why the last offer could not be judged; empty when it was, and after it went with the next offer, a drop or Shutdown()
        [[nodiscard]] const Optional<EngineError>& GetError() const noexcept {
            return _error;
        }

        /// See the class comment
        [[nodiscard]] uint64_t GetRevision() const noexcept {
            return _revision;
        }

    private:
        /// What the worker and the judge share. The worker writes the answer and then raises `done`; the judge reads the answer after it sees `done`
        struct Search : RefCountedAtomic {
            explicit Search(ChessEnginePtr engineToAsk) : engine(std::move(engineToAsk)) {}

            ChessEnginePtr engine;
            CancellationToken token = CancellationToken::Create();
            Mutex mutex{BE_LOCK_SITE("DrawOfferJudge")};  ///< guards `answer`
            std::expected<Analysis, EngineError> answer = std::unexpected(EngineError{EngineErrorKind::NoMove, "the search has not finished"});
            std::atomic<bool> done{false};
        };

        /// Runs on the pool: asks the engine and leaves the answer in @p search. Its arguments are by value, so the frame owns everything it uses
        static Task<void> RunSearch(IntrusivePtrAtomic<Search> search, SearchRequest request);

        /// Forgets the offer and the search (cancelled), and the verdict not yet given. The revision grows when a display showed the offer
        void Drop();
        void TakeAnswer();
        void Resolve(DrawVerdict verdict);
        void Fail(EngineError error);

        AnalysisEngineFactory _factory;  ///< empty: MakeAnalysisEngine over empty settings
        ChessEnginePtr _engine;

        DrawOfferState _state = DrawOfferState::Idle;
        uint64_t _offerVersion = 0;  ///< the version of the model the offer was made at; meaningless while Idle
        PieceColor _sideToMove = PieceColor::White;
        PieceColor _programColor = PieceColor::Black;
        Optional<DrawVerdict> _verdict;  ///< reached, not yet given by Update()

        IntrusivePtrAtomic<Search> _search;  ///< the search under way, null when none; main thread only

        Optional<EngineError> _error;
        uint64_t _revision = 0;
    };

    /**
     * @brief The person offers a draw: the one body behind every door (ChessGame::OfferDraw() for the menu, the control adapter).
     *
     * Refused as DrawOfferRefusalFor() says, and nothing changes then. Two people agree at once: the game ends as DrawAgreed. Against the program the offer goes to @p judge, whose verdict
     * comes with the frames; an offer the judge already has for this version does nothing. @p programPlays is the sides the program plays.
     */
    std::expected<void, DrawOfferRefusal> PersonOffersDraw(ChessGameModel& model, ProgramSides programPlays, DrawOfferJudge& judge);

}  // namespace BECore::Chess
