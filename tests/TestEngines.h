#pragma once

#include "ChessTestUtil.h"

#include <Chess/Engine/OpponentController.h>
#include <Chess/Engine/PositionAnalyst.h>
#include <Chess/Engine/SearchRequest.h>
#include <Chess/Game/GameStatus.h>
#include <Chess/Game/MoveGenerator.h>
#include <chrono>
#include <thread>

/// Engines made of a few lines, for the tests of the things that use an IChessEngine (the opponent controller, the game, the control adapter): no process, no Stockfish,
/// so the tests do not depend on a machine that has one on PATH. A test that needs a child process uses the fake UCI engine (FakeEngineUtil.h)
namespace BECore::Chess::Tests {

    using MadeEngine = std::expected<eastl::shared_ptr<IChessEngine>, EngineError>;

    inline constexpr std::chrono::milliseconds kEngineWaitLimit{30000};  ///< the give-up limit of a wait for something that must happen: it only ends a broken test, so it is generous under load
    inline constexpr std::chrono::milliseconds kEnginePollInterval{1};
    inline constexpr std::chrono::milliseconds kGateGiveUp{30000};  ///< a gated engine that is never opened answers a Timeout after this, so a lost test frees its worker

    /// Polls @p done until it is true; false when it has not become true within @p timeout. No fixed sleep
    template <typename Condition>
    bool WaitUntil(Condition done, std::chrono::milliseconds timeout = kEngineWaitLimit) {
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + timeout;
        while (!done()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(kEnginePollInterval);
        }
        return true;
    }

    /// An error as a sentence for a failed test ("no error" when there is none)
    inline eastl::string DescribeError(const Optional<EngineError>& error) {
        return error ? Format("{}: {}", EnumUtils<EngineErrorKind>::ToString(error->kind), error->detail) : eastl::string("no error");
    }

    /// The first legal move of the position a request leads to; NoMove when the game is over there
    inline std::expected<Move, EngineError> FirstLegalMove(const SearchRequest& request) {
        const std::expected<Position, EngineError> position = request.FinalPosition();
        if (!position) {
            return std::unexpected(position.error());
        }
        const MoveList moves = MoveGenerator::GenerateLegal(*position);
        if (moves.empty()) {
            return std::unexpected(EngineError{EngineErrorKind::NoMove, "the game is over"});
        }
        return moves.front();
    }

    /// An engine that counts how often it is asked and what limits it got; what it answers is the business of the class that derives from it. The reads are thread safe
    /// (a search runs on a pool worker)
    class RecordingEngine : public IChessEngine {
    public:
        std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken& token) override {
            {
                const std::lock_guard lock(_mutex);
                _lastLimits = request.limits;
            }
            _calls.fetch_add(1);  // after the limits, so a test that waits for the call finds them
            std::expected<Move, EngineError> answer = Answer(request, token);
            _answered.fetch_add(1);
            return answer;
        }

        /// How often the engine has been asked, answered or not
        [[nodiscard]] int Calls() const {
            return _calls.load();
        }

        /// How often it has returned
        [[nodiscard]] int Answered() const {
            return _answered.load();
        }

        /// The limits of the latest request
        [[nodiscard]] SearchLimits LastLimits() const {
            const std::lock_guard lock(_mutex);
            return _lastLimits;
        }

    protected:
        virtual std::expected<Move, EngineError> Answer(const SearchRequest& request, const CancellationToken& token) = 0;

