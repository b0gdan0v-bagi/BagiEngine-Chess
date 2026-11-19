#include "Puzzle.h"

#include <Chess/Game/MoveGenerator.h>
#include <Chess/Game/Position.h>

namespace BECore::Chess {

    namespace {

        constexpr char kColumnSeparator = '\t';
        constexpr char kLineSeparator = '\n';
        constexpr char kWordSeparator = ' ';
        constexpr size_t kColumnCount = 5;
        /// The setup move and the person's answer: the shortest line a puzzle can have
        constexpr size_t kMinLineMoves = 2;

        String ToString(StringView text) {
            return String(text.data(), text.size());
        }

        /// The words of @p text, in order; the views point into it
        Vector<StringView> Words(StringView text) {
            Vector<StringView> words;
            size_t at = 0;
            while (at < text.size()) {
                size_t end = text.find(kWordSeparator, at);
                if (end == StringView::npos) {
                    end = text.size();
                }
                if (end > at) {
                    words.push_back(text.substr(at, end - at));
                }
                at = end + 1;
            }
            return words;
        }

        Optional<int32_t> ParseRating(StringView text) {
            int32_t value = 0;
            if (text.empty()) {
                return std::nullopt;
            }
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size()) {
                return std::nullopt;
            }
            return value;
        }

        /// The first @p kColumnCount columns of a line; nothing when it has fewer (more are ignored)
        Optional<Array<StringView, kColumnCount>> SplitRow(StringView line) {
            Array<StringView, kColumnCount> columns;
            size_t at = 0;
            for (size_t column = 0; column < kColumnCount; ++column) {
                const size_t end = line.find(kColumnSeparator, at);
                if (end == StringView::npos) {
                    if (column + 1 != kColumnCount) {
                        return std::nullopt;
                    }
                    columns[column] = line.substr(at);
                    break;
                }
                columns[column] = line.substr(at, end - at);
                at = end + 1;
            }
            return columns;
        }

    }  // namespace

    std::expected<Puzzle, String> Puzzle::FromFields(StringView id, StringView startFen, StringView moves, StringView rating, StringView themes) {
        if (id.empty()) {
            return std::unexpected(String("empty id"));
        }
        const Optional<int32_t> parsedRating = ParseRating(rating);
        if (!parsedRating) {
            return std::unexpected(Format("rating '{}' is no number", ToString(rating)));
        }
        auto parsedFen = Position::FromFen(startFen);
        if (!parsedFen) {
            return std::unexpected(Format("bad FEN '{}'", ToString(startFen)));
        }

        Puzzle puzzle;
        puzzle.id = ToString(id);
        puzzle.startFen = ToString(startFen);
        puzzle.rating = *parsedRating;
        for (const StringView theme : Words(themes)) {
            puzzle.themes.push_back(ToString(theme));
        }

        Position position = eastl::move(*parsedFen);
        const Vector<StringView> texts = Words(moves);
        for (size_t ply = 0; ply < texts.size(); ++ply) {
            const Optional<Move> move = MoveGenerator::ParseUci(position, texts[ply]);
            if (!move) {
                return std::unexpected(Format("move {} '{}' is no legal move", ply + 1, ToString(texts[ply])));
            }
            position.DoMove(*move);
            puzzle.line.push_back(*move);
            if (ply == 0) {
                puzzle._solver = position.SideToMove();
            }
        }
        if (puzzle.line.size() < kMinLineMoves) {
            return std::unexpected(Format("{} moves, at least {} are needed", puzzle.line.size(), kMinLineMoves));
        }
        if (puzzle.line.size() % 2 != 0) {
            return std::unexpected(Format("{} moves: the person would not move last", puzzle.line.size()));
        }
        return puzzle;
    }

    PuzzleSet PuzzleSet::Parse(StringView tsv, Vector<String>* problems) {
        PuzzleSet set;
        size_t lineNumber = 0;
        size_t at = 0;
        while (at < tsv.size()) {
            size_t end = tsv.find(kLineSeparator, at);
            if (end == StringView::npos) {
                end = tsv.size();
            }
            StringView line = tsv.substr(at, end - at);
            at = end + 1;
            ++lineNumber;
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            if (lineNumber == 1 || line.empty()) {
                continue;
            }

            auto refuse = [&](const String& reason) {
                if (problems != nullptr) {
                    problems->push_back(Format("line {}: {}", lineNumber, reason));
                }
            };

            const auto columns = SplitRow(line);
            if (!columns) {
                refuse(Format("fewer than {} columns", kColumnCount));
                continue;
            }
            auto puzzle = Puzzle::FromFields((*columns)[0], (*columns)[1], (*columns)[2], (*columns)[3], (*columns)[4]);
            if (!puzzle) {
                refuse(puzzle.error());
                continue;
            }
            if (set.FindIndexById(puzzle->id)) {
                refuse(Format("duplicate id '{}'", puzzle->id));
                continue;
            }
            set._puzzles.push_back(eastl::move(*puzzle));
        }
        return set;
    }

    const Puzzle& PuzzleSet::At(size_t index) const {
        return _puzzles[index];
    }

    Optional<size_t> PuzzleSet::FindIndexById(StringView id) const {
        for (size_t i = 0; i < _puzzles.size(); ++i) {
            if (StringView(_puzzles[i].id.data(), _puzzles[i].id.size()) == id) {
                return i;
            }
        }
        return std::nullopt;
    }

}  // namespace BECore::Chess
