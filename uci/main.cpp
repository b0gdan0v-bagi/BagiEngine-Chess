// BagiChessUci: the own engine as a UCI program. It speaks the server side of UCI (Chess/Uci/UciServer.h) on stdin and stdout, with no window, no
// config and no engine services, so that a tournament tool can start it. Nothing but UCI lines goes to stdout: a stray line would break the tool.
// It lives outside src/ because the module globs src/*.cpp and a second main would collide with the engine's.

#include <Chess/Engine/OwnEngine.h>
#include <Chess/Uci/UciServer.h>
#include <iostream>
#include <string>

int main() {
    using namespace BECore;

    Chess::Uci::UciServer server(eastl::make_shared<Chess::OwnEngine>(), [](eastl::string_view line) {
        std::cout.write(line.data(), static_cast<std::streamsize>(line.size()));
        std::cout << '\n' << std::flush;
    });

    std::string line;
    while (!server.WantsQuit() && std::getline(std::cin, line)) {
        server.HandleLine(eastl::string_view(line.data(), line.size()));
    }
    // The end of the input is a quit: a search that is still running is cancelled and the program ends at once
    if (!server.WantsQuit()) {
        server.HandleLine("quit");
    }
    return 0;
}
