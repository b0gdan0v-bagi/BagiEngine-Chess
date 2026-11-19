#include "ChessSaveKeeper.h"

#include <BECore/Audio/AudioManager.h>
#include <BECore/Audio/AudioPreferences.h>
#include <BECore/GameManager/CoreManager.h>
#include <Chess/Save/ChessSaveState.h>
#include <Chess/Save/SaveSettings.h>

namespace BECore::Chess {

    ChessSaveKeeper::ChessSaveKeeper() = default;
    ChessSaveKeeper::~ChessSaveKeeper() = default;

    ChessSaveKeeper::Key ChessSaveKeeper::KeyOf(const ChessGameModel& model, const OpponentController& controller, const OpponentChoice& choice) {
        return Key{model.GetVersion(), controller.GetRevision(), choice.Color()};
    }

    void ChessSaveKeeper::Start(ChessGameModel& model, OpponentController& controller, OpponentChoice& choice) {
        _service.reset();
        _attemptFailed = false;
        _hasWritten = false;
        _failureLogged = false;
        _audio = AudioWrite{};

        const Optional<std::filesystem::path> directory = ResolveSaveDirectory(CoreManager::GetCommandLine(), SaveSettings::Load().rememberGame);
        if (!directory) {
            return;
        }
        UniquePtr<SaveGameService> service = MakeUnique<SaveGameService>(CoreManager::GetFileSystem());
        if (!service->Initialize(*directory)) {
            LOG_WARNING("Save: the folder '{}' cannot be made or is not a folder, the game is not remembered"_format(directory->string().c_str()));
            return;
        }

        ChessSaveState state;
        const std::expected<SaveInfo, SaveError> loaded = service->Load(kChessSaveSlot, kChessSaveSchema, state);
        if (loaded) {
            const std::expected<RestorePlan, SaveError> plan = Validate(state);
            if (plan) {
                Apply(*plan, model, controller, choice);
            } else {
                LOG_WARNING("Save: slot '{}' was not restored, the game starts new: {}"_format(kChessSaveSlot, plan.error().detail));
            }
        } else if (loaded.error().kind != SaveErrorKind::NotFound) {
            LOG_WARNING("Save: slot '{}' was not restored, the game starts new: {}"_format(kChessSaveSlot, loaded.error().detail));
        }

        AudioManager& audio = CoreManager::GetAudio();
        const std::expected<SaveInfo, SaveError> levels = LoadAudioPreferences(*service, audio);
        if (!levels && levels.error().kind != SaveErrorKind::NotFound) {
            LOG_WARNING("Save: slot '{}' was not restored, the levels stay as they are: {}"_format(kAudioPreferencesSlot, levels.error().detail));
        }

        _written = KeyOf(model, controller, choice);
        _audio.written = audio.GetLevelsRevision();
        _service = std::move(service);
    }

    void ChessSaveKeeper::Update(const ChessGameModel& model, const OpponentController& controller, const OpponentChoice& choice, bool puzzleOn) {
        if (!_service) {
            return;
        }
        UpdateAudio();
        if (puzzleOn) {
            return;
        }
        const Key key = KeyOf(model, controller, choice);
        if (key == _written || (_attemptFailed && key == _attempted)) {
            return;
        }
        if (_hasWritten && CoreManager::GetTime().GetTotalTime() - _lastWriteTime < kSaveDebounceSeconds) {
            return;
        }
        Write(model, controller, choice, key);
    }

    void ChessSaveKeeper::Flush(const ChessGameModel& model, const OpponentController& controller, const OpponentChoice& choice, bool puzzleOn) {
        if (!_service) {
            return;
        }
        FlushAudio();
        if (puzzleOn) {
            return;
        }
        const Key key = KeyOf(model, controller, choice);
        if (key != _written) {
            Write(model, controller, choice, key);
        }
    }

    void ChessSaveKeeper::UpdateAudio() {
        const uint64_t revision = CoreManager::GetAudio().GetLevelsRevision();
        if (revision == _audio.written || (_audio.attemptFailed && revision == _audio.attempted)) {
            return;
        }
        if (_audio.hasWritten && CoreManager::GetTime().GetTotalTime() - _audio.lastTime < kSaveDebounceSeconds) {
            return;
        }
        WriteAudio(revision);
    }

    void ChessSaveKeeper::FlushAudio() {
        const uint64_t revision = CoreManager::GetAudio().GetLevelsRevision();
        if (revision != _audio.written) {
            WriteAudio(revision);
        }
    }

    void ChessSaveKeeper::WriteAudio(uint64_t revision) {
        _audio.hasWritten = true;
        _audio.lastTime = CoreManager::GetTime().GetTotalTime();
        const std::expected<void, SaveError> saved = SaveAudioPreferences(*_service, CoreManager::GetAudio());
        if (saved) {
            _audio.written = revision;
            _audio.attemptFailed = false;
            _audio.failureLogged = false;
            return;
        }
        _audio.attempted = revision;
        _audio.attemptFailed = true;
        if (!_audio.failureLogged) {
            _audio.failureLogged = true;
            LOG_WARNING("Save: slot '{}' was not written: {}"_format(kAudioPreferencesSlot, saved.error().detail));
        }
    }

    void ChessSaveKeeper::Write(const ChessGameModel& model, const OpponentController& controller, const OpponentChoice& choice, const Key& key) {
        _hasWritten = true;
        _lastWriteTime = CoreManager::GetTime().GetTotalTime();
        const std::expected<void, SaveError> saved = _service->Save(kChessSaveSlot, kChessSaveSchema, Capture(model, controller, choice));
        if (saved) {
            _written = key;
            _attemptFailed = false;
            _failureLogged = false;
            return;
        }
        _attempted = key;
        _attemptFailed = true;
        if (!_failureLogged) {
            _failureLogged = true;
            LOG_WARNING("Save: slot '{}' was not written: {}"_format(kChessSaveSlot, saved.error().detail));
        }
    }

}  // namespace BECore::Chess
