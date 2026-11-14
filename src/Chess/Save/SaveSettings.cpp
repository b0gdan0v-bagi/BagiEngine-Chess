#include "SaveSettings.h"

#include <BECore/Control/ControlSettings.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Save/SaveStorage.h>
#include <Chess/Engine/OpponentSettings.h>
#include <SDL3/SDL_filesystem.h>

namespace BECore::Chess {

    namespace {

        constexpr const char* kSaveOrganization = "BagiEngine";
        constexpr const char* kSaveApplication = "BagiChess";
        constexpr eastl::string_view kSaveSubfolder = "saves";
        constexpr eastl::string_view kTrue = "true";

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
            const StringView controlPort = kControlPortPrefix.substr(0, kControlPortPrefix.size() - 1);
            return commandLine.HasFlag(kControl) || commandLine.HasFlag(kHeadless) || commandLine.HasFlag(kManualClock) || commandLine.HasFlag(controlPort) ||
                   commandLine.FindValue(controlPort).has_value();
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
        const auto root = [&configs](eastl::string_view name) { return configs.HasConfig(name) ? configs.GetConfig(name) : XmlNode{}; };
        return FromConfig(root(OpponentSettings::kSharedConfigName), root(OpponentSettings::kLocalConfigName));
    }

    std::filesystem::path DefaultSaveDirectory() {
        char* prefPath = SDL_GetPrefPath(kSaveOrganization, kSaveApplication);
        if (prefPath == nullptr) {
            LOG_WARNING("Save: no per-user folder, the game is not remembered: {}"_format(SDL_GetError()));
            return {};
        }
        std::filesystem::path directory(prefPath);
        SDL_free(prefPath);
        return directory / std::filesystem::path(kSaveSubfolder.data(), kSaveSubfolder.data() + kSaveSubfolder.size());
    }

    Optional<std::filesystem::path> ResolveSaveDirectory(const CommandLine& commandLine, bool rememberGame, std::filesystem::path (*defaultDirectory)()) {
        if (commandLine.HasFlag(SaveArguments::kNoSave)) {
            return std::nullopt;
        }
        if (const Optional<StringView> explicitFolder = commandLine.FindValue(SaveArguments::kSaveDir); explicitFolder && !explicitFolder->empty()) {
            return std::filesystem::path(explicitFolder->data(), explicitFolder->data() + explicitFolder->size());
        }
        if (!rememberGame || IsControlRun(commandLine)) {
            return std::nullopt;
        }
        std::filesystem::path directory = PersistentSaveDirectory();
        if (directory.empty()) {
            directory = defaultDirectory();
        }
        if (directory.empty()) {
            return std::nullopt;
        }
        return directory;
    }

}  // namespace BECore::Chess
