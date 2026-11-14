// BagiChessUci: the own engine as a UCI program. It speaks the server side of UCI (Chess/Uci/UciServer.h) on stdin and stdout, with no window, no
// config and no engine services, so that a tournament tool can start it. Nothing but UCI lines goes to stdout: a stray line would break the tool.
// It lives outside src/ because the module globs src/*.cpp and a second main would collide with the engine's.

#include <Chess/Engine/OwnEngine.h>
#include <Chess/Uci/UciServer.h>
#include <cstdio>
#include <cstring>

namespace {

    constexpr size_t kChunkBytes = 4096;

    /// The next line of stdin in @p line, without its end of line (a trailing '\r' is left to the server, which ignores it). A line of any length arrives whole; the last
    /// line of the input needs no end of line. False at the end of the input when not a character came
    bool ReadLine(BECore::String& line) {
        line.clear();
        char chunk[kChunkBytes];
        bool anything = false;
        while (std::fgets(chunk, sizeof(chunk), stdin) != nullptr) {
            anything = true;
            size_t length = std::strlen(chunk);
            const bool endsLine = length > 0 && chunk[length - 1] == '\n';
            if (endsLine) {
                --length;
            }
            line.append(chunk, length);
            if (endsLine) {
                break;
            }
        }
        return anything;
    }

    void WriteLine(BECore::StringView line) {
        std::fwrite(line.data(), 1, line.size(), stdout);
        std::fwrite("\n", 1, 1, stdout);
        std::fflush(stdout);
    }

}  // namespace

int main() {
    using namespace BECore;

    Chess::Uci::UciServer server(MakeShared<Chess::OwnEngine>(), WriteLine);

    String line;  // reused over the loop: its capacity stays
    while (!server.WantsQuit() && ReadLine(line)) {
        server.HandleLine(line);
    }
    // The end of the input is a quit: a search that is still running is cancelled and the program ends at once
    if (!server.WantsQuit()) {
        server.HandleLine("quit");
    }
    return 0;
}