    private:
        mutable std::mutex _mutex;  ///< guards _lastLimits
        SearchLimits _lastLimits;
        std::atomic<int> _calls{0};
        std::atomic<int> _answered{0};
    };

    /// The plainest engine there is: the first legal move of the position, at once
    class InstantEngine final : public RecordingEngine {
    protected:
        std::expected<Move, EngineError> Answer(const SearchRequest& request, const CancellationToken&) override {
            return FirstLegalMove(request);
        }
    };

    /// An engine that mates when it can (the first move that leaves the other side checkmated) and otherwise plays the first legal move, at once: it lets a test
    /// end a game with the program's move from a position set up for it
    class MatingEngine final : public RecordingEngine {
    protected:
        std::expected<Move, EngineError> Answer(const SearchRequest& request, const CancellationToken&) override {
            std::expected<Position, EngineError> position = request.FinalPosition();
            if (!position) {
                return std::unexpected(position.error());
            }
            const MoveList moves = MoveGenerator::GenerateLegal(*position);
            if (moves.empty()) {
                return std::unexpected(EngineError{EngineErrorKind::NoMove, "the game is over"});
            }
            for (const Move move : moves) {
                position->DoMove(move);
                const bool mates = EvaluateStatus(*position) == GameStatus::Checkmate;
                position->UndoMove();
                if (mates) {
                    return move;
                }
            }
            return moves.front();
        }
    };

    /**
     * @brief An engine that holds its answer until Open() is called, then answers like InstantEngine, or with a move chosen in advance.
     *
     * It honors its CancellationToken or ignores it, as asked: a cancelled search that honors it returns Cancelled at once (WasCancelled()), one that ignores
     * it stays at the gate and answers when it is opened, which is how the tests show that a stale answer is thrown away. A gate that is never opened gives up
     * with a Timeout after kGateGiveUp.
     */
    class GatedEngine final : public RecordingEngine {
    public:
        explicit GatedEngine(bool honorsToken = false, Optional<Move> answer = std::nullopt) : _honorsToken(honorsToken), _answer(answer) {}

        void Open() {
            _open.store(true);
        }

        /// A search returned Cancelled because its token was cancelled
        [[nodiscard]] bool WasCancelled() const {
            return _cancelled.load();
        }

    protected:
        std::expected<Move, EngineError> Answer(const SearchRequest& request, const CancellationToken& token) override {
            const bool released = WaitUntil([&] { return _open.load() || (_honorsToken && token.IsCancelled()); }, kGateGiveUp);
            if (_honorsToken && token.IsCancelled()) {
                _cancelled.store(true);
                return std::unexpected(EngineError{EngineErrorKind::Cancelled, "the search was cancelled at the gate"});
            }
            if (!released) {
                return std::unexpected(EngineError{EngineErrorKind::Timeout, "the gate was never opened"});
            }
            return _answer ? std::expected<Move, EngineError>(*_answer) : FirstLegalMove(request);
        }

    private:
        bool _honorsToken;
        Optional<Move> _answer;
        std::atomic<bool> _open{false};
        std::atomic<bool> _cancelled{false};
    };

    /// An engine that fails every search with the error it was given
    class FailingEngine final : public RecordingEngine {
    public:
        explicit FailingEngine(EngineError error) : _error(std::move(error)) {}

    protected:
        std::expected<Move, EngineError> Answer(const SearchRequest&, const CancellationToken&) override {
            return std::unexpected(_error);
        }

    private:
        EngineError _error;
    };

    /**
     * @brief An engine for the tests of the analysis: Analyse() answers a scripted line, or the first legal move of the position with a given score. FindMove() is the first legal move.
     *
     * It counts how often it is asked, keeps the latest request and can hold its answer at a gate until Open(): a gate that honors the CancellationToken returns Cancelled at once when the
     * token is cancelled (WasCancelled()), one that ignores it stays and answers when it is opened, which is how the tests show that a stale answer is thrown away. A gate that is never
     * opened gives up with a Timeout after kGateGiveUp. FailWith() makes every Analyse() fail. For a request of two lines or more it adds a second line: the first legal move that is not the
     * first line's, with the score of SetSecondLine() (a position with no other legal move has one line, as an engine gives it). Configure it while no search runs; the reads are thread safe (a search
     * runs on a pool worker).
     */
    class AnalysingEngine final : public IChessEngine {
    public:
        explicit AnalysingEngine(AnalysisScore score = {AnalysisScoreKind::Centipawns, 35}, Optional<Move> move = std::nullopt, int depth = 7) : _score(score), _move(move), _depth(depth) {}

        /// From now on Analyse() waits for Open() before it answers; @p honorsToken: a cancelled token ends the wait with Cancelled
        void Gate(bool honorsToken) {
            _gated = true;
            _honorsToken = honorsToken;
        }

        void Open() {
            _open.store(true);
        }

        /// The score and depth of the second line a request for two lines or more gets. Configure it while no search runs
        void SetSecondLine(AnalysisScore score, int depth = 6) {
            _secondScore = score;
            _secondDepth = depth;
        }

        /// From now on Analyse() fails with @p error
        void FailWith(EngineError error) {
            _error = std::move(error);
        }

        std::expected<Move, EngineError> FindMove(const SearchRequest& request, const CancellationToken&) override {
            return FirstLegalMove(request);
        }

        std::expected<Analysis, EngineError> Analyse(const SearchRequest& request, const CancellationToken& token) override {
            {
                const std::lock_guard lock(_mutex);
                _lastRequest = request;
            }
            _calls.fetch_add(1);  // after the request, so a test that waits for the call finds it
            std::expected<Analysis, EngineError> answer = Answer(request, token);
            _answered.fetch_add(1);
            return answer;
        }

        /// How often Analyse() was asked, answered or not
        [[nodiscard]] int Calls() const {
            return _calls.load();
        }

        /// How often it has returned
        [[nodiscard]] int Answered() const {
            return _answered.load();
        }

        /// The latest request (empty before the first)
        [[nodiscard]] SearchRequest LastRequest() const {
            const std::lock_guard lock(_mutex);
            return _lastRequest;
        }

        /// A search returned Cancelled because its token was cancelled at the gate
        [[nodiscard]] bool WasCancelled() const {
            return _cancelled.load();
        }

    private:
        std::expected<Analysis, EngineError> Answer(const SearchRequest& request, const CancellationToken& token) {
            if (_gated) {
                const bool released = WaitUntil([&] { return _open.load() || (_honorsToken && token.IsCancelled()); }, kGateGiveUp);
                if (_honorsToken && token.IsCancelled()) {
                    _cancelled.store(true);
                    return std::unexpected(EngineError{EngineErrorKind::Cancelled, "the search was cancelled at the gate"});
                }
                if (!released) {
                    return std::unexpected(EngineError{EngineErrorKind::Timeout, "the gate was never opened"});
                }
            }
            if (_error) {
                return std::unexpected(*_error);
            }
            Move move;
            if (_move) {
                move = *_move;
            } else {
                const std::expected<Move, EngineError> first = FirstLegalMove(request);
                if (!first) {
                    return std::unexpected(first.error());
                }
                move = *first;
            }
            Analysis analysis;
            analysis.lines.push_back(AnalysisLine{move, _score, _depth});
            if (request.lines >= 2) {
                const std::expected<Position, EngineError> position = request.FinalPosition();
                if (position) {
                    for (const Move other : MoveGenerator::GenerateLegal(*position)) {
                        if (other != move) {
                            analysis.lines.push_back(AnalysisLine{other, _secondScore, _secondDepth});
                            break;
                        }
                    }
                }
            }
            return analysis;
        }

        AnalysisScore _score;
        Optional<Move> _move;
        int _depth;
        AnalysisScore _secondScore{AnalysisScoreKind::Centipawns, 10};
        int _secondDepth = 6;
        bool _gated = false;
        bool _honorsToken = false;
        Optional<EngineError> _error;
        mutable std::mutex _mutex;  ///< guards _lastRequest
        SearchRequest _lastRequest;
        std::atomic<bool> _open{false};
        std::atomic<bool> _cancelled{false};
        std::atomic<int> _calls{0};
        std::atomic<int> _answered{0};
    };

    /**
     * @brief The AnalysisEngineFactory of a test: counts how often it is asked and for which engines, and hands out what the test decided.
     *
     * The first call gets the first engine, the second the second, and so on, the last engine repeating; or every call fails with one error. It is asked on the main thread only (where
     * PositionAnalyst::Update() calls it). AsFactory() captures `this`: keep the object alive as long as the analyst can call it, and do not move it.
     */
    class CountingAnalysisFactory {
    public:
        CountingAnalysisFactory(std::initializer_list<eastl::shared_ptr<IChessEngine>> engines) : _engines(engines) {}
        explicit CountingAnalysisFactory(EngineError error) : _error(std::move(error)) {}
        CountingAnalysisFactory(const CountingAnalysisFactory&) = delete;
        CountingAnalysisFactory& operator=(const CountingAnalysisFactory&) = delete;

        [[nodiscard]] AnalysisEngineFactory AsFactory() {
            return [this](OpponentEngine engine) { return Make(engine); };
        }

        /// How often the factory was asked
        [[nodiscard]] int Calls() const {
            return static_cast<int>(_engineIds.size());
        }

        /// The id of the engine ("stockfish", "own") of each call, in order
        [[nodiscard]] const Vector<eastl::string>& Engines() const {
            return _engineIds;
        }

    private:
        MadeEngine Make(OpponentEngine engine) {
            const size_t call = _engineIds.size();
            const StringView engineId = OpponentEngineId(engine);
            _engineIds.emplace_back(engineId.data(), engineId.size());
            if (_error) {
                return std::unexpected(*_error);
            }
            return _engines[eastl::min(call, _engines.size() - 1)];
        }

        Vector<eastl::shared_ptr<IChessEngine>> _engines;
        Optional<EngineError> _error;
        Vector<eastl::string> _engineIds;
    };

    /**
     * @brief The EngineFactory of a test: counts how often it is asked and for which engines and levels, and hands out what the test decided.
     *
     * The first call gets the first engine, the second the second, and so on, the last engine repeating; or every call fails with one error. It is asked on
     * the main thread only (that is where OpponentController calls its factory), so it keeps its log without a lock. AsFactory() captures `this`: keep the
     * object alive as long as the controller can call it, and do not move it.
     */
    class CountingFactory {
    public:
        explicit CountingFactory(std::initializer_list<eastl::shared_ptr<IChessEngine>> engines) : _engines(engines) {}
        explicit CountingFactory(EngineError error) : _error(std::move(error)) {}
        CountingFactory(const CountingFactory&) = delete;
        CountingFactory& operator=(const CountingFactory&) = delete;

        [[nodiscard]] EngineFactory AsFactory() {
            return [this](OpponentEngine engine, const OpponentLevel& level) { return Make(engine, level); };
        }

        /// How often the factory was asked
        [[nodiscard]] int Calls() const {
            return static_cast<int>(_levels.size());
        }

        /// The id of the level of each call, in order
        [[nodiscard]] const Vector<eastl::string>& Levels() const {
            return _levels;
        }

        /// The id of the engine ("stockfish", "own") of each call, in order
        [[nodiscard]] const Vector<eastl::string>& Engines() const {
            return _engineIds;
        }

    private:
        MadeEngine Make(OpponentEngine engine, const OpponentLevel& level) {
            const size_t call = _levels.size();
            const StringView engineId = OpponentEngineId(engine);
            _engineIds.emplace_back(engineId.data(), engineId.size());
            _levels.emplace_back(level.id.data(), level.id.size());
            if (_error) {
                return std::unexpected(*_error);
            }
            return _engines[eastl::min(call, _engines.size() - 1)];
        }

        Vector<eastl::shared_ptr<IChessEngine>> _engines;
        Optional<EngineError> _error;
        Vector<eastl::string> _levels;
        Vector<eastl::string> _engineIds;
    };

}  // namespace BECore::Chess::Tests
