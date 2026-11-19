#pragma once

#include <Chess/Engine/PositionAnalyst.h>

namespace BECore {
    class UICanvasComponent;
}

namespace BECore::Chess {

    class ChessGameModel;

    /// What the evaluation words were last built from: the model, its version and the move count it shows, the analyst and its revision. A step of a view moves neither version nor revision,
    /// so the shown count is part of the key: with a review held the words are those of the viewed position
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
        size_t _shownMoves = 0;
    };

    /// Whether the evaluation cell shows the review's evaluation of the viewed position: an earlier position is viewed and a review is held (any state but Idle). Else it shows the game's own
    [[nodiscard]] bool ReviewViewApplies(const ChessGameModel& model, const PositionAnalyst& analyst);

    /// Writes the UIVars the evaluation cell of every canvas that shows it binds: `analysisText` (AnalysisText::Evaluation, the line in White's view), `analysisBar` (AnalysisText::Share as
    /// a number; 0 while the evaluation is off, an even bar while it has no line) and `analysisEvaluationOn` ("true" / "false", the value the toggle compares) and `analysisRatingsOn` (the same for
    /// the ratings' toggle). With no game: both off. While ReviewViewApplies() the text and the bar are the review's entry of the viewed position (the evaluation's switch does not matter): its
    /// score in White's view, "..." with an even bar while it has not come, "No analysis" when the review failed before it
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
