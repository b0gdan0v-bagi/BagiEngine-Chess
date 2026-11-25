#include "SaveSettings.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/Control/ControlSettings.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Save/SaveStorage.h>
#include <Chess/Engine/OpponentSettings.h>
#include <SDL3/SDL_filesystem.h>

namespace BECore::Chess {

    namespace {

        constexpr const char* kSaveOrganization = "BagiEngine";
        constexpr const char* kSaveApplication = "BagiChess";
        constexpr StringView kSaveSubfolder = "saves";
        constexpr StringView kTrue = "true";

        /// The value of the attribute of @p root; nothing when the node or the attribute is missing
        Optional<bool> ReadRememberGame(const XmlNode& root) {
            if (!root) {
                return std::nullopt;
            }
            const auto value = root.GetAttribute(SaveSettings::kRememberGameAttribute);
            if (!value) {
                return std::nullopt;
            }
            return *value == kTrue;  // anything but "true" is false, "false" included
        }

        bool IsControlRun(const CommandLine& commandLine) {
            using namespace ControlArguments;
            return commandLine.HasFlag(kControl) || commandLine.HasFlag(kHeadless) || commandLine.HasFlag(kManualClock) || commandLine.Find(kControlPort).has_value();
        }

    }  // namespace

    SaveSettings SaveSettings::FromConfig(const XmlNode& shared, const XmlNode& local) {
        SaveSettings settings;
        const Optional<bool> remembered = ReadRememberGame(local);
        settings.rememberGame = remembered ? *remembered : ReadRememberGame(shared).value_or(false);
        return settings;
    }

    SaveSettings SaveSettings::Load() {
        const ConfigManager& configs = CoreManager::GetConfigManager();
        const auto root = [&configs](StringView name) { return configs.HasConfig(name) ? configs.GetConfig(name) : XmlNode{}; };
        return FromConfig(root(OpponentSettings::kSharedConfigName), root(OpponentSettings::kLocalConfigName));
    }

    Path DefaultSaveDirectory() {
        char* prefPath = SDL_GetPrefPath(kSaveOrganization, kSaveApplication);
        if (prefPath == nullptr) {
            LOG_WARNING("Save: no per-user folder, the game is not remembered: {}", SDL_GetError());
            return {};
        }
        const Path directory(prefPath);
        SDL_free(prefPath);
        return directory / kSaveSubfolder;
    }

    Optional<Path> ResolveSaveDirectory(const CommandLine& commandLine, bool rememberGame, Path (*defaultDirectory)()) {
        if (commandLine.HasFlag(SaveArguments::kNoSave)) {
            return std::nullopt;
        }
        if (const Optional<StringView> explicitFolder = commandLine.FindValue(SaveArguments::kSaveDir); explicitFolder && !explicitFolder->empty()) {
            return Path(*explicitFolder);
        }
        if (!rememberGame || IsControlRun(commandLine)) {
            return std::nullopt;
        }
        Path directory = PersistentSaveDirectory();
        if (directory.Empty()) {
            directory = defaultDirectory();
        }
        if (directory.Empty()) {
            return std::nullopt;
        }
        return directory;
    }

}  // namespace BECore::Chess
