#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/Position.h>
#include <tests/Fuzz/ChessFuzzTargets.h>

namespace BECore::Chess::Fuzzing {

    namespace {

        constexpr char kFenSeparator = '\n';

        /// The input split at its first newline: the FEN of the position, then the text of the move. No newline: the start position and the whole input as the move text
        struct MoveInput {
            StringView fen = Position::kStartFen;
            StringView text;
        };

        MoveInput SplitInput(const uint8_t* data, size_t size) {
            const StringView all(reinterpret_cast<const char*>(data), size);
            MoveInput input;
            const size_t separator = all.find(kFenSeparator);
            if (separator == StringView::npos) {
                input.text = all;
            } else {
                input.fen = all.substr(0, separator);
                input.text = all.substr(separator + 1);
            }
            return input;
        }

        /// The UCI spelling of a legal move written from its squares, the long algebraic form of the header of MoveGenerator: "e2e4", "e7e8q"
        String Spelled(Move move) {
            constexpr StringView kPromotionLetters = "nbrq";
            String text;
            text.push_back(static_cast<char>('a' + move.From().file));
            text.push_back(static_cast<char>('1' + move.From().rank));
            text.push_back(static_cast<char>('a' + move.To().file));
            text.push_back(static_cast<char>('1' + move.To().rank));
            if (move.IsPromotion()) {
                text.push_back(kPromotionLetters[static_cast<size_t>(move.PromotionType()) - static_cast<size_t>(PieceType::Knight)]);
            }
            return text;
        }

    }  // namespace

    void FuzzUciMove(const uint8_t* data, size_t size) {
        const MoveInput input = SplitInput(data, size);
        const auto parsed = Position::FromFen(input.fen);
        if (!parsed) {
            return;
        }
        Position position = *parsed;
        const String before = position.ToFen();

        // The second implementation: the text is a move exactly when it is the spelling of one of the legal moves
        const MoveList legal = MoveGenerator::GenerateLegal(position);
        size_t spelledLikeText = 0;
        Move expected;
        for (const Move move : legal) {
            const String spelling = Spelled(move);
            CHESS_FUZZ_CHECK(spelling == move.ToUci());
            if (StringView(spelling.data(), spelling.size()) == input.text) {
                ++spelledLikeText;
                expected = move;
            }
        }
        // A spelling names one move
        CHESS_FUZZ_CHECK(spelledLikeText <= 1);

        const Optional<Move> found = MoveGenerator::ParseUci(position, input.text);
        CHESS_FUZZ_CHECK(found.has_value() == (spelledLikeText == 1));
        CHESS_FUZZ_CHECK(position.ToFen() == before);
        if (!found) {
            return;
        }
        CHESS_FUZZ_CHECK(*found == expected);
        CHESS_FUZZ_CHECK(position.IsLegal(*found));

        const ZobristKey key = position.Key();
        position.DoMove(*found);
        position.UndoMove();
        CHESS_FUZZ_CHECK(position.Key() == key);
        CHESS_FUZZ_CHECK(position.ToFen() == before);
    }

    bool UciMoveIsAccepted(const uint8_t* data, size_t size) {
        const MoveInput input = SplitInput(data, size);
        const auto parsed = Position::FromFen(input.fen);
        return parsed.has_value() && MoveGenerator::ParseUci(*parsed, input.text).has_value();
    }

}  // namespace BECore::Chess::Fuzzing
