#pragma once

namespace BECore::Chess {

    /**
     * @brief What a person sets about the computer opponent in a file: where their Stockfish is.
     *
     * Two config files hold the same shape, a root element with the attribute `stockfishPath`:
     *
     * ```xml
     * <ChessSettings stockfishPath="/opt/homebrew/bin/stockfish"/>
     * ```
     *
     * `config/ChessSettings.xml` is committed and its value is empty. `config/ChessSettings.local.xml` is git-ignored and is where a person puts their
     * own path. Two stems because ConfigManager keys a file by its stem and two files with the same stem in one layer collide. The local value wins when it
     * is not empty.
     *
     * Nothing here is an error: a missing file, a missing attribute and an empty value all mean "not set", and the path is then empty, which FindStockfish()
     * takes to go on to `$STOCKFISH` and then to `stockfish` on `PATH`. The path is passed as it is; a relative one is taken from the working directory of the game.
     * Whether a file is there is FindStockfish()'s business; nothing is checked or logged here.
     */
    struct OpponentSettings {
        static constexpr StringView kSharedConfigName = "ChessSettings";
        static constexpr StringView kLocalConfigName = "ChessSettings.local";

        static constexpr StringView kStockfishPathAttribute = "stockfishPath";

        /// Empty when no file names one
        String stockfishPath;

        /**
         * @brief The settings in the root elements of the two configs; an invalid node (no such file) is "not set".
         *
         * The value of each root is trimmed of white space; @p local wins when it is not empty, else @p shared is taken. Pure: needs no engine services, so a test feeds it nodes.
         */
        [[nodiscard]] static OpponentSettings FromConfig(const XmlNode& shared, const XmlNode& local);

        /// FromConfig() over the two configs ConfigManager loaded; needs the engine services
        [[nodiscard]] static OpponentSettings Load();
    };

}  // namespace BECore::Chess
