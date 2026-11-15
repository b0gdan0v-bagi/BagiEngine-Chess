#pragma once

#include <Chess/Engine/ChessEngine.h>
#include <Chess/Game/GameStatus.h>
#include <Chess/Game/Move.h>
#include <Chess/Game/Position.h>
#include <Common/Printers.h>
#include <TaskSystem/TaskManager.h>
#include <TaskSystem/TaskScheduler.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>

// A test that needs a worker thread (a gate that a worker holds while the test thread opens it, a wait for IsThinking()
// while a search blocks, an engine that only stands in as "never answers") begins with this: under ENGINE_SINGLE_THREAD
// (BE_SINGLE_THREAD=1) the task system runs a job on the calling thread, so the test would hang, or pass on a Timeout
// without testing what it claims.
#if BE_SINGLE_THREAD
#define SKIP_WITHOUT_WORKER_THREADS() GTEST_SKIP() << "needs worker threads (BE_SINGLE_THREAD)"
#else
#define SKIP_WITHOUT_WORKER_THREADS() static_cast<void>(0)
#endif

// gtest finds PrintTo through the type's own namespace; without it a failed comparison of statuses prints raw bytes.
namespace BECore::Chess {

    inline void PrintTo(GameStatus status, std::ostream* os) {
        const auto name = EnumUtils<GameStatus>::ToString(status);
        os->write(name.data(), static_cast<std::streamsize>(name.size()));
    }

    inline void PrintTo(EngineErrorKind kind, std::ostream* os) {
        const auto name = EnumUtils<EngineErrorKind>::ToString(kind);
        os->write(name.data(), static_cast<std::streamsize>(name.size()));
    }

}  // namespace BECore::Chess

namespace BECore::Chess::Tests {

    /// One task frame, for a test that runs no CoreManager frame and waits for a search that may span frames: the same three steps as TaskManager::Update (it takes a
    /// PassKey, so a test cannot call it; keep the two in step by hand). Only a single-thread build has a task that yielded to resume
    inline void PumpTaskFrame() {
#if BE_SINGLE_THREAD
        TaskScheduler& scheduler = *TaskManager::GetInstance().GetScheduler();
        scheduler.ProcessDelayedTasks();
        scheduler.ProcessMainThreadTasks();
        (void)scheduler.ProcessBackgroundSlices(TaskManager::GetInstance().GetFrameBudget());
#endif
    }

    /// An environment variable set (or removed, for nullptr) for the length of a scope and put back after it. A test that starts a game reads STOCKFISH and PATH through
    /// FindStockfish(), so it fixes them with this instead of taking the machine's
    class ScopedEnvironment {
    public:
        ScopedEnvironment(const char* name, const char* value) : _name(name) {
            _hadValue = Read(name, _old);
            Set(value);
        }
        ~ScopedEnvironment() {
            Set(_hadValue ? _old.c_str() : nullptr);
        }
        ScopedEnvironment(const ScopedEnvironment&) = delete;
        ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    private:
        static bool Read(const char* name, String& value) {
#if defined(_WIN32)
            char* text = nullptr;
            size_t length = 0;
            const bool found = _dupenv_s(&text, &length, name) == 0 && text != nullptr;
            if (found) {
                value = text;
            }
            std::free(text);
            return found;
#else
            const char* text = std::getenv(name);
            if (text != nullptr) {
                value = text;
            }
            return text != nullptr;
#endif
        }

        void Set(const char* value) const {
#if defined(_WIN32)
            _putenv_s(_name, value != nullptr ? value : "");  // an empty value removes the variable
#else
            if (value != nullptr) {
                setenv(_name, value, 1);
            } else {
                unsetenv(_name);
            }
#endif
        }

        const char* _name;
        String _old;
        bool _hadValue = false;
    };

    /// The file FindStockfish() accepts: a regular file with the execute bit (what it holds is never read before a search)
    inline void MakeStockfishFile(const std::filesystem::path& path) {
        {
            //-NoStd: file layer (engine-file-layer)
            std::ofstream out(path);
            out << "not an engine\n";
        }
        using std::filesystem::perms;
        std::filesystem::permissions(path, perms::owner_all | perms::group_read | perms::group_exec | perms::others_read | perms::others_exec, std::filesystem::perm_options::replace);
    }

