#include "ChessGame.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UIWebText.h>
#include <Chess/Game/BoardOrientation.h>
#include <Chess/Game/EditorText.h>
#include <Chess/Game/GameEnding.h>
#include <Games/GameManager.h>
#include <Generated/ChessGame.gen.hpp>
#include <random>

namespace BECore::Chess {

    OpeningBook ChessGame::LoadOpeningBook(StringView virtualPath) {
        const std::filesystem::path file = CoreManager::GetFileSystem().ResolvePath(virtualPath);
        if (file.empty()) {
            LOG_WARNING("Opening names: '{}' not found, no opening is named"_format(virtualPath));
            return {};
        }
        std::ifstream stream(file, std::ios::binary);  //-NoStd: engine-file-layer: a text file of the content, read whole
        if (!stream.is_open()) {
            LOG_WARNING("Opening names: '{}' cannot be read, no opening is named"_format(virtualPath));
            return {};
        }
        std::ostringstream content;  //-NoStd: engine-file-layer: see above
        content << stream.rdbuf();
        const std::string text = content.str();  //-NoStd: engine-file-layer: see above
        Vector<String> problems;
        OpeningBook book = OpeningBook::Parse(StringView(text.data(), text.size()), &problems);
        for (const String& problem : problems) {
            LOG_WARNING("Opening names: {}"_format(problem));
        }
        return book;
    }

    PuzzleSet ChessGame::LoadPuzzles(StringView virtualPath) {
        const std::filesystem::path file = CoreManager::GetFileSystem().ResolvePath(virtualPath);
        if (file.empty()) {
            LOG_WARNING("Puzzles: '{}' not found, there are no puzzles"_format(virtualPath));
            return {};
        }
        std::ifstream stream(file, std::ios::binary);  //-NoStd: engine-file-layer: a text file of the content, read whole
        if (!stream.is_open()) {
            LOG_WARNING("Puzzles: '{}' cannot be read, there are no puzzles"_format(virtualPath));
            return {};
        }
        std::ostringstream content;  //-NoStd: engine-file-layer: see above
        content << stream.rdbuf();
        const std::string text = content.str();  //-NoStd: engine-file-layer: see above
        Vector<String> problems;
        PuzzleSet puzzles = PuzzleSet::Parse(StringView(text.data(), text.size()), &problems);
        for (const String& problem : problems) {
            LOG_WARNING("Puzzles: {}"_format(problem));
        }
        return puzzles;
    }

    ChessGame* ChessGame::GetActive() {
        auto* active = GameManager::GetInstance().GetActive();
        return active ? active->Cast<ChessGame>() : nullptr;
    }

    ChessGameModel* ChessGame::GetActiveModel() {
        ChessGame* chess = GetActive();
        return chess ? &chess->GetModel() : nullptr;
    }

    OpponentController* ChessGame::GetActiveOpponent() {
        ChessGame* chess = GetActive();
        return chess ? &chess->GetOpponent() : nullptr;
    }

    OpponentChoice* ChessGame::GetActiveChoice() {
        ChessGame* chess = GetActive();
        return chess ? &chess->GetChoice() : nullptr;
    }

    PositionAnalyst* ChessGame::GetActiveAnalyst() {
        ChessGame* chess = GetActive();
        return chess ? &chess->GetAnalyst() : nullptr;
    }

    DrawOfferJudge* ChessGame::GetActiveDrawJudge() {
        ChessGame* chess = GetActive();
        return chess ? &chess->GetDrawJudge() : nullptr;
    }

    const OpeningBook* ChessGame::GetActiveOpenings() {
        ChessGame* chess = GetActive();
        return chess ? &chess->GetOpenings() : nullptr;
    }

    PositionEditor* ChessGame::GetActiveEditor() {
        ChessGame* chess = GetActive();
        return chess ? &chess->GetEditor() : nullptr;
    }

    PuzzleSession* ChessGame::GetActivePuzzle() {
        ChessGame* chess = GetActive();
        return chess ? &chess->GetPuzzle() : nullptr;
    }

    ChessGame::ChessGame() : _opponent(_model), _choice(_opponent), _control(_model, _opponent, _analyst, _drawJudge, _openings, _editor) {
        // A new game draws a new seed; a device per draw because the source has to be copyable and a Reset is rare
        _model.SetSeedSource([]() -> uint32_t {
            //-NoStd: the operating system's randomness is the real boundary of the seed
            std::random_device device;
            return device();
        });
        // The settings are read at the time of the search, so the ones Start() loads are the ones used
        _analyst.SetEngineFactory([this](OpponentEngine engine) { return MakeAnalysisEngine(engine, _opponent.GetSettings()); });
        // A factory is move-only and the analyst's is replaced by tests after this constructor, so the judge asks the analyst at the time of the offer
        _drawJudge.SetEngineFactory([this](OpponentEngine engine) { return _analyst.MakeEngine(engine); });
    }

    IControllableGame* ChessGame::AsControllable() {
        return &_control;
    }

    PoolString ChessGame::GetName() const {
        return "Chess"_intern;
    }

    PoolString ChessGame::GetSceneName() const {
        return "ChessScene"_intern;
    }

