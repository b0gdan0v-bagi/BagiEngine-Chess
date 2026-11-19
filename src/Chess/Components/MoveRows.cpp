#include "MoveRows.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ClickerDisabled.h>
#include <Chess/Engine/OpponentController.h>
#include <Chess/Engine/PositionAnalyst.h>
#include <Chess/Game/ChessGameModel.h>
#include <Chess/Game/MoveList.h>
#include <Chess/Game/TakeBack.h>

namespace BECore::Chess {

    namespace {

        constexpr int kNoCurrentPly = -1;

        // Interned on the first use, not per frame; indexed by row
        struct MoveWordNames {
            Array<PoolString, kMovePanelRows> numbers;
            Array<PoolString, kMovePanelRows> whites;
            Array<PoolString, kMovePanelRows> blacks;
        };

        const MoveWordNames& GetWordNames() {
            static const MoveWordNames names = [] {
                MoveWordNames made;
                for (size_t row = 0; row < kMovePanelRows; ++row) {
                    made.numbers[row] = PoolString::Intern(Format("movePanelNumber{}", row).c_str());
                    made.whites[row] = PoolString::Intern(Format("movePanelWhite{}", row).c_str());
                    made.blacks[row] = PoolString::Intern(Format("movePanelBlack{}", row).c_str());
                }
                return made;
            }();
            return names;
        }

    }  // namespace

    Vector<MovePanelRow> CurrentMovePanelRows(const ChessGameModel* model, const PositionAnalyst* analyst) {
        Vector<MoveListRow> list;
        int shownPly = kNoCurrentPly;
        if (model) {
            list = BuildMoveList(model->GetStartFen(), model->GetMoves());
            shownPly = static_cast<int>(model->GetShownMoveCount()) - 1;
        }
        Vector<MovePanelRow> rows = BuildMovePanelRows(list, shownPly);
        ASSERT(rows.size() == kMovePanelRows);
        if (model && analyst && analyst->AreRatingsOn()) {
            const MoveRatings ratings = analyst->GetRatings(*model);
            Vector<bool> brilliantByPly;
            brilliantByPly.reserve(ratings.size());
            for (const Optional<MoveRating>& rating : ratings) {
                brilliantByPly.push_back(rating && rating->IsBrilliant());
            }
            MarkMovePanelRows(rows, brilliantByPly);
        }
        return rows;
    }

    bool MoveRowsKey::Changed(const ChessGameModel* model, const PositionAnalyst* analyst) {
        const uint64_t version = model ? model->GetVersion() : 0;
        const uint64_t analystRevision = analyst ? analyst->GetRevision() : 0;
        const size_t shownMoves = model ? model->GetShownMoveCount() : 0;
        if (_built && model == _model && analyst == _analyst && version == _version && analystRevision == _analystRevision && shownMoves == _shownMoves) {
            return false;
        }
        _built = true;
        _model = model;
        _analyst = analyst;
        _version = version;
        _analystRevision = analystRevision;
        _shownMoves = shownMoves;
        return true;
    }

    void PublishMovePanelWords(const Vector<MovePanelRow>& rows) {
        const MoveWordNames& names = GetWordNames();
        UIVars& vars = CoreManager::GetUIVars();
        for (size_t row = 0; row < kMovePanelRows && row < rows.size(); ++row) {
            vars.Set(names.numbers[row], StringView(rows[row].number));
            vars.Set(names.whites[row], StringView(rows[row].white.text));
            vars.Set(names.blacks[row], StringView(rows[row].black.text));
        }
    }

    MoveRowsOnCanvas::MoveRowsOnCanvas(StringView prefix) {
        for (size_t row = 0; row < kMovePanelRows; ++row) {
            _rows[row] = PoolString::Intern(Format("{}Row{}", prefix, row).c_str());
            _whiteMarks[row] = PoolString::Intern(Format("{}WhiteMark{}", prefix, row).c_str());
            _blackMarks[row] = PoolString::Intern(Format("{}BlackMark{}", prefix, row).c_str());
        }
    }

    void MoveRowsOnCanvas::Show(UICanvasComponent& canvas, const Vector<MovePanelRow>& rows) {
        for (size_t row = 0; row < kMovePanelRows && row < rows.size(); ++row) {
            const MovePanelRow& entry = rows[row];
            if (entry.shown != _rowShown[row]) {
                _rowShown[row] = entry.shown;
                canvas.SetElementVisible(_rows[row], entry.shown);
            }
            if (entry.white.current != _whiteMarked[row]) {
                _whiteMarked[row] = entry.white.current;
                canvas.SetElementVisible(_whiteMarks[row], entry.white.current);
            }
            if (entry.black.current != _blackMarked[row]) {
                _blackMarked[row] = entry.black.current;
                canvas.SetElementVisible(_blackMarks[row], entry.black.current);
            }
        }
    }

    size_t TakeBackPliesNow(const ChessGameModel* model) {
        const ChessGame* game = ChessGame::GetActive();
        if (!model || (game && game->IsPuzzleOn())) {
            return 0;  // a takeback would break the line; the judge takes back a wrong move by itself
        }
        const OpponentController* opponent = ChessGame::GetActiveOpponent();
        return TakeBackPlies(model->GetMoves().size(), model->GetTurn(), opponent ? opponent->GetPlays() : std::nullopt);
    }

    TakeBackOnCanvas::TakeBackOnCanvas(StringView prefix) : _name(PoolString::Intern(Format("{}TakeBack", prefix).c_str())) {}

    void TakeBackOnCanvas::Sync(UICanvasComponent& canvas, const ChessGameModel* model) {
        const bool disabled = TakeBackPliesNow(model) == 0;
        if (disabled == _disabled) {
            return;
        }
        if (SetClickerDisabled(canvas, _name, disabled)) {
            _disabled = disabled;
        }
    }

}  // namespace BECore::Chess
