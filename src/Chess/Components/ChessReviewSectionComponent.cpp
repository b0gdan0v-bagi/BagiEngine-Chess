#include "ChessReviewSectionComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIVars.h>
#include <Chess/ChessGame.h>
#include <Chess/Components/ClickerDisabled.h>
#include <Chess/Engine/ReviewText.h>
#include <Events/InputEvents.h>
#include <Generated/ChessReviewSectionComponent.gen.hpp>

namespace BECore::Chess {

    namespace {

        struct Ids {
            static constexpr auto chessReviewCancel = "ChessReviewCancel"_intern;
            static constexpr auto chessReviewStart = "ChessReviewStart"_intern;
            static constexpr auto reviewSectionCancel = "ReviewSectionCancel"_intern;
            static constexpr auto reviewSectionStart = "ReviewSectionStart"_intern;
            static constexpr auto reviewStatus = "reviewStatus"_intern;
        };

        /// The one rule of a start, for the button's grey look and for the action. A start on a review that runs or is done does nothing in the reviewer, so it reads grey as well
        bool MayStart(const ChessGame& game) {
            const ReviewState state = game.GetAnalyst().GetReviewState(game.GetModel());
            return !game.IsPuzzleOn() && !ReviewRefusalFor(game.GetModel()) && (state == ReviewState::Idle || state == ReviewState::Failed);
        }

        /// Writes @p disabled to the button only when it differs from @p known, which then follows it
        void Grey(UICanvasComponent& canvas, PoolString name, bool disabled, bool& known) {
            if (disabled != known) {
                known = disabled;
                SetClickerDisabled(canvas, name, disabled);
            }
        }

    }  // namespace

    void ChessReviewSectionComponent::OnAttached() {
        SubscribeInScene<InputEvents::ActionEvent, &ChessReviewSectionComponent::OnAction>(this);
    }

    void ChessReviewSectionComponent::OnAction(const InputEvents::ActionEvent& event) {
        const PoolString action = event.action;
        const bool start = action == Ids::chessReviewStart;
        const bool cancel = action == Ids::chessReviewCancel;
        ChessGame* game = (start || cancel) ? ChessGame::GetActive() : nullptr;
        if (!game) {
            return;
        }
        if (start) {
            if (MayStart(*game)) {
                game->GetAnalyst().StartReview(game->GetModel());
            }
        } else {
            game->GetAnalyst().AbandonReview();
        }
    }

    void ChessReviewSectionComponent::OnDraw() {
        const auto canvas = GetNode().GetComponent<UICanvasComponent>();
        if (!canvas) {
            return;
        }
        const ChessGame* game = ChessGame::GetActive();
        const ChessGameModel* model = game ? &game->GetModel() : nullptr;
        const uint64_t version = model ? model->GetVersion() : 0;
        const uint64_t revision = game ? game->GetAnalyst().GetReviewRevision() : 0;
        const bool puzzleOn = game && game->IsPuzzleOn();
        if (_published && game == _publishedGame && model == _publishedModel && version == _publishedVersion && revision == _publishedRevision && puzzleOn == _publishedPuzzleOn) {
            return;
        }
        _published = true;
        _publishedGame = game;
        _publishedModel = model;
        _publishedVersion = version;
        _publishedRevision = revision;
        _publishedPuzzleOn = puzzleOn;

        String status;
        bool mayStart = false;
        bool mayCancel = false;
        if (game) {
            const PositionAnalyst& analyst = game->GetAnalyst();
            const ReviewState state = analyst.GetReviewState(*model);
            status = ReviewText::Status(state, analyst.GetReviewProgress(*model), ReviewRefusalFor(*model), puzzleOn);
            mayStart = MayStart(*game);
            mayCancel = state != ReviewState::Idle;
        }
        CoreManager::GetUIVars().Set(Ids::reviewStatus, StringView(status));

        Grey(*canvas, Ids::reviewSectionStart, !mayStart, _startDisabled);
        Grey(*canvas, Ids::reviewSectionCancel, !mayCancel, _cancelDisabled);
    }

}  // namespace BECore::Chess
