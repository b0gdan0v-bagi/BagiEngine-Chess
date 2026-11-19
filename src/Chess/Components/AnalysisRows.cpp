#include "AnalysisRows.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ClickerDisabled.h>
#include <Chess/Engine/AnalysisText.h>
#include <Chess/Game/ChessGameModel.h>

namespace BECore::Chess {

    namespace {

        struct Ids {
            static constexpr auto analysisBar = "analysisBar"_intern;
            static constexpr auto analysisEvaluationOn = "analysisEvaluationOn"_intern;
            static constexpr auto analysisRatingsOn = "analysisRatingsOn"_intern;
            static constexpr auto analysisText = "analysisText"_intern;
        };

        constexpr double kEmptyBar = 0.0;

        struct Words {
            String text;
            double bar = kEmptyBar;
        };

        /// The words of the review's entry for the position the model shows. A view never shows the game's last position (that one is the game, not a view), so the entry has a best move
        Words ReviewWords(const ChessGameModel& model, const PositionAnalyst& analyst) {
            const Optional<ReviewedPosition> entry = analyst.GetReviewedPosition(model, model.GetShownMoveCount());
            if (!entry) {
                const bool failed = analyst.GetReviewState(model) == ReviewState::Failed;
                return {AnalysisText::Evaluation(std::nullopt, true, false, failed), AnalysisText::kEvenShare};
            }
            const AnalysisLine line{entry->best, ScoreForWhite(entry->score, entry->sideToMove), kReviewDepth};
            return {AnalysisText::Evaluation(line, true, false, false), AnalysisText::Share(line.score)};
        }

        Words GameWords(const ChessGameModel* model, const PositionAnalyst* analyst) {
            const bool on = model && analyst && analyst->IsEvaluationOn();
            Optional<AnalysisLine> lineForWhite;
            bool failed = false;
            bool gameOver = false;
            if (on) {
                lineForWhite = analyst->GetEvaluation(*model);
                if (lineForWhite) {
                    lineForWhite->score = ScoreForWhite(lineForWhite->score, model->GetTurn());
                }
                failed = analyst->GetError().has_value();
                gameOver = model->IsGameOver();
            }
            double bar = kEmptyBar;
            if (on) {
                bar = lineForWhite && !failed && !gameOver ? AnalysisText::Share(lineForWhite->score) : AnalysisText::kEvenShare;
            }
            return {AnalysisText::Evaluation(lineForWhite, on, gameOver, failed), bar};
        }

    }  // namespace

    bool ReviewViewApplies(const ChessGameModel& model, const PositionAnalyst& analyst) {
        return model.IsViewing() && analyst.GetReviewState(model) != ReviewState::Idle;
    }

    bool AnalysisKey::Changed(const ChessGameModel* model, const PositionAnalyst* analyst) {
        const uint64_t version = model ? model->GetVersion() : 0;
        const uint64_t revision = analyst ? analyst->GetRevision() : 0;
        const size_t shownMoves = model ? model->GetShownMoveCount() : 0;
        if (_built && model == _model && analyst == _analyst && version == _version && revision == _revision && shownMoves == _shownMoves) {
            return false;
        }
        _built = true;
        _model = model;
        _analyst = analyst;
        _version = version;
        _revision = revision;
        _shownMoves = shownMoves;
        return true;
    }

    void PublishAnalysisWords(const ChessGameModel* model, const PositionAnalyst* analyst) {
        const bool on = model && analyst && analyst->IsEvaluationOn();
        const Words words = model && analyst && ReviewViewApplies(*model, *analyst) ? ReviewWords(*model, *analyst) : GameWords(model, analyst);

        UIVars& vars = CoreManager::GetUIVars();
        vars.Set(Ids::analysisText, StringView(words.text));
        vars.Set(Ids::analysisBar, StringView(Format("{:.3f}", words.bar)));
        vars.Set(Ids::analysisEvaluationOn, on ? "true" : "false");
        vars.Set(Ids::analysisRatingsOn, model && analyst && analyst->AreRatingsOn() ? "true" : "false");
    }

    bool HintAllowedNow(const ChessGameModel* model) {
        const OpponentController* opponent = ChessGame::GetActiveOpponent();
        const PuzzleSession* puzzle = ChessGame::GetActivePuzzle();
        return model && opponent && puzzle && !model->IsDrafting() && !HintRefusalFor(*model, HintSeatsFor(*model, *puzzle, opponent->GetSides()));
    }

    HintOnCanvas::HintOnCanvas(StringView name) : _name(PoolString::Intern(name)) {}

    void HintOnCanvas::Sync(UICanvasComponent& canvas, const ChessGameModel* model) {
        const bool disabled = !HintAllowedNow(model);
        if (disabled == _disabled) {
            return;
        }
        if (SetClickerDisabled(canvas, _name, disabled)) {
            _disabled = disabled;
        }
    }

}  // namespace BECore::Chess
