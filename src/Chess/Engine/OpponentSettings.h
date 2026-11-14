#pragma once

namespace BECore::Chess {

    /**
     * @brief What a person sets about the computer opponent in a file: where their Stockfish is.
     *
     * Two config files hold the same shape, one root element and the attribute `stockfishPath`:
     *
     * ```xml
     * <ChessSettings stockfishPath="/opt/homebrew/bin/stockfish"/>
     * ```
     *
     * `config/ChessSettings.xml` is committed and its value is empty. `config/ChessSettings.local.xml` is git-ignored and is where a person puts their
     * own path. They are two files with two names (the stems `ChessSettings` and `ChessSettings.local`) because ConfigManager keys a file by its stem
     * and two files with the same stem in one layer collide. The local value wins when it is not empty.
     *
     * Nothing here is an error: a missing file, a missing attribute and an empty value all mean "not set", and the path is then empty. An empty path
     * is what FindStockfish() takes to go on to `$STOCKFISH` and then to `stockfish` on `PATH`, so the order of the lookup is the config, the
     * environment variable, `PATH`. The path is passed as it is; use an absolute one, a relative one is taken from the working directory of the game.
     * Nothing is checked here (whether a file is there is FindStockfish()'s business) and nothing is logged.
     */
    struct OpponentSettings {
        /// The names of the two configs: ConfigManager keys a file by its name without `.xml`
        static constexpr StringView kSharedConfigName = "ChessSettings";
        static constexpr StringView kLocalConfigName = "ChessSettings.local";

        /// The attribute of the root element of both files
        static constexpr StringView kStockfishPathAttribute = "stockfishPath";

        /// The path of the Stockfish to start; empty when no file names one
        eastl::string stockfishPath;

        /**
         * @brief The settings in the root elements of the two configs; an invalid node (no such file) is "not set".
         *
         * The value of each root is trimmed of white space; @p local wins when it is not empty, else @p shared is taken. The name of the root element
         * and attributes other than `stockfishPath` are not looked at. Pure: needs no engine services, so a test feeds it strings.
         */
        [[nodiscard]] static OpponentSettings FromConfig(const XmlNode& shared, const XmlNode& local);

        /// FromConfig() over the two configs ConfigManager loaded (a config that is not there is "not set"); needs the engine services
        [[nodiscard]] static OpponentSettings Load();
    };

}  // namespace BECore::Chess
