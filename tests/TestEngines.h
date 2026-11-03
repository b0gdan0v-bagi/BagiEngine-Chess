#pragma once

#include "ChessTestUtil.h"

#include <Chess/Engine/OpponentController.h>
#include <Chess/Engine/SearchRequest.h>
#include <Chess/Game/MoveGenerator.h>
#include <chrono>
#include <thread>

/// Engines made of a few lines, for the tests of the things that use an IChessEngine (the opponent controller, the game, the control adapter): no process, no Stockfish,
/// so the tests do not depend on a machine that has one on PATH. A test that needs a child process uses the fake UCI engine (FakeEngineUtil.h)
namespace BECore::Chess::Tests {

    using MadeEngine = std::expected<eastl::shared_ptr<IChessEngine>, EngineError>;

    inline constexpr std::chrono::milliseconds kEngineWaitLimit{15000};  ///< how long a test waits for something that must happen
    inline constexpr std::chrono::milliseconds kEnginePollInterval{1};
    inline constexpr std::chrono::milliseconds kGateGiveUp{10000};  ///< a gated engine that is never opened answers a Timeout after this, so a lost test frees its worker

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
    inline eastl::string DescribeError(const std::optional<EngineError>& error) {
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

    /**
     * @brief An engine that holds its answer until Open() is called, then answers like InstantEngine, or with a move chosen in advance.
     *
     * It honors its CancellationToken or ignores it, as asked: a cancelled search that honors it returns Cancelled at once (WasCancelled()), one that ignores
     * it stays at the gate and answers when it is opened, which is how the tests show that a stale answer is thrown away. A gate that is never opened gives up
     * with a Timeout after kGateGiveUp.
     */
    class GatedEngine final : public RecordingEngine {
    public:
        explicit GatedEngine(bool honorsToken = false, std::optional<Move> answer = std::nullopt) : _honorsToken(honorsToken), _answer(answer) {}

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
        std::optional<Move> _answer;
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

    /// Counts how often the engine inside it is asked and passes every request on; for an engine that is not a RecordingEngine
    class CountingEngine final : public RecordingEngine {
    public:
        explicit CountingEngine(eastl::shared_ptr<IChessEngine> inner) : _inner(std::move(inner)) {}

    protected:
        std::expected<Move, EngineError> Answer(const SearchRequest& request, const CancellationToken& token) override {
            return _inner->FindMove(request, token);
        }

    private:
        eastl::shared_ptr<IChessEngine> _inner;
    };

    /**
     * @brief The EngineFactory of a test: counts how often it is asked and for which levels, and hands out what the test decided.
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
            return [this](const OpponentLevel& level) { return Make(level); };
        }

        /// How often the factory was asked
        [[nodiscard]] int Calls() const {
            return static_cast<int>(_levels.size());
        }

        /// The id of the level of each call, in order
        [[nodiscard]] const eastl::vector<eastl::string>& Levels() const {
            return _levels;
        }

    private:
        MadeEngine Make(const OpponentLevel& level) {
            const size_t call = _levels.size();
            _levels.emplace_back(level.id.data(), level.id.size());
            if (_error) {
                return std::unexpected(*_error);
            }
            return _engines[eastl::min(call, _engines.size() - 1)];
        }

        eastl::vector<eastl::shared_ptr<IChessEngine>> _engines;
        std::optional<EngineError> _error;
        eastl::vector<eastl::string> _levels;
    };

}  // namespace BECore::Chess::Tests
