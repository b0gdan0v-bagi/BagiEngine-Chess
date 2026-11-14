#pragma once

#include <BECore/Application/CommandLine.h>

namespace BECore::Chess {

    /**
     * @brief Whether the game remembers itself between runs, as a person sets it in the two settings configs (the same files as OpponentSettings): the attribute `rememberGame` of the root.
     *
     * ```xml
     * <ChessSettings rememberGame="true"/>
     * ```
     *
     * `config/ChessSettings.xml` is committed and says `true`; `config/ChessSettings.local.xml` (git-ignored) wins when it has the attribute at all, so a person turns the memory off there. Nothing
     * here is an error: a missing file or attribute, and a value that is not exactly `true` or `false`, read as `false`. Whether a run really saves is ResolveSaveDirectory()'s decision, which looks
     * at this and at the command line.
     */
    struct SaveSettings {
        /// The attribute of the root element of both files
        static constexpr StringView kRememberGameAttribute = "rememberGame";

        bool rememberGame = false;

        /// The settings in the root elements of the two configs; an invalid node (no such file) has no attribute. Pure: a test feeds it strings
        [[nodiscard]] static SaveSettings FromConfig(const XmlNode& shared, const XmlNode& local);

        /// FromConfig() over the two configs ConfigManager loaded (a config that is not there has no attribute); needs the engine services
        [[nodiscard]] static SaveSettings Load();
    };

    /// The flags of the command line that the save policy reads (the control flags are the engine's: BECore/Control/ControlSettings.h)
    namespace SaveArguments {
        inline constexpr StringView kNoSave = "--no-save";
        inline constexpr StringView kSaveDir = "--save-dir";
    }  // namespace SaveArguments

    /// The per-user folder of the game for its saves (`saves` under the preference folder SDL gives for BagiEngine / BagiChess; SDL makes the preference folder, the keeper makes `saves`); empty, with
    /// one warning, when there is none
    [[nodiscard]] std::filesystem::path DefaultSaveDirectory();

    /**
     * @brief Where this run reads and writes its save, or nothing when it neither reads nor writes one: the gate of persistence. Pure apart from @p defaultDirectory, so a test passes its own.
     *
     * In this order:
     * 1. `--no-save`: nothing, whatever else is asked.
     * 2. `--save-dir=<path>` (a non-empty path): that folder, in any run (a control run, a headless run, a test). This is how the smoke and the tests exercise the save.
     * 3. @p rememberGame false: nothing.
     * 4. Any of `--control`, `--control-port`, `--headless`, `--manual-clock`: nothing (a control run must neither read nor overwrite a person's game). The control settings are not read:
     *    the game starts before the control server does, and a control API switched on by an attribute of ApplicationConfig.xml is a local edit nobody ships, so the flags are the contract.
     * 5. PersistentSaveDirectory() when it is not empty (the browser's folder), else @p defaultDirectory().
     *
     * A folder that comes back empty from the last step is "nothing".
     */
    [[nodiscard]] Optional<std::filesystem::path> ResolveSaveDirectory(const CommandLine& commandLine, bool rememberGame, std::filesystem::path (*defaultDirectory)() = &DefaultSaveDirectory);

}  // namespace BECore::Chess
