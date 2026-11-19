#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Game/ChessTypes.h>

namespace BECore::InputEvents {
    struct ActionEvent;
}

namespace BECore::Chess {

    class ChessGame;
    class ChessGameModel;

    /**
     * @brief The menu section "Review", on the node `ReviewSection` before its canvas: answers `ChessReviewStart` and `ChessReviewCancel`.
     *
     * The actions are answered wherever they come from, also while the canvas is hidden, and each asks the rule again, because the grey look of a
     * button is from the last draw. Start does not close the menu: the person watches the progress here. Cancel drops the review in any state but Idle, a done one too (that is how its marks
     * go). The component owns no state of the game: `{reviewStatus}` and the two greyed looks are written from the game when the game, its model's version, the analyst's revision or whether a
     * puzzle is on changed (never per frame; the revision moves with every position that arrives, a start, a cancel and a failure, the version with a takeback or a reset that drop the review).
     */
    class ChessReviewSectionComponent : public IComponent {
        BE_CLASS(ChessReviewSectionComponent)
    public:
        ChessReviewSectionComponent() = default;
        ~ChessReviewSectionComponent() override = default;

        void OnAttached() override;
        void OnDraw() override;

    private:
        void OnAction(const InputEvents::ActionEvent& event);

        bool _published = false;
        const ChessGame* _publishedGame = nullptr;
        const ChessGameModel* _publishedModel = nullptr;
        uint64_t _publishedVersion = 0;
        uint64_t _publishedRevision = 0;
        bool _publishedPuzzleOn = false;

        // What the XML starts with: Start live, Cancel grey
        bool _startDisabled = false;
        bool _cancelDisabled = true;
    };

}  // namespace BECore::Chess
