#pragma once

#include <Chess/Engine/UciEngine.h>
#include <gtest/gtest.h>

// A test against a real Stockfish begins with this: it declares `path` (the Stockfish executable) or skips the test
#define SKIP_WITHOUT_STOCKFISH(path)                                                                                                                                                                   \
    const std::optional<eastl::string> path = ::BECore::Chess::Tests::StockfishPath();                                                                                                                 \
    if (!path.has_value()) {                                                                                                                                                                           \
        GTEST_SKIP() << "no Stockfish (or no process code on this platform): set STOCKFISH to its executable or put stockfish on PATH";                                                                \
    }

/// The real Stockfish, which the tests neither ship nor install: they find it like the bridge does (STOCKFISH, then PATH) and skip without it
namespace BECore::Chess::Tests {

    /// Where Stockfish is, or nothing; nothing too where the process code is not there
    inline std::optional<eastl::string> StockfishPath() {
#if defined(_WIN32)
        return std::nullopt;
#else
        return FindStockfish({});
#endif
    }

    /// The settings of a UciEngine over the Stockfish at @p path, no options
    inline UciEngineSettings StockfishSettings(const eastl::string& path) {
        UciEngineSettings settings;
        settings.launch.path = path;
        return settings;
    }

}  // namespace BECore::Chess::Tests
