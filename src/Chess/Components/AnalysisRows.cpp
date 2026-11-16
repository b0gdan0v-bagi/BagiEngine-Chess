#include "AnalysisRows.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <Chess/ChessGame.h>
#include <Chess/Engine/AnalysisText.h>
#include <Chess/Game/ChessGameModel.h>

namespace BECore::Chess {

    namespace {

        constexpr double kEvenBar = 0.5;
        constexpr double kEmptyBar = 0.0;

    }  // namespace

    bool AnalysisKey::Changed(const ChessGameModel* model, const PositionAnalyst* analyst) {
        const uint64_t version = model ? model->GetVersion() : 0;
        const uint64_t revision = analyst ? analyst->GetRevision() : 0;
        if (_built && model == _model && analyst == _analyst && version == _version && revision == _revision) {
            return false;
        }
        _built = true;
        _model = model;
        _analyst = analyst;
        _version = version;
        _revision = revision;
        return true;
    }

    void PublishAnalysisWords(const ChessGameModel* model, const PositionAnalyst* analyst) {
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
            bar = lineForWhite && !failed && !gameOver ? AnalysisText::Share(lineForWhite->score) : kEvenBar;
        }

        UIVars& vars = CoreManager::GetUIVars();
        vars.Set(PoolString::Intern("analysisText"), StringView(AnalysisText::Evaluation(lineForWhite, on, gameOver, failed)));
        vars.Set(PoolString::Intern("analysisBar"), StringView(Format("{:.3f}", bar)));
        vars.Set(PoolString::Intern("analysisEvaluationOn"), on ? "true" : "false");
    }

    bool HintAllowedNow(const ChessGameModel* model) {
        const OpponentController* opponent = ChessGame::GetActiveOpponent();
        return model && opponent && !HintRefusalFor(*model, opponent->GetSides());
    }

    HintOnCanvas::HintOnCanvas(StringView name) : _name(PoolString::Intern(name)) {}

    void HintOnCanvas::Sync(UICanvasComponent& canvas, const ChessGameModel* model) {
        const bool disabled = !HintAllowedNow(model);
        if (disabled == _disabled) {
            return;
        }
        UIElement* button = canvas.GetRoot().FindByName(_name);
        UIInputClicker* clicker = button ? button->GetComponent<UIInputClicker>() : nullptr;
        if (clicker) {
            clicker->disabled = disabled;
            _disabled = disabled;
        }
    }

}  // namespace BECore::Chess
