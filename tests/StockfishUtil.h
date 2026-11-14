#pragma once

#include "TestEngines.h"

#include <Chess/Engine/UciEngine.h>
#include <gtest/gtest.h>

// A test against a real Stockfish begins with this: it declares `path` (the Stockfish executable) or skips the test
#define SKIP_WITHOUT_STOCKFISH(path)                                                                                                                                                                   \
    const BECore::Optional<eastl::string> path = ::BECore::Chess::Tests::StockfishPath();                                                                                                              \
    if (!path.has_value()) {                                                                                                                                                                           \
        GTEST_SKIP() << "no Stockfish (or no process code in this build): set STOCKFISH to its executable or put stockfish on PATH";                                                                   \
    }

/// The real Stockfish, which the tests neither ship nor install: they find it like the bridge does (STOCKFISH, then PATH) and skip without it
namespace BECore::Chess::Tests {

    /// Where Stockfish is, or nothing; nothing too where the process code is not there (CHESS_NO_STOCKFISH, the web)
    inline Optional<eastl::string> StockfishPath() {
#if !CHESS_HAS_UCI_PROCESS
        return std::nullopt;
#else
        return FindStockfish({});
#endif
    }

    /// An error as a sentence for a failed test
    inline eastl::string Describe(const EngineError& error) {
        return DescribeError(error);
    }

    /// The settings of a UciEngine over the Stockfish at @p path, no options
    inline UciEngineSettings StockfishSettings(const eastl::string& path) {
        UciEngineSettings settings;
        settings.launch.path = path;
        return settings;
    }

}  // namespace BECore::Chess::Tests
