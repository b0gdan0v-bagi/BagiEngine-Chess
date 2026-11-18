#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>

/// Fuzz targets of the parsers that read text nobody here wrote (a FEN, a UCI move, a UCI line of a program that is not ours). The shape is the engine's (engine/tests/Fuzz/CLAUDE.md,
/// "A target"); the library is the game's own because the engine's BagiFuzzTargets is not built in a game. tests/Fuzz/CLAUDE.md
namespace BECore::Chess::Fuzzing {

    /// One target: untrusted bytes in, properties checked inside, a process that dies (CHESS_FUZZ_CHECK, a sanitizer, an engine assertion) on a finding
    using ChessFuzzFunction = void (*)(const uint8_t* data, size_t size);

    /// Whether the parser takes the input (a FEN is a position, a line is a known command); the corpus test holds that the seeds of a target hold both answers
    using ChessFuzzVerdict = bool (*)(const uint8_t* data, size_t size);

    struct ChessFuzzTarget {
        const char* name;  ///< The suffix of Fuzz<name> and the name of the test case
        ChessFuzzFunction function;
        ChessFuzzVerdict accepts;
        const char* corpusFolder;  ///< The folder of tests/Fuzz/corpus with the seeds of this target
        uint32_t costWeight;       ///< How many times dearer than a plain parser one input is: the replay divides its default iteration count by it
    };

    /// Every target, in a fixed order
    Span<const ChessFuzzTarget> ChessFuzzTargetList();

    /// Prints the failed condition and aborts the process, so the replay and a libFuzzer run both see a crash
    [[noreturn]] void ChessFuzzCheckFailed(const char* condition, const char* file, int line);

    /// A property of a target. Not an engine ASSERT: it must hold in every build, asserts on or off
#define CHESS_FUZZ_CHECK(condition)                                                                                                                                                                    \
    do {                                                                                                                                                                                               \
        if (!(condition)) {                                                                                                                                                                            \
            ::BECore::Chess::Fuzzing::ChessFuzzCheckFailed(#condition, __FILE__, __LINE__);                                                                                                            \
        }                                                                                                                                                                                              \
    } while (false)

    /// Position::FromFen over the text, then what a position promises: the pieces, the king that is not attacked, the rights, the en passant square, ToFen as a fixed point, and one ply
    /// of every legal move
    void FuzzFen(const uint8_t* data, size_t size);
    bool FenIsAccepted(const uint8_t* data, size_t size);

    /// MoveGenerator::ParseUci over the position of a FEN; the input is the FEN, a newline and the move text (no newline: the start position, and the whole input is the move text)
    void FuzzUciMove(const uint8_t* data, size_t size);
    bool UciMoveIsAccepted(const uint8_t* data, size_t size);

    /// Uci::ParseCommand and Uci::ParseInt over one line (any byte), and the game of a `position` command through SearchRequest::FinalPosition
    void FuzzUciCommand(const uint8_t* data, size_t size);
    bool UciCommandIsAccepted(const uint8_t* data, size_t size);

}  // namespace BECore::Chess::Fuzzing