    void ChessGame::Start() {
        _model.Reset();
        _openings = LoadOpeningBook(kOpeningsPath);
        _puzzle.SetPuzzles(LoadPuzzles(kPuzzlesPath));
        _opponent.SetSettings(OpponentSettings::Load());
        // Two people: nothing is looked up or started until a side is picked, so none of the three can be refused (DefaultEngine() is Stockfish only when a file was found by SetSettings())
        [[maybe_unused]] const auto side = _opponent.SetSide(std::nullopt);
        [[maybe_unused]] const auto level = _opponent.SetLevel(kDefaultOpponentLevel);
        [[maybe_unused]] const auto engine = _opponent.SetEngine(_opponent.DefaultEngine());
        _choice.Reset();      // a new game starts with White remembered as the person's color
        _analyst.Shutdown();  // nothing is analysed or rated until the evaluation or the ratings are switched on or a hint is asked for
        _drawJudge.Shutdown();
        // Last: the game it restores goes into the fresh model and choice above, and the key it takes is that of the game it leaves
        _keeper.Start(_model, _opponent, _choice);
        _stopped = false;
    }

    void ChessGame::UpdateKeeper() {
        if (_stopped) {
            return;  // frames that still run after Stop() must not save what Shutdown() did to the controller (two people)
        }
        _keeper.Update(_model, _opponent, _choice, IsPuzzleOn());
    }

    void ChessGame::Stop() {
        // Written first, while the opponent still holds what the person picked (Shutdown() forgets it); the opponent goes before TaskManager::Shutdown() (ChessGame.h)
        _keeper.Flush(_model, _opponent, _choice, IsPuzzleOn());
        _stopped = true;
        _opponent.Shutdown();
        _analyst.Shutdown();
        _drawJudge.Shutdown();
    }

    std::expected<void, DrawOfferRefusal> ChessGame::OfferDraw() {
        if (IsPuzzleOn()) {
            return std::unexpected(DrawOfferRefusal::Puzzle);
        }
        return PersonOffersDraw(_model, _opponent.GetSides(), _drawJudge);
    }

    bool ChessGame::CanOfferDraw() const {
        return !IsPuzzleOn() && !DrawOfferRefusalFor(_model, _opponent.GetSides()).has_value();
    }

    Optional<PieceColor> ChessGame::ResigningNow() const {
        if (_model.IsGameOver() || IsPuzzleOn()) {
            return std::nullopt;
        }
        const ProgramSides sides = _opponent.GetSides();
        return ResigningColor(_model.GetTurn(), _opponent.GetPlays(), sides.white && sides.black);
    }

    void ChessGame::BeginEditing(EditStart start) {
        switch (start) {
            case EditStart::Current:
                _editor.Load(_model.GetPosition());
                break;
            case EditStart::Empty:
                _editor.Clear();
                break;
            case EditStart::Start:
                _editor.SetStart();
                break;
        }
        _editorMessage.clear();
        _model.ShowDraft(_editor.GetSquares());
    }

    void ChessGame::EditChanged() {
        if (!IsEditing()) {
            return;
        }
        _editorMessage.clear();
        _model.ShowDraft(_editor.GetSquares());
    }

    bool ChessGame::PlayFromHere() {
        if (!IsEditing()) {
            return false;
        }
        const auto position = _editor.Build();
        if (!position) {
            _editorMessage = String(EditorText::Refusal(position.error()));
            return false;
        }
        _editorMessage.clear();
        _model.SetPosition(*position);
        return true;
    }

    bool ChessGame::PasteFen(StringView text) {
        text = UIWebText::TrimSpace(text);
        // LoadFen() leaves the draft as it was on an error; with no editor open nothing is shown either way
        if (const auto result = _editor.LoadFen(text); !result) {
            _editorMessage = String(EditorText::Refusal(result.error()));
            return false;
        }
        _editorMessage.clear();
        _model.ShowDraft(_editor.GetSquares());
        return true;
    }

    void ChessGame::SetEditorMessage(StringView message) {
        _editorMessage = String(message);
    }

    void ChessGame::CancelEditing() {
        if (IsEditing()) {
            _editor.MarkClosed();  // the game's version does not move, and the control state loses its "editor"
        }
        _editorMessage.clear();
        _model.EndDraft();
    }

    void ChessGame::Reset() {
        _model.Reset();
    }

    bool ChessGame::BeginPuzzle(size_t index) {
        return _puzzle.Begin(_model, index);
    }

    bool ChessGame::NextPuzzle() {
        return _puzzle.Next(_model);
    }

    bool ChessGame::RetryPuzzle() {
        return _puzzle.Retry(_model);
    }

    bool ChessGame::LeavePuzzle() {
        const bool wasOn = IsPuzzleOn();
        _puzzle.End();
        if (wasOn) {
            _model.Reset();
        }
        return wasOn;
    }

    void ChessGame::UpdatePuzzle(float deltaSeconds) {
        _puzzle.Update(_model, deltaSeconds);
    }

    PieceColor ChessGame::BoardBottomColor() const {
        if (const Puzzle* puzzle = _puzzle.GetCurrent(); puzzle && IsPuzzleOn()) {
            return puzzle->SolverColor();
        }
        return BottomColor(_opponent.GetPlays());
    }

    bool ChessGame::ProgramToMoveNow() const {
        if (const Puzzle* puzzle = _puzzle.GetCurrent(); puzzle && IsPuzzleOn()) {
            return _model.GetTurn() != puzzle->SolverColor();
        }
        return _opponent.PlaysFor(_model.GetTurn());
    }

}  // namespace BECore::Chess
