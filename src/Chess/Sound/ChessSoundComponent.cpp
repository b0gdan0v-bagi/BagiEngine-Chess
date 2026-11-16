#include "ChessSoundComponent.h"

#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/AudioManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Resource/ResourceManager.h>
#include <Chess/ChessGame.h>
#include <Chess/Game/Bitboard.h>
#include <Chess/Sound/SoundCue.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/ChessSoundComponent.gen.hpp>

namespace BECore::Chess {

    void ChessSoundComponent::OnAttached() {
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &ChessSoundComponent::OnUpdate>(this);
        SubscribeInScene<InputEvents::ActionEvent, &ChessSoundComponent::OnAction>(this);
        // Cached by path: the first Play() of a cue finds its clip loaded
        for (const SoundCue cue : EnumUtils<SoundCue>::Values()) {
            CoreManager::GetResourceManager().Load<AudioClip>(ClipPath(cue).ToStringView());
        }
    }

    void ChessSoundComponent::OnAction(const InputEvents::ActionEvent& event) {
        if (ActionClicks(event.action)) {
            CoreManager::GetAudio().Play(ClipPath(SoundCue::Click), CueBus(SoundCue::Click), CueVolume(SoundCue::Click));
        }
    }

    void ChessSoundComponent::OnUpdate(const SceneEvents::SceneUpdateEvent& /*event*/) {
        const ChessGameModel* model = ChessGame::GetActiveModel();
        if (!model) {
            _seenModel = nullptr;
            return;
        }

        const Vector<Move>& moves = model->GetMoves();
        const Position& position = model->GetPosition();
        const int pieces = PopCount(position.Pieces());
        if (model == _seenModel && moves.size() > _seenMoves) {
            MoveOutcome outcome;
            outcome.move = moves.back();
            outcome.piecesBefore = _seenPieces;
            outcome.piecesAfter = pieces;
            outcome.check = position.IsInCheck();
            outcome.status = model->GetStatus();
            const SoundCue cue = CueOfMove(outcome);
            CoreManager::GetAudio().Play(ClipPath(cue), CueBus(cue), CueVolume(cue));
        }
        _seenModel = model;
        _seenMoves = moves.size();
        _seenPieces = pieces;
    }

}  // namespace BECore::Chess
