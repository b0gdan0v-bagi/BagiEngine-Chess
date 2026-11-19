#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore::Chess {

    /**
     * @brief Marks a brilliant move on the board: a gold badge with a dark "!!" in the top right corner of the destination square of the move the board shows.
     *
     * The shown move is the last one of the position the board shows (ChessGameModel::GetShownMoveCount()), so a viewed earlier position shows that move's mark. It draws only while the
     * analyst's ratings are on (the person's switch: PositionAnalyst::AreRatingsOn()) and that move is rated brilliant; nothing for an unrated move, a draft or no game. The squares are
     * placed by ChessBoardComponent::SquareToPixel(), so the turned board and every view follow; the badge and the glyph are filled rects (Game/RatingMark.h).
     *
     * The node is on layer 1 and listed before `Result`, so the dim of a finished game covers the mark. Owns no state and allocates nothing per frame: it asks the analyst for one rating.
     */
    class ChessRatingMarkComponent : public IComponent {
        BE_CLASS(ChessRatingMarkComponent)
    public:
        ChessRatingMarkComponent() = default;
        ~ChessRatingMarkComponent() override = default;

        void OnDraw() override;
    };

}  // namespace BECore::Chess
