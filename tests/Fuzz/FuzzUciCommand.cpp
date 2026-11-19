#include <Chess/Engine/SearchRequest.h>
#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/Position.h>
#include <Chess/Uci/UciCommand.h>
#include <tests/Fuzz/ChessFuzzTargets.h>
#include <tests/Fuzz/ChessFuzzText.h>

namespace BECore::Chess::Fuzzing {

    namespace {

        using Uci::GoParams;
        using Uci::UciCommand;
        using Uci::UciCommandKind;

        using Text::ReadInt;
        using Text::ViewOf;
        using Text::WithoutEdges;
        using Text::Words;

        /// The two blanks `go` and `position` split words on
        constexpr StringView kWordSeparators = " \t";

        /// A `position` command with more moves than this replays only the first ones, so a line of many tokens stays cheap
        constexpr size_t kMostReplayedMoves = 64;

        constexpr StringView kFirstWordsOfKinds[] = {"uci", "isready", "ucinewgame", "setoption", "position", "go", "stop", "ponderhit", "quit"};
        constexpr UciCommandKind kKindsOfFirstWords[] = {UciCommandKind::Uci, UciCommandKind::IsReady, UciCommandKind::NewGame,   UciCommandKind::SetOption, UciCommandKind::Position,
                                                         UciCommandKind::Go,  UciCommandKind::Stop,    UciCommandKind::PonderHit, UciCommandKind::Quit};

        constexpr StringView kGoKeywords[] = {"depth", "movetime", "wtime", "btime", "winc", "binc", "movestogo"};

        bool SameGo(const GoParams& a, const GoParams& b) {
            return a.depth == b.depth && a.moveTime == b.moveTime && a.wTime == b.wTime && a.bTime == b.bTime && a.wInc == b.wInc && a.bInc == b.bInc && a.movesToGo == b.movesToGo &&
                   a.infinite == b.infinite;
        }

        bool SameCommand(const UciCommand& a, const UciCommand& b) {
            return a.kind == b.kind && SameGo(a.go, b.go) && a.position.startFen == b.position.startFen && a.position.moves == b.position.moves && a.setOption.name == b.setOption.name &&
                   a.setOption.value == b.setOption.value;
        }

        UciCommandKind KindOf(const Words& words) {
            if (words.empty()) {
                return UciCommandKind::Unknown;
            }
            for (size_t index = 0; index < std::size(kFirstWordsOfKinds); ++index) {
                if (words.front() != kFirstWordsOfKinds[index]) {
                    continue;
                }
                // A `position` with neither `startpos` nor `fen` is no command
                if (kKindsOfFirstWords[index] == UciCommandKind::Position && (words.size() < 2 || (words[1] != "startpos" && words[1] != "fen"))) {
                    return UciCommandKind::Unknown;
                }
                return kKindsOfFirstWords[index];
            }
            return UciCommandKind::Unknown;
        }

        /// The words from @p begin up to @p end joined by one space
        String Joined(const Words& words, size_t begin, size_t end) {
            String text;
            for (size_t index = begin; index < end; ++index) {
                if (index > begin) {
                    text.push_back(' ');
                }
                text.append(words[index].data(), words[index].size());
            }
            return text;
        }

        GoParams ExpectedGo(const Words& words) {
            GoParams go;
            Optional<int>* const fields[] = {&go.depth, &go.moveTime, &go.wTime, &go.bTime, &go.wInc, &go.bInc, &go.movesToGo};
            for (size_t index = 1; index < words.size(); ++index) {
                if (words[index] == "infinite" || words[index] == "ponder") {
                    go.infinite = true;
                }
                // A number is never a keyword, so taking it with its keyword hides no other keyword: the last keyword with a number after it wins
                for (size_t field = 0; field < std::size(kGoKeywords); ++field) {
                    if (words[index] == kGoKeywords[field] && index + 1 < words.size()) {
                        if (const Optional<int> value = ReadInt(words[index + 1])) {
                            *fields[field] = value;
                        }
                    }
                }
            }
            return go;
        }

        Uci::PositionParams ExpectedPosition(const Words& words) {
            Uci::PositionParams position;
            size_t next = 2;
            if (words[1] == "startpos") {
                position.startFen = String(Position::kStartFen);
            } else {
                while (next < words.size() && words[next] != "moves") {
                    ++next;
                }
                position.startFen = Joined(words, 2, next);
            }
            if (next < words.size() && words[next] == "moves") {
                for (++next; next < words.size(); ++next) {
                    position.moves.push_back(String(words[next]));
                }
            }
            return position;
        }

