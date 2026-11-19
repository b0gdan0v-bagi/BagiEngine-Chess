#pragma once

#include <BECore/Scene/IComponent.h>
#include <Chess/Game/ChessTypes.h>

namespace BECore::InputEvents {
    struct ActionEvent;
}

namespace BECore::Chess {

    class ChessGame;

    /**
     * @brief The menu section "Puzzles", on the node `PuzzleSection` before its canvas: answers `ChessPuzzleNext` (Start and Next: the first puzzle when none is on, else the one after),
     * `ChessPuzzleRetry` and `ChessPuzzleLeave`.
     *
     * The actions are answered wherever they come from, also while the canvas is hidden (the puzzle bar presses the same three), and each asks the game's rule again, because the grey look of a
     * button is from the last draw. After a success the menu closes (`ChessCloseMenu`, harmless when none is open), so the board is in front. The component owns no state of the game: the
     * lines `{puzzleNumber}`, `{puzzleRating}`, `{puzzleState}` and the word `{puzzleNextWord}` are written from the session when the game, its model's version, the session's revision, the
     * size of the set or whether a puzzle is on changed (never per frame). Start / Next is greyed while the set is empty, Retry and Leave while no puzzle is on.
     */
    class ChessPuzzleSectionComponent : public IComponent {
        BE_CLASS(ChessPuzzleSectionComponent)
    public:
        ChessPuzzleSectionComponent() = default;
        ~ChessPuzzleSectionComponent() override = default;

        void OnAttached() override;
        void OnDraw() override;

    private:
        void OnAction(const InputEvents::ActionEvent& event);

        bool _published = false;
        const ChessGame* _publishedGame = nullptr;
        const void* _publishedModel = nullptr;
        uint64_t _publishedVersion = 0;
        uint64_t _publishedRevision = 0;
        size_t _publishedCount = 0;
        bool _publishedOn = false;

        // What the XML starts with: every button live
        bool _nextDisabled = false;
        bool _retryDisabled = false;
        bool _leaveDisabled = false;
    };

}  // namespace BECore::Chess
