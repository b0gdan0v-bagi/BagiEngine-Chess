#pragma once

#include <Chess/Engine/PositionAnalyst.h>

namespace BECore {
    class UICanvasComponent;
}

namespace BECore::Chess {

    class ChessGameModel;

    /// What the evaluation words were last built from: the model and its version, the analyst and its revision. A view of an earlier position moves none of them: the evaluation is of the
    /// game's position
    class AnalysisKey {
    public:
        /// Whether the words of @p model and @p analyst are other than the ones last built (always on the first call); remembers them as built
        bool Changed(const ChessGameModel* model, const PositionAnalyst* analyst);

    private:
        bool _built = false;
        const ChessGameModel* _model = nullptr;
        const PositionAnalyst* _analyst = nullptr;
        uint64_t _version = 0;
        uint64_t _revision = 0;
    };

    /// Writes the UIVars the evaluation cell of every canvas that shows it binds: `analysisText` (AnalysisText::Evaluation, the line in White's view), `analysisBar` (AnalysisText::Share as
    /// a number; 0 while the evaluation is off, an even bar while it has no line) and `analysisEvaluationOn` ("true" / "false", the value the toggle compares). With no game: off
    void PublishAnalysisWords(const ChessGameModel* model, const PositionAnalyst* analyst);

    /**
     * @brief The Hint button `<name>` of one canvas: greyed (its clicker `disabled`) while HintRefusalFor() refuses, so a press that would do nothing looks it.
     *
     * The node file authors it disabled, as a game with no one to ask starts greyed. Remembers what it set, so a call changes the clicker only when the answer changes. With no game it stays greyed.
     */
    class HintOnCanvas {
    public:
        explicit HintOnCanvas(StringView name);

        void Sync(UICanvasComponent& canvas, const ChessGameModel* model);

    private:
        PoolString _name;
        bool _disabled = true;
    };

    /// Whether the game of @p model may be asked for a hint now (HintRefusalFor() with the seats the active opponent plays); false with no game and while a position is edited (the hint would end the
    /// draft)
    [[nodiscard]] bool HintAllowedNow(const ChessGameModel* model);

}  // namespace BECore::Chess