        Uci::SetOptionParams ExpectedSetOption(const Words& words) {
            Uci::SetOptionParams option;
            size_t index = 1;
            if (index < words.size() && words[index] == "name") {
                const size_t nameBegin = index + 1;
                index = nameBegin;
                while (index < words.size() && words[index] != "value") {
                    ++index;
                }
                option.name = Joined(words, nameBegin, index);
            }
            if (index < words.size() && words[index] == "value") {
                option.value = Joined(words, index + 1, words.size());
            }
            return option;
        }

        /// The command that the header of ParseCommand describes for these words, and nothing else set
        UciCommand ExpectedCommand(const Words& words) {
            UciCommand expected;
            expected.kind = KindOf(words);
            switch (expected.kind) {
                case UciCommandKind::Go:
                    expected.go = ExpectedGo(words);
                    break;
                case UciCommandKind::Position:
                    expected.position = ExpectedPosition(words);
                    break;
                case UciCommandKind::SetOption:
                    expected.setOption = ExpectedSetOption(words);
                    break;
                default:
                    break;
            }
            return expected;
        }

        /// What UciServer does with a `position`: the FEN, then each move through ParseUci (the first it refuses ends the game), then SearchRequest::FinalPosition
        void CheckGameOf(const Uci::PositionParams& params) {
            for (const String& move : params.moves) {
                CHESS_FUZZ_CHECK(!move.empty());
                CHESS_FUZZ_CHECK(move.find_first_of(" \t") == String::npos);
            }

            SearchRequest game;
            game.startFen = params.startFen;
            const auto start = Position::FromFen(ViewOf(params.startFen));
            if (!start) {
                const auto final = game.FinalPosition();
                CHESS_FUZZ_CHECK(!final.has_value());
                CHESS_FUZZ_CHECK(final.error().kind == EngineErrorKind::BadRequest);
                return;
            }

            Position position = *start;
            for (size_t index = 0; index < params.moves.size() && index < kMostReplayedMoves; ++index) {
                const Optional<Move> move = MoveGenerator::ParseUci(position, ViewOf(params.moves[index]));
                if (!move) {
                    break;
                }
                position.DoMove(*move);
                game.moves.push_back(*move);
            }

            const auto final = game.FinalPosition();
            CHESS_FUZZ_CHECK(final.has_value());
            CHESS_FUZZ_CHECK(final->Key() == position.Key());
            const String fen = final->ToFen();
            const auto again = Position::FromFen(ViewOf(fen));
            CHESS_FUZZ_CHECK(again.has_value());
            CHESS_FUZZ_CHECK(again->Key() == final->Key());
        }

    }  // namespace

    void FuzzUciCommand(const uint8_t* data, size_t size) {
        const StringView line = ViewOf(data, size);

        const Optional<int> number = Uci::ParseInt(line);
        CHESS_FUZZ_CHECK(number == ReadInt(line));

        const UciCommand command = Uci::ParseCommand(line);
        CHESS_FUZZ_CHECK(SameCommand(command, Uci::ParseCommand(line)));
        CHESS_FUZZ_CHECK(SameCommand(command, ExpectedCommand(Text::WordsOf(WithoutEdges(line), kWordSeparators))));

        // The tolerance of the header: spaces at both ends and a `\r` change nothing. A tab is no edge for the trimming, so it goes around the trimmed line only
        String padded = "  ";
        padded.append(line.data(), line.size());
        padded.append("  \r");
        CHESS_FUZZ_CHECK(SameCommand(command, Uci::ParseCommand(ViewOf(padded))));

        const StringView trimmed = WithoutEdges(line);
        String tabbed = " \t ";
        tabbed.append(trimmed.data(), trimmed.size());
        tabbed.append(" \t \r");
        CHESS_FUZZ_CHECK(SameCommand(command, Uci::ParseCommand(ViewOf(tabbed))));

        if (command.kind == UciCommandKind::Position) {
            CheckGameOf(command.position);
        }
    }

    bool UciCommandIsAccepted(const uint8_t* data, size_t size) {
        return Uci::ParseCommand(ViewOf(data, size)).kind != UciCommandKind::Unknown;
    }

}  // namespace BECore::Chess::Fuzzing