    /// Square from its algebraic name ("e4").
    inline Square Sq(StringView name) {
        return Square{static_cast<int8_t>(name[0] - 'a'), static_cast<int8_t>(name[1] - '1')};
    }

    inline int SqIdx(StringView name) {
        return Sq(name).Index();
    }

    /// A normal move or a promotion from its UCI text ("e2e4", "a7a8q"). Castling and en passant have their own helpers.
    inline Move Mv(StringView uci) {
        if (uci.size() == 5) {
            PieceType promotion = PieceType::Queen;
            switch (uci[4]) {
                case 'n':
                    promotion = PieceType::Knight;
                    break;
                case 'b':
                    promotion = PieceType::Bishop;
                    break;
                case 'r':
                    promotion = PieceType::Rook;
                    break;
                default:
                    break;
            }
            return Move(Sq(uci.substr(0, 2)), Sq(uci.substr(2, 2)), MoveKind::Promotion, promotion);
        }
        return Move(Sq(uci.substr(0, 2)), Sq(uci.substr(2, 2)));
    }

    inline Move CastlingMove(StringView uci) {
        return Move(Sq(uci.substr(0, 2)), Sq(uci.substr(2, 2)), MoveKind::Castling);
    }

    inline Move EnPassantMove(StringView uci) {
        return Move(Sq(uci.substr(0, 2)), Sq(uci.substr(2, 2)), MoveKind::EnPassant);
    }

    /// A position from a FEN that the test knows is valid; a parse error fails the test and gives the start position.
    inline Position PositionFromFen(StringView fen) {
        auto position = Position::FromFen(fen);
        if (!position) {
            ADD_FAILURE() << "Invalid FEN in test: " << String(fen).c_str() << " (" << EnumUtils<FenError>::ToString(position.error()).data() << ")";
            return Position::Start();
        }
        return *position;
    }

    // Positions every move generator is checked against (https://www.chessprogramming.org/Perft_Results).
    inline constexpr StringView kKiwipeteFen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    inline constexpr StringView kPosition3Fen = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
    inline constexpr StringView kPosition4Fen = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
    inline constexpr StringView kPosition5Fen = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";
    inline constexpr StringView kPosition6Fen = "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10";

    namespace MirrorDetail {

        inline char SwapCase(char c) {
            if (c >= 'a' && c <= 'z') {
                return static_cast<char>(c - 'a' + 'A');
            }
            if (c >= 'A' && c <= 'Z') {
                return static_cast<char>(c - 'A' + 'a');
            }
            return c;
        }

        inline Vector<String> SplitOn(const String& text, char separator) {
            Vector<String> parts;
            String current;
            for (const char c : text) {
                if (c == separator) {
                    parts.push_back(current);
                    current.clear();
                } else {
                    current.push_back(c);
                }
            }
            parts.push_back(current);
            return parts;
        }

    }  // namespace MirrorDetail

    /// The same game seen from the other side of the table: ranks flipped, colors swapped, and the side to move, the castling rights and the en passant square with them
    inline String MirrorFen(StringView fen) {
        const Vector<String> fields = MirrorDetail::SplitOn(String(fen.data(), fen.size()), ' ');
        EXPECT_GE(fields.size(), 4U);

        Vector<String> ranks = MirrorDetail::SplitOn(fields[0], '/');
        String placement;
        for (auto rank = ranks.rbegin(); rank != ranks.rend(); ++rank) {
            if (!placement.empty()) {
                placement.push_back('/');
            }
            for (const char c : *rank) {
                placement.push_back(MirrorDetail::SwapCase(c));
            }
        }

        const String side = fields[1] == "w" ? "b" : "w";

        String castling;
        for (const char c : fields[2]) {
            castling.push_back(MirrorDetail::SwapCase(c));
        }
        if (castling != "-") {
            // The order of the letters is the FEN's: KQkq
            String ordered;
            for (const char c : String("KQkq")) {
                if (castling.find(c) != String::npos) {
                    ordered.push_back(c);
                }
            }
            castling = ordered;
        }

        String enPassant = fields[3];
        if (enPassant != "-") {
            enPassant[1] = static_cast<char>('1' + '8' - enPassant[1]);
        }

        String mirrored = placement + " " + side + " " + castling + " " + enPassant;
        for (size_t i = 4; i < fields.size(); ++i) {
            mirrored += " " + fields[i];
        }
        return mirrored;
    }

}  // namespace BECore::Chess::Tests
