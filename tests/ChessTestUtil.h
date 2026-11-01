#pragma once

#include <Chess/Game/GameStatus.h>
#include <Chess/Game/Move.h>
#include <Chess/Game/Position.h>
#include <Common/Printers.h>

// gtest finds PrintTo through the type's own namespace; without it a failed comparison of statuses prints raw bytes.
namespace BECore::Chess {

    inline void PrintTo(GameStatus status, std::ostream* os) {
        const auto name = EnumUtils<GameStatus>::ToString(status);
        os->write(name.data(), static_cast<std::streamsize>(name.size()));
    }

}  // namespace BECore::Chess

namespace BECore::Chess::Tests {

    /// Square from its algebraic name ("e4").
    inline Square Sq(eastl::string_view name) {
        return Square{static_cast<int8_t>(name[0] - 'a'), static_cast<int8_t>(name[1] - '1')};
    }

    inline int SqIdx(eastl::string_view name) {
        return Sq(name).Index();
    }

    /// A normal move or a promotion from its UCI text ("e2e4", "a7a8q"). Castling and en passant have their own helpers.
    inline Move Mv(eastl::string_view uci) {
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

    inline Move CastlingMove(eastl::string_view uci) {
        return Move(Sq(uci.substr(0, 2)), Sq(uci.substr(2, 2)), MoveKind::Castling);
    }

    inline Move EnPassantMove(eastl::string_view uci) {
        return Move(Sq(uci.substr(0, 2)), Sq(uci.substr(2, 2)), MoveKind::EnPassant);
    }

    /// A position from a FEN that the test knows is valid; a parse error fails the test and gives the start position.
    inline Position PositionFromFen(eastl::string_view fen) {
        auto position = Position::FromFen(fen);
        if (!position) {
            ADD_FAILURE() << "Invalid FEN in test: " << eastl::string(fen).c_str() << " (" << EnumUtils<FenError>::ToString(position.error()).data() << ")";
            return Position::Start();
        }
        return *position;
    }

    // Positions every move generator is checked against (https://www.chessprogramming.org/Perft_Results).
    inline constexpr eastl::string_view kKiwipeteFen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    inline constexpr eastl::string_view kPosition3Fen = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
    inline constexpr eastl::string_view kPosition4Fen = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
    inline constexpr eastl::string_view kPosition5Fen = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";
    inline constexpr eastl::string_view kPosition6Fen = "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10";

}  // namespace BECore::Chess::Tests
