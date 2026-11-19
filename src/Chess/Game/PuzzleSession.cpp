#include "PuzzleSession.h"

namespace BECore::Chess {

    namespace {

        /// The person's moves stand at the odd places of the line and of the model's moves: the program plays the setup move first
        constexpr size_t kSetupMoves = 1;

        bool IsPersonsPlace(size_t moveIndex) {
            return moveIndex % 2 == kSetupMoves;
        }

    }  // namespace

    void PuzzleSession::SetPuzzles(PuzzleSet puzzles) {
        End();
        _puzzles = eastl::move(puzzles);
    }

    bool PuzzleSession::Begin(ChessGameModel& model, size_t index) {
        if (index >= _puzzles.Size()) {
            return false;
        }
        const Puzzle& puzzle = _puzzles.At(index);
        const auto position = Position::FromFen(puzzle.startFen);
        if (!position) {
            return false;
        }
        model.SetPosition(*position);
        if (!model.TryMove(puzzle.line[0])) {
            return false;
        }
        _index = index;
        _startFen = model.GetStartFen();
        _mistakes = 0;
        _wrong = false;
        _seenMoves = model.GetMoves().size();
        _replyWait = 0.0f;
        ++_revision;
        return true;
    }

    bool PuzzleSession::Retry(ChessGameModel& model) {
        return IsActiveFor(model) && Begin(model, *_index);
    }

    bool PuzzleSession::Next(ChessGameModel& model) {
        if (_puzzles.IsEmpty()) {
            return false;
        }
        return Begin(model, IsActiveFor(model) ? (*_index + 1) % _puzzles.Size() : 0);
    }

    void PuzzleSession::End() {
        if (!_index) {
            return;
        }
        _index.reset();
        _mistakes = 0;
        _wrong = false;
        ++_revision;
    }

    bool PuzzleSession::Leave(ChessGameModel& model) {
        const bool wasOn = IsActiveFor(model);
        End();
        if (wasOn) {
            model.Reset();
        }
        return wasOn;
    }

    const Puzzle* PuzzleSession::GetCurrent() const {
        return _index ? &_puzzles.At(*_index) : nullptr;
    }

    bool PuzzleSession::IsActiveFor(const ChessGameModel& model) const {
        const Puzzle* puzzle = GetCurrent();
        if (puzzle == nullptr || model.GetStartFen() != _startFen) {
            return false;
        }
        const Vector<Move>& moves = model.GetMoves();
        if (moves.empty() || moves.size() > puzzle->line.size()) {
            return false;
        }
        const size_t last = moves.size() - 1;
        for (size_t i = 0; i < moves.size(); ++i) {
            // Only the last move of the person may differ: Update() has not judged it yet
            if (moves[i] != puzzle->line[i] && !(i == last && IsPersonsPlace(i))) {
                return false;
            }
        }
        return true;
    }

    bool PuzzleSession::LastPersonMoveIsRight(const ChessGameModel& model) const {
        const Vector<Move>& moves = model.GetMoves();
        if (moves.empty() || IsPersonsPlace(moves.size())) {
            return false;
        }
        return moves.back() == GetCurrent()->line[moves.size() - 1] || model.GetStatus() == GameStatus::Checkmate;
    }

    bool PuzzleSession::Update(ChessGameModel& model, float deltaSeconds) {
        if (!IsActiveFor(model)) {
            return false;
        }
        const Puzzle& puzzle = *GetCurrent();
        const size_t count = model.GetMoves().size();
        const bool personMoved = !IsPersonsPlace(count);

        if (count != _seenMoves) {
            _seenMoves = count;
            _replyWait = 0.0f;
            if (!personMoved) {
                return false;
            }
            _wrong = false;
            if (!LastPersonMoveIsRight(model)) {
                model.TakeBack(1);
                _seenMoves = count - 1;
                ++_mistakes;
                _wrong = true;
            }
            ++_revision;
            return true;
        }

        if (!personMoved || count >= puzzle.line.size() || model.IsGameOver()) {
            return false;
        }
        if (deltaSeconds > 0.0f) {
            _replyWait += deltaSeconds;
        }
        if (_replyWait < kPuzzleReplySeconds || !model.TryMove(puzzle.line[count])) {
            return false;
        }
        _seenMoves = count + 1;
        _replyWait = 0.0f;
        ++_revision;
        return true;
    }

    PuzzlePhase PuzzleSession::GetPhase(const ChessGameModel& model) const {
        if (GetCurrent() == nullptr || !LastPersonMoveIsRight(model)) {
            return PuzzlePhase::YourMove;
        }
        if (model.GetStatus() == GameStatus::Checkmate || model.GetMoves().size() >= GetCurrent()->line.size()) {
            return PuzzlePhase::Solved;
        }
        return PuzzlePhase::Replying;
    }

    size_t PuzzleSession::GetStep(const ChessGameModel& model) const {
        const size_t count = model.GetMoves().size();
        if (IsPersonsPlace(count)) {
            return count / 2;
        }
        if (count == 0) {
            return 0;
        }
        return LastPersonMoveIsRight(model) ? count / 2 : count / 2 - 1;
    }

    size_t PuzzleSession::GetSteps() const noexcept {
        const Puzzle* puzzle = GetCurrent();
        return puzzle == nullptr ? 0 : puzzle->SolverMoveCount();
    }

}  // namespace BECore::Chess
