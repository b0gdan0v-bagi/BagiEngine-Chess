#include "ChessSaveKeeper.h"

#include <BECore/Audio/AudioManager.h>
#include <BECore/Audio/AudioPreferences.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Time/Time.h>
#include <Chess/Save/ChessGraphicsState.h>
#include <Chess/Save/ChessSaveState.h>
#include <Chess/Save/SaveSettings.h>

namespace BECore::Chess {

    ChessSaveKeeper::ChessSaveKeeper() = default;
    ChessSaveKeeper::~ChessSaveKeeper() = default;

    ChessSaveKeeper::Key ChessSaveKeeper::KeyOf(const ChessGameModel& model, const OpponentController& controller, const OpponentChoice& choice) {
        return Key{model.GetVersion(), controller.GetRevision(), choice.Color()};
    }

    void ChessSaveKeeper::Start(ChessGameModel& model, OpponentController& controller, OpponentChoice& choice, GraphicsChoice& graphics) {
        _service.reset();
        _graphics = nullptr;
        _attemptFailed = false;
        _hasWritten = false;
        _failureLogged = false;
        _audio = SlotWrite{};
        _look = SlotWrite{};

        const Optional<Path> directory = ResolveSaveDirectory(CoreManager::GetCommandLine(), SaveSettings::Load().rememberGame);
        if (!directory) {
            return;
        }
        UniquePtr<SaveGameService> service = MakeUnique<SaveGameService>(CoreManager::GetFileSystem());
        if (!service->Initialize(*directory)) {
            LOG_WARNING("Save: the folder '{}' cannot be made or is not a folder, the game is not remembered", directory->CStr());
            return;
        }

        ChessSaveState state;
        const std::expected<SaveInfo, SaveError> loaded = service->Load(kChessSaveSlot, kChessSaveSchema, state);
        if (loaded) {
            const std::expected<RestorePlan, SaveError> plan = Validate(state);
            if (plan) {
                Apply(*plan, model, controller, choice);
            } else {
                LOG_WARNING("Save: slot '{}' was not restored, the game starts new: {}", kChessSaveSlot, plan.error().detail);
            }
        } else if (loaded.error().kind != SaveErrorKind::NotFound) {
            LOG_WARNING("Save: slot '{}' was not restored, the game starts new: {}", kChessSaveSlot, loaded.error().detail);
        }

        AudioManager& audio = CoreManager::GetAudio();
        const std::expected<SaveInfo, SaveError> levels = LoadAudioPreferences(*service, audio);
        if (!levels && levels.error().kind != SaveErrorKind::NotFound) {
            LOG_WARNING("Save: slot '{}' was not restored, the levels stay as they are: {}", kAudioPreferencesSlot, levels.error().detail);
        }

        ChessGraphicsState look;
        const std::expected<SaveInfo, SaveError> loadedLook = service->Load(kChessGraphicsSlot, kChessGraphicsSchema, look);
        if (loadedLook) {
            const std::expected<GraphicsPreset, SaveError> preset = Validate(look);
            if (preset) {
                Apply(*preset, graphics);
            } else {
                LOG_WARNING("Save: slot '{}' was not restored, the look stays as it is: {}", kChessGraphicsSlot, preset.error().detail);
            }
        } else if (loadedLook.error().kind != SaveErrorKind::NotFound) {
            LOG_WARNING("Save: slot '{}' was not restored, the look stays as it is: {}", kChessGraphicsSlot, loadedLook.error().detail);
        }

        _written = KeyOf(model, controller, choice);
        _audio.written = audio.GetLevelsRevision();
        _look.written = graphics.GetRevision();
        _graphics = &graphics;
        _service = std::move(service);
    }

    void ChessSaveKeeper::Update(const ChessGameModel& model, const OpponentController& controller, const OpponentChoice& choice, bool puzzleOn) {
        if (!_service) {
            return;
        }
        UpdateSlots();
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
        FlushSlots();
        if (puzzleOn) {
            return;
        }
        const Key key = KeyOf(model, controller, choice);
        if (key != _written) {
            Write(model, controller, choice, key);
        }
    }

    void ChessSaveKeeper::UpdateSlots() {
        UpdateSlot(_audio, CoreManager::GetAudio().GetLevelsRevision(), kAudioPreferencesSlot, [this] { return SaveAudioPreferences(*_service, CoreManager::GetAudio()); });
        UpdateSlot(_look, _graphics->GetRevision(), kChessGraphicsSlot, [this] { return _service->Save(kChessGraphicsSlot, kChessGraphicsSchema, Capture(*_graphics)); });
    }

    void ChessSaveKeeper::FlushSlots() {
        FlushSlot(_audio, CoreManager::GetAudio().GetLevelsRevision(), kAudioPreferencesSlot, [this] { return SaveAudioPreferences(*_service, CoreManager::GetAudio()); });
        FlushSlot(_look, _graphics->GetRevision(), kChessGraphicsSlot, [this] { return _service->Save(kChessGraphicsSlot, kChessGraphicsSchema, Capture(*_graphics)); });
    }

    void ChessSaveKeeper::UpdateSlot(SlotWrite& slot, uint64_t revision, StringView name, SlotSaver save) {
        if (revision == slot.written || (slot.attemptFailed && revision == slot.attempted)) {
            return;
        }
        if (slot.hasWritten && CoreManager::GetTime().GetTotalTime() - slot.lastTime < kSaveDebounceSeconds) {
            return;
        }
        WriteSlot(slot, revision, name, save);
    }

    void ChessSaveKeeper::FlushSlot(SlotWrite& slot, uint64_t revision, StringView name, SlotSaver save) {
        if (revision != slot.written) {
            WriteSlot(slot, revision, name, save);
        }
    }

    void ChessSaveKeeper::WriteSlot(SlotWrite& slot, uint64_t revision, StringView name, SlotSaver save) {
        slot.hasWritten = true;
        slot.lastTime = CoreManager::GetTime().GetTotalTime();
        const std::expected<void, SaveError> saved = save();
        if (saved) {
            slot.written = revision;
            slot.attemptFailed = false;
            slot.failureLogged = false;
            return;
        }
        slot.attempted = revision;
        slot.attemptFailed = true;
        if (!slot.failureLogged) {
            slot.failureLogged = true;
            LOG_WARNING("Save: slot '{}' was not written: {}", name, saved.error().detail);
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
            LOG_WARNING("Save: slot '{}' was not written: {}", kChessSaveSlot, saved.error().detail);
        }
    }

}  // namespace BECore::Chess
